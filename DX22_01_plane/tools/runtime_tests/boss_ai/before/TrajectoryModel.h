#pragma once

//#include <DirectX.h>
#include "SimpleMath.h"
#include "Collision.h"

// 弾道予測用の物理モデルインターフェース
class ITrajectoryModel
{
public:
	virtual ~ITrajectoryModel() = default;

	// 各フレームのシミュレーション処理
	// position: シミュレーション中のボール位置（入出力）
	// velocity: シミュレーション中のボール速度（入出力）
	// acceleration: シミュレーション中のボール加速度（入出力）
	virtual void SimulateStep(
		DirectX::SimpleMath::Vector3& position,
		DirectX::SimpleMath::Vector3& velocity,
		DirectX::SimpleMath::Vector3& acceleration) = 0;

	// 衝突時の応答を計算
	// velocity: 衝突後の速度（入出力）
	// normal: 衝突面の法線ベクトル
	virtual void HandleCollision(
		DirectX::SimpleMath::Vector3& velocity,
		const DirectX::SimpleMath::Vector3& normal) = 0;

	// シミュレーション終了判定
	virtual bool ShouldStop(const DirectX::SimpleMath::Vector3& velocity) = 0;

	virtual const char* GetModelName() const = 0;
};

//=====================================================================
// BallTrajectoryModel: ボール本体の物理モデル
// 現在のPlayerBall/Update()と同じ物理定数を使用
//=====================================================================
class BallTrajectoryModel : public ITrajectoryModel
{
private:
	// 物理定数（PlayerBall.cpp の Update() から抽出）
	static constexpr float GRAVITY = 0.1f;           // 重力
	static constexpr float DECELERATION_POWER = 0.02f; // 摩擦係数
	static constexpr float RESTITUTION = 0.5f;      // 反発係数
	static constexpr float FRICTION = 0.95f;        // 地面摩擦
	static constexpr float VELOCITY_THRESHOLD = 0.03f; // 速度閾値

public:
	void SimulateStep(
		DirectX::SimpleMath::Vector3& position,
		DirectX::SimpleMath::Vector3& velocity,
		DirectX::SimpleMath::Vector3& acceleration) override;

	void HandleCollision(
		DirectX::SimpleMath::Vector3& velocity,
		const DirectX::SimpleMath::Vector3& normal) override;

	bool ShouldStop(const DirectX::SimpleMath::Vector3& velocity) override;

	const char* GetModelName() const override { return "Ball"; }
};

//=====================================================================
// SimpleTrajectoryModel: 簡略化されたモデル（テスト/デバッグ用）
// 衝突なし、簡単な物理のみ
//=====================================================================
class SimpleTrajectoryModel : public ITrajectoryModel
{
private:
	static constexpr float GRAVITY = 0.15f;        // 若干強い重力
	static constexpr float AIR_RESISTANCE = 0.98f; // 空気抵抗
	static constexpr float VELOCITY_THRESHOLD = 0.1f;

public:
	void SimulateStep(
		DirectX::SimpleMath::Vector3& position,
		DirectX::SimpleMath::Vector3& velocity,
		DirectX::SimpleMath::Vector3& acceleration) override;

	void HandleCollision(
		DirectX::SimpleMath::Vector3& velocity,
		const DirectX::SimpleMath::Vector3& normal) override;

	bool ShouldStop(const DirectX::SimpleMath::Vector3& velocity) override;

	const char* GetModelName() const override { return "Simple"; }
};

//=====================================================================
// HighRollingTrajectoryModel: 転がりが強いモデル（グリーン用）
// 摩擦が大きく、より早く止まる
//=====================================================================
class HighRollingTrajectoryModel : public ITrajectoryModel
{
private:
	static constexpr float GRAVITY = 0.08f;         // 弱い重力
	static constexpr float DECELERATION_POWER = 0.05f; // 大きな摩擦
	static constexpr float RESTITUTION = 0.3f;     // 低い反発
	static constexpr float FRICTION = 0.85f;       // 高い地面摩擦
	static constexpr float VELOCITY_THRESHOLD = 0.02f;

public:
	void SimulateStep(
		DirectX::SimpleMath::Vector3& position,
		DirectX::SimpleMath::Vector3& velocity,
		DirectX::SimpleMath::Vector3& acceleration) override;

	void HandleCollision(
		DirectX::SimpleMath::Vector3& velocity,
		const DirectX::SimpleMath::Vector3& normal) override;

	bool ShouldStop(const DirectX::SimpleMath::Vector3& velocity) override;

	const char* GetModelName() const override { return "HighRolling"; }
};

//=====================================================================
// SlipperyTrajectoryModel: 滑るモデル（ラフ用）
// 摩擦が小さく、より遠くへ行く
//=====================================================================
class SlipperyTrajectoryModel : public ITrajectoryModel
{
private:
	static constexpr float GRAVITY = 0.12f;        // 通常の重力
	static constexpr float DECELERATION_POWER = 0.01f; // 小さな摩擦
	static constexpr float RESTITUTION = 0.6f;    // 高い反発
	static constexpr float FRICTION = 0.98f;      // 低い地面摩擦
	static constexpr float VELOCITY_THRESHOLD = 0.05f;

public:
	void SimulateStep(
		DirectX::SimpleMath::Vector3& position,
		DirectX::SimpleMath::Vector3& velocity,
		DirectX::SimpleMath::Vector3& acceleration) override;

	void HandleCollision(
		DirectX::SimpleMath::Vector3& velocity,
		const DirectX::SimpleMath::Vector3& normal) override;

	bool ShouldStop(const DirectX::SimpleMath::Vector3& velocity) override;

	const char* GetModelName() const override { return "Slippery"; }
};
