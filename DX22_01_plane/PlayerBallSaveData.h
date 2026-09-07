#pragma once

#include "PlayerBallDataLoader.h"
#include "BallStatusJson.h"
#include "UiText.h"
#include <stdexcept>

namespace PlayerBallSaveData
{
	using nlohmann::json;
	inline nlohmann::json BallToJson(const PlayerBallData& ball)
	{
		json upgrades = json::array();
		for (const BallUpgradeStep& upgrade : ball.upgradeTable)
		{
			upgrades.push_back(WriteBallStatus(upgrade));
		}
		return {
			{ "definition_id", ball.definitionId },
			{ "instance_id", ball.instanceId },
			{ "upgrade_level", ball.upgradeLevel },
			{ "upgrade_table", std::move(upgrades) },
			{ "upgrade_model_version", 2 },
			{ "status", WriteBallStatus(ball.status) },
		};
	}

	inline PlayerBallData BallFromJson(const json& value)
	{
		PlayerBallData ball;
		ball.definitionId = value.at("definition_id").get<std::string>();
		ball.instanceId = value.at("instance_id").get<std::uint64_t>();
		ball.upgradeLevel = value.at("upgrade_level").get<int>();
		if (ball.definitionId.empty() || ball.definitionId.size() > 128 ||
			ball.instanceId == 0 ||
			ball.upgradeLevel < 0 ||
			ball.upgradeLevel > PlayerBallData::MaxUpgradeLevel)
		{
			throw std::runtime_error(UiText::InvalidSaveData);
		}

		const json& upgrades = value.at("upgrade_table");
		if (!upgrades.is_array() ||
			upgrades.size() != PlayerBallData::MaxUpgradeLevel)
		{
			throw std::runtime_error(UiText::InvalidSaveData);
		}
		const int upgradeModelVersion = value.value("upgrade_model_version", 1);
		if (upgradeModelVersion < 1 || upgradeModelVersion > 2 || !value.at("status").is_object())
			throw std::runtime_error(UiText::InvalidSaveData);
		for (const char* field : { "attack", "defense", "mass", "radius", "restitution", "friction", "abilities" })
			if (!value.at("status").contains(field)) throw std::runtime_error(UiText::InvalidSaveData);
		ball.status = ReadBallStatus(value.at("status"));
		if (!IsValidBallStatus(ball.status))
		{
			throw std::runtime_error(UiText::InvalidSaveData);
		}
		BallStatus previous = ball.status;
		for (int index = 0; index < PlayerBallData::MaxUpgradeLevel; ++index)
		{
			if (!upgrades[index].is_object() || !upgrades[index].contains("attack") || !upgrades[index].contains("defense"))
				throw std::runtime_error(UiText::InvalidSaveData);
			ball.upgradeTable[index] = ReadBallStatus(upgrades[index], previous);
			if (!IsValidBallStatus(ball.upgradeTable[index])) throw std::runtime_error(UiText::InvalidSaveData);
			previous = ball.upgradeTable[index];
		}
		// 旧セーブの個体IDと強化段階を保ち、性能を新しい強化軸へ移行する。
		if (upgradeModelVersion < 2)
		{
			const auto definitions = PlayerBallDataLoader::Load("assets/data/player_status.json", {}, {});
			for (const auto& definition : definitions.ballDefinitions)
			{
				if (definition.definitionId != ball.definitionId) continue;
				ball.upgradeTable = definition.upgradeTable;
				ball.status = ball.upgradeLevel == 0 ? definition.status : ball.upgradeTable[ball.upgradeLevel - 1];
				break;
			}
		}

		return ball;
	}

}
