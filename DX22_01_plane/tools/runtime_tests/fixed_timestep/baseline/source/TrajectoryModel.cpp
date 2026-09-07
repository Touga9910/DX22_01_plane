#include "TrajectoryModel.h"
#include "Game.h"
#include "Ground.h"

using namespace DirectX::SimpleMath;

//=====================================================================
// BallTrajectoryModel の実装
//=====================================================================

void BallTrajectoryModel::SimulateStep(
	Vector3& position,
	Vector3& velocity,
	Vector3& acceleration)
{
	// 1. 減速の計算
	if (velocity.LengthSquared() > VELOCITY_THRESHOLD)
	{
		Vector3 deceleration = -velocity;
		deceleration.Normalize();
		acceleration = deceleration * DECELERATION_POWER;
		velocity += acceleration;
	}

	// 2. 重力を適用
	velocity.y -= GRAVITY;

	// 3. 位置を更新
	position += velocity;
}

void BallTrajectoryModel::HandleCollision(
	Vector3& velocity,
	const Vector3& normal)
{
	// 速度を法線成分と接線成分に分解
	float velocityNormal = Collision::Dot(velocity, normal);
	Vector3 v1 = velocityNormal * normal;      // 法線方向の速度
	Vector3 v2 = velocity - v1;                // 接線方向の速度

	// 反射速度ベクトルを計算
	Vector3 reflectVelocity = v2 * FRICTION - v1 * RESTITUTION;
	velocity = reflectVelocity;
}

bool BallTrajectoryModel::ShouldStop(const Vector3& velocity)
{
	// 速度が非常に小さくなったら停止
	return velocity.LengthSquared() < 0.0001f;
}

//=====================================================================
// SimpleTrajectoryModel の実装
//=====================================================================

void SimpleTrajectoryModel::SimulateStep(
	Vector3& position,
	Vector3& velocity,
	Vector3& acceleration)
{
	// 1. 空気抵抗を適用（速度を減衰させる）
	velocity *= AIR_RESISTANCE;

	// 2. 重力を適用
	velocity.y -= GRAVITY;

	// 3. 位置を更新
	position += velocity;
}

void SimpleTrajectoryModel::HandleCollision(
	Vector3& velocity,
	const Vector3& normal)
{
	// 簡単な衝突応答：法線方向の速度を反転
	float velocityNormal = Collision::Dot(velocity, normal);
	if (velocityNormal < 0)
	{
		velocity -= 2.0f * velocityNormal * normal;
		velocity *= 0.5f; // 衝突時のエネルギー損失
	}
}

bool SimpleTrajectoryModel::ShouldStop(const Vector3& velocity)
{
	return velocity.LengthSquared() < VELOCITY_THRESHOLD * VELOCITY_THRESHOLD;
}

//=====================================================================
// HighRollingTrajectoryModel の実装
//=====================================================================

void HighRollingTrajectoryModel::SimulateStep(
	Vector3& position,
	Vector3& velocity,
	Vector3& acceleration)
{
	// 1. 大きな摩擦を適用
	if (velocity.LengthSquared() > VELOCITY_THRESHOLD)
	{
		Vector3 deceleration = -velocity;
		deceleration.Normalize();
		acceleration = deceleration * DECELERATION_POWER;
		velocity += acceleration;
	}

	// 2. 弱い重力
	velocity.y -= GRAVITY;

	// 3. 位置を更新
	position += velocity;
}

void HighRollingTrajectoryModel::HandleCollision(
	Vector3& velocity,
	const Vector3& normal)
{
	// 速度を法線成分と接線成分に分解
	float velocityNormal = Collision::Dot(velocity, normal);
	Vector3 v1 = velocityNormal * normal;
	Vector3 v2 = velocity - v1;

	// 低い反発係数で強く減速
	Vector3 reflectVelocity = v2 * FRICTION - v1 * RESTITUTION;
	velocity = reflectVelocity;
}

bool HighRollingTrajectoryModel::ShouldStop(const Vector3& velocity)
{
	return velocity.LengthSquared() < 0.005f; // より早く停止
}

//=====================================================================
// SlipperyTrajectoryModel の実装
//=====================================================================

void SlipperyTrajectoryModel::SimulateStep(
	Vector3& position,
	Vector3& velocity,
	Vector3& acceleration)
{
	// 1. 小さな摩擦のみを適用
	if (velocity.LengthSquared() > VELOCITY_THRESHOLD)
	{
		Vector3 deceleration = -velocity;
		deceleration.Normalize();
		acceleration = deceleration * DECELERATION_POWER; // 0.01f で非常に小さい
		velocity += acceleration;
	}

	// 2. 通常の重力
	velocity.y -= GRAVITY;

	// 3. 位置を更新
	position += velocity;
}

void SlipperyTrajectoryModel::HandleCollision(
	Vector3& velocity,
	const Vector3& normal)
{
	// 速度を法線成分と接線成分に分解
	float velocityNormal = Collision::Dot(velocity, normal);
	Vector3 v1 = velocityNormal * normal;
	Vector3 v2 = velocity - v1;

	// 高い反発係数で跳ねやすい
	Vector3 reflectVelocity = v2 * FRICTION - v1 * RESTITUTION;
	velocity = reflectVelocity;
}

bool SlipperyTrajectoryModel::ShouldStop(const Vector3& velocity)
{
	return velocity.LengthSquared() < 0.02f; // 遅く停止
}
