#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

// Manages persistent Heavy stacks, per-shot enemy movement, and temporary effects.
namespace HeavyCollisionRules
{
	static constexpr float DefaultDistancePerStack = 40.0f;
	static constexpr int DefaultGainLimitPerTurn = 10;

	struct GainResult
	{
		int requested = 0;
		int granted = 0;
		int discardedByTurnLimit = 0;
		int discardedByStorageLimit = 0;
		int turnLimit = DefaultGainLimitPerTurn;

		/// <summary>
		/// スタックが一つ以上付与されたかを返す。
		/// </summary>
		explicit operator bool() const
		{
			return granted > 0;
		}
	};

	struct State
	{
		int stackCount = 0;
		float distancePerStack = DefaultDistancePerStack;
		int gainLimitPerTurn = DefaultGainLimitPerTurn;
		int gainedThisTurn = 0;
		std::unordered_map<std::uintptr_t, float> distanceByEnemyThisShot;
		std::unordered_set<std::uintptr_t> microGeneratedEnemiesThisShot;
		float pendingEnemyCollisionMultiplier = 1.0f;
		float activeEnemyCollisionMultiplier = 1.0f;
	};

	/// <summary>
	/// 距離とターン上限の設定を保ち、衝突スタックの状態を初期化する。
	/// </summary>
	inline void Reset(State& state)
	{
		const float distancePerStack =
			std::isfinite(state.distancePerStack) && state.distancePerStack > 0.0f
			? state.distancePerStack
			: DefaultDistancePerStack;
		const int gainLimit = (std::max)(0, state.gainLimitPerTurn);
		state = {};
		state.distancePerStack = distancePerStack;
		state.gainLimitPerTurn = gainLimit;
	}

	/// <summary>
	/// スタックを一つ生成するために必要な移動距離を設定する。
	/// </summary>
	inline void SetDistancePerStack(State& state, float distance)
	{
		state.distancePerStack =
			std::isfinite(distance) && distance > 0.0f
			? distance
			: DefaultDistancePerStack;
	}

	/// <summary>
	/// ターン内の生成上限を設定し、現在の生成数をその上限内に収める。
	/// </summary>
	inline void SetGainLimitPerTurn(State& state, int gainLimit)
	{
		state.gainLimitPerTurn = (std::max)(0, gainLimit);
		state.gainedThisTurn = (std::min)(
			state.gainedThisTurn,
			state.gainLimitPerTurn);
	}

	/// <summary>
	/// ショット内の生成記録を初期化し、予約された敵衝突倍率を適用する。
	/// </summary>
	inline void BeginShot(State& state)
	{
		state.gainedThisTurn = 0;
		state.distanceByEnemyThisShot.clear();
		state.microGeneratedEnemiesThisShot.clear();
		state.activeEnemyCollisionMultiplier =
			(std::max)(1.0f, state.pendingEnemyCollisionMultiplier);
		state.pendingEnemyCollisionMultiplier = 1.0f;
	}

	/// <summary>
	/// スタック生成の対象となる敵の移動距離をショット内で累積する。
	/// </summary>
	inline void RecordEnemyMovement(
		State& state,
		std::uintptr_t enemyId,
		float distance,
		bool generatesDistanceStacks)
	{
		if (!generatesDistanceStacks || enemyId == 0 || distance <= 0.0f) return;
		state.distanceByEnemyThisShot[enemyId] += distance;
	}

	/// <summary>
	/// ターン上限と保有上限までスタックを付与し、付与数と超過数を返す。
	/// </summary>
	inline GainResult ApplyGain(State& state, int amount)
	{
		GainResult result;
		result.requested = (std::max)(0, amount);
		result.turnLimit = (std::max)(0, state.gainLimitPerTurn);
		const int remainingThisTurn = (std::max)(
			0,
			result.turnLimit - state.gainedThisTurn);
		const int permitted = (std::min)(
			result.requested,
			remainingThisTurn);
		result.discardedByTurnLimit = result.requested - permitted;
		const int storageRemaining =
			(std::numeric_limits<int>::max)() -
			(std::max)(0, state.stackCount);
		result.granted = (std::min)(permitted, storageRemaining);
		result.discardedByStorageLimit = permitted - result.granted;
		state.stackCount += result.granted;
		state.gainedThisTurn += result.granted;
		return result;
	}

	/// <summary>
	/// 敵ごとの移動距離と生成ボーナスからスタックを付与し、ショット内の記録を初期化する。
	/// </summary>
	inline GainResult EndShot(State& state, int generationBonus = 0)
	{
		long long requested = 0;
		for (const auto& [enemyId, distance] : state.distanceByEnemyThisShot)
		{
			(void)enemyId;
			requested += (std::max)(0, static_cast<int>(
				std::floor(distance / state.distancePerStack)));
			requested = (std::min)(
				requested,
				static_cast<long long>((std::numeric_limits<int>::max)()));
		}
		if (requested > 0)
		{
			requested = (std::min)(
				requested + (std::max)(0, generationBonus),
				static_cast<long long>((std::numeric_limits<int>::max)()));
		}
		const GainResult result = ApplyGain(
			state,
			static_cast<int>(requested));
		state.distanceByEnemyThisShot.clear();
		state.microGeneratedEnemiesThisShot.clear();
		state.activeEnemyCollisionMultiplier = 1.0f;
		return result;
	}

	/// <summary>
	/// 同じ敵にはショット中に一度だけスタックを生成する。
	/// </summary>
	inline GainResult GenerateOncePerEnemy(
		State& state,
		std::uintptr_t enemyId,
		int amount = 1)
	{
		GainResult result;
		result.turnLimit = (std::max)(0, state.gainLimitPerTurn);
		if (enemyId == 0 || amount <= 0 ||
			!state.microGeneratedEnemiesThisShot.insert(enemyId).second)
		{
			return result;
		}
		return ApplyGain(state, amount);
	}

	/// <summary>
	/// 指定数まで保有スタックを消費し、実際の消費数を返す。
	/// </summary>
	inline int Consume(State& state, int amount)
	{
		const int requested = (std::max)(0, amount);
		const int consumed = (std::min)(state.stackCount, requested);
		state.stackCount -= consumed;
		return consumed;
	}

	/// <summary>
	/// 次のショットに適用する敵衝突のダメージ倍率を予約する。
	/// </summary>
	inline void ArmNextEnemyCollisionMultiplier(State& state, float multiplier)
	{
		state.pendingEnemyCollisionMultiplier = (std::max)(1.0f, multiplier);
	}

	/// <summary>
	/// ショット中の倍率を敵同士の衝突ダメージに適用する。
	/// </summary>
	inline int ScaleEnemyCollisionDamage(const State& state, int damage)
	{
		return (std::max)(0, static_cast<int>(std::lround(
			static_cast<float>(damage) * state.activeEnemyCollisionMultiplier)));
	}

	struct FinisherResult
	{
		int referenced = 0;
		int consumed = 0;
		int bonusDamage = 0;
	};

	/// <summary>
	/// フィニッシャー用のスタックを消費し、消費数に応じた追加ダメージを求める。
	/// </summary>
	inline FinisherResult ConsumeForFinisher(
		State& state,
		float damagePerStack,
		int consumeAmount)
	{
		FinisherResult result;
		result.referenced = (std::max)(0, state.stackCount);
		result.consumed = Consume(state, consumeAmount);
		result.bonusDamage = (std::max)(0, static_cast<int>(
			damagePerStack * static_cast<float>(result.consumed) + 0.5f));
		return result;
	}
}
