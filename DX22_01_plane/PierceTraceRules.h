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

	inline void BeginShot(State& state)
	{
		state.piercedEnemiesThisShot.clear();
	}

	inline bool RecordPierce(State& state, std::uintptr_t enemyId)
	{
		if (enemyId == 0 ||
			!state.piercedEnemiesThisShot.insert(enemyId).second)
		{
			return false;
		}
		state.stackCount = (std::min)(MaxStackCount, state.stackCount + 1);
		return true;
	}

	inline int ConsumeStacks(State& state, int amount)
	{
		const int available = (std::max)(0, state.stackCount);
		const int consumed = amount <= 0
			? available
			: (std::min)(available, amount);
		state.stackCount -= consumed;
		return consumed;
	}

	inline std::vector<MinionLaunch> BuildMinionLaunches(
		const State& state,
		int consumedStacks,
		bool finisher)
	{
		std::vector<MinionLaunch> launches;
		const int availableTraces = (std::min)(
			static_cast<int>(state.traces.size()),
			static_cast<int>(MaxTraceCount));
		for (int minionIndex = 0; minionIndex < availableTraces; ++minionIndex)
		{
			const int creationStack = minionIndex * 3 + 1;
			if (consumedStacks < creationStack) break;

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
