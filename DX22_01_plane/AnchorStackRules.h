#pragma once

#include "SimpleMath.h"

#include <algorithm>
#include <cmath>

// Value-only rules for adding, converting, and consuming anchor stacks.
namespace AnchorStackRules
{
	// Player-owned anchor state shared across enemies.
	struct State
	{
		int playerStacks = 0;
		int conversionProgress = 0; // Shared remainder of the 5:1 conversion.
		bool returnPositionPending = false;
		DirectX::SimpleMath::Vector3 returnPosition = DirectX::SimpleMath::Vector3::Zero;
		int nextAnchorGrantBonus = 0;
	};

	static constexpr int EnemyStacksPerPlayerStack = 5;

	// Add a non-negative amount and clamp it to the configured maximum.
	inline int AddClamped(int value, int amount, int maximum)
	{
		return std::clamp(value + (std::max)(0, amount), 0, (std::max)(0, maximum));
	}

	// Add player stacks and return the amount actually added.
	inline int AddPlayerStacks(State& state, int amount, int maximum)
	{
		const int before = state.playerStacks;
		state.playerStacks = AddClamped(state.playerStacks, amount, maximum);
		return state.playerStacks - before;
	}

	// Add enemy stacks and return the amount actually added.
	inline int AddEnemyStacks(int& enemyStacks, int amount, int maximum)
	{
		const int before = enemyStacks;
		enemyStacks = AddClamped(enemyStacks, amount, maximum);
		return enemyStacks - before;
	}

	// Consume player stacks. A non-positive amount consumes all stacks.
	inline int ConsumePlayerStacks(State& state, int amount)
	{
		const int available = (std::max)(0, state.playerStacks);
		const int consumed = amount <= 0 ? available : (std::min)(available, amount);
		state.playerStacks -= consumed;
		return consumed;
	}

	// Consume one enemy's stacks and feed the shared 5:1 conversion gauge.
	struct EnemyConsumeResult
	{
		int consumed = 0;
		int playerStacksGenerated = 0;
	};

	inline EnemyConsumeResult ConsumeEnemyStacks(
		int& enemyStacks,
		State& state,
		int amount,
		int playerStackMaximum)
	{
		const int available = (std::max)(0, enemyStacks);
		const int consumed = amount <= 0
			? available
			: (std::min)(available, amount);
		enemyStacks -= consumed;

		const int totalProgress = state.conversionProgress + consumed;
		const int generated = totalProgress / EnemyStacksPerPlayerStack;
		state.conversionProgress = totalProgress % EnemyStacksPerPlayerStack;
		const int actuallyAdded = AddPlayerStacks(
			state, generated, playerStackMaximum);
		return { consumed, actuallyAdded };
	}

	inline int CalculateEnemyGrant(
		int baseAmount,
		int playerStacks,
		int playerStacksPerBonus,
		int oneShotBonus)
	{
		const int growth = playerStacksPerBonus > 0
			? (std::max)(0, playerStacks) / playerStacksPerBonus
			: 0;
		return (std::max)(0, baseAmount) + growth +
			(std::max)(0, oneShotBonus);
	}

	inline int CalculateShield(int consumedEnemyStacks, int maximumShield)
	{
		const int consumed = std::clamp(
			consumedEnemyStacks, 0, EnemyStacksPerPlayerStack);
		const int maximum = (std::max)(0, maximumShield);
		if (consumed == 0 || maximum == 0) return 0;
		return (std::max)(1, static_cast<int>(std::lround(
			static_cast<float>(consumed * maximum) /
			static_cast<float>(EnemyStacksPerPlayerStack))));
	}

	inline int FinisherDamagePerEnemyStack(int playerStacksBeforeConsume)
	{
		return 2 + (std::min)(
			2,
			(std::max)(0, playerStacksBeforeConsume) / 5);
	}

	inline int DefeatAutoConsumeAmount(int enemyStacks)
	{
		return (std::max)(0, enemyStacks) / 2;
	}

	inline void SaveReturnPosition(
		State& state,
		const DirectX::SimpleMath::Vector3& position)
	{
		state.returnPosition = position;
		state.returnPositionPending = true;
	}

	inline bool ConsumeReturnPosition(
		State& state,
		DirectX::SimpleMath::Vector3& position)
	{
		if (!state.returnPositionPending) return false;
		position = state.returnPosition;
		state.returnPositionPending = false;
		state.returnPosition = DirectX::SimpleMath::Vector3::Zero;
		return true;
	}
}
