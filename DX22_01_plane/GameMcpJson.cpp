#include "GameMcpJson.h"
#include "Game.h"
#include "BallStatusJson.h"
#include "PlayerBallData.h"
#include "StageData.h"
#include "BattleScene.h"
#include "ResultScene.h"
#include "RestSiteScene.h"
#include "ShopScene.h"
#include "StageSelectScene.h"
#include "TitleScene.h"
#include "json/json.hpp"
#include <chrono>

using namespace GameMcpJson;

namespace GameMcpJson
{
	/// <summary>
	/// 現在の時刻をUNIXエポックからのミリ秒で返す。
	/// </summary>
	std::int64_t UnixTimeMilliseconds()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}
	/// <summary>
	/// 報酬選択や敗北の状態と戦闘フェーズを確認し、MCP用のゲーム状態名を返す。
	/// </summary>
	const char* GetGameStateName(const Game& game)
	{
		if (game.IsClearRewardActive())
		{
			return "clear_reward";
		}

		if (game.GetLastBattleResult() == BattleResult::Defeat &&
			game.GetCurrentSceneType() == SceneType::Result)
		{
			return "game_over";
		}

		switch (game.GetBattleState())
		{
		case BattleState::AimingDirection:
			return "aiming_direction";
		case BattleState::AimingPower:
			return "aiming_power";
		case BattleState::BallsMoving:
			return "balls_moving";
		case BattleState::EnemyAttack:
			return "enemy_attack";
		case BattleState::TurnEnd:
			return "turn_end";
		case BattleState::Finished:
			return "finished";
		case BattleState::Inactive:
		default:
			return "inactive";
		}
	}


	/// <summary>
	/// シーンの型に対応するMCP用の画面名を返す。
	/// </summary>
	const char* GetSceneName(const Scene* scene)
	{
		if (dynamic_cast<const TitleScene*>(scene) != nullptr)
		{
			return "title";
		}
		if (dynamic_cast<const StageSelectScene*>(scene) != nullptr)
		{
			return "stage_select";
		}
		if (dynamic_cast<const BattleScene*>(scene) != nullptr)
		{
			return "battle";
		}
		if (dynamic_cast<const RestSiteScene*>(scene) != nullptr)
		{
			return "rest_site";
		}
		if (dynamic_cast<const ShopScene*>(scene) != nullptr)
		{
			return "shop";
		}
		if (dynamic_cast<const ResultScene*>(scene) != nullptr)
		{
			return "result";
		}
		return "unknown";
	}

	/// <summary>
	/// 三次元ベクトルをx、y、zの各座標を持つJSONに変換する。
	/// </summary>
	nlohmann::json VectorToJson(
		const DirectX::SimpleMath::Vector3& vector)
	{
		return {
			{ "x", vector.x },
			{ "y", vector.y },
			{ "z", vector.z },
		};
	}

	/// <summary>
	/// ステージ種別を通常戦、中ボス戦、ボス戦のMCP用識別子に変換する。
	/// </summary>
	const char* StageTypeToMcpString(StageType stageType)
	{
		switch (stageType)
		{
		case StageType::MidBoss:
			return "midboss";
		case StageType::Boss:
			return "boss";
		default:
			return "normal";
		}
	}
	/// <summary>
	/// 自球の状態をJSONに変換し、既存のMCPクライアント向けに能力の互換項目を追加する。
	/// </summary>
	nlohmann::json PlayerBallStatusToMcpJson(const BallStatus& status)
	{
		// Keep the historical flat ability aliases used by MCP clients while
		// reusing the canonical serializer for every current status value.
		nlohmann::json value = WritePlayerBallStatus(status);
		value["split"] = status.abilities.split;
		value["pierce"] = status.abilities.pierce;
		value["anchor"] = status.abilities.anchor;
		value["refractAfterPierce"] = status.abilities.refractAfterPierce;
		value["heavyDistanceStacks"] = status.abilities.heavyDistanceStacks;
		value["lightweight"] = status.abilities.lightweight;
		value["reflectionSetup"] = status.abilities.reflectionSetup;
		value["returnToShotStart"] = status.abilities.returnToShotStart;
		return value;
	}

	/// <summary>
	/// 球の識別子、強化情報、現在の状態をMCP用のJSONに変換する。
	/// </summary>
	nlohmann::json BallDataToJson(
		const PlayerBallData& ball,
		int index)
	{
		return {
			{ "index", index },
			{ "definition_id", ball.definitionId },
			{ "category", BallCategoryId(ball.category) },
			{ "instance_id", ball.instanceId },
			{ "upgrade_level", ball.upgradeLevel },
			{ "can_upgrade", ball.CanUpgrade() },
			{ "next_upgrade", ball.CanUpgrade() ? WritePlayerBallStatus(ball.upgradeTable[ball.upgradeLevel]) : nlohmann::json(nullptr) },
			{ "status", PlayerBallStatusToMcpJson(ball.status) },
		};
	}

	/// <summary>
	/// インスタンスIDに対応する球をデッキ内で探し、見つかった位置を返す。
	/// </summary>
	int FindDeckBallIndex(
		const Game& game,
		std::uint64_t instanceId)
	{
		for (int index = 0;
			index < game.GetDeckBallCount();
			index++)
		{
			const PlayerBallData* ball =
				game.GetDeckBall(index);
			if (ball != nullptr &&
				ball->instanceId == instanceId)
			{
				return index;
			}
		}
		return -1;
	}
	/// <summary>
	/// ランの戦闘モードをMCP用の識別子に変換する。
	/// </summary>
	const char* McpRunCombatModeId(RunCombatMode mode)
	{
		return mode == RunCombatMode::PhaseTest ? "phase" : "legacy";
	}

	/// <summary>
	/// コマンドの成否とメッセージを応答用のJSONにまとめる。
	/// </summary>
	nlohmann::json CommandResult(
		bool ok,
		const std::string& message)
	{
		return {
			{ "ok", ok },
			{ "message", message },
		};
	}
}
