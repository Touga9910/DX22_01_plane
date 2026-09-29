from __future__ import annotations

import argparse
import json
import re
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
MSBUILD_NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


def normalize(path: str) -> str:
    return path.replace("\\", "/")


def load_project_sources(project_file: Path) -> tuple[list[str], list[str]]:
    root = ET.parse(project_file).getroot()
    sources = [
        normalize(node.attrib["Include"])
        for node in root.findall(".//m:ClCompile", MSBUILD_NS)
        if "Include" in node.attrib
    ]
    headers = [
        normalize(node.attrib["Include"])
        for node in root.findall(".//m:ClInclude", MSBUILD_NS)
        if "Include" in node.attrib
    ]
    return sources, headers


def classify_source(path: str) -> str:
    lowered = path.lower()
    if lowered.startswith("imgui/") or lowered == "stb_image.cpp":
        return "embedded_third_party"
    return "authored"


def resolve_local_include(source: str, include: str, known: set[str]) -> str | None:
    source_parent = Path(source).parent
    candidates = [normalize(str(source_parent / include)), normalize(include)]
    for candidate in candidates:
        if candidate in known:
            return candidate
    return None


def strongly_connected_components(graph: dict[str, set[str]]) -> list[list[str]]:
    index = 0
    stack: list[str] = []
    on_stack: set[str] = set()
    indices: dict[str, int] = {}
    low_links: dict[str, int] = {}
    result: list[list[str]] = []

    def visit(node: str) -> None:
        nonlocal index
        indices[node] = index
        low_links[node] = index
        index += 1
        stack.append(node)
        on_stack.add(node)
        for dependency in graph.get(node, set()):
            if dependency not in indices:
                visit(dependency)
                low_links[node] = min(low_links[node], low_links[dependency])
            elif dependency in on_stack:
                low_links[node] = min(low_links[node], indices[dependency])
        if low_links[node] != indices[node]:
            return
        component: list[str] = []
        while stack:
            item = stack.pop()
            on_stack.remove(item)
            component.append(item)
            if item == node:
                break
        if len(component) > 1:
            result.append(sorted(component))

    for node in graph:
        if node not in indices:
            visit(node)
    return sorted(result, key=lambda item: (-len(item), item))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    project_file = args.project.resolve()
    project_dir = project_file.parent
    sources, registered_headers = load_project_sources(project_file)
    filesystem_headers = [
        normalize(str(path.relative_to(project_dir)))
        for path in project_dir.rglob("*.h")
    ]
    known = set(sources) | set(registered_headers) | set(filesystem_headers)
    graph: dict[str, set[str]] = defaultdict(set)
    unresolved: dict[str, list[str]] = defaultdict(list)

    for item in sorted(known):
        path = project_dir / item
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8-sig")
        except UnicodeDecodeError:
            text = path.read_text(encoding="cp932")
        for include in INCLUDE_RE.findall(text):
            resolved = resolve_local_include(item, normalize(include), known)
            if resolved is None:
                unresolved[item].append(normalize(include))
            else:
                graph[item].add(resolved)
        graph.setdefault(item, set())

    direct_consumers: dict[str, set[str]] = defaultdict(set)
    for consumer, dependencies in graph.items():
        for dependency in dependencies:
            direct_consumers[dependency].add(consumer)

    def transitive_dependencies(node: str) -> set[str]:
        visited: set[str] = set()
        pending = list(graph.get(node, set()))
        while pending:
            current = pending.pop()
            if current in visited:
                continue
            visited.add(current)
            pending.extend(graph.get(current, set()))
        return visited

    header_impact = []
    authored_sources = [item for item in sources if classify_source(item) == "authored"]
    for header in sorted(item for item in known if item.lower().endswith(".h")):
        affected = [
            source
            for source in authored_sources
            if header in transitive_dependencies(source)
        ]
        header_impact.append(
            {
                "header": header,
                "direct_consumer_count": len(direct_consumers.get(header, set())),
                "transitive_authored_cpp_count": len(affected),
                "affected_authored_cpp": affected,
            }
        )
    header_impact.sort(
        key=lambda item: (
            -item["transitive_authored_cpp_count"],
            -item["direct_consumer_count"],
            item["header"],
        )
    )

    payload = {
        "project": str(project_file),
        "authored_cpp": authored_sources,
        "embedded_third_party_cpp": [
            item for item in sources if classify_source(item) == "embedded_third_party"
        ],
        "registered_header_count": len(registered_headers),
        "header_impact": header_impact,
        "cycles": strongly_connected_components(graph),
        "unresolved_quoted_includes": dict(sorted(unresolved.items())),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"authored_cpp={len(payload['authored_cpp'])}")
    print(f"embedded_third_party_cpp={len(payload['embedded_third_party_cpp'])}")
    print(f"cycles={len(payload['cycles'])}")
    for item in header_impact[:20]:
        print(
            f"{item['header']}: "
            f"transitive_cpp={item['transitive_authored_cpp_count']} "
            f"direct={item['direct_consumer_count']}"
        )


if __name__ == "__main__":
    main()
