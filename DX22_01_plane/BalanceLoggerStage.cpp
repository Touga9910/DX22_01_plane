#include "BalanceLogger.h"
#include "BalanceLoggerTelemetryJson.h"
#include <algorithm>
#include <utility>

using json = nlohmann::json;
using namespace BalanceLogDetail;

void BalanceLogger::BeginStage(
	const std::string& stageId,
	const std::string& stageType,
	int difficulty,
	int playerCurrentHp,
	int playerMaxHp,
	const std::vector<BalanceEnemySnapshot>& enemies,
	const json& stageContext)
{
	if (!m_RunActive)
	{
		return;
	}

	if (m_StageActive)
	{
		EndStage(
			"abandoned",
			playerCurrentHp,
			playerMaxHp,
			0);
	}

	// Latest-shot MCP data belongs to the previous battle until the next
	// battle begins. Clear it here so a restarted battle cannot expose stale
	// Pierce or physics telemetry before its first shot.
	m_ShotPhysicsTelemetry.Reset();
	m_PierceAttackTelemetry.Reset();
	m_LatestShotPhysicsTelemetry = json(nullptr);
	m_LatestShotPierceAttackTelemetry = json(nullptr);
	m_LatestShotBounceTelemetry = json(nullptr);
	m_LatestShotCombatResult = json(nullptr);

	json enemiesJson = json::array();
	for (const BalanceEnemySnapshot& enemy : enemies)
	{
		enemiesJson.push_back(
			{
				{ "id", enemy.id },
				{ "max_hp", enemy.maxHp },
				{ "attack", enemy.attack },
				{ "defense", enemy.defense },
				{ "mass", enemy.mass },
				{ "radius", enemy.radius },
				{ "restitution", enemy.restitution },
				{ "friction", enemy.friction },
				{
					"position",
					{
						enemy.positionX,
						enemy.positionY,
						enemy.positionZ,
					}
				},
			});
	}

	m_StageStartedMs = GetEpochMilliseconds();
	json stage =
	{
		{
			"stage_index",
			static_cast<int>(m_Root["stages"].size()) + 1
		},
		{ "stage_id", stageId },
		{ "stage_type", stageType },
		{ "difficulty", difficulty },
		{ "stage_context", stageContext },
		{ "started_at", MakeUtcTimestamp() },
		{ "elapsed_from_run_start_ms", m_StageStartedMs - m_RunStartedMs },
		{
			"stage_start",
			{
				{ "player_hp", playerCurrentHp },
				{ "player_max_hp", playerMaxHp },
				{ "enemy_count", static_cast<int>(enemies.size()) },
				{ "enemies", enemiesJson },
			}
		},
		{ "shots", json::array() },
		{ "damage_events", json::array() },
	};

	m_Root["stages"].push_back(std::move(stage));
	m_CurrentStageIndex = m_Root["stages"].size() - 1;
	m_StageActive = true;
	m_ShotActive = false;
	m_SavePending = true;

	Save();
}

void BalanceLogger::EndStage(
	const std::string& result,
	int playerHp,
	int playerMaxHp,
	int enemiesDefeatedTotal)
{
	json* stage = GetCurrentStage();
	if (stage == nullptr)
	{
		return;
	}

	if (m_ShotActive)
	{
		const int enemyCount =
			(*stage)["stage_start"].value("enemy_count", 0);
		EndShot(
			playerHp,
			(std::max)(0, enemyCount - enemiesDefeatedTotal),
			enemiesDefeatedTotal);
		stage = GetCurrentStage();
		if (stage == nullptr)
		{
			return;
		}
	}

	const json& shots = (*stage)["shots"];
	int noHitShotCount = 0;
	int totalPlayerEnemyHits = 0;
	int totalEnemyEnemyHits = 0;
	int totalPlayerEnemyDamage = 0;
	int totalEnemyEnemyDamage = 0;
	int totalPlayerDamageTaken = 0;
	bool stageEnemyDamageKnown = true;
	double totalShotScore = 0.0;

	for (const json& shot : shots)
	{
		const int collisionEffect =
			shot.value("collision_effect", 0);
		if (collisionEffect == 0)
		{
			noHitShotCount++;
		}

		totalPlayerEnemyHits +=
			shot.value("player_enemy_hit_count", 0);
		totalEnemyEnemyHits +=
			shot.value("enemy_enemy_hit_count", 0);
		if (shot.contains("player_enemy_damage") &&
			shot["player_enemy_damage"].is_number_integer())
		{
			totalPlayerEnemyDamage +=
				shot["player_enemy_damage"].get<int>();
		}
		else
		{
			stageEnemyDamageKnown = false;
		}
		if (shot.contains("enemy_enemy_damage") &&
			shot["enemy_enemy_damage"].is_number_integer())
		{
			totalEnemyEnemyDamage +=
				shot["enemy_enemy_damage"].get<int>();
		}
		else
		{
			stageEnemyDamageKnown = false;
		}
		totalShotScore +=
			shot.value("shot_score", 0.0);
	}
	for (const json& damageEvent : (*stage)["damage_events"])
	{
		totalPlayerDamageTaken +=
			damageEvent.value("damage", 0);
	}

	const int totalShots = static_cast<int>(shots.size());
	const double averageShotScore = totalShots == 0
		? 0.0
		: totalShotScore / static_cast<double>(totalShots);
	const double hpRatio = playerMaxHp <= 0
		? 0.0
		: static_cast<double>(playerHp) /
			static_cast<double>(playerMaxHp);

	(*stage)["stage_result"] =
	{
		{ "result", result },
		{ "ended_at", MakeUtcTimestamp() },
		{
			"duration_ms",
			GetEpochMilliseconds() - m_StageStartedMs
		},
		{ "total_shots", totalShots },
		{ "turn_count", totalShots },
		{ "player_hp", playerHp },
		{ "player_max_hp", playerMaxHp },
		{ "remaining_hp_ratio", RoundToTwoDecimals(hpRatio) },
		{ "enemies_defeated", enemiesDefeatedTotal },
		{ "player_enemy_hit_count", totalPlayerEnemyHits },
		{ "enemy_enemy_hit_count", totalEnemyEnemyHits },
		{
			"player_enemy_damage",
			stageEnemyDamageKnown
				? json(totalPlayerEnemyDamage)
				: json(nullptr)
		},
		{
			"enemy_enemy_damage",
			stageEnemyDamageKnown
				? json(totalEnemyEnemyDamage)
				: json(nullptr)
		},
		{
			"total_enemy_damage",
			stageEnemyDamageKnown
				? json(totalPlayerEnemyDamage + totalEnemyEnemyDamage)
				: json(nullptr)
		},
		{ "player_damage_taken", totalPlayerDamageTaken },
		{ "no_hit_shot_count", noHitShotCount },
		{
			"no_hit_shot_rate",
			totalShots == 0
				? 0.0
				: RoundToTwoDecimals(
					static_cast<double>(noHitShotCount) /
					static_cast<double>(totalShots))
		},
		{
			"average_shot_score",
			RoundToTwoDecimals(averageShotScore)
		},
	};

	m_StageActive = false;
	m_CurrentStageIndex = NoStage;
	m_SavePending = true;
	Save();
}
