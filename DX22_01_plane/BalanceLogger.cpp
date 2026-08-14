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
	const std::string& controllerType,
	const json& runContext)
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
				{ "instance_id", ball.instanceId },
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
						{ "anchor", ball.anchor },
					}
				},
			});
	}

	m_Root =
	{
		{ "schema_version", 2 },
		{ "run_id", runId },
		{ "started_at", MakeUtcTimestamp() },
		{ "controller_type", controllerType },
		{
			"controller",
			{
				{ "type", controllerType },
				{
					"profile",
					runContext.value("controller_profile", std::string())
				},
			}
		},
		{ "run_context", runContext },
		{ "configuration", MakeConfigurationSnapshot() },
		{
			"build",
			{
				{ "compiled_date", __DATE__ },
				{ "compiled_time", __TIME__ },
#if defined(_MSC_VER)
				{ "compiler", "msvc" },
				{ "compiler_version", _MSC_VER },
#elif defined(__clang__)
				{ "compiler", "clang" },
				{ "compiler_version", __clang_version__ },
#elif defined(__GNUC__)
				{ "compiler", "gcc" },
				{ "compiler_version", __VERSION__ },
#else
				{ "compiler", "unknown" },
#endif
			}
		},
		{
			"initial_player",
			{
				{ "max_hp", playerMaxHp },
				{ "current_hp", playerCurrentHp },
				{ "deck", deckJson },
			}
		},
		{ "stages", json::array() },
		{ "events", json::array() },
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
	int enemiesDefeatedTotal,
	const json& shotContext)
{
	json* stage = GetCurrentStage();
	if (stage == nullptr || m_ShotActive)
	{
		return;
	}

	m_ShotStartedMs = GetEpochMilliseconds();
	m_PlayerEnemyHitCount = 0;
	m_EnemyEnemyHitCount = 0;
	m_PlayerEnemyDamage = 0;
	m_EnemyEnemyDamage = 0;
	m_PlayerEnemyDamageObserved = false;
	m_EnemyEnemyDamageObserved = false;
	m_HasCurrentDamageCollision = false;
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
		{ "shot_context", shotContext },
		{ "enemy_damage_events", json::array() },
		{
			"controller_type",
			m_Root.value("controller_type", "human")
		},
	};

	m_ShotActive = true;
}

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
	Save();
}

void BalanceLogger::RecordEvent(
	const std::string& eventType,
	const json& details)
{
	if (!m_RunActive || eventType.empty())
	{
		return;
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
	if (m_StageActive && m_CurrentStageIndex != NoStage)
	{
		event["stage_index"] =
			static_cast<int>(m_CurrentStageIndex) + 1;
	}
	m_Root["events"].push_back(std::move(event));
	Save();
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
	const bool playerEnemyDamageKnown =
		m_PlayerEnemyHitCount == 0 || m_PlayerEnemyDamageObserved;
	const bool enemyEnemyDamageKnown =
		m_EnemyEnemyHitCount == 0 || m_EnemyEnemyDamageObserved;
	m_PendingShot["player_enemy_damage"] =
		playerEnemyDamageKnown
		? json(m_PlayerEnemyDamage)
		: json(nullptr);
	m_PendingShot["enemy_enemy_damage"] =
		enemyEnemyDamageKnown
		? json(m_EnemyEnemyDamage)
		: json(nullptr);
	m_PendingShot["total_enemy_damage"] =
		playerEnemyDamageKnown && enemyEnemyDamageKnown
		? json(m_PlayerEnemyDamage + m_EnemyEnemyDamage)
		: json(nullptr);
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
	m_HasCurrentDamageCollision = false;

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

json BalanceLogger::MakeConfigurationSnapshot()
{
	static const std::filesystem::path paths[] =
	{
		"assets/data/stage_01.json",
		"assets/data/enemy_data.json",
		"assets/data/player_status.json",
		"assets/data/player_deck.json",
		"assets/data/dynamic_balance.json",
		"assets/data/difficulty_profiles.json",
		"assets/data/balance_validation.json",
		"assets/data/encounter_balance.json",
		"assets/data/pocket_rules.json",
		"assets/data/balance_autoplay.json",
		"assets/data/balance_targets.json",
		"tools/game_mcp/player_profiles.json",
	};

	json files = json::array();
	for (const std::filesystem::path& path : paths)
	{
		files.push_back(MakeFileFingerprint(path));
	}
	return { { "files", std::move(files) } };
}

json BalanceLogger::MakeFileFingerprint(
	const std::filesystem::path& path)
{
	json result =
	{
		{ "path", path.generic_string() },
		{ "exists", false },
	};

	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		return result;
	}

	constexpr std::uint64_t fnvOffset = 14695981039346656037ull;
	constexpr std::uint64_t fnvPrime = 1099511628211ull;
	std::uint64_t hash = fnvOffset;
	std::uint64_t size = 0;
	char buffer[4096];
	while (file)
	{
		file.read(buffer, sizeof(buffer));
		const std::streamsize count = file.gcount();
		for (std::streamsize index = 0; index < count; index++)
		{
			hash ^= static_cast<unsigned char>(buffer[index]);
			hash *= fnvPrime;
		}
		size += static_cast<std::uint64_t>(count);
	}

	std::ostringstream hashStream;
	hashStream << std::hex << std::setw(16) << std::setfill('0') << hash;
	result["exists"] = true;
	result["size_bytes"] = size;
	result["fnv1a64"] = hashStream.str();
	return result;
}
