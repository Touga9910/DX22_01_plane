#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class PierceAttackSource
{
	PlayerDirect,
	PlayerPierce,
	Minion,
	Trace,
	Other,
};

/// <summary>
/// 貫通系の攻撃元をログ用の識別文字列に変換する。
/// </summary>
inline const char* ToString(PierceAttackSource source)
{
	switch (source)
	{
	case PierceAttackSource::PlayerDirect: return "player_direct";
	case PierceAttackSource::PlayerPierce: return "player_pierce";
	case PierceAttackSource::Minion: return "minion";
	case PierceAttackSource::Trace: return "trace";
	default: return "other";
	}
}

// Per-source counters are exclusive: one damage event belongs to exactly one
// source. Attributes such as a trace-launched minion are reported separately.
struct PierceAttackSourceTotals
{
	int attackCheckCount = 0;
	int hitCount = 0;
	int effectiveAttackCount = 0;
	int calculatedDamage = 0;
	int appliedHpDamage = 0;
	int overkillDamage = 0;
};

struct PierceMinionTelemetrySnapshot
{
	std::uintptr_t key = 0;
	int ordinal = 0;
	int attack = 0;
	float spawnX = 0.0f;
	float spawnZ = 0.0f;
	float directionX = 0.0f;
	float directionZ = 0.0f;
	float launchSpeed = 0.0f;
	float movementDistance = 0.0f;
	long long lifetimeMicroseconds = 0;
	std::string destructionReason = "active_at_shot_end";
	int attackCheckCount = 0;
	int hitCount = 0;
	int effectiveAttackCount = 0;
	int calculatedDamage = 0;
	int appliedHpDamage = 0;
	int overkillDamage = 0;
	int uniqueTargetCount = 0;
};

struct PierceAttackTelemetrySnapshot
{
	PierceAttackSourceTotals playerDirect;
	PierceAttackSourceTotals playerPierce;
	PierceAttackSourceTotals minion;
	PierceAttackSourceTotals trace;
	PierceAttackSourceTotals other;
	PierceAttackSourceTotals total;
	int uniqueEffectiveTargetCount = 0;
	int minionsGenerated = 0;
	int maximumSimultaneousMinions = 0;
	int minionsWithoutEffectiveAttack = 0;
	int minionsWithMultipleTargets = 0;
	std::vector<PierceMinionTelemetrySnapshot> minions;
};

// Value-oriented shot telemetry owned by BalanceLogger. It has no gameplay
// authority and does nothing outside an active logged shot.
class PierceAttackTelemetry final
{
public:
	/// <summary>
	/// 貫通系攻撃のショット計測を開始し、集計とミニオンの記録を初期化する。
	/// </summary>
	void BeginShot()
	{
		m_Active = true;
		m_Snapshot = {};
		m_Minions.clear();
		m_UniqueEffectiveTargets.clear();
		m_CurrentMinionCount = 0;
	}

	/// <summary>
	/// ショット計測を停止し、集計とミニオンの記録を消去する。
	/// </summary>
	void Reset()
	{
		m_Active = false;
		m_Snapshot = {};
		m_Minions.clear();
		m_UniqueEffectiveTargets.clear();
		m_CurrentMinionCount = 0;
	}

	/// <summary>
	/// ミニオンの生成時刻、攻撃力、出現位置と発射条件を記録する。
	/// </summary>
	void RecordMinionSpawn(
		std::uintptr_t key,
		int attack,
		float spawnX,
		float spawnZ,
		float directionX,
		float directionZ,
		float launchSpeed)
	{
		if (!m_Active || key == 0 || m_Minions.contains(key)) return;
		MinionState state;
		state.snapshot.key = key;
		state.snapshot.ordinal =
			static_cast<int>(m_Minions.size()) + 1;
		state.snapshot.attack = attack;
		state.snapshot.spawnX = spawnX;
		state.snapshot.spawnZ = spawnZ;
		state.snapshot.directionX = directionX;
		state.snapshot.directionZ = directionZ;
		state.snapshot.launchSpeed = launchSpeed;
		state.spawnedAtMicroseconds = NowMicroseconds();
		state.active = true;
		m_Minions.emplace(key, std::move(state));
		++m_Snapshot.minionsGenerated;
		++m_CurrentMinionCount;
		m_Snapshot.maximumSimultaneousMinions = (std::max)(
			m_Snapshot.maximumSimultaneousMinions,
			m_CurrentMinionCount);
	}

	/// <summary>
	/// 記録済みのミニオンに、その更新で移動した距離を加算する。
	/// </summary>
	void RecordMinionMovement(std::uintptr_t key, float distance)
	{
		if (!m_Active || distance <= 0.0f) return;
		auto found = m_Minions.find(key);
		if (found == m_Minions.end()) return;
		found->second.snapshot.movementDistance += distance;
	}

	/// <summary>
	/// 破棄されたミニオンの破棄理由と生存時間を記録する。
	/// </summary>
	void RecordMinionDestroyed(
		std::uintptr_t key,
		const std::string& reason)
	{
		if (!m_Active) return;
		auto found = m_Minions.find(key);
		if (found == m_Minions.end() || !found->second.active) return;
		FinishMinion(found->second, reason, NowMicroseconds());
	}

	/// <summary>
	/// 貫通系の攻撃について、攻撃元別の実ダメージと過剰ダメージ、対象を記録する。
	/// </summary>
	void RecordAttack(
		PierceAttackSource source,
		std::uintptr_t sourceKey,
		std::uintptr_t targetKey,
		int calculatedDamage,
		int appliedHpDamage,
		bool targetWasAlive)
	{
		if (!m_Active) return;
		calculatedDamage = (std::max)(0, calculatedDamage);
		appliedHpDamage = (std::max)(0, appliedHpDamage);
		const int overkill = targetWasAlive
			? (std::max)(0, calculatedDamage - appliedHpDamage)
			: 0;
		RecordTotals(
			m_Snapshot.total,
			calculatedDamage,
			appliedHpDamage,
			overkill,
			targetWasAlive);
		RecordTotals(
			TotalsFor(source),
			calculatedDamage,
			appliedHpDamage,
			overkill,
			targetWasAlive);
		if (appliedHpDamage > 0 && targetKey != 0)
			m_UniqueEffectiveTargets.insert(targetKey);

		if (source != PierceAttackSource::Minion) return;
		auto found = m_Minions.find(sourceKey);
		if (found == m_Minions.end()) return;
		PierceMinionTelemetrySnapshot& minion = found->second.snapshot;
		++minion.attackCheckCount;
		if (targetWasAlive) ++minion.hitCount;
		if (appliedHpDamage > 0)
		{
			++minion.effectiveAttackCount;
			if (targetKey != 0)
				found->second.uniqueTargets.insert(targetKey);
		}
		minion.calculatedDamage += calculatedDamage;
		minion.appliedHpDamage += appliedHpDamage;
		minion.overkillDamage += overkill;
	}

	/// <summary>
	/// 残ったミニオンの計測を閉じ、貫通系攻撃のショット集計を確定する。
	/// </summary>
	PierceAttackTelemetrySnapshot EndShot()
	{
		if (!m_Active) return {};
		const long long now = NowMicroseconds();
		for (auto& [key, minion] : m_Minions)
		{
			(void)key;
			if (minion.active)
				FinishMinion(minion, "active_at_shot_end", now);
		}
		m_Snapshot.uniqueEffectiveTargetCount =
			static_cast<int>(m_UniqueEffectiveTargets.size());
		m_Snapshot.minions.clear();
		m_Snapshot.minions.reserve(m_Minions.size());
		for (auto& [key, minion] : m_Minions)
		{
			(void)key;
			minion.snapshot.uniqueTargetCount =
				static_cast<int>(minion.uniqueTargets.size());
			if (minion.snapshot.effectiveAttackCount == 0)
				++m_Snapshot.minionsWithoutEffectiveAttack;
			if (minion.snapshot.uniqueTargetCount >= 2)
				++m_Snapshot.minionsWithMultipleTargets;
			m_Snapshot.minions.push_back(minion.snapshot);
		}
		std::sort(
			m_Snapshot.minions.begin(),
			m_Snapshot.minions.end(),
			[](const auto& first, const auto& second)
			{
				return first.ordinal < second.ordinal;
			});
		m_Active = false;
		return m_Snapshot;
	}

	/// <summary>
	/// 貫通系攻撃のショット計測が進行中かを返す。
	/// </summary>
	bool IsActive() const { return m_Active; }

private:
	struct MinionState
	{
		PierceMinionTelemetrySnapshot snapshot;
		std::unordered_set<std::uintptr_t> uniqueTargets;
		long long spawnedAtMicroseconds = 0;
		bool active = false;
	};

	/// <summary>
	/// 単調増加時計の時刻をマイクロ秒単位で返す。
	/// </summary>
	static long long NowMicroseconds()
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	/// <summary>
	/// 攻撃の判定、命中、有効打、各ダメージの値を集計に加算する。
	/// </summary>
	static void RecordTotals(
		PierceAttackSourceTotals& totals,
		int calculatedDamage,
		int appliedHpDamage,
		int overkill,
		bool targetWasAlive)
	{
		++totals.attackCheckCount;
		if (targetWasAlive) ++totals.hitCount;
		if (appliedHpDamage > 0) ++totals.effectiveAttackCount;
		totals.calculatedDamage += calculatedDamage;
		totals.appliedHpDamage += appliedHpDamage;
		totals.overkillDamage += overkill;
	}

	/// <summary>
	/// 指定された攻撃元に対応する集計を返す。
	/// </summary>
	PierceAttackSourceTotals& TotalsFor(PierceAttackSource source)
	{
		switch (source)
		{
		case PierceAttackSource::PlayerDirect: return m_Snapshot.playerDirect;
		case PierceAttackSource::PlayerPierce: return m_Snapshot.playerPierce;
		case PierceAttackSource::Minion: return m_Snapshot.minion;
		case PierceAttackSource::Trace: return m_Snapshot.trace;
		default: return m_Snapshot.other;
		}
	}

	/// <summary>
	/// ミニオンの破棄理由と生存時間を確定し、活動中の数を減らす。
	/// </summary>
	void FinishMinion(
		MinionState& state,
		const std::string& reason,
		long long nowMicroseconds)
	{
		state.active = false;
		state.snapshot.destructionReason = reason.empty()
			? "unknown"
			: reason;
		state.snapshot.lifetimeMicroseconds = (std::max)(
			0LL,
			nowMicroseconds - state.spawnedAtMicroseconds);
		m_CurrentMinionCount = (std::max)(0, m_CurrentMinionCount - 1);
	}

	bool m_Active = false;
	PierceAttackTelemetrySnapshot m_Snapshot;
	std::unordered_map<std::uintptr_t, MinionState> m_Minions;
	std::unordered_set<std::uintptr_t> m_UniqueEffectiveTargets;
	int m_CurrentMinionCount = 0;
};
