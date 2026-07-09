#pragma once
#include "BallBase.h"
#include "EnemyData.h"

class PlayerBall;

class EnemyBall : public BallBase
{
public:
    EnemyBall() = default;
    ~EnemyBall() = default;

    void Init(const EnemyData& data);

    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    void Defeat() override;

	// プレイヤーに攻撃するための関数
    void Attack(PlayerBall* player);

	// 敵のIDを取得するための関数
    const std::string& GetEnemyId() const { return m_EnemyData.id; }

	// ホットリロード用のデータを適用するための関数
    void ApplyHotReloadData(const EnemyData& data);

    void ApplyStatusKeepHpRate(const BallStatus& status)
    {
        if (m_IsDefeated)
        {
            m_Status = status;
            return;
        }

        float hpRate = 1.0f;

        if (m_Status.maxHp > 0)
        {
            hpRate = static_cast<float>(m_HP) / static_cast<float>(m_Status.maxHp);
        }

        m_Status = status;

        int newHp = static_cast<int>(m_Status.maxHp * hpRate);

        if (newHp < 1)
        {
            newHp = 1;
        }

        if (newHp > m_Status.maxHp)
        {
            newHp = m_Status.maxHp;
        }

        m_HP = newHp;
    }
    // ImGuiで敵の情報を表示するための関数
    void DrawImGui(const std::string& label) override;
private:
    // 必要に応じて敵固有の変数を定義
    // 例: 敵の種類、HP、あるいは自律移動用のタイマーなど
    int m_CurrentFrame = 0;

	EnemyData m_EnemyData;  // 敵のデータを保持する変数

    // 敵の初期位置を保持する変数
    DirectX::SimpleMath::Vector3 m_InitPosition = DirectX::SimpleMath::Vector3(50.0f, 0.0f, 50.0f);
};