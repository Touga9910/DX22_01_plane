#pragma once

#include "SimpleMath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace PierceTraceRules
{
	using DirectX::SimpleMath::Vector3;

	static constexpr std::size_t MaxTraceCount = 2;

	struct Trace
	{
		Vector3 start = Vector3::Zero;
		Vector3 end = Vector3::Zero;
		Vector3 direction = Vector3::UnitZ;
		int durability = 0;
		std::uint64_t id = 0;
	};

	struct State
	{
		std::vector<Trace> traces;
		std::uint64_t nextId = 1;
		int generatedCount = 0;
		int overwrittenCount = 0;
		int usedCount = 0;
		int durabilityConsumed = 0;
	};

	struct UseConfig
	{
		float angleToleranceDegrees = 12.0f;
		float requiredDistance = 8.0f;
		float width = 2.0f;
		float nonPierceSpeedMultiplier = 1.0f;
	};

	struct ShotUseState
	{
		std::unordered_map<std::uint64_t, float> alignedDistance;
		std::unordered_set<std::uint64_t> usedTraceIds;
		bool usedAnyTrace = false;
	};

	struct UseResult
	{
		bool activated = false;
		std::uint64_t traceId = 0;
		int remainingDurability = 0;
	};

	inline bool AddTrace(
		State& state,
		const Vector3& start,
		const Vector3& end,
		int durability)
	{
		Vector3 direction = end - start;
		direction.y = 0.0f;
		if (durability <= 0 || direction.LengthSquared() <= 0.01f) return false;
		direction.Normalize();
		if (state.traces.size() >= MaxTraceCount)
		{
			state.traces.erase(state.traces.begin());
			++state.overwrittenCount;
		}
		state.traces.push_back({ start, end, direction, durability, state.nextId++ });
		++state.generatedCount;
		return true;
	}

	inline float DistanceToTraceLine(const Trace& trace, const Vector3& point)
	{
		Vector3 offset = point - trace.start;
		offset.y = 0.0f;
		const float along = offset.Dot(trace.direction);
		return (offset - trace.direction * along).Length();
	}

	inline float AlignedOverlap(
		const Trace& trace,
		const Vector3& from,
		const Vector3& to,
		const UseConfig& config)
	{
		Vector3 movement = to - from;
		movement.y = 0.0f;
		const float distance = movement.Length();
		if (distance <= 0.0001f) return 0.0f;
		movement /= distance;
		const float radians = std::clamp(config.angleToleranceDegrees, 0.0f, 89.0f) *
			3.14159265358979323846f / 180.0f;
		if (movement.Dot(trace.direction) < std::cos(radians)) return 0.0f;

		const Vector3 midpoint = (from + to) * 0.5f;
		if (DistanceToTraceLine(trace, midpoint) > (std::max)(0.0f, config.width))
			return 0.0f;

		Vector3 traceSpan = trace.end - trace.start;
		traceSpan.y = 0.0f;
		const float traceLength = traceSpan.Length();
		const float fromProjection = (from - trace.start).Dot(trace.direction);
		const float toProjection = (to - trace.start).Dot(trace.direction);
		const float overlapStart = (std::max)(0.0f, (std::min)(fromProjection, toProjection));
		const float overlapEnd = (std::min)(traceLength, (std::max)(fromProjection, toProjection));
		return (std::max)(0.0f, overlapEnd - overlapStart);
	}

	inline UseResult AccumulateMovement(
		State& state,
		ShotUseState& shot,
		const Vector3& from,
		const Vector3& to,
		const UseConfig& config,
		bool strongUse,
		Vector3& velocity)
	{
		for (std::size_t index = 0; index < state.traces.size(); ++index)
		{
			Trace& trace = state.traces[index];
			if (shot.usedTraceIds.contains(trace.id)) continue;
			const float overlap = AlignedOverlap(trace, from, to, config);
			if (overlap <= 0.0f) continue;
			float& accumulated = shot.alignedDistance[trace.id];
			accumulated += overlap;
			if (accumulated + 0.0001f < (std::max)(0.01f, config.requiredDistance))
				continue;

			const std::uint64_t usedId = trace.id;
			shot.usedTraceIds.insert(usedId);
			shot.usedAnyTrace = true;
			++state.usedCount;
			--trace.durability;
			++state.durabilityConsumed;
			const int remaining = (std::max)(0, trace.durability);
			if (!strongUse)
			{
				const float multiplier = std::clamp(
					config.nonPierceSpeedMultiplier, 1.0f, 1.25f);
				velocity *= multiplier;
			}
			if (trace.durability <= 0)
				state.traces.erase(state.traces.begin() + static_cast<std::ptrdiff_t>(index));
			return { true, usedId, remaining };
		}
		return {};
	}
}
