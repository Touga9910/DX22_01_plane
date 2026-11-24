#pragma once

#include "SimpleMath.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace PierceTraceRules
{
	using DirectX::SimpleMath::Vector3;

	static constexpr std::size_t MaxTraceCount = 3;
	static constexpr int MaxStackCount = 15;
	static constexpr float MinionLaunchSpeed = 5.0f;
	static constexpr int MinionBaseAttack = 1;
	static constexpr float MinionKnockbackUpgrade = 1.5f;
	static constexpr float MinionOverchargeKnockbackUpgrade = 1.25f;

	struct Trace
	{
		Vector3 start = Vector3::Zero;
		Vector3 end = Vector3::Zero;
		Vector3 direction = Vector3::UnitZ;
		std::uint64_t id = 0;
	};

	struct State
	{
		std::vector<Trace> traces;
		int stackCount = 0;
		std::unordered_set<std::uintptr_t> piercedEnemiesThisShot;
		std::uint64_t nextId = 1;
		int generatedCount = 0;
		int overwrittenCount = 0;
	};

	struct MinionLaunch
	{
		Vector3 start = Vector3::Zero;
		Vector3 direction = Vector3::UnitZ;
		int attack = MinionBaseAttack;
		float knockbackMultiplier = 1.0f;
	};

	/// <summary>
	/// ショット開始時に、貫通済みの敵の記録を消去する。
	/// </summary>
	inline void BeginShot(State& state)
	{
		state.piercedEnemiesThisShot.clear();
	}

	/// <summary>
	/// それまでに貫通していない敵を記録し、上限まで貫通スタックを加算する。
	/// </summary>
	inline bool RecordPierce(
		State& state,
		std::uintptr_t enemyId,
		int amount = 1)
	{
		if (enemyId == 0 ||
			!state.piercedEnemiesThisShot.insert(enemyId).second)
		{
			return false;
		}
		state.stackCount = (std::min)(
			MaxStackCount,
			state.stackCount + (std::max)(1, amount));
		return true;
	}

	/// <summary>
	/// 指定された上限まで貫通スタックを消費し、実際の消費数を返す。
	/// </summary>
	inline int ConsumeStacks(State& state, int amount)
	{
		const int available = (std::max)(0, state.stackCount);
		const int consumed = amount <= 0
			? available
			: (std::min)(available, amount);
		state.stackCount -= consumed;
		return consumed;
	}

	// Return only the stack count that can actually contribute to a minion.
	// The finisher's 10-15 stack overcharge thresholds are intentionally kept.
	// IMPORTANT: ConsumeStacks(state, 0) means consume ALL, so callers must
	// skip consumption when this function returns 0.
	/// <summary>
	/// 保有スタック、消費上限、貫通痕の数とフィニッシャー条件から有効な消費数を求める。
	/// </summary>
	inline int CalculateEffectiveMinionStackConsume(
		const State& state,
		int consumeLimit,
		bool finisher)
	{
		if (consumeLimit <= 0 || state.stackCount <= 0) return 0;
		const int traceCount = (std::min)(
			static_cast<int>(state.traces.size()),
			static_cast<int>(MaxTraceCount));
		if (traceCount <= 0) return 0;

		const int available = (std::min)({
			state.stackCount, consumeLimit, MaxStackCount });
		int useful = (std::min)(available, traceCount * 3);

		if (finisher)
		{
			// Follow the exact upgrade thresholds in BuildMinionLaunches.
			// A single trace can benefit from thresholds 10 and 11,
			// two traces from 12 and 13, and three from 14 and 15.
			for (int index = 0; index < traceCount; ++index)
			{
				const int attackThreshold = 10 + index * 2;
				const int knockbackThreshold = attackThreshold + 1;
				if (available >= attackThreshold) useful = attackThreshold;
				if (available >= knockbackThreshold) useful = knockbackThreshold;
			}
		}
		return useful;
	}

    // Each group of three stacks starts with a launch from the newest remaining trace.
    /// <summary>
    /// 消費するスタック数と貫通痕の数から、発射できるミニオン数を求める。
    /// </summary>
    inline std::size_t MinionLaunchCount(const State& state, int consumedStacks)
    {
        const auto stacks = static_cast<std::size_t>((std::max)(0, consumedStacks));
        return (std::min)({state.traces.size(), MaxTraceCount, (stacks + 2) / 3});
    }

	/// <summary>
	/// 貫通痕と消費したスタック数から、ミニオンごとの発射位置と発射条件を組み立てる。
	/// </summary>
	inline std::vector<MinionLaunch> BuildMinionLaunches(
		const State& state,
		int consumedStacks,
		bool finisher)
	{
		std::vector<MinionLaunch> launches;
		const int launchCount = static_cast<int>(MinionLaunchCount(state, consumedStacks));
		for (int minionIndex = 0; minionIndex < launchCount; ++minionIndex)
		{
			const int creationStack = minionIndex * 3 + 1;

			const Trace& trace = state.traces[
				state.traces.size() - 1 - static_cast<std::size_t>(minionIndex)];
			MinionLaunch launch;
			launch.start = trace.start;
			launch.direction = trace.direction;
			if (consumedStacks >= creationStack + 1) ++launch.attack;
			if (consumedStacks >= creationStack + 2)
				launch.knockbackMultiplier *= MinionKnockbackUpgrade;

			if (finisher && consumedStacks >= 10 + minionIndex * 2)
				++launch.attack;
			if (finisher && consumedStacks >= 11 + minionIndex * 2)
				launch.knockbackMultiplier *= MinionOverchargeKnockbackUpgrade;
			launches.push_back(launch);
		}
		return launches;
	}

	/// <summary>
	/// 有効な長さの貫通痕を記録し、上限を超える場合は最も古い貫通痕を入れ替える。
	/// </summary>
	inline bool AddTrace(
		State& state,
		const Vector3& start,
		const Vector3& end)
	{
		Vector3 direction = end - start;
		direction.y = 0.0f;
		if (direction.LengthSquared() <= 0.01f) return false;
		direction.Normalize();
		if (state.traces.size() >= MaxTraceCount)
		{
			state.traces.erase(state.traces.begin());
			++state.overwrittenCount;
		}
		state.traces.push_back({ start, end, direction, state.nextId++ });
		++state.generatedCount;
		return true;
	}
}
