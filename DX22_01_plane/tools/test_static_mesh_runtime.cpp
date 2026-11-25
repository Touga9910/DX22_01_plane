#include "StaticMesh.h"
#include "AssimpPerse.h"
#include <fstream>
#include <iostream>
#include <string_view>
#include "test_checks.h"

// Texturless OBJ fixtures exercise the real importer without a graphics device.
ID3D11Device* Renderer::m_pDevice{};
ID3D11DeviceContext* Renderer::m_pDeviceContext{};

static void CheckEmpty(StaticMesh& mesh)
{
    assert(mesh.GetVertices().empty());
    assert(mesh.GetIndices().empty());
    assert(mesh.GetSubsets().empty());
    assert(mesh.GetMaterials().empty());
    assert(mesh.GetTextureNames().empty());
    assert(mesh.GetTextures().empty());
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--missing-only")
    {
        StaticMesh missing;
        missing.Load("missing-mesh-fixture.obj");
        CheckEmpty(missing);
        return 0;
    }

    {
        std::ofstream file("mesh-fixture.obj");
        file << "o triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
        assert(file.good());
    }
    StaticMesh mesh;
    mesh.Load("mesh-fixture.obj");
    const auto vertices = mesh.GetVertices().size();
    const auto indices = mesh.GetIndices().size();
    const auto subsets = mesh.GetSubsets().size();
    const auto materials = mesh.GetMaterials().size();
    const auto names = mesh.GetTextureNames();
    assert(vertices == 3 && indices == 3 && subsets == 1 && materials > 0);

    mesh.Load("mesh-fixture.obj");
    assert(mesh.GetVertices().size() == vertices);
    assert(mesh.GetIndices().size() == indices);
    assert(mesh.GetSubsets().size() == subsets);
    assert(mesh.GetMaterials().size() == materials);
    assert(mesh.GetTextureNames() == names);
    if (argc == 2 && std::string_view(argv[1]) == "--repeat-only") return 0;

    mesh.Load("missing-mesh-fixture.obj");
    CheckEmpty(mesh);
    assert(AssimpPerse::GetVertices().empty());
    assert(AssimpPerse::GetIndices().empty());
    assert(AssimpPerse::GetSubsets().empty());
    assert(AssimpPerse::GetMaterials().empty());
    assert(AssimpPerse::GetTextures().empty());
    { std::ofstream corrupt("corrupt-mesh-fixture.obj"); corrupt << "not a model"; }
    mesh.Load("corrupt-mesh-fixture.obj");
    CheckEmpty(mesh);
    mesh.Load("mesh-fixture.obj");
    assert(mesh.GetVertices().size() == vertices && mesh.GetIndices().size() == indices);
    assert(mesh.GetMaterials().size() == materials && mesh.GetTextureNames() == names);
    std::cout << "Static mesh missing/corrupt files, repeated loads, empty fallback, and recovery passed.\n";
}
