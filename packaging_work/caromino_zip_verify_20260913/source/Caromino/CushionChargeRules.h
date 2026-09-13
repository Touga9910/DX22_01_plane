#pragma once

#include "Collision.h"
#include "TableConfig.h"

#include <algorithm>
#include <array>
#include <cmath>

// Shared 12-region state: four parts on each long side and two on each short side.
namespace CushionChargeRules
{
	using DirectX::SimpleMath::Vector3;

	static constexpr int RegionCount = 12;

	struct Charge
	{
		bool active = false;
		// A charge becomes usable at the beginning of the following player shot.
		// Charges created or refreshed during the current shot remain false.
		bool usableThisShot = false;
		float speedMultiplier = 1.0f;
	};

	using State = std::array<Charge, RegionCount>;

	inline int RegionFromContact(
		const Collision::Segment& wall,
		const Vector3& contact)
	{
		const Vector3 direction = wall.end - wall.start;
		const float halfWidth = TableConfig::GetFieldWidth() * 0.5f;
		const float halfDepth = TableConfig::GetFieldDepth() * 0.5f;
		if (std::abs(direction.x) >= std::abs(direction.z))
		{
			const float normalized = std::clamp(
				(contact.x + halfWidth) / (halfWidth * 2.0f), 0.0f, 0.999999f);
			const int part = static_cast<int>(normalized * 4.0f);
			return contact.z >= 0.0f ? part : 4 + part;
		}

		const float normalized = std::clamp(
			(contact.z + halfDepth) / (halfDepth * 2.0f), 0.0f, 0.999999f);
		const int part = static_cast<int>(normalized * 2.0f);
		return contact.x < 0.0f ? 8 + part : 10 + part;
	}

	inline bool IsCharger(float speedMultiplier)
	{
		return speedMultiplier > 1.0001f;
	}

	inline void BeginPlayerShot(State& state)
	{
		for (Charge& charge : state)
		{
			if (charge.active)
			{
				charge.usableThisShot = true;
			}
		}
	}

	// Unused charges are valid for exactly one following player shot.
	// Fresh charges have usableThisShot == false and survive for that next shot.
	inline void EndPlayerShot(State& state)
	{
		for (Charge& charge : state)
		{
			if (charge.active && charge.usableThisShot)
			{
				charge = {};
			}
		}
	}

	// Returns true only when this contact applies the one speed boost for the shot.
	inline bool ApplyPlayerWallContact(
		State& state,
		int region,
		float chargerSpeedMultiplier,
		bool& boostConsumedThisShot,
		Vector3& reflectedVelocity)
	{
		if (region < 0 || region >= RegionCount) return false;
		Charge& charge = state[static_cast<std::size_t>(region)];
		if (IsCharger(chargerSpeedMultiplier))
		{
			charge.active = true;
			charge.usableThisShot = false;
			charge.speedMultiplier = std::clamp(chargerSpeedMultiplier, 1.0f, 3.0f);
			return false;
		}
		if (!charge.active || !charge.usableThisShot) return false;
		const bool applyBoost = !boostConsumedThisShot;
		if (applyBoost)
		{
			reflectedVelocity *= charge.speedMultiplier;
			boostConsumedThisShot = true;
		}
		charge = {};
		return applyBoost;
	}

	inline int ActiveCount(const State& state)
	{
		return static_cast<int>(std::count_if(
			state.begin(), state.end(), [](const Charge& charge) { return charge.active; }));
	}

	inline int PendingNextShotCount(const State& state)
	{
		return static_cast<int>(std::count_if(
			state.begin(), state.end(), [](const Charge& charge)
			{
				return charge.active && !charge.usableThisShot;
			}));
	}
}
