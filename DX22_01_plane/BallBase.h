#pragma once
#include "Object.h"

#include "Texture.h"
#include "MeshRenderer.h"
#include "StaticMesh.h"
#include "utility.h"
#include "Material.h"
#include "Collision.h"

class BallBase : public Object {
protected:
    // --- 共通の物理パラメーター ---
    DirectX::SimpleMath::Vector3 m_OldPosition = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_Velocity = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_Acceleration = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
    float m_Radius = 1.0f;

    // --- 共通の描画リソース ---
    MeshRenderer m_MeshRenderer;
    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsetList;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    // --- 共通の回転情報 ---
    DirectX::SimpleMath::Quaternion m_RollingRotation = DirectX::SimpleMath::Quaternion::Identity;

public:
    virtual void UpdatePhysics(); // 物理演算を共通化
    void DrawMesh(const DirectX::SimpleMath::Matrix& worldMtx);             // 描画処理を共通化
    void SetRadius(float r) { m_Radius = r; }   // 半径設定の共通化
    void LoadModel(const char* modelFilePath, const char* texDirectory); // モデル読み込み共通化

    DirectX::SimpleMath::Quaternion GetRollingRotation() const { return m_RollingRotation; } // クオータニオンを渡す
    Collision::Sphere GetSphere() const {
        // Objectクラスが持つ座標（m_position）と自身の半径を渡す
        return { m_Transform.position, m_Radius };
    }
};