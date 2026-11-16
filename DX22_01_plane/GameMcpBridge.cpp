#include "GameMcpBridge.h"
#include "GameMcpJson.h"
#include "StageDataLoader.h"
#include "json/json.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using namespace GameMcpJson;

namespace
{
	bool ReadJson(
		const std::filesystem::path& path,
		nlohmann::json& output)
	{
		std::ifstream file(path);
		if (!file)
		{
			return false;
		}

		try
		{
			file >> output;
			return true;
		}
		catch (const nlohmann::json::exception&)
		{
			return false;
		}
	}

	bool WriteJsonAtomically(
		const std::filesystem::path& path,
		const nlohmann::json& value)
	{
		std::error_code error;
		std::filesystem::create_directories(
			path.parent_path(),
			error);
		if (error)
		{
			return false;
		}

		const std::filesystem::path temporaryPath =
			path.string() + ".tmp";
		{
			std::ofstream file(
				temporaryPath,
				std::ios::trunc);
			if (!file)
			{
				return false;
			}
			file << value.dump(2);
			file.flush();
			if (!file)
			{
				return false;
			}
		}

#if defined(_WIN32)
		constexpr DWORD replaceFlags =
			MOVEFILE_REPLACE_EXISTING |
			MOVEFILE_WRITE_THROUGH;
		for (int attempt = 0; attempt < 8; attempt++)
		{
			if (MoveFileExW(
				temporaryPath.c_str(),
				path.c_str(),
				replaceFlags))
			{
				return true;
			}

			const DWORD replaceError = GetLastError();
			if (replaceError != ERROR_ACCESS_DENIED &&
				replaceError != ERROR_SHARING_VIOLATION &&
				replaceError != ERROR_LOCK_VIOLATION)
			{
				break;
			}
			std::this_thread::sleep_for(
				std::chrono::milliseconds((std::min)(16, 1 << attempt)));
		}
#else
		std::filesystem::rename(
			temporaryPath,
			path,
			error);
		if (!error)
		{
			return true;
		}
#endif

		std::filesystem::remove(temporaryPath, error);
		return false;
	}
}

bool GameMcpBridge::Initialize(
	Game& game,
	const std::string& configPath)
{
	nlohmann::json config;
	if (!ReadJson(configPath, config))
	{
		std::cout
			<< "[GameMcpBridge] Config not found or invalid: "
			<< configPath << std::endl;
		return false;
	}

	try
	{
		m_Enabled = config.value("enabled", false);
		m_AllowWriteActions =
			config.value("allow_write_actions", true);
		m_PublishIntervalFrames =
			(std::max)(
				1,
				config.value(
					"state_publish_interval_frames",
					60));
		m_CommandPollIntervalFrames =
			(std::max)(
				1,
				config.value(
					"command_poll_interval_frames",
					6));
		m_BridgeDirectory =
			config.value(
				"bridge_directory",
				std::string("runtime/game_mcp"));
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr
			<< "[GameMcpBridge] Invalid config: "
			<< error.what() << std::endl;
		m_Enabled = false;
		return false;
	}

	if (!m_Enabled)
	{
		std::cout << "[GameMcpBridge] Disabled" << std::endl;
		return false;
	}

	m_StageEnemyIds.clear();
	for (const EnemyData& enemy :
		StageDataLoader::LoadEnemyDefinitions(
			"assets/data/enemy_data.json"))
	{
		m_StageEnemyIds.push_back(enemy.id);
	}

	std::error_code error;
	std::filesystem::create_directories(
		m_BridgeDirectory,
		error);
	if (error)
	{
		std::cerr
			<< "[GameMcpBridge] Cannot create bridge directory: "
			<< error.message() << std::endl;
		m_Enabled = false;
		return false;
	}

	std::filesystem::remove(GetCommandPath(), error);
	m_FramesUntilPublish = 0;
	m_FramesUntilCommandPoll = 0;
	PublishState(game, true);
	m_FramesUntilPublish = m_PublishIntervalFrames;

	std::cout
		<< "[GameMcpBridge] Enabled at "
		<< m_BridgeDirectory.string()
		<< std::endl;
	return true;
}

void GameMcpBridge::Update(Game& game)
{
	if (!m_Enabled)
	{
		return;
	}

	m_FramesUntilCommandPoll--;
	if (m_FramesUntilCommandPoll <= 0)
	{
		ProcessPendingCommand(game);
		m_FramesUntilCommandPoll = m_CommandPollIntervalFrames;
	}

	m_FramesUntilPublish--;
	if (m_FramesUntilPublish <= 0)
	{
		PublishState(game, true);
		m_FramesUntilPublish = m_PublishIntervalFrames;
	}
}

void GameMcpBridge::Shutdown(Game& game)
{
	if (!m_Enabled)
	{
		return;
	}

	PublishState(game, false);
	m_Enabled = false;
}

std::filesystem::path GameMcpBridge::GetStatePath() const
{
	return m_BridgeDirectory / "game_state.json";
}

std::filesystem::path GameMcpBridge::GetCommandPath() const
{
	return m_BridgeDirectory / "pending_command.json";
}

std::filesystem::path GameMcpBridge::GetResultPath() const
{
	return m_BridgeDirectory / "last_result.json";
}
void GameMcpBridge::PublishState(
	Game& game,
	bool running)
{
	m_StateSequence++;
	if (!WriteJsonAtomically(
		GetStatePath(),
		BuildState(game, running)))
	{
		std::cerr
			<< "[GameMcpBridge] Failed to publish game state"
			<< std::endl;
	}
}
void GameMcpBridge::ProcessPendingCommand(Game& game)
{
	const std::filesystem::path commandPath =
		GetCommandPath();
	if (!std::filesystem::exists(commandPath))
	{
		return;
	}

	nlohmann::json command;
	if (!ReadJson(commandPath, command))
	{
		return;
	}

	const std::string commandId =
		command.value("command_id", std::string());
	if (commandId.empty() ||
		commandId == m_LastCommandId)
	{
		std::error_code error;
		std::filesystem::remove(commandPath, error);
		return;
	}
	m_LastCommandId = commandId;

	nlohmann::json result;
	try
	{
		result = ExecuteCommand(game, command);
	}
	catch (const nlohmann::json::exception& error)
	{
		result = CommandResult(
			false,
			std::string("Invalid command arguments: ") +
				error.what());
	}

	PublishState(game, true);
	m_FramesUntilPublish = m_PublishIntervalFrames;
	result["schema_version"] = 1;
	result["command_id"] = commandId;
	result["action"] =
		command.value("action", std::string());
	result["completed_at_unix_ms"] =
		UnixTimeMilliseconds();
	result["state_sequence"] = m_StateSequence;

	if (!WriteJsonAtomically(GetResultPath(), result))
	{
		std::cerr
			<< "[GameMcpBridge] Failed to publish command result"
			<< std::endl;
		return;
	}

	std::error_code error;
	std::filesystem::remove(commandPath, error);
}
