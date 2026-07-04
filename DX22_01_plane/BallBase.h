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
    int m_HP = 3;               // 現在のHP
    int m_MaxHP = 3;            // 最大HPや初期HP
	bool m_IsDefeated = false;  // 敗北フラグ

    DirectX::SimpleMath::Vector3 m_InitialPosition = DirectX::SimpleMath::Vector3::Zero;

    // --- 共通の物理パラメーター ---
    DirectX::SimpleMath::Vector3 m_OldPosition = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_Velocity = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_Acceleration = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
    float m_Radius = 2.0f;
    float m_ModelBaseRadius = 1.0f; // モデル本来の半径（スケール適用前）
    float m_Mass = 1.0f;            // 質量
    float m_Restitution = 0.8f;     // 反発係数
    float m_Friction = 0.02f;       // 摩擦係数

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
    virtual void Damage(int damage);
    virtual void Defeat();

    virtual void UpdatePhysics();   // 物理演算を共通化

    void DrawMesh(const DirectX::SimpleMath::Matrix& worldMtx);             // 描画処理を共通化
    void SetRadius(float r) { m_Radius = r; }   // 半径設定の共通化
    void LoadModel(const char* modelFilePath, const char* texDirectory); // モデル読み込み共通化

    DirectX::SimpleMath::Quaternion GetRollingRotation() const { return m_RollingRotation; } // クオータニオンを渡す
    Collision::Sphere GetSphere() const {
        // Objectクラスが持つ座標（m_position）と自身の半径を渡す
        return { m_Transform.position, m_Radius };
    }

    void SetInitialPosition(const DirectX::SimpleMath::Vector3& position)
    {
        m_InitialPosition = position;
    }

    virtual void ResetToInitialPosition();

    virtual void OnPocketHit();

    // ダメージを受ける共通処理
    virtual void TakeDamage(int damage);

    bool IsDefeated() const { return m_IsDefeated; }

    int GetHP() const { return m_HP; }
    int GetMaxHP() const { return m_MaxHP; }
    DirectX::SimpleMath::Vector3 GetVelocity() const { return m_Velocity; }
    float GetRadius() const { return m_Radius; }

    void SetHP(int hp) { m_HP = hp; }
    void SetMaxHP(int maxHp) { m_MaxHP = maxHp; }

    void ResetDefeated()
    {
        m_IsDefeated = false;
    }

    bool IsHpZero() const
    {
        return m_HP <= 0;
    }

    // ボールが停止しているかを判定（速度の二乗ノルムが閾値未満なら true）
    // PlayerBall / EnemyBall の既存停止閾値（0.03f）と統一
    bool IsStopped() const
    {
        return m_Velocity.LengthSquared() < 0.03f;
    }

    // ImGuiで物理パラメーターを表示・編集するための共通関数
    virtual void DrawImGui(const std::string& label);
};

