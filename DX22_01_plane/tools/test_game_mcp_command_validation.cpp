#include "GameMcpCommandValidation.h"
#include "test_checks.h"
#include <Windows.h>
#include <iostream>

int main()
{
    using nlohmann::json;
    using GameMcpCommandValidation::Validate;
    const json command = {
        {"command_id", "command-1"}, {"action", "heal"},
        {"arguments", json::object()}
    };
    const auto valid = Validate(command);
    assert(valid.IsValid());
    assert(valid.commandId == "command-1");
    assert(valid.action == "heal");

    auto withoutArguments = command;
    withoutArguments.erase("arguments");
    assert(Validate(withoutArguments).IsValid());

    const json invalidValues[] = {
        nullptr, false, 7, 7.5, "text", json::array(), json::object()
    };
    for (const auto& value : invalidValues)
    {
        assert(!Validate(value).IsValid());
        for (const auto* field : { "command_id", "action", "arguments" })
        {
            if ((value.is_string() && std::string(field) != "arguments") ||
                (value.is_object() && std::string(field) == "arguments"))
            {
                continue;
            }
            auto invalid = command;
            invalid[field] = value;
            const auto checked = Validate(invalid);
            assert(!checked.IsValid());
            if (std::string(field) != "command_id")
            {
                assert(checked.commandId == "command-1");
            }
        }
    }
    for (const auto* field : { "command_id", "action" })
    {
        auto invalid = command;
        invalid.erase(field);
        assert(!Validate(invalid).IsValid());
        invalid[field] = "";
        assert(!Validate(invalid).IsValid());
    }
    for (const auto* field : { "offer_index", "route_index", "relic_index",
        "catalog_index", "choice_index", "target_index", "difficulty", "par" })
    {
        for (const json& value : { json(0.5), json(true), json("0"),
            json(4294967296ULL), json(-4294967296LL), json(18446744073709551615ULL) })
        {
            auto invalid = command;
            invalid["arguments"][field] = value;
            assert(!Validate(invalid).IsValid());
        }
        for (int index : { -1, 0, 2147483647 })
        {
            auto validIndex = command;
            validIndex["arguments"][field] = index;
            assert(Validate(validIndex).IsValid());
        }
    }
    for (const auto* field : { "instance_id", "run_seed", "expected_revision" })
    {
        for (const json& value : { json(-1), json(0.5), json(true), json("1") })
        {
            auto invalid = command;
            invalid["arguments"][field] = value;
            assert(!Validate(invalid).IsValid());
        }
        auto upperBound = command;
        upperBound["arguments"][field] = std::string(field) == "run_seed"
            ? json(4294967295ULL) : json(18446744073709551615ULL);
        assert(Validate(upperBound).IsValid());
    }
    auto invalidSeed = command;
    invalidSeed["arguments"]["run_seed"] = 4294967296ULL;
    assert(!Validate(invalidSeed).IsValid());
    auto nextCommand = command;
    nextCommand["command_id"] = "command-2";
    assert(Validate(nextCommand).IsValid());

    const auto directory = std::filesystem::temp_directory_path() /
        ("dx22_mcp_validation_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "pending_command.json";
    using namespace GameMcpCommandValidation;
    json readCommand;
    assert(ReadPendingCommand(path, readCommand) == FileReadStatus::Unavailable);
    assert(!std::filesystem::exists(path));
    for (const auto* malformed : { "", "{", "{\"command_id\":NaN}", "not json" })
    {
        { std::ofstream file(path); file << malformed; }
        assert(ReadPendingCommand(path, readCommand) == FileReadStatus::Malformed);
        assert(!std::filesystem::exists(path));
        { std::ofstream file(path); file << command.dump(); }
        assert(ReadPendingCommand(path, readCommand) == FileReadStatus::Ready);
        assert(Validate(readCommand).IsValid());
    }
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    assert(ReadPendingCommand(path, readCommand) == FileReadStatus::Unavailable);
    assert(std::filesystem::exists(path));
    CloseHandle(locked);
    assert(ReadPendingCommand(path, readCommand) == FileReadStatus::Ready);
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    std::cout << "MCP command envelope validation passed.\n";
}
