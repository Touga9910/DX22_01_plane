#include "GameSaveManager.h"

#pragma execution_character_set("utf-8")

#include "Game.h"
#include "PlayerBallSaveData.h"
#include "RestSiteScene.h"
#include "UiText.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

using nlohmann::json;

namespace
{
	constexpr int kSchemaVersion = 1;
	const std::filesystem::path kSavePath =
		std::filesystem::path("saves") / "run_save.json";

	struct RestoredDeckState
	{
		std::vector<PlayerBallData> drawPile;
		std::vector<PlayerBallData> discardPile;
		std::vector<PlayerBallData> offeredBalls;
		std::optional<PlayerBallData> heldBall;
		std::optional<PlayerBallData> currentBall;
		int previousHeldOfferIndex = -1;
		bool currentBallUsed = false;
		std::uint64_t nextInstanceId = 1;
		std::mt19937 randomEngine;
	};

	std::string SceneToId(SceneType scene)
	{
		switch (scene)
		{
		case SceneType::Select: return "select";
		case SceneType::Battle: return "battle";
		case SceneType::RestSite: return "rest";
		case SceneType::Shop: return "shop";
		default: return "invalid";
		}
	}

	SceneType SceneFromId(const std::string& id)
	{
		if (id == "select") return SceneType::Select;
		if (id == "battle") return SceneType::Battle;
		if (id == "rest") return SceneType::RestSite;
		if (id == "shop") return SceneType::Shop;
		throw std::runtime_error(UiText::InvalidSaveData);
	}

	template<typename Engine>
	std::string SerializeEngine(const Engine& engine)
	{
		std::ostringstream stream;
		stream << engine;
		return stream.str();
	}

	template<typename Engine>
	Engine DeserializeEngine(const std::string& value)
	{
		Engine engine;
		std::istringstream stream(value);
		stream >> engine;
		if (stream.fail())
		{
			throw std::runtime_error(UiText::RandomRestoreFailed);
		}
		return engine;
	}

	std::string MakeUtcTimestamp()
	{
		const std::time_t now = std::chrono::system_clock::to_time_t(
			std::chrono::system_clock::now());
		std::tm utc{};
		gmtime_s(&utc, &now);
		std::ostringstream stream;
		stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
		return stream.str();
	}

	std::string CalculateChecksum(const json& payload)
	{
		const std::string bytes = payload.dump();
		std::uint64_t hash = 14695981039346656037ull;
		for (const unsigned char value : bytes)
		{
			hash ^= value;
			hash *= 1099511628211ull;
		}
		std::ostringstream stream;
		stream << std::hex << std::setw(16) << std::setfill('0') << hash;
		return stream.str();
	}

	using PlayerBallSaveData::BallToJson;
	using PlayerBallSaveData::BallFromJson;

	json BallListToJson(const std::vector<PlayerBallData>& balls)
	{
		json result = json::array();
		for (const PlayerBallData& ball : balls)
		{
			result.push_back(BallToJson(ball));
		}
		return result;
	}

	std::vector<PlayerBallData> BallListFromJson(const json& values)
	{
		if (!values.is_array() || values.size() > 200)
		{
			throw std::runtime_error(UiText::InvalidSaveData);
		}
		std::vector<PlayerBallData> result;
		result.reserve(values.size());
		for (const json& value : values)
		{
			result.push_back(BallFromJson(value));
		}
		return result;
	}

	json ReadAndValidateDocument()
	{
		std::ifstream file(kSavePath, std::ios::binary);
		if (!file)
		{
			throw std::runtime_error(UiText::NoSave);
		}
		json document;
		file >> document;
		if (document.at("schema_version").get<int>() != kSchemaVersion)
		{
			throw std::runtime_error(UiText::UnsupportedSaveData);
		}
		const json& payload = document.at("payload");
		if (document.at("checksum").get<std::string>() !=
			CalculateChecksum(payload))
		{
			throw std::runtime_error(UiText::CorruptedSaveData);
		}
		SceneFromId(payload.at("resume_scene").get<std::string>());
		const json& run = payload.at("run");
		const int maxHp = run.at("max_hp").get<int>();
		const int currentHp = run.at("current_hp").get<int>();
		const int floor = run.at("progress").get<int>();
		const int money = run.at("money").get<int>();
		if (maxHp < 1 || maxHp > 100000 || currentHp < 0 ||
			currentHp > maxHp || floor < 1 || floor > 1000000 ||
			money < 0 || money > 1000000000)
		{
			throw std::runtime_error(UiText::InvalidSaveData);
		}
		return payload;
	}

	void ValidateUniqueInstances(const RestoredDeckState& deck)
	{
		std::set<std::uint64_t> ids;
		std::uint64_t maximumId = 0;
		std::size_t count = 0;
		auto addBall = [&](const PlayerBallData& ball)
		{
			if (!ids.insert(ball.instanceId).second)
			{
				throw std::runtime_error(UiText::InvalidSaveData);
			}
			maximumId = (std::max)(maximumId, ball.instanceId);
			++count;
		};
		for (const PlayerBallData& ball : deck.drawPile) addBall(ball);
		for (const PlayerBallData& ball : deck.discardPile) addBall(ball);
		for (const PlayerBallData& ball : deck.offeredBalls) addBall(ball);
		if (deck.heldBall.has_value()) addBall(*deck.heldBall);
		if (deck.currentBall.has_value()) addBall(*deck.currentBall);
		if (count < PlayerDeck::MinimumDeckSize || count > 200 ||
			deck.nextInstanceId <= maximumId)
		{
			throw std::runtime_error(UiText::InvalidSaveData);
		}
	}
}

RunSaveInfo GameSaveManager::Inspect()
{
	RunSaveInfo info;
	info.exists = std::filesystem::exists(kSavePath);
	if (!info.exists)
	{
		return info;
	}
	try
	{
		const json payload = ReadAndValidateDocument();
		const json& run = payload.at("run");
		info.floor = run.at("progress").get<int>();
		info.currentHp = run.at("current_hp").get<int>();
		info.maxHp = run.at("max_hp").get<int>();
		info.money = run.at("money").get<int>();
		info.savedAt = payload.at("saved_at_utc").get<std::string>();
		info.valid = true;
	}
	catch (const std::exception& exception)
	{
		info.error = exception.what();
	}
	return info;
}

bool GameSaveManager::Save(
	Game& game,
	SceneType resumeScene,
	bool sceneAlreadyActive,
	std::string& message)
{
	if (game.IsDebugMode()) { message = "Debug battles do not overwrite run saves."; return false; }
	if (SceneToId(resumeScene) == "invalid")
	{
		message = UiText::SavingUnavailable;
		return false;
	}
	try
	{
		const std::uint32_t routeCounter = game.m_RouteSelectionCounter;
		bool restActionUsed = false;
		if (sceneAlreadyActive && resumeScene == SceneType::RestSite)
		{
			const RestSiteScene* rest =
				dynamic_cast<const RestSiteScene*>(game.GetCurrentScene());
			restActionUsed = rest != nullptr && rest->HasUsedAction();
		}

		json relics = json::array();
		for (const bool owned : game.m_OwnedRelics)
		{
			relics.push_back(owned);
		}
		const RunResultSnapshot& runStatistics =
			game.m_RunStatistics.GetState();
		json acquiredBalls = json::array();
		for (const std::string& ballId :
			runStatistics.acquiredBallIds)
		{
			acquiredBalls.push_back(ballId);
		}
		json ballUsage = json::object();
		for (const auto& [ballId, shotCount] :
			runStatistics.ballShotCounts)
		{
			ballUsage[ballId] = shotCount;
		}
		const PlayerDeck& deck = game.m_PlayerDeck;
		const json payload = {
			{ "run_map", game.m_RunProgress.GetMap().Save() },
			{ "saved_at_utc", MakeUtcTimestamp() },
			{ "resume_scene", SceneToId(resumeScene) },
			{ "scene_state", {
				{ "rest_action_used", restActionUsed },
				{ "shop_relic_offers", game.m_ShopRelicOffers },
			} },
			{ "run", {
				{ "max_hp", game.m_PlayerRunStatus.maxHp },
				{ "current_hp", game.m_PlayerRunStatus.currentHp },
				{ "money", game.m_PlayerRunStatus.money },
				{ "progress", game.m_PlayerRunStatus.progress },
				{ "cleared_stage_count", game.m_RunProgress.GetClearedBattleCount() },
				{ "area_progress", game.m_RunProgress.GetAreaProgress() },
				{ "ascension", game.m_ActiveAscension },
				{ "run_phase", ToString(game.m_RunProgress.GetPhase()) },
				{ "selected_stage_id", game.m_PlayerRunStatus.GetSelectedStageId() },
				{ "last_stage_id", game.m_PlayerRunStatus.GetLastStageId() },
				{ "owned_relics", std::move(relics) },
			} },
			{ "random", {
				{ "run_seed", game.m_RunRandomSeed },
				{ "stage_selection_seed", game.m_StageSelectionSeed },
				{ "route_selection_seed", game.m_RouteSelectionSeed },
				{ "route_selection_counter", routeCounter },
				{ "stage_selector_state", SerializeEngine(game.m_StageSelector.m_RandomEngine) },
				{ "pocket_state", SerializeEngine(game.m_PocketRandomEngine) },
				{ "relic_state", SerializeEngine(game.m_RelicRandomEngine) },
			} },
			{ "dynamic_balance", {
				{ "enabled", game.m_DynamicBalanceEnabled },
				{ "level", game.m_DynamicBalanceLevel },
				{ "applied_enabled", game.m_DynamicBalanceAppliedEnabled },
				{ "applied_level", game.m_DynamicBalanceAppliedLevel },
				{ "last_result", game.m_DynamicBalanceLastResult },
				{ "last_reason", game.m_DynamicBalanceLastReason },
			} },
			{ "run_statistics", {
				{ "reached_floor", runStatistics.reachedFloor },
				{ "area_progress", runStatistics.areaProgress },
				{ "total_battles", runStatistics.totalBattles },
				{ "midboss_challenges", runStatistics.midBossChallenges },
				{ "midboss_defeats", runStatistics.midBossDefeats },
				{ "final_boss_reached", runStatistics.finalBossReached },
				{ "final_boss_defeated", runStatistics.finalBossDefeated },
				{ "final_boss_id", runStatistics.finalBossId },
				{ "total_shots", runStatistics.totalShots },
				{ "total_damage", runStatistics.totalDamage },
				{ "current_turn_damage", runStatistics.currentTurnDamage },
				{ "maximum_turn_damage", runStatistics.maximumTurnDamage },
				{ "damage_taken", runStatistics.damageTaken },
				{ "active_frames", runStatistics.activeFrames },
				{ "acquired_ball_ids", std::move(acquiredBalls) },
				{ "ball_shot_counts", std::move(ballUsage) },
			} },
			{ "deck", {
				{ "draw_pile", BallListToJson(deck.m_DrawPile) },
				{ "discard_pile", BallListToJson(deck.m_DiscardPile) },
				{ "offered_balls", BallListToJson(deck.m_OfferedBalls) },
				{ "held_ball", deck.m_HeldBall.has_value()
					? BallToJson(*deck.m_HeldBall) : json(nullptr) },
				{ "current_ball", deck.m_CurrentBall.has_value()
					? BallToJson(*deck.m_CurrentBall) : json(nullptr) },
				{ "previous_held_offer_index", deck.m_PreviousHeldOfferIndex },
				{ "current_ball_used", deck.m_IsCurrentBallUsed },
				{ "next_instance_id", deck.m_NextInstanceId },
				{ "random_state", SerializeEngine(deck.m_RandomEngine) },
			} },
		};
		const json document = {
			{ "schema_version", kSchemaVersion },
			{ "checksum", CalculateChecksum(payload) },
			{ "payload", payload },
		};

		std::filesystem::create_directories(kSavePath.parent_path());
		const std::filesystem::path temporaryPath = kSavePath.string() + ".tmp";
		{
			std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				throw std::runtime_error(UiText::TemporarySaveFailed);
			}
			file << std::setw(2) << document << '\n';
			file.flush();
			if (!file)
			{
				throw std::runtime_error(UiText::SaveWriteFailed);
			}
		}
		if (!MoveFileExW(
			temporaryPath.c_str(),
			kSavePath.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			std::filesystem::remove(temporaryPath);
			throw std::runtime_error(UiText::SaveCommitFailed);
		}
		message = UiText::RunSaved;
		return true;
	}
	catch (const std::exception& exception)
	{
		message = std::string(UiText::SaveFailedPrefix) + exception.what();
		return false;
	}
}

bool GameSaveManager::Load(Game& game, std::string& message)
{
	try
	{
		const json payload = ReadAndValidateDocument();
		SceneType resumeScene =
			SceneFromId(payload.at("resume_scene").get<std::string>());
		const json& run = payload.at("run");
		const int activeAscension = std::clamp(run.value("ascension", 0), 0, ProgressionProfile::MaximumAscension);
		const json& random = payload.at("random");
		const json& dynamicBalance = payload.at("dynamic_balance");
		const json& deckJson = payload.at("deck");
		RunResultSnapshot restoredStatistics{};
		if (payload.contains("run_statistics"))
		{
			const json& statistics = payload.at("run_statistics");
			restoredStatistics.reachedFloor =
				statistics.value("reached_floor", 1);
			restoredStatistics.areaProgress =
				statistics.value("area_progress", 0);
			restoredStatistics.totalBattles =
				statistics.value("total_battles", 0);
			restoredStatistics.midBossChallenges =
				statistics.value("midboss_challenges", 0);
			restoredStatistics.midBossDefeats =
				statistics.value("midboss_defeats", 0);
			restoredStatistics.finalBossReached =
				statistics.value("final_boss_reached", false);
			restoredStatistics.finalBossDefeated =
				statistics.value("final_boss_defeated", false);
			restoredStatistics.finalBossId =
				statistics.value("final_boss_id", std::string());
			restoredStatistics.totalShots =
				statistics.value("total_shots", 0);
			restoredStatistics.totalDamage =
				statistics.value("total_damage", 0);
			restoredStatistics.currentTurnDamage =
				statistics.value("current_turn_damage", 0);
			restoredStatistics.maximumTurnDamage =
				statistics.value("maximum_turn_damage", 0);
			restoredStatistics.damageTaken =
				statistics.value("damage_taken", 0);
			restoredStatistics.activeFrames =
				statistics.value("active_frames", std::uint64_t{ 0 });
			if (statistics.contains("acquired_ball_ids"))
			{
				const json& acquired = statistics.at("acquired_ball_ids");
				if (!acquired.is_array() || acquired.size() > 200)
				{
					throw std::runtime_error(UiText::InvalidSaveData);
				}
				for (const json& value : acquired)
				{
					const std::string ballId = value.get<std::string>();
					if (ballId.empty() || ballId.size() > 128)
					{
						throw std::runtime_error(UiText::InvalidSaveData);
					}
					restoredStatistics.acquiredBallIds.push_back(ballId);
				}
			}
			if (statistics.contains("ball_shot_counts"))
			{
				const json& usage = statistics.at("ball_shot_counts");
				if (!usage.is_object() || usage.size() > 200)
				{
					throw std::runtime_error(UiText::InvalidSaveData);
				}
				for (const auto& [ballId, count] : usage.items())
				{
					const int shotCount = count.get<int>();
					if (ballId.empty() || ballId.size() > 128 ||
						shotCount < 0 || shotCount > 100000000)
					{
						throw std::runtime_error(UiText::InvalidSaveData);
					}
					restoredStatistics.ballShotCounts[ballId] = shotCount;
				}
			}
			if (restoredStatistics.reachedFloor < 1 ||
				restoredStatistics.reachedFloor > 1000000 ||
				restoredStatistics.areaProgress < 0 ||
				restoredStatistics.areaProgress > 1000000 ||
				restoredStatistics.totalBattles < 0 ||
				restoredStatistics.midBossChallenges < 0 ||
				restoredStatistics.midBossDefeats < 0 ||
				restoredStatistics.midBossDefeats >
					restoredStatistics.midBossChallenges ||
				restoredStatistics.finalBossId.size() > 128 ||
				restoredStatistics.totalShots < 0 ||
				restoredStatistics.totalDamage < 0 ||
				restoredStatistics.currentTurnDamage < 0 ||
				restoredStatistics.maximumTurnDamage < 0 ||
				restoredStatistics.damageTaken < 0)
			{
				throw std::runtime_error(UiText::InvalidSaveData);
			}
		}

		PlayerRunStatus restoredStatus;
		restoredStatus.maxHp = run.at("max_hp").get<int>();
		restoredStatus.currentHp = run.at("current_hp").get<int>();
		restoredStatus.money = run.at("money").get<int>();
		restoredStatus.progress = run.at("progress").get<int>();
		restoredStatus.SetSelectedStageId(
			run.at("selected_stage_id").get<std::string>());
		restoredStatus.SetLastStageId(
			run.at("last_stage_id").get<std::string>());
		const int clearedStages = run.at("cleared_stage_count").get<int>();
		int areaProgress = run.value("area_progress", clearedStages);
		RunPhase runPhase = RunPhaseFromString(
			run.value("run_phase", std::string("normal_route")));
		if (clearedStages < 0 || clearedStages > 1000000 ||
			areaProgress < 0 || areaProgress > 1000000)
		{
			throw std::runtime_error(UiText::InvalidSaveData);
		}

		// Migrate legacy normal-route saves past the finite endpoint to the
		// guaranteed boss-preparation rest.
		const bool migratedToBossPreparation =
			!game.m_BalanceValidationEnduranceMode &&
			runPhase == RunPhase::NormalRoute &&
			areaProgress >= Game::kNormalRouteAreaGoal;
		if (migratedToBossPreparation)
		{
			areaProgress = Game::kNormalRouteAreaGoal;
			restoredStatus.progress = Game::kNormalRouteAreaGoal;
			restoredStatistics.areaProgress = Game::kNormalRouteAreaGoal;
			runPhase = RunPhase::BossPreparation;
			resumeScene = SceneType::RestSite;
		}

		std::array<bool, static_cast<std::size_t>(RelicType::Count)> relics{};
		const json& relicJson = run.at("owned_relics");
		if (!relicJson.is_array() || relicJson.size() > relics.size())
		{
			throw std::runtime_error(UiText::InvalidSaveData);
		}
		for (std::size_t index = 0; index < relicJson.size(); ++index)
		{
			relics[index] = relicJson[index].get<bool>();
		}

		RestoredDeckState restoredDeck;
		restoredDeck.drawPile = BallListFromJson(deckJson.at("draw_pile"));
		restoredDeck.discardPile = BallListFromJson(deckJson.at("discard_pile"));
		restoredDeck.offeredBalls = BallListFromJson(deckJson.at("offered_balls"));
		if (!deckJson.at("held_ball").is_null())
		{
			restoredDeck.heldBall = BallFromJson(deckJson.at("held_ball"));
		}
		if (!deckJson.at("current_ball").is_null())
		{
			restoredDeck.currentBall = BallFromJson(deckJson.at("current_ball"));
		}
		restoredDeck.previousHeldOfferIndex =
			deckJson.at("previous_held_offer_index").get<int>();
		restoredDeck.currentBallUsed =
			deckJson.at("current_ball_used").get<bool>();
		restoredDeck.nextInstanceId =
			deckJson.at("next_instance_id").get<std::uint64_t>();
		restoredDeck.randomEngine = DeserializeEngine<std::mt19937>(
			deckJson.at("random_state").get<std::string>());
		if (restoredDeck.previousHeldOfferIndex < -1 ||
			restoredDeck.previousHeldOfferIndex >=
				static_cast<int>(restoredDeck.offeredBalls.size()))
		{
			throw std::runtime_error(UiText::InvalidSaveData);
		}
		ValidateUniqueInstances(restoredDeck);

		const std::uint32_t runSeed = random.at("run_seed").get<std::uint32_t>();
		const std::uint32_t stageSeed =
			random.at("stage_selection_seed").get<std::uint32_t>();
		const std::uint32_t routeSeed =
			random.at("route_selection_seed").get<std::uint32_t>();
		const std::uint32_t routeCounter =
			random.at("route_selection_counter").get<std::uint32_t>();
		const std::mt19937 stageEngine = DeserializeEngine<std::mt19937>(
			random.at("stage_selector_state").get<std::string>());
		const std::mt19937 pocketEngine = DeserializeEngine<std::mt19937>(
			random.at("pocket_state").get<std::string>());
		const bool hasRelicRandomState = random.contains("relic_state");
		const std::mt19937 relicEngine = hasRelicRandomState
			? DeserializeEngine<std::mt19937>(
				random.at("relic_state").get<std::string>())
			: std::mt19937(runSeed ^ 0xd3a2646cu);
		std::vector<int> shopRelicOffers;
		if (payload.contains("scene_state"))
		{
			const json& sceneState = payload.at("scene_state");
			shopRelicOffers = sceneState.value(
				"shop_relic_offers", std::vector<int>{});
			for (int relicIndex : shopRelicOffers)
			{
				if (relicIndex < 0 || relicIndex >= game.GetRelicCount())
				{
					throw std::runtime_error(UiText::InvalidSaveData);
				}
			}
		}

		const int dynamicLevel = dynamicBalance.at("level").get<int>();
		const int appliedLevel = dynamicBalance.at("applied_level").get<int>();
		if (dynamicLevel < game.m_DynamicBalanceMinLevel ||
			dynamicLevel > game.m_DynamicBalanceMaxLevel ||
			appliedLevel < game.m_DynamicBalanceMinLevel ||
			appliedLevel > game.m_DynamicBalanceMaxLevel)
		{
			throw std::runtime_error(UiText::InvalidSaveData);
		}

		RunMap restoredMap;
		if (payload.contains("run_map"))
		{
			restoredMap = RunMap::Restore(payload.at("run_map"));
		}
		else
		{
			// Historical choices are unknown. Start the visible map at this checkpoint.
			restoredMap.Generate(routeSeed, areaProgress, (std::max)(0, Game::kNormalRouteAreaGoal - areaProgress));
			if (runPhase == RunPhase::BossPreparation || runPhase == RunPhase::FinalBossReady || runPhase == RunPhase::FinalBoss)
			{
				restoredMap.Choose(0);
				if (runPhase != RunPhase::BossPreparation) restoredMap.CompleteActive();
				if (runPhase == RunPhase::FinalBoss) restoredMap.Choose(1);
			}
			else if (resumeScene != SceneType::Select)
			{
				restoredMap.MigrateEntry(resumeScene == SceneType::Shop ? StageRouteType::Shop :
					resumeScene == SceneType::RestSite ? StageRouteType::RestSite : StageRouteType::NormalBattle);
			}
		}
		const RunMapNode* activeMapNode = restoredMap.Node(restoredMap.Active());
		bool mapSceneValid = false;
		if (runPhase == RunPhase::NormalRoute)
		{
			mapSceneValid = resumeScene == SceneType::Select ? activeMapNode == nullptr && !restoredMap.Available().empty() :
				activeMapNode != nullptr && (
					(resumeScene == SceneType::Shop && activeMapNode->type == StageRouteType::Shop) ||
					(resumeScene == SceneType::RestSite && activeMapNode->type == StageRouteType::RestSite) ||
					(resumeScene == SceneType::Battle && (activeMapNode->type == StageRouteType::NormalBattle || activeMapNode->type == StageRouteType::MidBoss)));
		}
		else if (runPhase == RunPhase::BossPreparation)
			mapSceneValid = resumeScene == SceneType::RestSite && activeMapNode && activeMapNode->type == StageRouteType::BossPreparation;
		else if (runPhase == RunPhase::FinalBossReady)
			mapSceneValid = resumeScene == SceneType::Select && !activeMapNode && restoredMap.Available() == std::vector<int>{restoredMap.AreaCount() * 3 + 1};
		else if (runPhase == RunPhase::FinalBoss)
			mapSceneValid = resumeScene == SceneType::Battle && activeMapNode && activeMapNode->type == StageRouteType::FinalBoss;
		if (restoredMap.CompletedAreas() != areaProgress || !mapSceneValid)
			throw std::runtime_error(UiText::InvalidSaveData);

		game.StartNewRun("human", "save_load", "", "", runSeed);
		game.m_ActiveAscension = activeAscension;
		game.m_RestHealRatio = (std::max)(0.05f, game.m_DefaultRestHealRatio - ProgressionProfile::RestHealPenalty(activeAscension));
		game.m_PlayerRunStatus = std::move(restoredStatus);
		game.m_RunProgress.Restore({
			std::move(restoredMap),
			clearedStages,
			areaProgress,
			runPhase,
		});
		game.m_OwnedRelics = relics;
		game.m_RunRandomSeed = runSeed;
		game.m_StageSelectionSeed = stageSeed;
		game.m_RouteSelectionSeed = routeSeed;
		game.m_RouteSelectionCounter = routeCounter;
		game.m_StageSelector.m_RandomEngine = stageEngine;
		game.m_PocketRandomEngine = pocketEngine;
		game.m_RelicRandomEngine = relicEngine;
		game.m_DynamicBalanceEnabled = dynamicBalance.at("enabled").get<bool>();
		game.m_DynamicBalanceLevel = dynamicLevel;
		game.m_DynamicBalanceAppliedEnabled =
			dynamicBalance.at("applied_enabled").get<bool>();
		game.m_DynamicBalanceAppliedLevel = appliedLevel;
		game.m_DynamicBalanceLastResult =
			dynamicBalance.at("last_result").get<std::string>();
		game.m_DynamicBalanceLastReason =
			dynamicBalance.at("last_reason").get<std::string>();
		game.m_RunStatistics.Restore(restoredStatistics);
		game.m_RunActive = true;

		PlayerDeck& deck = game.m_PlayerDeck;
		deck.m_DrawPile = std::move(restoredDeck.drawPile);
		deck.m_DiscardPile = std::move(restoredDeck.discardPile);
		deck.m_OfferedBalls = std::move(restoredDeck.offeredBalls);
		deck.m_HeldBall = std::move(restoredDeck.heldBall);
		deck.m_CurrentBall = std::move(restoredDeck.currentBall);
		deck.m_PreviousHeldOfferIndex = restoredDeck.previousHeldOfferIndex;
		deck.m_IsCurrentBallUsed = restoredDeck.currentBallUsed;
		deck.m_NextInstanceId = restoredDeck.nextInstanceId;
		deck.m_RandomEngine = restoredDeck.randomEngine;

		game.m_IsRestoringRunSave = true;
		game.ChangeScene(resumeScene);
		game.m_IsRestoringRunSave = false;
		if (hasRelicRandomState)
		{
			// Undo the temporary shop roll so the next saved roll stays deterministic.
			game.m_RelicRandomEngine = relicEngine;
		}
		if (resumeScene == SceneType::Shop && !shopRelicOffers.empty())
		{
			game.m_ShopRelicOffers = std::move(shopRelicOffers);
		}
		if (migratedToBossPreparation)
		{
			game.RecordBalanceEvent(
				"run_endpoint_save_migrated",
				{
					{ "area_progress", areaProgress },
					{ "run_phase", ToString(runPhase) },
				});
		}
		if (!migratedToBossPreparation &&
			resumeScene == SceneType::RestSite &&
			payload.at("scene_state").at("rest_action_used").get<bool>())
		{
			if (RestSiteScene* rest = dynamic_cast<RestSiteScene*>(game.GetCurrentScene()))
			{
				rest->MarkActionUsed();
			}
		}
		message = UiText::RunLoaded;
		return true;
	}
	catch (const std::exception& exception)
	{
		game.m_IsRestoringRunSave = false;
		message = std::string(UiText::LoadFailedPrefix) + exception.what();
		return false;
	}
}

bool GameSaveManager::Remove(std::string* error)
{
	std::error_code code;
	std::filesystem::remove(kSavePath, code);
	if (code)
	{
		if (error != nullptr)
		{
			*error = code.message();
		}
		return false;
	}
	return true;
}
