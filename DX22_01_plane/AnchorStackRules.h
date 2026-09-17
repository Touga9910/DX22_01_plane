#pragma once

#include <algorithm>

namespace AnchorStackRules
{
	struct State
	{
		int playerStacks = 0;
	};

	inline int AddClamped(int value, int amount, int maximum)
	{
		return std::clamp(value + (std::max)(0, amount), 0, (std::max)(0, maximum));
	}

	inline int AddPlayerStacks(State& state, int amount, int maximum)
	{
		const int before = state.playerStacks;
		state.playerStacks = AddClamped(state.playerStacks, amount, maximum);
		return state.playerStacks - before;
	}

	inline int AddEnemyStacks(int& enemyStacks, int amount, int maximum)
	{
		const int before = enemyStacks;
		enemyStacks = AddClamped(enemyStacks, amount, maximum);
		return enemyStacks - before;
	}

	// Transfer, never copy: the source is always emptied and the target receives
	// as much as its cap permits. The normal game cap is intentionally generous.
	inline int TransferEnemyToEnemy(int& source, int& target, int maximum)
	{
		const int moving = (std::max)(0, source);
		source = 0;
		const int before = target;
		target = AddClamped(target, moving, maximum);
		return target - before;
	}

	inline int TransferEnemyToPlayer(int& enemy, State& state, int maximum)
	{
		const int moving = (std::max)(0, enemy);
		enemy = 0;
		return AddPlayerStacks(state, moving, maximum);
	}

	inline int ConsumePlayerStacks(State& state, int amount)
	{
		const int available = (std::max)(0, state.playerStacks);
		const int consumed = amount <= 0 ? available : (std::min)(available, amount);
		state.playerStacks -= consumed;
		return consumed;
	}
}
