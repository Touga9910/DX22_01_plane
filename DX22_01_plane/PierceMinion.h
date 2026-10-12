#pragma once

#include "BallStatus.h"
#include "Component.h"
#include "SimpleMath.h"

class BallComponent;

class PierceMinion final : public Component
{
public:
	PierceMinion(
		const BallStatus& status,
		const DirectX::SimpleMath::Vector3& position,
		const DirectX::SimpleMath::Vector3& direction,
		float launchSpeed);

	void Awake() override;
	void FixedUpdate() override;
	void Draw() override;
	void OnDestroy() override;
	void Destroy();

	BallComponent* GetBall() const { return m_Ball; }

private:
	BallStatus m_Status;
	DirectX::SimpleMath::Vector3 m_Position;
	DirectX::SimpleMath::Vector3 m_Direction;
	float m_LaunchSpeed = 0.0f;
	BallComponent* m_Ball = nullptr;
};
