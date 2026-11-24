#include "BalanceLogger.h"
#include <algorithm>
#include <utility>

using json = nlohmann::json;

/// <summary>
/// ショット中の敵同士の衝突を、連鎖と物理計測の記録に追加する。
/// </summary>
void BalanceLogger::RecordEnemyEnemyCollision(
	std::uintptr_t firstEnemyKey,
	const std::string& firstEnemyId,
	std::uintptr_t secondEnemyKey,
	const std::string& secondEnemyId,
	float approachSpeed,
	float positionX,
	float positionZ)
{
	if (!m_ShotActive)
	{
		return;
	}
	RecordDamageCollision(BalanceCollisionType::EnemyEnemy);
	m_ShotPhysicsTelemetry.RecordEnemyEnemyCollision(
		firstEnemyKey,
		firstEnemyId,
		secondEnemyKey,
		secondEnemyId,
		approachSpeed,
		positionX,
		positionZ);
}

/// <summary>
/// 敵の識別子と移動距離をショットの物理計測に記録する。
/// </summary>
void BalanceLogger::RecordEnemyMovement(
	std::uintptr_t enemyKey,
	const std::string& enemyId,
	float distance)
{
	m_ShotPhysicsTelemetry.RecordEnemyMovement(
		enemyKey,
		enemyId,
		distance);
}

/// <summary>
/// 物理ステップにかかった時間をショットの物理計測に記録する。
/// </summary>
void BalanceLogger::RecordPhysicsStep(
	long long processingMicroseconds)
{
	m_ShotPhysicsTelemetry.RecordPhysicsStep(
		processingMicroseconds);
}

/// <summary>
/// ショット中の壁反射を連番付きの計測データとして記録する。
/// </summary>
void BalanceLogger::RecordWallReflection(
	int wallIndex,
	const std::string& wallGroup,
	const std::string& ballKind,
	float positionX,
	float positionZ,
	float velocityBeforeX,
	float velocityBeforeZ,
	float velocityAfterPhysicsX,
	float velocityAfterPhysicsZ,
	float velocityAfterEffectsX,
	float velocityAfterEffectsZ)
{
	if (!m_ShotActive) return;
	const int timelineEventIndex = m_BounceTimelineNextEventIndex++;
	m_ShotPhysicsTelemetry.RecordWallReflection(
		wallIndex,
		wallGroup,
		ballKind,
		positionX,
		positionZ,
		velocityBeforeX,
		velocityBeforeZ,
		velocityAfterPhysicsX,
		velocityAfterPhysicsZ,
		velocityAfterEffectsX,
		velocityAfterEffectsZ,
		timelineEventIndex);
	++m_BouncePhysicalReflectionsSoFar;
	if (ballKind == "player") ++m_BouncePlayerReflectionsSoFar;
	else if (ballKind == "pinball_target") ++m_BouncePinballReflectionsSoFar;
}

/// <summary>
/// 反射による特殊ダメージが発生した段階と結果をショットの記録に加える。
/// </summary>
void BalanceLogger::RecordBounceSpecialDamage(
	int damageStage,
	int calculatedDamage,
	int appliedHpDamage)
{
	if (!m_ShotActive || damageStage <= 0) return;
	++m_BounceSpecialDamageEventCount;
	m_BounceSpecialCalculatedDamage += (std::max)(0, calculatedDamage);
	m_BounceSpecialAppliedDamage += (std::max)(0, appliedHpDamage);
	m_BounceMaximumDamageStage = (std::max)(
		m_BounceMaximumDamageStage,
		damageStage);
	if (!m_BounceAttackTimeline.empty())
	{
		auto& attack = m_BounceAttackTimeline.back();
		attack["reflection_derived"] = true;
		attack["reflection_derivation"] = "pinball_damage_stage";
		attack["reflection_damage_stage"] = damageStage;
		attack["reflection_bonus_calculated_damage"] =
			(std::max)(0, calculatedDamage);
		attack["reflection_bonus_applied_hp_damage"] =
			(std::max)(0, appliedHpDamage);
	}
}

/// <summary>
/// 貫通ミニオンの生成位置、方向、攻撃力などを貫通攻撃の計測に記録する。
/// </summary>
void BalanceLogger::RecordPierceMinionSpawn(
	std::uintptr_t minionKey,
	int attack,
	float spawnX,
	float spawnZ,
	float directionX,
	float directionZ,
	float launchSpeed)
{
	m_PierceAttackTelemetry.RecordMinionSpawn(
		minionKey,
		attack,
		spawnX,
		spawnZ,
		directionX,
		directionZ,
		launchSpeed);
}

/// <summary>
/// 貫通ミニオンの移動距離を個体ごとの計測に記録する。
/// </summary>
void BalanceLogger::RecordPierceMinionMovement(
	std::uintptr_t minionKey,
	float distance)
{
	m_PierceAttackTelemetry.RecordMinionMovement(
		minionKey,
		distance);
}

/// <summary>
/// 貫通ミニオンが消滅した理由を記録する。
/// </summary>
void BalanceLogger::RecordPierceMinionDestroyed(
	std::uintptr_t minionKey,
	const std::string& reason)
{
	m_PierceAttackTelemetry.RecordMinionDestroyed(
		minionKey,
		reason);
}

/// <summary>
/// 貫通攻撃の発生源、標的、計算上のダメージと実際に適用したダメージを記録する。
/// </summary>
void BalanceLogger::RecordPierceAttack(
	PierceAttackSource source,
	std::uintptr_t sourceKey,
	std::uintptr_t targetKey,
	int calculatedDamage,
	int appliedHpDamage,
	bool targetWasAlive)
{
	m_PierceAttackTelemetry.RecordAttack(
		source,
		sourceKey,
		targetKey,
		calculatedDamage,
		appliedHpDamage,
		targetWasAlive);
	if (!m_ShotActive) return;
	const auto [targetIt, inserted] = m_BounceTargetIndices.try_emplace(
		targetKey,
		m_BounceNextTargetIndex);
	if (inserted) ++m_BounceNextTargetIndex;
	const auto empowered = m_BouncePendingEmpoweredBonuses.find(targetKey);
	const bool reflectionDerived =
		empowered != m_BouncePendingEmpoweredBonuses.end();
	const int reflectionBonus = reflectionDerived ? empowered->second : 0;
	const int safeCalculatedDamage = (std::max)(0, calculatedDamage);
	const int safeAppliedDamage = (std::max)(0, appliedHpDamage);
	const char* contactSource = !m_HasCurrentDamageCollision
		? "unknown"
		: (m_CurrentDamageCollisionType == BalanceCollisionType::PlayerEnemy
			? "player_enemy_collision"
			: "enemy_enemy_collision");
	m_BounceAttackTimeline.push_back({
		{ "attack_event_index",
			static_cast<int>(m_BounceAttackTimeline.size()) },
		{ "timeline_event_index", m_BounceTimelineNextEventIndex++ },
		{ "source", contactSource },
		{ "telemetry_source", ToString(source) },
		{ "target_index", targetIt->second },
		{ "target_was_alive", targetWasAlive },
		{ "calculated_damage", safeCalculatedDamage },
		{ "applied_hp_damage", safeAppliedDamage },
		{ "overkill_damage", targetWasAlive
			? (std::max)(0, safeCalculatedDamage - safeAppliedDamage)
			: 0 },
		{ "timing_phase", m_BouncePhysicalReflectionsSoFar > 0
			? "after_first_reflection"
			: "before_first_reflection" },
		{ "physical_reflections_before",
			m_BouncePhysicalReflectionsSoFar },
		{ "player_reflections_before", m_BouncePlayerReflectionsSoFar },
		{ "pinball_reflections_before", m_BouncePinballReflectionsSoFar },
		{ "reflection_derived", reflectionDerived },
		{ "reflection_derivation", reflectionDerived
			? json("cushion_empowered_hit")
			: json(nullptr) },
		{ "reflection_damage_stage", nullptr },
		{ "reflection_bonus_calculated_damage", reflectionDerived
			? json(reflectionBonus)
			: json(nullptr) },
		{ "reflection_bonus_applied_hp_damage", nullptr },
	});
	if (reflectionDerived)
		m_BouncePendingEmpoweredBonuses.erase(empowered);
}

/// <summary>
/// ダメージを発生させた衝突の種別を保存し、ショット内の衝突の集計を更新する。
/// </summary>
void BalanceLogger::RecordDamageCollision(
	BalanceCollisionType collisionType,
	int damageToFirstEnemy,
	int damageToSecondEnemy)
{
	if (!m_ShotActive)
	{
		return;
	}
	m_CurrentDamageCollisionType = collisionType;
	m_HasCurrentDamageCollision = true;

	switch (collisionType)
	{
	case BalanceCollisionType::PlayerEnemy:
		m_PlayerEnemyHitCount++;
		if (damageToFirstEnemy >= 0 || damageToSecondEnemy >= 0)
		{
			m_PlayerEnemyDamageObserved = true;
			m_PlayerEnemyDamage +=
				(std::max)(0, damageToFirstEnemy) +
				(std::max)(0, damageToSecondEnemy);
		}
		break;
	case BalanceCollisionType::EnemyEnemy:
		m_EnemyEnemyHitCount++;
		if (damageToFirstEnemy >= 0 || damageToSecondEnemy >= 0)
		{
			m_EnemyEnemyDamageObserved = true;
			m_EnemyEnemyDamage +=
				(std::max)(0, damageToFirstEnemy) +
				(std::max)(0, damageToSecondEnemy);
		}
		break;
	default:
		break;
	}
}

/// <summary>
/// 現在の衝突で敵に与えたダメージを、衝突種別と対応付けて記録する。
/// </summary>
void BalanceLogger::RecordEnemyDamage(
	const std::string& enemyId,
	int damage)
{
	if (!m_ShotActive || !m_HasCurrentDamageCollision)
	{
		return;
	}

	const int safeDamage = (std::max)(0, damage);
	const char* source = "player_enemy_collision";
	if (m_CurrentDamageCollisionType ==
		BalanceCollisionType::PlayerEnemy)
	{
		m_PlayerEnemyDamageObserved = true;
		m_PlayerEnemyDamage += safeDamage;
	}
	else
	{
		source = "enemy_enemy_collision";
		m_EnemyEnemyDamageObserved = true;
		m_EnemyEnemyDamage += safeDamage;
	}

	m_PendingShot["enemy_damage_events"].push_back(
		{
			{ "source", source },
			{ "enemy_id", enemyId },
			{ "damage", safeDamage },
		});
}

/// <summary>
/// 衝突に起因しない正の対敵ダメージをショットの合計に加える。
/// </summary>
void BalanceLogger::RecordNonCollisionEnemyDamage(
    const std::string& source, const std::string& enemyId, int damage)
{
    if (!m_ShotActive || damage <= 0) return;
    m_PendingShot["non_collision_enemy_damage"] =
        m_PendingShot.value("non_collision_enemy_damage", 0) + damage;
    m_PendingShot["enemy_damage_events"].push_back({
        {"source", source}, {"enemy_id", enemyId}, {"damage", damage}});
}

/// <summary>
/// プレイヤーが受けたダメージをステージのイベントとして記録する。
/// </summary>
void BalanceLogger::RecordPlayerDamage(
	const std::string& source,
	int damage,
	const std::string& sourceId)
{
	json* stage = GetCurrentStage();
	if (stage == nullptr || damage <= 0)
	{
		return;
	}

	json event =
	{
		{ "recorded_at", MakeUtcTimestamp() },
		{
			"elapsed_from_run_start_ms",
			GetEpochMilliseconds() - m_RunStartedMs
		},
		{ "source", source },
		{ "damage", damage },
	};
	if (!sourceId.empty())
	{
		event["source_id"] = sourceId;
	}
	(*stage)["damage_events"].push_back(std::move(event));
	m_SavePending = true;
}

/// <summary>
/// ラン中の進行イベントを記録し、該当するショット内イベントの集計も更新する。
/// </summary>
void BalanceLogger::RecordEvent(
	const std::string& eventType,
	const json& details)
{
	if (!m_RunActive || eventType.empty())
	{
		return;
	}
	if (m_ShotActive && eventType == "cushion_empowered_hit")
	{
		const std::uintptr_t targetKey = static_cast<std::uintptr_t>(
			details.value("target_id", static_cast<std::uint64_t>(0)));
		if (targetKey != 0)
		{
			m_BouncePendingEmpoweredBonuses[targetKey] = (std::max)(
				0,
				details.value("damage_bonus", 0));
		}
	}

	json event =
	{
		{ "event_type", eventType },
		{ "recorded_at", MakeUtcTimestamp() },
		{
			"elapsed_from_run_start_ms",
			GetEpochMilliseconds() - m_RunStartedMs
		},
		{ "details", details },
	};
	if (m_ShotActive && m_PendingShot.is_object())
	{
		m_PendingShot["shot_events"].push_back({
			{ "event_type", eventType },
			{ "details", details },
		});
	}
	if (m_StageActive && m_CurrentStageIndex != NoStage)
	{
		event["stage_index"] =
			static_cast<int>(m_CurrentStageIndex) + 1;
	}
	m_Root["events"].push_back(std::move(event));
	m_SavePending = true;
}
