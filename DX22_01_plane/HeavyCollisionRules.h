#pragma once

#include <algorithm>

namespace HeavyCollisionRules
{
	struct State
	{
		int collisionCount = 0;
	};

	inline void Reset(State& state)
	{
		state = {};
	}

	inline void RecordEnemyEnemyCollision(State& state)
	{
		state.collisionCount = (std::min)(9999, state.collisionCount + 1);
	}

	struct FinisherResult
	{
		int referenced = 0;
		int consumed = 0;
		int bonusDamage = 0;
	};

	// consumeAmount <= 0 means that the finisher cashes in the complete pool.
	inline FinisherResult ConsumeForFinisher(
		State& state,
		float damagePerCollision,
		int consumeAmount)
	{
		FinisherResult result;
		result.referenced = (std::max)(0, state.collisionCount);
		result.consumed = consumeAmount <= 0
			? result.referenced
			: (std::min)(result.referenced, consumeAmount);
		result.bonusDamage = (std::max)(0, static_cast<int>(
			damagePerCollision * static_cast<float>(result.consumed) + 0.5f));
		state.collisionCount -= result.consumed;
		return result;
	}
}
