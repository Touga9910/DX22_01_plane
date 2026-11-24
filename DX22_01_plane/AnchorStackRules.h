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

	/// <summary>
	/// 加算量と上限を非負として扱い、加算後のスタック数を0から上限までに収める。
	/// </summary>
	inline int AddClamped(int value, int amount, int maximum)
	{
		return std::clamp(value + (std::max)(0, amount), 0, (std::max)(0, maximum));
	}

	/// <summary>
	/// プレイヤーのスタックを上限まで加え、実際に増えた数を返す。
	/// </summary>
	inline int AddPlayerStacks(State& state, int amount, int maximum)
	{
		const int before = state.playerStacks;
		state.playerStacks = AddClamped(state.playerStacks, amount, maximum);
		return state.playerStacks - before;
	}

	/// <summary>
	/// 敵のスタックを上限まで加え、実際に増えた数を返す。
	/// </summary>
	inline int AddEnemyStacks(int& enemyStacks, int amount, int maximum)
	{
		const int before = enemyStacks;
		enemyStacks = AddClamped(enemyStacks, amount, maximum);
		return enemyStacks - before;
	}

	inline int CalculateEnemyGrant(
		int baseAmount,
		int playerStacks,
		int playerStacksPerBonus,
		int oneShotBonus);

	struct EnemyGrantResult
	{
		int requested = 0;
		int granted = 0;
		int before = 0;
		int after = 0;
	};

	// Resolve an anchor grant at collision time. Keeping this value-only makes
	// the grant independent from a later player pocket/despawn event.
	/// <summary>
	/// 衝突で敵に付与するスタックを計算し、付与前後の数と付与量を返す。
	/// </summary>
	inline EnemyGrantResult GrantEnemyStacksOnCollision(
		int& enemyStacks,
		int baseAmount,
		int playerStacks,
		int playerStacksPerBonus,
		int oneShotBonus,
		int maximum)
	{
		EnemyGrantResult result;
		result.before = enemyStacks;
		result.requested = CalculateEnemyGrant(
			baseAmount,
			playerStacks,
			playerStacksPerBonus,
			oneShotBonus);
		result.granted = AddEnemyStacks(
			enemyStacks, result.requested, maximum);
		result.after = enemyStacks;
		return result;
	}

	struct EnemyShareResult
	{
		int firstBefore = 0;
		int secondBefore = 0;
		int shared = 0;
		int firstGained = 0;
		int secondGained = 0;
	};

	// Living-enemy filtering belongs to the collision caller. This rule only
	// shares the larger current value and never sums the two values.
	/// <summary>
	/// 2体の敵のスタックを上限内に収め、高い方の数を両方に適用する。
	/// </summary>
	inline EnemyShareResult ShareEnemyStacks(
		int& firstStacks,
		int& secondStacks,
		int maximum)
	{
		EnemyShareResult result;
		const int cap = (std::max)(0, maximum);
		result.firstBefore = std::clamp(firstStacks, 0, cap);
		result.secondBefore = std::clamp(secondStacks, 0, cap);
		result.shared = (std::max)(result.firstBefore, result.secondBefore);
		firstStacks = result.shared;
		secondStacks = result.shared;
		result.firstGained = result.shared - result.firstBefore;
		result.secondGained = result.shared - result.secondBefore;
		return result;
	}

	/// <summary>
	/// プレイヤーのスタックを指定量だけ消費する。指定量が0以下の場合は全量を消費する。
	/// </summary>
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

	/// <summary>
	/// 敵のスタックを消費し、その数に応じたプレイヤーのスタック生成数と換算の進捗を求める。
	/// </summary>
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

	/// <summary>
	/// 基本値にプレイヤーのスタックによる加算と一度限りの加算を足し、敵への付与量を求める。
	/// </summary>
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

	/// <summary>
	/// 消費した敵スタック数を上限で制限し、1スタック当たりの値を掛けてシールド量を求める。
	/// </summary>
	inline int CalculateShield(int consumedEnemyStacks, int shieldPerStack)
	{
		const int consumed = std::clamp(
			consumedEnemyStacks, 0, EnemyStacksPerPlayerStack);
		return consumed * (std::max)(0, shieldPerStack);
	}

	/// <summary>
	/// 消費前のプレイヤースタック数に応じて、敵スタック1つ当たりのフィニッシャーダメージを求める。
	/// </summary>
	inline int FinisherDamagePerEnemyStack(int playerStacksBeforeConsume)
	{
		return 2 + (std::min)(
			2,
			(std::max)(0, playerStacksBeforeConsume) / 5);
	}

	/// <summary>
	/// 撃破時に自動消費する敵スタック数として、現在の非負のスタック数の半分を返す。
	/// </summary>
	inline int DefeatAutoConsumeAmount(int enemyStacks)
	{
		return (std::max)(0, enemyStacks) / 2;
	}

	/// <summary>
	/// 復帰先の座標を保存し、未使用の復帰位置として記録する。
	/// </summary>
	inline void SaveReturnPosition(
		State& state,
		const DirectX::SimpleMath::Vector3& position)
	{
		state.returnPosition = position;
		state.returnPositionPending = true;
	}

	/// <summary>
	/// 保存済みの復帰位置を取り出し、次の呼び出しで再利用されないように消去する。
	/// </summary>
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
