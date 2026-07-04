#pragma once
#include "BallBase.h"

class PlayerBall;

class EnemyBall : public BallBase
{
public:
    EnemyBall() = default;
    ~EnemyBall() = default;

    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    void Defeat() override;

	// プレイヤーに攻撃するための関数
    void Attack(PlayerBall* player);

    // 敵の初期位置を設定するための関数
    void SetInitPosition(DirectX::SimpleMath::Vector3 pos) { m_InitPosition = pos; }
    // ImGuiで敵の情報を表示するための関数
    void DrawImGui(const std::string& label) override;
private:
    // 必要に応じて敵固有の変数を定義
    // 例: 敵の種類、HP、あるいは自律移動用のタイマーなど
    int m_CurrentFrame = 0;

	// 敵の攻撃力を保持する変数
    int m_AttackPower = 1;

    // 敵の初期位置を保持する変数
    DirectX::SimpleMath::Vector3 m_InitPosition = DirectX::SimpleMath::Vector3(50.0f, 0.0f, 50.0f);
};