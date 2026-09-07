#include "PlayerBallDataLoader.h"
#include "BallStatusJson.h"

#include "json/json.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace
{
	void LoadUpgradeTableFromJson(PlayerBallData& ballData, const json& ballJson)
	{
		BallStatus previous = ballData.status;
		for (int index = 0; index < PlayerBallData::MaxUpgradeLevel; ++index)
		{
			BallStatus step = previous;
			step.attack += 1;
			step.defense += 1;
			if (ballJson.contains("upgrades") && ballJson["upgrades"].is_array() &&
				index < static_cast<int>(ballJson["upgrades"].size()) && ballJson["upgrades"][index].is_object())
			{
				step = ReadBallStatus(ballJson["upgrades"][index], previous);
			}
			ballData.upgradeTable[index] = NormalizeBallStatus(step);
			previous = ballData.upgradeTable[index];
		}
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
					ReadBallStatus(root["status"], result.defaultBallStatus);
			}

			result.defaultBallStatus = NormalizeBallStatus(result.defaultBallStatus);
			result.defaultRunStatus.maxHp = root.value("maxHp", fallbackRunStatus.maxHp);
			result.defaultRunStatus.currentHp =
				root.value("currentHp", result.defaultRunStatus.maxHp);
			result.restHealRatio = std::clamp(
				root.value("restHealRatio", result.restHealRatio),
				0.01f,
				1.0f);
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
							ReadBallStatus(
								ballJson["status"],
								result.defaultBallStatus));
					}
					else
					{
						ballData.status = NormalizeBallStatus(
							ReadBallStatus(
								ballJson,
								result.defaultBallStatus));
					}

					LoadUpgradeTableFromJson(ballData, ballJson);

					result.ballDefinitions.push_back(ballData);
				}
			}
		}
		catch (...)
		{
			result.defaultBallStatus = fallbackBallStatus;
			result.defaultRunStatus = fallbackRunStatus;
			result.restHealRatio = 0.25f;
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
		LoadUpgradeTableFromJson(defaultBall, json::object());
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

					// 同じIDの重複は、同種ボールの別個体として扱う。
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

	// デッキファイルがない場合や有効なIDを含まない場合でも、ゲームを開始可能にする。
	if (deck.empty() && !ballDefinitions.empty())
	{
		std::cerr
			<< "[PlayerDeck] Falling back to the first ball definition: "
			<< ballDefinitions.front().definitionId << '\n';
		deck.push_back(ballDefinitions.front());
	}

	return deck;
}
