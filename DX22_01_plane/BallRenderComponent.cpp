#include "BallRenderComponent.h"

#include "EnemyBall.h"
#include "Camera.h"
#include "GameObject.h"
#include "GameObjectTag.h"
#include "Renderer.h"
#include "TagComponent.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <Windows.h>
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace DirectX::SimpleMath;

namespace
{
    namespace fs = std::filesystem;
    constexpr int kFirstNumber = 1;
    constexpr int kLastNumber = 15;

    // Ball-only PS b6 data; the shared directional light remains in b3.
    struct alignas(16) ReflectionConstants
    {
        DirectX::XMFLOAT4 Eye;    // xyz: camera position
        DirectX::XMFLOAT4 Params; // x: specular, y: power, z: environment, w: defeat fade
    };
    static_assert(sizeof(ReflectionConstants) == 32);


    // Occupancy is associated with living renderer components, not enemy types.
    std::array<const BallRenderComponent*, kLastNumber + 1> s_NumberOwners{};

    void Log(const std::string& message)
    {
        const std::string line = "[Caromino/FBX] " + message + "\n";
        std::cerr << line << std::flush;
        OutputDebugStringA(line.c_str());
    }

    void WarnOnce(const std::string& message)
    {
        Log("ERROR: " + message);
        static bool shown = false;
        if (!shown)
        {
            shown = true;
            MessageBoxA(nullptr, message.c_str(),
                "Caromino: FBX model load warning", MB_OK | MB_ICONWARNING);
        }
    }

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    bool IsLegacyBallModel(const char* path)
    {
        if (!path) return false;
        return Lower(path).find("golf_ball.obj") != std::string::npos;
    }

    int PreferredEnemyNumber(const std::string& id)
    {
        if (id == "enemy_normal") return 1;
        if (id == "enemy_strong") return 2;
        if (id == "enemy_tank") return 3;
        if (id == "enemy_striker") return 4;
        if (id == "enemy_midboss_guard") return 5;
        if (id == "enemy_midboss_pocket") return 6;
        if (id == "enemy_nuisance_spawner") return 7;
        if (id == "enemy_boss_core") return 8;
        if (id == "enemy_collision_shell") return 9;
        if (id == "enemy_swarm_amplifier") return 10;
        if (id == "enemy_impact_brittle") return 11;
        if (id == "enemy_protection_resonator") return 12;
        return 13;
    }

    int ReserveNumber(const BallRenderComponent* owner, int preferred)
    {
        preferred = std::clamp(preferred, kFirstNumber, kLastNumber);
        constexpr int count = kLastNumber - kFirstNumber + 1;
        for (int offset = 0; offset < count; ++offset)
        {
            const int number = kFirstNumber +
                (preferred - kFirstNumber + offset) % count;
            if (!s_NumberOwners[number] || s_NumberOwners[number] == owner)
            {
                s_NumberOwners[number] = owner;
                return number;
            }
        }
        Log("WARNING: 15 numbered balls are already in use. Falling back to cue ball.");
        return 0;
    }

    // FBX is placed under assets/model/BilliardBalls/.
    // Supports direct placement, original source/ directory and a nested
    // 'Billiard Balls' unpacked directory; never requires OBJ or MTL files.
    fs::path FindFBX()
    {
        const fs::path dir = "assets/model/BilliardBalls";
        const std::array<fs::path, 6> roots = {
            fs::path("."), fs::path("DX22_01_plane"),
            fs::path(".."), fs::path("../DX22_01_plane"),
            fs::path("../.."), fs::path("../../DX22_01_plane")
        };
        const std::array<fs::path, 4> layouts = {
            fs::path("."), fs::path("source"),
            fs::path("source/Billiard Balls"), fs::path("Billiard Balls")
        };
        for (const auto& root : roots)
        {
            for (const auto& layout : layouts)
            {
                const auto candidate = root / dir / layout / "Billiard Balls.FBX";
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec)) return candidate;
            }
        }
        return {};
    }

    std::string TextureStem(int number)
    {
        if (number == 0) return "BallCue";
        if (number == 15) return "Ball 15"; // Name in the uploaded FBX.
        return "Ball" + std::to_string(number);
    }

    fs::path FindColorTexture(const fs::path& fbxPath, int number)
    {
        const fs::path base = fbxPath.parent_path();
        const std::array<fs::path, 7> directories = {
            base, base / "textures", base / "source", base.parent_path(),
            base.parent_path() / "textures", base.parent_path() / "source",
            base.parent_path().parent_path() / "textures"
        };
        const std::array<std::string, 2> stems = {
            TextureStem(number),
            number == 0 ? "BallCue" : "Ball" + std::to_string(number)
        };
        const std::array<const char*, 4> extensions = { ".jpg", ".jpeg", ".JPG", ".JPEG" };
        for (const auto& directory : directories)
            for (const auto& stem : stems)
                for (const char* ext : extensions)
                {
                    const auto candidate = directory / (stem + ext);
                    std::error_code ec;
                    if (fs::is_regular_file(candidate, ec)) return candidate;
                }
        return {};
    }

    // Parse a FBX material's diffuse texture name. 'Ball 15.jpg' and
    // 'BallCue.jpg' are intentionally handled without relying on mesh order.
    int TextureNumber(std::string fileName)
    {
        const auto slash = fileName.find_last_of("/\\");
        if (slash != std::string::npos) fileName.erase(0, slash + 1);
        const auto dot = fileName.find_last_of('.');
        if (dot != std::string::npos) fileName.resize(dot);
        fileName = Lower(fileName);
        fileName.erase(std::remove_if(fileName.begin(), fileName.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }), fileName.end());
        if (fileName == "ballcue" || fileName == "ballclube") return 0;
        if (fileName.rfind("ball", 0) != 0) return -1;
        const std::string digits = fileName.substr(4);
        if (digits.empty() || digits.size() > 2 || !std::all_of(digits.begin(), digits.end(),
            [](unsigned char c) { return std::isdigit(c) != 0; })) return -1;
        const int number = std::stoi(digits);
        return number >= 1 && number <= 15 ? number : -1;
    }

    const aiMesh* FindByTexture(const aiScene* scene, int number)
    {
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            const aiMesh* mesh = scene->mMeshes[i];
            if (mesh->mMaterialIndex >= scene->mNumMaterials) continue;
            const aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            aiString texturePath;
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS &&
                TextureNumber(texturePath.C_Str()) == number)
                return mesh;
        }
        return nullptr;
    }

    const aiMesh* FindByNode(const aiScene* scene, const aiNode* node, int number)
    {
        if (!node) return nullptr;
        std::string name = Lower(node->mName.C_Str());
        name.erase(std::remove_if(name.begin(), name.end(),
            [](unsigned char c) { return std::isspace(c) != 0; }), name.end());
        const std::string expected = number == 0 ? "ballclube" : "ball" + std::to_string(number);
        if ((name == expected ||
            (name.rfind(expected, 0) == 0 && name.size() > expected.size() &&
                !std::isdigit(static_cast<unsigned char>(name[expected.size()])))) &&
            node->mNumMeshes > 0)
        {
            const unsigned int index = node->mMeshes[0];
            if (index < scene->mNumMeshes) return scene->mMeshes[index];
        }
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            if (const aiMesh* mesh = FindByNode(scene, node->mChildren[i], number))
                return mesh;
        return nullptr;
    }

    class OneBallMesh final : public Mesh
    {
    public:
        OneBallMesh(std::vector<VERTEX_3D> vertices, std::vector<unsigned int> indices)
        {
            m_vertices = std::move(vertices);
            m_indices = std::move(indices);
        }
    };
}

BallRenderComponent::~BallRenderComponent()
{
    ReleaseBilliardNumber();
}

void BallRenderComponent::OnDestroy()
{
    ReleaseBilliardNumber();
}

void BallRenderComponent::ReleaseBilliardNumber()
{
    if (m_BilliardNumber >= 1 && m_BilliardNumber <= 15 &&
        s_NumberOwners[m_BilliardNumber] == this)
        s_NumberOwners[m_BilliardNumber] = nullptr;
    m_BilliardNumber = -1;
    m_UsesBilliardModel = false;
    m_MissingBilliardTexture = false;
}

bool BallRenderComponent::TryLoadBilliardFBX(const char* filePath, int number)
{
    m_RenderReady = false;
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_ConvertToLeftHanded | aiProcess_Triangulate | aiProcess_GenSmoothNormals);
    if (!scene || !scene->HasMeshes())
    {
        Log(std::string("FBX import failed: ") + filePath + " | " + importer.GetErrorString());
        return false;
    }

    // Prefer texture-to-mesh mapping; original FBX contains one diffuse map per ball.
    // Node names are a fallback, and the cue-ball node is named 'Ball Clube'.
    const aiMesh* mesh = FindByTexture(scene, number);
    if (!mesh) mesh = FindByNode(scene, scene->mRootNode, number);
    if (!mesh || mesh->mNumVertices == 0 || mesh->mNumFaces == 0)
    {
        Log("Cannot find the requested ball mesh in FBX: #" + std::to_string(number));
        return false;
    }

    Vector3 minimum(FLT_MAX, FLT_MAX, FLT_MAX);
    Vector3 maximum(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
    {
        const aiVector3D& p = mesh->mVertices[i];
        minimum.x = (std::min)(minimum.x, p.x);
        minimum.y = (std::min)(minimum.y, p.y);
        minimum.z = (std::min)(minimum.z, p.z);
        maximum.x = (std::max)(maximum.x, p.x);
        maximum.y = (std::max)(maximum.y, p.y);
        maximum.z = (std::max)(maximum.z, p.z);
    }
    // Ignore the original rack's node translation. Normalize the selected ball
    // to radius 1 so the existing game ball scale (e.g. 2.4) stays meaningful.
    const Vector3 center = (minimum + maximum) * 0.5f;
    float radius = 0.0f;
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
    {
        const aiVector3D& p = mesh->mVertices[i];
        const Vector3 local(p.x, p.y, p.z);
        radius = (std::max)(radius, (local - center).Length());
    }
    if (!(radius > 0.0001f) || !std::isfinite(radius))
    {
        Log("FBX ball mesh has invalid radius");
        return false;
    }

    std::vector<VERTEX_3D> vertices;
    vertices.reserve(mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
    {
        const aiVector3D& p = mesh->mVertices[i];
        VERTEX_3D vertex{};
        vertex.position = (Vector3(p.x, p.y, p.z) - center) / radius;
        if (mesh->HasNormals())
        {
            const aiVector3D& n = mesh->mNormals[i];
            vertex.normal = Vector3(n.x, n.y, n.z);
            vertex.normal.Normalize();
        }
        else
            vertex.normal = vertex.position;
        vertex.uv = mesh->HasTextureCoords(0)
            ? Vector2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
            : Vector2(0.0f, 0.0f);
        vertex.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
        vertices.push_back(vertex);
    }

    std::vector<unsigned int> indices;
    indices.reserve(mesh->mNumFaces * 3);
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
    {
        const aiFace& face = mesh->mFaces[f];
        if (face.mNumIndices != 3) continue;
        indices.insert(indices.end(), face.mIndices, face.mIndices + 3);
    }
    if (indices.empty())
    {
        Log("FBX mesh has no triangles");
        return false;
    }

    if (Renderer::GetDevice() == nullptr || Renderer::GetDeviceContext() == nullptr)
    {
        Log("Direct3D device/context is not ready while loading the FBX ball");
        return false;
    }

    OneBallMesh oneBall(std::move(vertices), std::move(indices));
    m_MeshRenderer.Init(oneBall);
    // Ball-only shader. Legacy light/diffuse expression is preserved verbatim
    // in the vertex shader, and only additional highlights are calculated in PS.
    m_ReflectionEnabled = CreateReflectionBuffer();
    if (m_ReflectionEnabled)
    {
        m_ReflectionEnabled = m_Shader.CreateChecked(
            "shader/billiardBallVS.hlsl", "shader/billiardBallPS.hlsl");
    }
    if (!m_ReflectionEnabled)
    {
        m_ReflectionBuffer.Reset();
        Log("ERROR: Billiard reflection shader unavailable; using the existing lit shader.");
        if (!m_Shader.CreateChecked("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl"))
            return false;
    }
    m_Subsets.clear();
    SUBSET subset{};
    subset.IndexNum = static_cast<UINT>(oneBall.GetIndices().size());
    subset.VertexNum = static_cast<UINT>(oneBall.GetVertices().size());
    subset.IndexBase = 0;
    subset.VertexBase = 0;
    subset.MaterialIdx = 0;
    m_Subsets.push_back(subset);

    m_Textures.clear();
    m_Materials.clear();
    const fs::path texturePath = FindColorTexture(fs::path(filePath), number);
    bool textureLoaded = false;
    if (!texturePath.empty())
    {
        auto texture = std::make_unique<Texture>();
        textureLoaded = texture->Load(texturePath.string());
        if (textureLoaded) m_Textures.push_back(std::move(texture));
    }

    MATERIAL materialData{};
    materialData.Ambient = Color(1, 1, 1, 1);
    materialData.Diffuse = Color(1, 1, 1, 1);
    materialData.Specular = Color(0.75f, 0.75f, 0.75f, 1);
    materialData.Emission = Color(0, 0, 0, 1);
    materialData.Shiness = 32.0f;
    materialData.TextureEnable = textureLoaded ? TRUE : FALSE;
    auto material = std::make_unique<Material>();
    material->Create(materialData);
    m_Materials.push_back(std::move(material));

    m_ModelBaseRadius = 1.0f;
    m_UsesBilliardModel = true;
    m_MissingBilliardTexture = !textureLoaded;
    Log("LOADED #" + std::to_string(number) + " FBX=" + filePath +
        " vertices=" + std::to_string(oneBall.GetVertices().size()) +
        " triangles=" + std::to_string(oneBall.GetIndices().size() / 3));
    if (textureLoaded)
        Log("TEXTURE #" + std::to_string(number) + " " + texturePath.string());
    else
        Log("WARNING: Diffuse texture not found or not loaded for ball #" +
            std::to_string(number) + " (expected BallCue.jpg or BallN.jpg/.jpeg)");
    m_RenderReady = true;
    return true;
}

void BallRenderComponent::LoadOriginalModel(const char* modelFilePath,
    const char* textureDirectory)
{
    // Existing non-FBX drawing is unchanged.
    m_RenderReady = false;
    m_ReflectionEnabled = false;
    m_ReflectionBuffer.Reset();
    StaticMesh staticMesh;
    staticMesh.Load(modelFilePath ? modelFilePath : "",
        textureDirectory ? textureDirectory : "");
    if (staticMesh.GetVertices().empty() || staticMesh.GetIndices().empty())
    {
        Log("Fallback model has no drawable vertices or indices");
        return;
    }
    if (Renderer::GetDevice() == nullptr || Renderer::GetDeviceContext() == nullptr)
    {
        Log("Direct3D device/context is not ready while loading the fallback model");
        return;
    }
    m_MeshRenderer.Init(staticMesh);
    if (!m_Shader.CreateChecked("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl"))
    {
        Log("Fallback model shader creation failed");
        return;
    }
    m_Subsets = staticMesh.GetSubsets();
    m_Textures = staticMesh.GetTextures();
    m_Materials.clear();
    for (const MATERIAL& data : staticMesh.GetMaterials())
    {
        MATERIAL safe = data;
        if (safe.TextureEnable &&
            (m_Materials.size() >= m_Textures.size() ||
                m_Textures[m_Materials.size()] == nullptr))
            safe.TextureEnable = FALSE;
        auto material = std::make_unique<Material>();
        material->Create(safe);
        m_Materials.push_back(std::move(material));
    }
    m_ModelBaseRadius = 0.0f;
    for (const auto& v : staticMesh.GetVertices())
        m_ModelBaseRadius = (std::max)(m_ModelBaseRadius, v.position.Length());
    m_RenderReady = !m_Subsets.empty() && !m_Materials.empty();
    if (!m_RenderReady)
        Log("Fallback model has no drawable subsets or materials");
}

void BallRenderComponent::LoadModel(const char* modelFilePath,
    const char* textureDirectory)
{
    ReleaseBilliardNumber();
    if (IsLegacyBallModel(modelFilePath))
    {
        int number = 0;
        if (GameObject* object = GetGameObject())
        {
            if (const TagComponent* tag = object->GetComponent<TagComponent>())
            {
                if (tag->GetTag() == GameObjectTag::Enemy)
                {
                    int preferred = 13;
                    if (const EnemyBall* enemy = object->GetComponent<EnemyBall>())
                        preferred = PreferredEnemyNumber(enemy->GetEnemyId());
                    number = ReserveNumber(this, preferred);
                }
                else if (object->GetName() == "NuisanceBall")
                    number = ReserveNumber(this, 15);
            }
        }
        m_BilliardNumber = number;
        const fs::path fbx = FindFBX();
        if (!fbx.empty() && TryLoadBilliardFBX(fbx.string().c_str(), number))
            return;
        WarnOnce("Billiard Balls.FBX was not found or could not be loaded.\n"
            "Copy the original FBX + JPG/JPEG textures into\n"
            "DX22_01_plane/assets/model/BilliardBalls/\n"
            "or its source/ subdirectory. See Visual Studio Output (Debug).\n"
            "Original golf ball will be used temporarily.");
        ReleaseBilliardNumber();
    }
    LoadOriginalModel(modelFilePath, textureDirectory);
}

bool BallRenderComponent::CreateReflectionBuffer()
{
    m_ReflectionBuffer.Reset();
    ID3D11Device* device = Renderer::GetDevice();
    if (!device) return false;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(sizeof(ReflectionConstants));
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    const HRESULT hr = device->CreateBuffer(
        &desc, nullptr, m_ReflectionBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        Log("Reflection buffer creation failed. Falling back to existing ball lighting.");
        return false;
    }
    return true;
}

void BallRenderComponent::BindReflectionBuffer() const
{
    if (!m_ReflectionEnabled || !m_ReflectionBuffer) return;
    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    if (!context) return;
    const Vector3 eye = Camera::GetInstance().GetPosition();
    const ReflectionConstants constants{
        { eye.x, eye.y, eye.z, 1.0f },
        {
            m_SpecularEnabled ? m_SpecularStrength : 0.0f,
            m_Shininess,
            m_EnvironmentEnabled ? m_EnvironmentStrength : 0.0f,
            m_DefeatVisualProgress
        }
    };
    context->UpdateSubresource(m_ReflectionBuffer.Get(), 0, nullptr, &constants, 0, 0);
    ID3D11Buffer* buffer = m_ReflectionBuffer.Get();
    context->PSSetConstantBuffers(6, 1, &buffer);
}

void BallRenderComponent::SetSpecularStrength(float strength)
{
    m_SpecularStrength = std::clamp(strength, 0.0f, 2.0f);
}

void BallRenderComponent::SetShininess(float shininess)
{
    m_Shininess = std::clamp(shininess, 4.0f, 256.0f);
}

void BallRenderComponent::SetEnvironmentStrength(float strength)
{
    m_EnvironmentStrength = std::clamp(strength, 0.0f, 1.0f);
}

void BallRenderComponent::SetDefeatVisualProgress(float progress)
{
    m_DefeatVisualProgress = std::clamp(progress, 0.0f, 1.0f);
}

void BallRenderComponent::BeginDraw()
{
    if (!m_RenderReady || Renderer::GetDeviceContext() == nullptr) return;
    m_Shader.SetGPU();
    BindReflectionBuffer();
    m_MeshRenderer.BeforeDraw();
}

void BallRenderComponent::SetTint(const Color& color)
{
    Color tint = color;
    if (m_UsesBilliardModel)
    {
        // Keep the white cue ball white, and preserve the diffuse colors/labels
        // of numbered balls instead of multiplying them by the old enemy tint.
        if (m_MissingBilliardTexture && m_BilliardNumber > 0)
            tint = Color(1.0f, 0.0f, 1.0f, 1.0f); // Visible missing-texture marker
        else if (m_BilliardNumber == 0)
            tint = Color(1.0f, 1.0f, 1.0f, color.w);
        else
        {
            const float darkest = (std::min)({ color.x, color.y, color.z });
            const float flash = std::clamp((darkest - 0.65f) / 0.35f, 0.0f, 1.0f);
            const float boost = 1.0f + 0.20f * flash;
            tint = Color(boost, boost, boost, color.w);
        }
    }
    for (const auto& material : m_Materials)
    {
        if (!material) continue;
        material->SetDiffuse(tint);
        material->Update();
    }
}

void BallRenderComponent::DrawMesh(const Matrix& worldMatrix)
{
    if (!m_RenderReady || Renderer::GetDeviceContext() == nullptr) return;
    // Always restore the sphere shader here: PlayerBall can draw many trails
    // and other rendering code may have changed shaders since BeginDraw().
    m_Shader.SetGPU();
    BindReflectionBuffer();
    m_MeshRenderer.BeforeDraw();
    Renderer::SetWorldMatrix(const_cast<Matrix*>(&worldMatrix));
    for (const SUBSET& subset : m_Subsets)
    {
        const UINT materialIndex = subset.MaterialIdx;
        if (materialIndex >= m_Materials.size()) continue;
        m_Materials[materialIndex]->SetGPU();
        if (m_Materials[materialIndex]->isTextureEnable() &&
            materialIndex < m_Textures.size() && m_Textures[materialIndex])
            m_Textures[materialIndex]->SetGPU();
        m_MeshRenderer.DrawSubset(subset.IndexNum, subset.IndexBase,
            subset.VertexBase);
    }
}
