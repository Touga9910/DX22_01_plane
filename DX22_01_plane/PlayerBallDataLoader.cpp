#include "PlayerBallDataLoader.h"

#include "json/json.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace
{
	void LoadAbilitiesFromJson(BallStatus& status, const json& statusJson)
	{
		if (statusJson.contains("abilities") && statusJson["abilities"].is_object())
		{
			const json& abilitiesJson = statusJson["abilities"];
			status.abilities.split = abilitiesJson.value("split", status.abilities.split);
			status.abilities.pierce = abilitiesJson.value("pierce", status.abilities.pierce);
		}
	}

	BallStatus LoadBallStatusFromJson(const json& statusJson, const BallStatus& defaultStatus)
	{
		BallStatus status = defaultStatus;

		if (!statusJson.is_object())
		{
			return status;
		}

		status.maxHp = statusJson.value("maxHp", status.maxHp);
		status.attack = statusJson.value("attack", status.attack);
		status.defense = statusJson.value("defense", status.defense);
		status.mass = statusJson.value("mass", status.mass);
		status.radius = statusJson.value("radius", status.radius);
		status.restitution = statusJson.value("restitution", status.restitution);
		status.friction = statusJson.value("friction", status.friction);
		LoadAbilitiesFromJson(status, statusJson);

		return status;
	}

	BallStatus NormalizeBallStatus(BallStatus status)
	{
		status.maxHp = (std::max)(1, status.maxHp);
		status.mass = (std::max)(0.0001f, status.mass);
		status.radius = (std::max)(0.0f, status.radius);
		status.restitution = std::clamp(status.restitution, 0.0f, 1.0f);
		status.friction = (std::max)(0.0f, status.friction);

		return status;
	}

	PlayerRunStatus NormalizePlayerRunStatus(PlayerRunStatus status)
	{
		status.maxHp = (std::max)(1, status.maxHp);
		status.currentHp = std::clamp(status.currentHp, 0, status.maxHp);

		return status;
	}
}

PlayerBallDataLoadResult PlayerBallDataLoader::Load(
	const std::string& filePath,
	const BallStatus& fallbackBallStatus,
	const PlayerRunStatus& fallbackRunStatus)
{
	PlayerBallDataLoadResult result;
	result.defaultBallStatus = fallbackBallStatus;
	result.defaultRunStatus = fallbackRunStatus;
	result.defaultRunStatus.maxHp = result.defaultBallStatus.maxHp;
	result.defaultRunStatus.currentHp = result.defaultRunStatus.maxHp;

	std::ifstream file(filePath);
	if (file.is_open())
	{
		try
		{
			json root;
			file >> root;

			if (root.contains("status") && root["status"].is_object())
			{
				result.defaultBallStatus =
					LoadBallStatusFromJson(root["status"], result.defaultBallStatus);
			}

			result.defaultBallStatus = NormalizeBallStatus(result.defaultBallStatus);
			result.defaultRunStatus.maxHp = result.defaultBallStatus.maxHp;
			result.defaultRunStatus.currentHp =
				root.value("currentHp", result.defaultRunStatus.maxHp);

			if (root.contains("balls") && root["balls"].is_array())
			{
				for (const json& ballJson : root["balls"])
				{
					if (!ballJson.is_object())
					{
						continue;
					}

					PlayerBallData ballData;
					ballData.definitionId = ballJson.value("id", "");
					if (ballData.definitionId.empty())
					{
						ballData.definitionId =
							"player_ball_" + std::to_string(result.ballDefinitions.size());
					}

					if (ballJson.contains("status") && ballJson["status"].is_object())
					{
						ballData.status = NormalizeBallStatus(
							LoadBallStatusFromJson(
								ballJson["status"],
								result.defaultBallStatus));
					}
					else
					{
						ballData.status = NormalizeBallStatus(
							LoadBallStatusFromJson(
								ballJson,
								result.defaultBallStatus));
					}

					result.ballDefinitions.push_back(ballData);
				}
			}
		}
		catch (...)
		{
			result.defaultBallStatus = fallbackBallStatus;
			result.defaultRunStatus = fallbackRunStatus;
			result.ballDefinitions.clear();
		}
	}

	result.defaultBallStatus = NormalizeBallStatus(result.defaultBallStatus);
	result.defaultRunStatus = NormalizePlayerRunStatus(result.defaultRunStatus);

	if (result.ballDefinitions.empty())
	{
		PlayerBallData defaultBall;
		defaultBall.definitionId = "player_default";
		defaultBall.status = result.defaultBallStatus;
		result.ballDefinitions.push_back(defaultBall);
	}

	return result;
}

std::vector<PlayerBallData> PlayerBallDataLoader::LoadDeck(
	const std::string& filePath,
	const std::vector<PlayerBallData>& ballDefinitions)
{
	std::vector<PlayerBallData> deck;

	std::ifstream file(filePath);
	if (file.is_open())
	{
		try
		{
			json root;
			file >> root;

			if (root.contains("deck") && root["deck"].is_array())
			{
				for (const json& deckEntry : root["deck"])
				{
					if (!deckEntry.is_string())
					{
						std::cerr
							<< "[PlayerDeck] Ignored a non-string deck entry in "
							<< filePath << '\n';
						continue;
					}

					const std::string definitionId = deckEntry.get<std::string>();
					const auto definition = std::find_if(
						ballDefinitions.begin(),
						ballDefinitions.end(),
						[&definitionId](const PlayerBallData& ballData)
						{
							return ballData.definitionId == definitionId;
						});

					if (definition == ballDefinitions.end())
					{
						std::cerr
							<< "[PlayerDeck] Unknown ball id: "
							<< definitionId << '\n';
						continue;
					}

					// A repeated ID represents another copy of the same ball.
					deck.push_back(*definition);
				}
			}
			else
			{
				std::cerr
					<< "[PlayerDeck] Missing deck array in "
					<< filePath << '\n';
			}
		}
		catch (const std::exception& exception)
		{
			std::cerr
				<< "[PlayerDeck] Failed to load " << filePath
				<< ": " << exception.what() << '\n';
		}
	}
	else
	{
		std::cerr << "[PlayerDeck] Could not open " << filePath << '\n';
	}

	// Keep the game playable when the deck file is missing or has no valid IDs.
	if (deck.empty() && !ballDefinitions.empty())
	{
		std::cerr
			<< "[PlayerDeck] Falling back to the first ball definition: "
			<< ballDefinitions.front().definitionId << '\n';
		deck.push_back(ballDefinitions.front());
	}

	return deck;
}
