#include "BalanceLogger.h"
#include "BalanceLoggerTelemetryJson.h"

#include <iostream>
#include <utility>

using json = nlohmann::json;
using namespace BalanceLogDetail;

BalanceLogger& BalanceLogger::GetInstance()
{
	static BalanceLogger instance;
	return instance;
}

BalanceLogger::BalanceLogger()
	: m_BalanceMetadata(LoadBalanceMetadata())
{
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
	const std::string startedAt = MakeUtcTimestamp();
	m_BalanceMetadata = LoadBalanceMetadata();
	m_LogPath =
		std::filesystem::path("logs") /
		(runContext.value("debug_sandbox", false) ? "debug_battle" : "balance") /
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
		{ "schema_version", 4 },
		{ "data_schema_version", 4 },
		{
			"balance_version",
			m_BalanceMetadata.value(
				"balance_version",
				std::string("Legacy/Unknown"))
		},
		{
			"balance_config_hash",
			m_BalanceMetadata.value(
				"balance_config_hash",
				std::string())
		},
		{ "run_id", runId },
		{ "started_at", startedAt },
		{ "controller_type", controllerType },
		{
			"controller",
			{
				{ "type", controllerType },
				{
					"profile",
					runContext.value("controller_profile", std::string())
				},
				{
					"build_profile",
					runContext.value("build_profile", std::string())
				},
				{
					"build_profile_settings_hash",
					runContext.value(
						"build_profile_settings_hash",
						std::string())
				},
			}
		},
		{ "run_context", runContext },
		{
			"measurement",
			{
				{ "measured_at", startedAt },
				{ "mode", controllerType },
				{
					"seeds",
					runContext.value("randomness", json(nullptr))
				},
				{
					"validation",
					runContext.value("validation", json(nullptr))
				},
			}
		},
		{ "configuration", MakeConfigurationSnapshot() },
		{
			"build",
			{
				{ "compiled_date", __DATE__ },
				{ "compiled_time", __TIME__ },
				{
					"git_commit",
					m_BalanceMetadata.value(
						"git_commit",
						std::string("unknown"))
				},
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
	m_ShotPhysicsTelemetry.Reset();
	m_PierceAttackTelemetry.Reset();
	m_LatestShotPhysicsTelemetry = json(nullptr);
	m_LatestShotPierceAttackTelemetry = json(nullptr);
	m_LatestShotBounceTelemetry = json(nullptr);
	m_LatestShotCombatResult = json(nullptr);
	m_RunActive = true;
	m_SavePending = true;

	Save();

	std::cout << "[BalanceLogger] Run log: "
		<< m_LogPath.string() << std::endl;
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

	m_SavePending = true;
	Save();
	m_RunActive = false;
	m_StageActive = false;
	m_ShotActive = false;
	m_CurrentStageIndex = NoStage;
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
