#include "GameMcpBridge.h"
#include "GameMcpJson.h"
#include "StageDataLoader.h"
#include "json/json.hpp"
#include "test_checks.h"
#include <Windows.h>
#include <fstream>
#include <iostream>

class Game
{
public:
    int executedCommands = 0;
};

// Exercise the real bridge file/command lifecycle without a renderer or saves.
std::vector<EnemyData> StageDataLoader::LoadEnemyDefinitions(const std::string&)
{
    return {};
}

nlohmann::json GameMcpBridge::BuildState(Game& game, bool running) const
{
    return { { "running", running }, { "executed_commands", game.executedCommands } };
}

nlohmann::json GameMcpBridge::ExecuteCommand(Game& game, const nlohmann::json&)
{
    ++game.executedCommands;
    return GameMcpJson::CommandResult(true, "Applied once.");
}

std::int64_t GameMcpJson::UnixTimeMilliseconds() { return 1234; }
nlohmann::json GameMcpJson::CommandResult(bool ok, const std::string& message)
{
    return { { "ok", ok }, { "message", message } };
}

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("dx22_mcp_storage_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto config = directory / "config.json";
    const auto commandPath = directory / "pending_command.json";
    const auto resultPath = directory / "last_result.json";
    {
        std::ofstream file(config);
        file << nlohmann::json({ { "enabled", true },
            { "bridge_directory", directory.generic_string() },
            { "command_poll_interval_frames", 1 } }).dump();
    }
    Game game;
    GameMcpBridge bridge;
    assert(bridge.Initialize(game, config.string()));
    {
        std::ofstream file(resultPath);
        file << nlohmann::json({ { "command_id", "old-command" } }).dump();
    }
    {
        std::ofstream file(commandPath);
        file << nlohmann::json({ { "command_id", "once-command" },
            { "action", "heal" }, { "arguments", nlohmann::json::object() } }).dump();
    }
    // A reader without delete sharing can outlast the bounded atomic-write retry.
    HANDLE locked = CreateFileW(resultPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    bridge.Update(game);
    assert(game.executedCommands == 1);
    assert(std::filesystem::exists(commandPath));
    CloseHandle(locked);

    // Once the lock is released, acknowledge the original command without
    // executing a non-idempotent action (purchase/heal/reward) a second time.
    bridge.Update(game);
    assert(game.executedCommands == 1);
    assert(!std::filesystem::exists(commandPath));
    nlohmann::json result;
    { std::ifstream file(resultPath); file >> result; }
    assert(result.at("command_id") == "once-command");
    assert(result.at("ok") == true);
    bridge.Shutdown(game);
    for (const auto* name : { "config.json", "game_state.json", "last_result.json" })
        std::filesystem::remove(directory / name);
    std::filesystem::remove(directory);
    std::cout << "MCP bridge result retry passed.\n";
}
