#include "BalanceLogger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

using json = nlohmann::json;

namespace
{
	double RoundToTwoDecimals(double value)
	{
		return std::round(value * 100.0) / 100.0;
	}
}

BalanceLogger& BalanceLogger::GetInstance()
{
	static BalanceLogger instance;
	return instance;
}

void BalanceLogger::BeginRun(
	int playerMaxHp,
	int playerCurrentHp,
	const std::vector<BalanceBallSnapshot>& deck,
	const std::string& controllerType)
{
	if (m_RunActive)
	{
		EndRun(
			"restarted",
			playerCurrentHp,
			playerMaxHp,
			0);
	}

	m_RunStartedMs = GetEpochMilliseconds();
	const std::string runId = MakeRunId();
	m_LogPath =
		std::filesystem::path("logs") /
		"balance" /
		(runId + ".json");

	json deckJson = json::array();
	for (const BalanceBallSnapshot& ball : deck)
	{
		deckJson.push_back(
			{
				{ "id", ball.id },
				{ "upgrade_level", ball.upgradeLevel },
				{ "attack", ball.attack },
				{ "defense", ball.defense },
				{ "mass", ball.mass },
				{ "radius", ball.radius },
				{ "restitution", ball.restitution },
				{ "friction", ball.friction },
				{
					"abilities",
					{
						{ "split", ball.split },
						{ "pierce", ball.pierce },
					}
				},
			});
	}

	m_Root =
	{
		{ "schema_version", 1 },
		{ "run_id", runId },
		{ "started_at", MakeUtcTimestamp() },
		{ "controller_type", controllerType },
		{
			"initial_player",
			{
				{ "max_hp", playerMaxHp },
				{ "current_hp", playerCurrentHp },
				{ "deck", deckJson },
			}
		},
		{ "stages", json::array() },
	};

	m_CurrentStageIndex = NoStage;
	m_StageActive = false;
	m_ShotActive = false;
	m_RunActive = true;

	Save();

	std::cout << "[BalanceLogger] Run log: "
		<< m_LogPath.string() << std::endl;
}

void BalanceLogger::BeginStage(
	const std::string& stageId,
	const std::string& stageType,
	int difficulty,
	int playerCurrentHp,
	int playerMaxHp,
	const std::vector<BalanceEnemySnapshot>& enemies)
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
	};

	m_Root["stages"].push_back(std::move(stage));
	m_CurrentStageIndex = m_Root["stages"].size() - 1;
	m_StageActive = true;
	m_ShotActive = false;

	Save();
}

void BalanceLogger::BeginShot(
	const std::string& ballId,
	int upgradeLevel,
	float power,
	float positionX,
	float positionY,
	float positionZ,
	float velocityX,
	float velocityY,
	float velocityZ,
	int playerHp,
	int enemiesAlive,
	int enemiesDefeatedTotal)
{
	json* stage = GetCurrentStage();
	if (stage == nullptr || m_ShotActive)
	{
		return;
	}

	m_ShotStartedMs = GetEpochMilliseconds();
	m_PlayerEnemyHitCount = 0;
	m_EnemyEnemyHitCount = 0;
	m_DefeatedAtShotStart = enemiesDefeatedTotal;

	m_PendingShot =
	{
		{
			"shot_no",
			static_cast<int>((*stage)["shots"].size()) + 1
		},
		{ "fired_at", MakeUtcTimestamp() },
		{ "elapsed_from_run_start_ms", m_ShotStartedMs - m_RunStartedMs },
		{ "ball_id", ballId },
		{ "upgrade_level", upgradeLevel },
		{ "power", power },
		{ "position", { positionX, positionY, positionZ } },
		{ "velocity", { velocityX, velocityY, velocityZ } },
		{ "player_hp_before", playerHp },
		{ "enemies_alive_before", enemiesAlive },
		{
			"controller_type",
			m_Root.value("controller_type", "human")
		},
	};

	m_ShotActive = true;
}

void BalanceLogger::RecordDamageCollision(
	BalanceCollisionType collisionType)
{
	if (!m_ShotActive)
	{
		return;
	}

	switch (collisionType)
	{
	case BalanceCollisionType::PlayerEnemy:
		m_PlayerEnemyHitCount++;
		break;
	case BalanceCollisionType::EnemyEnemy:
		m_EnemyEnemyHitCount++;
		break;
	default:
		break;
	}
}

void BalanceLogger::EndShot(
	int playerHp,
	int enemiesAlive,
	int enemiesDefeatedTotal)
{
	json* stage = GetCurrentStage();
	if (stage == nullptr || !m_ShotActive)
	{
		return;
	}

	const long long nowMs = GetEpochMilliseconds();
	const int collisionEffect =
		m_PlayerEnemyHitCount +
		m_EnemyEnemyHitCount * 2;

	m_PendingShot["resolved_at"] = MakeUtcTimestamp();
	m_PendingShot["duration_ms"] = nowMs - m_ShotStartedMs;
	m_PendingShot["player_enemy_hit_count"] =
		m_PlayerEnemyHitCount;
	m_PendingShot["enemy_enemy_hit_count"] =
		m_EnemyEnemyHitCount;
	m_PendingShot["total_damage_collision_count"] =
		m_PlayerEnemyHitCount + m_EnemyEnemyHitCount;
	m_PendingShot["collision_effect"] = collisionEffect;
	m_PendingShot["shot_score"] =
		CalculateShotScore(collisionEffect);
	m_PendingShot["enemies_defeated"] =
		(std::max)(
			0,
			enemiesDefeatedTotal - m_DefeatedAtShotStart);
	m_PendingShot["enemies_alive_after"] = enemiesAlive;
	m_PendingShot["player_hp_after"] = playerHp;

	(*stage)["shots"].push_back(std::move(m_PendingShot));
	m_PendingShot = json{};
	m_ShotActive = false;

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
		totalShotScore +=
			shot.value("shot_score", 0.0);
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
		{ "player_hp", playerHp },
		{ "player_max_hp", playerMaxHp },
		{ "remaining_hp_ratio", RoundToTwoDecimals(hpRatio) },
		{ "enemies_defeated", enemiesDefeatedTotal },
		{ "player_enemy_hit_count", totalPlayerEnemyHits },
		{ "enemy_enemy_hit_count", totalEnemyEnemyHits },
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
	Save();
}

void BalanceLogger::EndRun(
	const std::string& result,
	int playerHp,
	int playerMaxHp,
	int clearedStageCount)
{
	if (!m_RunActive)
	{
		return;
	}

	if (m_StageActive)
	{
		EndStage(
			"abandoned",
			playerHp,
			playerMaxHp,
			0);
	}

	const double hpRatio = playerMaxHp <= 0
		? 0.0
		: static_cast<double>(playerHp) /
			static_cast<double>(playerMaxHp);

	m_Root["run_result"] =
	{
		{ "result", result },
		{ "ended_at", MakeUtcTimestamp() },
		{
			"duration_ms",
			GetEpochMilliseconds() - m_RunStartedMs
		},
		{
			"reached_stage_index",
			static_cast<int>(m_Root["stages"].size())
		},
		{ "cleared_stage_count", clearedStageCount },
		{ "player_hp", playerHp },
		{ "player_max_hp", playerMaxHp },
		{ "remaining_hp_ratio", RoundToTwoDecimals(hpRatio) },
	};

	Save();
	m_RunActive = false;
	m_StageActive = false;
	m_ShotActive = false;
	m_CurrentStageIndex = NoStage;
}

void BalanceLogger::Save()
{
	if (!m_RunActive || m_LogPath.empty())
	{
		return;
	}

	std::error_code error;
	std::filesystem::create_directories(
		m_LogPath.parent_path(),
		error);
	if (error)
	{
		std::cerr
			<< "[BalanceLogger] Failed to create log directory: "
			<< error.message() << std::endl;
		return;
	}

	std::filesystem::path temporaryPath = m_LogPath;
	temporaryPath += ".tmp";

	{
		std::ofstream file(
			temporaryPath,
			std::ios::out | std::ios::trunc);
		if (!file)
		{
			std::cerr
				<< "[BalanceLogger] Failed to open log file: "
				<< temporaryPath.string() << std::endl;
			return;
		}

		file << std::setw(2) << m_Root << '\n';
		if (!file)
		{
			std::cerr
				<< "[BalanceLogger] Failed to write log file: "
				<< temporaryPath.string() << std::endl;
			return;
		}
	}

	std::filesystem::remove(m_LogPath, error);
	error.clear();
	std::filesystem::rename(
		temporaryPath,
		m_LogPath,
		error);

	if (error)
	{
		std::cerr
			<< "[BalanceLogger] Failed to replace log file: "
			<< error.message() << std::endl;
	}
}

json* BalanceLogger::GetCurrentStage()
{
	if (!m_RunActive ||
		!m_StageActive ||
		m_CurrentStageIndex == NoStage ||
		!m_Root.contains("stages") ||
		m_CurrentStageIndex >= m_Root["stages"].size())
	{
		return nullptr;
	}

	return &m_Root["stages"][m_CurrentStageIndex];
}

long long BalanceLogger::GetEpochMilliseconds()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string BalanceLogger::MakeUtcTimestamp()
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t nowTime =
		std::chrono::system_clock::to_time_t(now);

	std::tm utcTime{};
#if defined(_WIN32)
	gmtime_s(&utcTime, &nowTime);
#else
	gmtime_r(&nowTime, &utcTime);
#endif

	std::ostringstream stream;
	stream << std::put_time(
		&utcTime,
		"%Y-%m-%dT%H:%M:%SZ");
	return stream.str();
}

std::string BalanceLogger::MakeRunId()
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t nowTime =
		std::chrono::system_clock::to_time_t(now);
	const long long milliseconds =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch()).count() % 1000;

	std::tm localTime{};
#if defined(_WIN32)
	localtime_s(&localTime, &nowTime);
#else
	localtime_r(&nowTime, &localTime);
#endif

	std::ostringstream stream;
	stream
		<< "run_"
		<< std::put_time(&localTime, "%Y%m%d_%H%M%S")
		<< '_'
		<< std::setw(3)
		<< std::setfill('0')
		<< milliseconds;
	return stream.str();
}

double BalanceLogger::CalculateShotScore(int collisionEffect)
{
	const int safeCollisionEffect =
		(std::max)(0, collisionEffect);
	const double score =
		100.0 *
		(1.0 - std::exp(
			-static_cast<double>(safeCollisionEffect) / 2.0));
	return RoundToTwoDecimals(score);
}
