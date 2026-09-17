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
		bool usableThisShot = false;
		float speedMultiplier = 1.0f;
		int stackCount = 0;
		int maxStack = 3;
	};

	enum class UseKind
	{
		None,
		Generated,
		Strong,
		Weak,
	};

	struct ContactResult
	{
		UseKind kind = UseKind::None;
		int generated = 0;
		int consumed = 0;
		int damageBonus = 0;
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
			if (charge.stackCount > 0)
			{
				charge.active = true;
				charge.usableThisShot = true;
			}
		}
	}

	// Stacks no longer decay just because a shot ended. Fresh stacks become
	// usable on the next shot; unspent stacks remain on the table.
	inline void EndPlayerShot(State& state)
	{
		for (Charge& charge : state)
		{
			charge.active = charge.stackCount > 0;
			charge.usableThisShot = false;
		}
	}

	inline ContactResult ApplyStackContact(
		State& state,
		int region,
		int generateAmount,
		int maximumStack,
		int consumeAmount,
		bool strongUse,
		bool allowMultipleStrongUses,
		float generatedSpeedMultiplier,
		float weakSpeedMultiplier,
		int strongDamageBonus,
		int finisherBonusPerUse,
		bool& strongUseConsumedThisShot,
		int& strongUseCountThisShot,
		Vector3& reflectedVelocity)
	{
		if (region < 0 || region >= RegionCount) return {};
		Charge& charge = state[static_cast<std::size_t>(region)];
		charge.maxStack = std::clamp(maximumStack, 1, 99);
		if (generateAmount > 0)
		{
			const int before = charge.stackCount;
			charge.stackCount = std::clamp(
				charge.stackCount + generateAmount, 0, charge.maxStack);
			charge.active = charge.stackCount > 0;
			charge.usableThisShot = false;
			charge.speedMultiplier = std::clamp(generatedSpeedMultiplier, 1.0f, 3.0f);
			return { UseKind::Generated, charge.stackCount - before, 0, 0 };
		}
		if (!charge.active || !charge.usableThisShot || charge.stackCount <= 0)
			return {};
		if (strongUse && strongUseConsumedThisShot && !allowMultipleStrongUses)
			return {};

		const int requested = strongUse ? (std::max)(1, consumeAmount) : 1;
		const int consumed = (std::min)(charge.stackCount, requested);
		charge.stackCount -= consumed;
		charge.active = charge.stackCount > 0;
		if (!charge.active) charge.usableThisShot = false;
		if (strongUse)
		{
			reflectedVelocity *= charge.speedMultiplier;
			strongUseConsumedThisShot = true;
			++strongUseCountThisShot;
			return { UseKind::Strong, 0, consumed,
				strongDamageBonus + finisherBonusPerUse };
		}
		reflectedVelocity *= std::clamp(weakSpeedMultiplier, 1.0f, 1.25f);
		return { UseKind::Weak, 0, consumed, 0 };
	}

	// Returns true only when this contact applies the one speed boost for the shot.
	inline bool ApplyPlayerWallContact(
		State& state,
		int region,
		float chargerSpeedMultiplier,
		bool& boostConsumedThisShot,
		Vector3& reflectedVelocity)
	{
		int useCount = 0;
		const ContactResult result = ApplyStackContact(
			state, region, IsCharger(chargerSpeedMultiplier) ? 1 : 0, 1, 1,
			true, false, chargerSpeedMultiplier, 1.0f, 0, 0,
			boostConsumedThisShot, useCount, reflectedVelocity);
		return result.kind == UseKind::Strong;
	}

	inline int ActiveCount(const State& state)
	{
		return static_cast<int>(std::count_if(
			state.begin(), state.end(), [](const Charge& charge) { return charge.stackCount > 0; }));
	}

	inline int PendingNextShotCount(const State& state)
	{
		return static_cast<int>(std::count_if(
			state.begin(), state.end(), [](const Charge& charge)
			{
				return charge.stackCount > 0 && !charge.usableThisShot;
			}));
	}

	inline int TotalStacks(const State& state)
	{
		int total = 0;
		for (const Charge& charge : state) total += (std::max)(0, charge.stackCount);
		return total;
	}
}
