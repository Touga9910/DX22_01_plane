#pragma once

#include "SimpleMath.h"

#include <algorithm>

// アンカーカテゴリで使用するスタックの加算・移動・消費ルールをまとめる
namespace AnchorStackRules
{
	// プレイヤー側が保持するアンカースタック状態
	struct State
	{
		int playerStacks = 0;	// 現在プレイヤー側に蓄積しているスタック数
		int conversionProgress = 0; // Enemy Anchor消費の5:1変換で残った共有端数
		bool returnPositionPending = false;
		DirectX::SimpleMath::Vector3 returnPosition = DirectX::SimpleMath::Vector3::Zero;
		int nextAnchorGrantBonus = 0;
	};

	static constexpr int EnemyStacksPerPlayerStack = 5;

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

	// プレイヤー側のスタックを指定数消費し、実際に消費した数を返す
	// amountが0以下の場合は現在保持しているスタックをすべて消費
	inline int ConsumePlayerStacks(State& state, int amount)
	{
		const int available = (std::max)(0, state.playerStacks);
		const int consumed = amount <= 0 ? available : (std::min)(available, amount);
		state.playerStacks -= consumed;
		return consumed;
	}

	// 指定した敵のEnemy Anchorだけを消費し、5個ごとにPlayer Anchorへ変換する。
	// 5未満の端数は敵ごとではなくState内の共有ゲージへ保持する。
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
