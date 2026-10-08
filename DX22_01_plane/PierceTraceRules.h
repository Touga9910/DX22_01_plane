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
