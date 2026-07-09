#pragma once

#include "Object.h"

#include "Texture.h"
#include "MeshRenderer.h"
#include "StaticMesh.h"
#include "utility.h"
#include "Material.h"
#include "Collision.h"
#include "BallStatus.h"

#include <memory>
#include <string>
#include <vector>

class BallBase : public Object
{
public:
    //=======================================
    // ダメージ・生存状態
    //=======================================
    virtual void Damage(int damage);
    virtual void TakeDamage(int damage);
    virtual void Defeat();

    bool IsDefeated() const { return m_IsDefeated; }
    bool IsHpZero() const { return m_HP <= 0; }
    void ResetDefeated() { m_IsDefeated = false; }

    //=======================================
    // ステータス
    //=======================================
    void SetStatus(const BallStatus& status)
    {
        // ステータスを反映し、HPと撃破状態を初期化する
        m_Status = status;
        m_HP = m_Status.maxHp;
        m_IsDefeated = false;
    }

    const BallStatus& GetStatus() const { return m_Status; }

    int GetHP() const { return m_HP; }
    int GetMaxHP() const { return m_Status.maxHp; }
    int GetAttack() const { return m_Status.attack; }
    int GetDefense() const { return m_Status.defense; }

    void SetHP(int hp) { m_HP = hp; }

    void SetMaxHP(int maxHp)
    {
        // 最大HPを変更し、現在HPが最大HPを超えないように補正する
        m_Status.maxHp = maxHp;

        if (m_HP > m_Status.maxHp)
        {
            m_HP = m_Status.maxHp;
        }
    }

    //=======================================
    // 位置・物理情報
    //=======================================
    virtual void UpdatePhysics();
    virtual void ResetToInitialPosition();
    virtual void OnPocketHit();

    void SetInitialPosition(const DirectX::SimpleMath::Vector3& position)
    {
        // リセット時に戻す基準位置を保存する
        m_InitialPosition = position;
    }

    DirectX::SimpleMath::Vector3 GetVelocity() const { return m_Velocity; }
    DirectX::SimpleMath::Quaternion GetRollingRotation() const { return m_RollingRotation; }

    float GetRadius() const { return m_Radius; }
    void SetRadius(float radius) { m_Radius = radius; }

    Collision::Sphere GetSphere() const
    {
        // 現在位置と半径から、当たり判定用の球情報を作成する
        return { m_Transform.position, m_Radius };
    }

    bool IsStopped() const
    {
        // 速度の二乗が小さい場合、停止中として扱う
        return m_Velocity.LengthSquared() < 0.03f;
    }

    //=======================================
    // モデル・描画
    //=======================================
    void LoadModel(const char* modelFilePath, const char* texDirectory);
    void DrawMesh(const DirectX::SimpleMath::Matrix& worldMtx);
    virtual void DrawImGui(const std::string& label);

protected:
    //=======================================
    // 内部補助
    //=======================================
    void UpdateRadius()
    {
        // モデル本来の半径に、XYZの最大スケールを掛けて当たり判定半径を更新する
        float maxScale = m_Transform.scale.x;

        if (m_Transform.scale.y > maxScale) maxScale = m_Transform.scale.y;
        if (m_Transform.scale.z > maxScale) maxScale = m_Transform.scale.z;

        m_Radius = m_ModelBaseRadius * maxScale;
    }

protected:
    //=======================================
    // ステータス
    //=======================================
    BallStatus m_Status;                  // ステータス構造体
    int m_HP = 3;                         // 現在HP
    bool m_IsDefeated = false;            // 撃破済みかどうか

    //=======================================
    // 位置
    //=======================================
    DirectX::SimpleMath::Vector3 m_InitialPosition = DirectX::SimpleMath::Vector3::Zero; // 初期位置
    DirectX::SimpleMath::Vector3 m_OldPosition = DirectX::SimpleMath::Vector3::Zero; // 前フレーム位置

    //=======================================
    // 物理パラメータ
    //=======================================
    DirectX::SimpleMath::Vector3 m_Velocity = DirectX::SimpleMath::Vector3::Zero;                // 速度
    DirectX::SimpleMath::Vector3 m_Acceleration = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);    // 加速度

    float m_Radius = 2.0f;        // 当たり判定半径
    float m_ModelBaseRadius = 1.0f;        // モデル本来の半径
    float m_Mass = 1.0f;        // 質量
    float m_Restitution = 0.8f;        // 反発係数
    float m_Friction = 0.02f;       // 摩擦係数

    //=======================================
    // 描画リソース
    //=======================================
    MeshRenderer m_MeshRenderer;                          // メッシュ描画用
    std::vector<std::unique_ptr<Material>> m_Materials;    // マテリアル一覧
    std::vector<SUBSET> m_subsetList;                      // サブセット一覧
    std::vector<std::unique_ptr<Texture>> m_Textures;      // テクスチャ一覧

    //=======================================
    // 回転情報
    //=======================================
    DirectX::SimpleMath::Quaternion m_RollingRotation = DirectX::SimpleMath::Quaternion::Identity; // 転がり回転
};
