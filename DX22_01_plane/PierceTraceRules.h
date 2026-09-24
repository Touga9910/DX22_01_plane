#pragma once

#include "SimpleMath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 貫通カテゴリが敵を貫通した軌道へ残す「貫通痕」と、その利用状態を管理
namespace PierceTraceRules
{
	using DirectX::SimpleMath::Vector3;

	static constexpr std::size_t MaxTraceCount = 2; // 同時に保持できる貫通痕の最大数

	// フィールド上へ残る貫通痕1本分の情報
	struct Trace
	{
		Vector3 start = Vector3::Zero;      // 痕の開始位置
		Vector3 end = Vector3::Zero;        // 痕の終了位置
		Vector3 direction = Vector3::UnitZ; // 開始位置から終了位置へ向くXZ平面上の単位方向
		int durability = 0;                 // 残り利用可能回数。利用ごとに1減少する
		std::uint64_t id = 0;               // 痕を一意に識別するID
	};

	// ラン・戦闘中に保持する貫通痕全体の状態と計測値
	struct State
	{
		std::vector<Trace> traces;          // 現在フィールド上に存在する貫通痕
		std::uint64_t nextId = 1;           // 次に生成する痕へ割り当てるID
		int generatedCount = 0;             // 生成した痕の累計本数
		int overwrittenCount = 0;           // 上限超過により古い痕を上書きした累計回数
		int usedCount = 0;                  // 痕の利用が成立した累計回数
		int durabilityConsumed = 0;         // 利用によって消費した耐久値の累計
	};

	// 貫通痕を利用したと判定する条件と、利用時の速度倍率
	struct UseConfig
	{
		float angleToleranceDegrees = 12.0f; // 痕の方向と移動方向の許容角度差（度）
		float requiredDistance = 8.0f;        // 利用成立に必要な痕上の累積移動距離
		float width = 2.0f;                   // 痕の中心線から許容する距離
		float pierceSpeedMultiplier = 1.1f;   // 貫通カテゴリが強利用した際の速度倍率
		float nonPierceSpeedMultiplier = 1.0f;// 非貫通カテゴリが利用した際の速度倍率
	};

	// 1ショット中の貫通痕利用進捗を保持
	struct ShotUseState
	{
		std::unordered_map<std::uint64_t, float> alignedDistance; // 痕IDごとの累積沿線移動距離
		std::unordered_set<std::uint64_t> usedTraceIds;           // このショットですでに利用済みの痕ID
		bool usedAnyTrace = false;                                // このショットで1本以上の痕を利用したか
	};

	// 移動処理によって貫通痕の利用が成立した結果
	struct UseResult
	{
		bool activated = false;       // 今回の移動で痕利用が成立したか
		std::uint64_t traceId = 0;    // 利用した痕のID。未発動時は0
		int remainingDurability = 0;  // 利用後に残った痕の耐久値
	};

	// startからendまでの新しい貫通痕を追加
	// durabilityが0以下、または軌道が短すぎる場合はfalseを返して追加しない
	// 保持上限に達している場合は最も古い痕を削除してから追加
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

	// pointから貫通痕の延長直線までのXZ平面上の距離を返す
	inline float DistanceToTraceLine(const Trace& trace, const Vector3& point)
	{
		Vector3 offset = point - trace.start;
		offset.y = 0.0f;
		const float along = offset.Dot(trace.direction);
		return (offset - trace.direction * along).Length();
	}

	// fromからtoへの移動が痕の方向・幅条件を満たす場合、痕区間と重なる移動距離を返す
	// 移動量が小さい、角度条件外、痕から離れすぎている場合は0を返す
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

	// 1回の移動区間について、各痕上を進んだ距離をショット状態へ累積
	// requiredDistanceへ到達した最初の未使用痕を1回利用し、耐久を1消費して速度倍率を適用
	// 耐久が0になった痕は削除し、利用が成立しなかった場合は既定値のUseResultを返す
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
			const float multiplier = strongUse
				? std::clamp(config.pierceSpeedMultiplier, 1.0f, 3.0f)
				: std::clamp(config.nonPierceSpeedMultiplier, 1.0f, 1.25f);
			velocity *= multiplier;
			if (trace.durability <= 0)
				state.traces.erase(state.traces.begin() + static_cast<std::ptrdiff_t>(index));
			return { true, usedId, remaining };
		}
		return {};
	}
}
