#pragma once

#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace GameMcpCommandValidation
{
    enum class FileReadStatus { Unavailable, Malformed, Ready };

    inline FileReadStatus ReadJsonFile(
        const std::filesystem::path& path, nlohmann::json& output)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return FileReadStatus::Unavailable;

        std::string contents;
        char buffer[4096];
        while (file)
        {
            file.read(buffer, sizeof(buffer));
            contents.append(buffer, static_cast<std::size_t>(file.gcount()));
        }
        if (file.bad() || !file.eof()) return FileReadStatus::Unavailable;
        try
        {
            output = nlohmann::json::parse(contents);
            return FileReadStatus::Ready;
        }
        catch (const nlohmann::json::exception&)
        {
            return FileReadStatus::Malformed;
        }
    }

    inline FileReadStatus ReadPendingCommand(
        const std::filesystem::path& path, nlohmann::json& output)
    {
        const auto status = ReadJsonFile(path, output);
        // Only discard a completely read but malformed command. A transient
        // sharing or I/O failure must leave the pending operation available.
        if (status == FileReadStatus::Malformed)
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
        return status;
    }

    struct Envelope
    {
        std::string commandId;
        std::string action;
        std::string error;

        bool IsValid() const { return error.empty(); }
    };

    // Validate the envelope before dispatch or result metadata access. Invalid
    // commands with a usable ID still receive a matching failure response.
    inline Envelope Validate(const nlohmann::json& command)
    {
        Envelope envelope;
        if (!command.is_object())
        {
            envelope.error = "Command must be an object.";
            return envelope;
        }

        const auto id = command.find("command_id");
        if (id == command.end() || !id->is_string() ||
            id->get_ref<const std::string&>().empty())
        {
            envelope.error = "command_id must be a nonempty string.";
            return envelope;
        }
        envelope.commandId = id->get<std::string>();

        const auto action = command.find("action");
        if (action == command.end() || !action->is_string() ||
            action->get_ref<const std::string&>().empty())
        {
            envelope.error = "action must be a nonempty string.";
            return envelope;
        }
        envelope.action = action->get<std::string>();

        const auto arguments = command.find("arguments");
        if (arguments != command.end() && !arguments->is_object())
        {
            envelope.error = "arguments must be an object.";
            return envelope;
        }
        if (arguments == command.end())
        {
            return envelope;
        }

        // nlohmann's arithmetic conversions truncate fractions and wrap values
        // outside the destination range. Check before an index can select or
        // purchase a different object from the one requested.
        for (const auto* field : { "offer_index", "route_index", "relic_index",
            "catalog_index", "choice_index", "target_index", "difficulty", "par" })
        {
            const auto value = arguments->find(field);
            if (value == arguments->end()) continue;
            const bool valid = value->is_number_unsigned()
                ? value->get<std::uint64_t>() <=
                    static_cast<std::uint64_t>((std::numeric_limits<int>::max)())
                : value->is_number_integer() &&
                    value->get<std::int64_t>() >= (std::numeric_limits<int>::min)() &&
                    value->get<std::int64_t>() <= (std::numeric_limits<int>::max)();
            if (!valid)
            {
                envelope.error = std::string(field) + " must be a 32-bit integer.";
                return envelope;
            }
        }
        for (const auto* field : { "instance_id", "run_seed", "expected_revision" })
        {
            const auto value = arguments->find(field);
            if (value == arguments->end()) continue;
            if (!value->is_number_integer() ||
                (!value->is_number_unsigned() && value->get<std::int64_t>() < 0) ||
                (std::string(field) == "run_seed" &&
                    value->get<std::uint64_t>() > (std::numeric_limits<std::uint32_t>::max)()))
            {
                envelope.error = std::string(field) + " must be an unsigned integer in range.";
                return envelope;
            }
        }
        return envelope;
    }
}
