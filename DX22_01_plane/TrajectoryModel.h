#pragma once

#include <DirectX.h>
#include "SimpleMath.h"

// 弾道予測用の物理モデルインターフェース
class ITrajectoryModel
{
public:
	virtual ~ITrajectoryModel() = default;

	// 各フレームのシミュレーション処理
	virtual void SimulateStep(
		DirectX::SimpleMath::Vector3& position,
		DirectX::SimpleMath::Vector3& velocity,
		DirectX::SimpleMath::Vector3& acceleration) = 0;

	// 衝突時の応答を計算
	virtual void HandleCollision(
		DirectX::SimpleMath::Vector3& velocity,
		const DirectX::SimpleMath::Vector3& normal) = 0;

	// シミュレーション終了判定
	virtual bool ShouldStop(const DirectX::SimpleMath::Vector3& velocity) = 0;

	virtual const char* GetModelName() const = 0;
};

// ボール本体モデル（現在の物理）
class BallTrajectoryModel : public ITrajectoryModel
{
private:
	static constexpr float GRAVITY = 0.1f;
	static constexpr float DECELERATION_POWER = 0.02f;
	static constexpr float RESTITUTION = 0.5f;
	static constexpr float FRICTION = 0.95f;

public:
	void SimulateStep(DirectX::SimpleMath::Vector3& position,
		DirectX::SimpleMath::Vector3& velocity,
		DirectX::SimpleMath::Vector3& acceleration) override;

	void HandleCollision(DirectX::SimpleMath::Vector3& velocity,
		const DirectX::SimpleMath::Vector3& normal) override;

	bool ShouldStop(const DirectX::SimpleMath::Vector3& velocity) override;

	const char* GetModelName() const override { return "Ball"; }
};

// 簡略化されたモデル（テスト用）
class SimpleTrajectoryModel : public ITrajectoryModel
{
private:
	static constexpr float GRAVITY = 0.15f;
	static constexpr float AIR_RESISTANCE = 0.98f;

public:
	void SimulateStep(DirectX::SimpleMath::Vector3& position,
		DirectX::SimpleMath::Vector3& velocity,
		DirectX::SimpleMath::Vector3& acceleration) override;

	void HandleCollision(DirectX::SimpleMath::Vector3& velocity,
		const DirectX::SimpleMath::Vector3& normal) override;

	bool ShouldStop(const DirectX::SimpleMath::Vector3& velocity) override;

	const char* GetModelName() const override { return "Simple"; }
};
