"""Build and execute C++ regression projects with checkout-local dependencies."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def find_msbuild() -> Path:
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    result = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
        "Microsoft.Component.MSBuild", "-find", r"MSBuild\**\Bin\MSBuild.exe"],
        check=True, capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    candidates = [Path(line) for line in result.stdout.splitlines() if line.strip()]
    if not candidates: raise RuntimeError("Visual Studio MSBuild was not found")
    return candidates[0]


def run(command: list[str], log: Path, cwd: Path, timeout: int) -> int | str:
    with log.open("wb") as output:
        try:
            return subprocess.run(command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT,
                timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW).returncode
        except subprocess.TimeoutExpired:
            # subprocess.run kills/reaps only the process it launched.
            return "timeout"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--tests", nargs="+", help="Project stems, e.g. test_game_world_lifetime")
    parser.add_argument("--embedded-debug-info", action="store_true", help="Opt-in /Z7 fallback for an inconsistent PDB installation")
    parser.add_argument("--toolset-version", help="Use the same installed MSVC version throughout a run")
    parser.add_argument("--discard-intermediates", action="store_true", help="Keep binaries/logs but discard this run's regenerable object files")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label): parser.error("Use a simple fresh label")
    projects = sorted((ROOT / "DX22_01_plane/tools").glob("test_*.vcxproj"))
    if args.tests:
        unknown = set(args.tests) - {p.stem for p in projects}
        if unknown: parser.error(f"Unknown test projects: {sorted(unknown)}")
        projects = [p for p in projects if p.stem in args.tests]
    output = ROOT / "logs/native_tests" / args.label
    output.mkdir(parents=True, exist_ok=False)
    msbuild = find_msbuild()
    report = {"started_at_utc": datetime.now(timezone.utc).isoformat(),
        "embedded_debug_info": args.embedded_debug_info, "toolset_version": args.toolset_version,
        "msbuild": str(msbuild), "results": []}
    for project in projects:
        folder = output / project.stem
        folder.mkdir()
        command = [str(msbuild), str(project), "/t:Build", "/p:Configuration=Debug", "/p:Platform=x64",
            "/p:WindowsTargetPlatformVersion=10.0", "/p:PreferredToolArchitecture=x64",
            f"/p:IntDir={folder / 'obj'}\\", f"/p:OutDir={folder / 'bin'}\\",
            f"/p:ForceImportAfterCppTargets={ROOT / 'tools/runtime_tests.targets'}", "/v:minimal", "/nologo"]
        if args.embedded_debug_info: command.append("/p:Dx22TestEmbeddedDebugInfo=true")
        if args.toolset_version: command.append(f"/p:VCToolsVersion={args.toolset_version}")
        start = time.monotonic()
        build = run(command, folder / "build.log", ROOT, 300)
        exe = folder / "bin" / f"{project.stem}.exe"
        working = folder / "work"
        working.mkdir()
        # Logger builds its own missing/malformed-data fixtures; every other
        # test gets copied data and a private output tree, never user saves.
        if project.stem != "test_balance_logger":
            shutil.copytree(ROOT / "DX22_01_plane/assets/data", working / "assets/data")
            (working / "tools/runtime_tests/debug_mode").mkdir(parents=True)
        execution = run([str(exe)], folder / "test.log", working, 60) if build == 0 and exe.exists() else "not_run"
        row = {"test": project.stem, "build_exit": build, "test_exit": execution,
            "seconds": round(time.monotonic()-start, 3), "ok": build == 0 and execution == 0}
        report["results"].append(row)
        (output / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps(row), flush=True)
        if args.discard_intermediates:
            objects = folder / "obj"
            if objects.exists():
                assert objects.resolve().is_relative_to(output.resolve())
                assert not (objects.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)
                shutil.rmtree(objects)
    return 0 if all(r["ok"] for r in report["results"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
