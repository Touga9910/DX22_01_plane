#include "BalanceLogger.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

using json = nlohmann::json;

json BalanceLogger::MakeConfigurationSnapshot()
{
	json paths = json::array();
	std::ifstream registry("assets/data/balance_version.json");
	if (registry)
	{
		try
		{
			json value;
			registry >> value;
			if (value.contains("managed_files") &&
				value["managed_files"].is_array())
			{
				paths = value["managed_files"];
			}
		}
		catch (const json::exception&)
		{
			paths = json::array();
		}
	}
	if (paths.empty())
	{
		paths = {
			"assets/data/stage_01.json",
			"assets/data/enemy_data.json",
			"assets/data/player_status.json",
			"assets/data/player_deck.json",
			"assets/data/dynamic_balance.json",
			"assets/data/difficulty_profiles.json",
			"assets/data/encounter_balance.json",
			"assets/data/pocket_rules.json",
		};
	}

	json files = json::array();
	for (const json& value : paths)
	{
		if (!value.is_string())
		{
			continue;
		}
		const std::filesystem::path path = value.get<std::string>();
		files.push_back(MakeFileFingerprint(path));
	}
	return { { "files", std::move(files) } };
}

json BalanceLogger::LoadBalanceMetadata()
{
	json result = {
		{ "balance_version", "Legacy/Unknown" },
		{ "balance_config_hash", "" },
		{ "git_commit", "unknown" },
	};
	std::ifstream source("assets/data/balance_version.json");
	if (!source)
	{
		return result;
	}
	try
	{
		json registry;
		source >> registry;
		if (registry.contains("current_version") &&
			registry["current_version"].is_string())
		{
			result["balance_version"] = registry["current_version"];
		}
		if (registry.contains("current_config_hash") &&
			registry["current_config_hash"].is_string())
		{
			result["balance_config_hash"] =
				registry["current_config_hash"];
		}
		if (registry.contains("registered_git_commit") &&
			registry["registered_git_commit"].is_string())
		{
			result["git_commit"] = registry["registered_git_commit"];
		}
	}
	catch (const json::exception&)
	{
		return result;
	}
	return result;
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
