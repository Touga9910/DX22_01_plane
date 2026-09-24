#pragma once

#include <algorithm>

// アンカーカテゴリで使用するスタックの加算・移動・消費ルールをまとめる
namespace AnchorStackRules
{
	// プレイヤー側が保持するアンカースタック状態
	struct State
	{
		int playerStacks = 0;	// 現在プレイヤー側に蓄積しているスタック数
	};

	// valueへ負数を無視したamountを加え、0～maximumへ制限した値を返す
	inline int AddClamped(int value, int amount, int maximum)
	{
		return std::clamp(value + (std::max)(0, amount), 0, (std::max)(0, maximum));
	}

	// プレイヤー側へスタックを加算し、実際に増加した数を返す
	inline int AddPlayerStacks(State& state, int amount, int maximum)
	{
		const int before = state.playerStacks;
		state.playerStacks = AddClamped(state.playerStacks, amount, maximum);
		return state.playerStacks - before;
	}

	// 敵側へスタックを加算し、実際に増加した数を返す
	inline int AddEnemyStacks(int& enemyStacks, int amount, int maximum)
	{
		const int before = enemyStacks;
		enemyStacks = AddClamped(enemyStacks, amount, maximum);
		return enemyStacks - before;
	}

	// 敵から別の敵へスタックを移動する(コピーではなく移動なのでsourceは必ず0になる)
	// targetには上限まで加算し、実際にtargetへ移動できたスタック数を返す
	inline int TransferEnemyToEnemy(int& source, int& target, int maximum)
	{
		const int moving = (std::max)(0, source);
		source = 0;
		const int before = target;
		target = AddClamped(target, moving, maximum);
		return target - before;
	}

	// 敵が持つスタックをプレイヤー側へ移動
	// enemyは必ず0になり、実際にプレイヤー側へ移動できたスタック数を返す
	inline int TransferEnemyToPlayer(int& enemy, State& state, int maximum)
	{
		const int moving = (std::max)(0, enemy);
		enemy = 0;
		return AddPlayerStacks(state, moving, maximum);
	}

	// プレイヤー側のスタックを指定数消費し、実際に消費した数を返す
	// amountが0以下の場合は現在保持しているスタックをすべて消費
	inline int ConsumePlayerStacks(State& state, int amount)
	{
		const int available = (std::max)(0, state.playerStacks);
		const int consumed = amount <= 0 ? available : (std::min)(available, amount);
		state.playerStacks -= consumed;
		return consumed;
	}
}
