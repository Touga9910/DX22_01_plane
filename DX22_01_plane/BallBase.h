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
    // --- 共通のステータス ---
    int m_HP = 3;           // 最大HPや初期HP
    
    // --- 共通の物理パラメーター ---
    DirectX::SimpleMath::Vector3 m_OldPosition = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_Velocity = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_Acceleration = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
    float m_Radius = 2.0f;
    float m_ModelBaseRadius = 1.0f; // モデル本来の半径（スケール適用前）
    float m_Mass = 1.0f;   // 質量
    float m_Restitution = 0.8f;   // 反発係数
    float m_Friction = 0.02f;  // 摩擦係数

    // --- 共通の描画リソース ---
    MeshRenderer m_MeshRenderer;
    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsetList;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    // --- 共通の回転情報 ---
    DirectX::SimpleMath::Quaternion m_RollingRotation = DirectX::SimpleMath::Quaternion::Identity;

    void UpdateRadius()
    {
        float maxScale = m_Transform.scale.x;
        if (m_Transform.scale.y > maxScale) maxScale = m_Transform.scale.y;
        if (m_Transform.scale.z > maxScale) maxScale = m_Transform.scale.z;
        m_Radius = m_ModelBaseRadius * maxScale;
    }

public:
    virtual void UpdatePhysics();   // 物理演算を共通化

    void DrawMesh(const DirectX::SimpleMath::Matrix& worldMtx);             // 描画処理を共通化
    void SetRadius(float r) { m_Radius = r; }   // 半径設定の共通化
    void LoadModel(const char* modelFilePath, const char* texDirectory); // モデル読み込み共通化

    DirectX::SimpleMath::Quaternion GetRollingRotation() const { return m_RollingRotation; } // クオータニオンを渡す
    Collision::Sphere GetSphere() const {
        // Objectクラスが持つ座標（m_position）と自身の半径を渡す
        return { m_Transform.position, m_Radius };
    }
    // ImGuiで物理パラメーターを表示・編集するための共通関数
    virtual void DrawImGui(const std::string& label);

    // ダメージを受ける共通処理
    void TakeDamage(int damage) {
        m_HP -= damage;
        if (m_HP <= 0)
        {
            m_IsDead = true; // Object クラスが持つ死亡フラグが true になります！
        }
    }
};

