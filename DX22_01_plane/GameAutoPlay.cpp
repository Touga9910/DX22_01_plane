#include "Game.h"
#include "BalanceAutoPlayer.h"

#pragma execution_character_set("utf-8")
#include "Renderer.h"
#include "BalanceLogger.h"
#include "GameMcpBridge.h"
#include "GamePresentation.h"
#include "BattleScene.h"
#include "ResultScene.h"
#include "RestSiteScene.h"
#include "ShopScene.h"
#include "StageSelectScene.h"
#include "TitleScene.h"
#include "BallPhysicsComponent.h"
#include "input.h"

#include "PlayerBall.h"  // DrawImGui呼び出しに必要
#include "EnemyBall.h"   // DrawImGui呼び出しに必要
#include "EnemyAttackComponent.h"
#include "BallComponent.h"
#include "PlayerBallDataLoader.h"
#include "StageDataLoader.h"
#include "EnemyData.h"
#include "Pocket.h"
#include "TableConfig.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>


namespace
{
	constexpr int kExtraRewardMoney = 10;
	constexpr int kAutoShopRemoveCost = 15;
	constexpr int kAutoShopBallCost = 20;
	constexpr int kAutoMaximumDeckSize = 8;

	float GetAutoBallValue(const PlayerBallData& ball)
	{
		float value =
			static_cast<float>(ball.status.attack) * 4.0f +
			static_cast<float>(ball.upgradeLevel) * 5.0f;
		if (ball.status.abilities.pierce)
		{
			value += 6.0f;
		}
		if (ball.status.abilities.anchor)
		{
			value += 5.0f;
		}
		if (ball.category == BallCategory::Bounce)
		{
			value += 4.0f;
		}
		return value;
	}
}

void BalanceAutoPlayer::SetEnabled(bool enabled)
{
	m_Enabled = enabled;
	m_DecisionFrame = 0;
}

void BalanceAutoPlayer::LoadConfig(
	const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout
			<< "[BalanceAutoPlay] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;

		m_Enabled = config.value("enabled", false);
		m_RestartAfterGameOver =
			config.value("restart_after_game_over", true);
		m_StopAfterCurrentRunDefault =
			config.value("stop_after_current_run", false);
		m_StopAfterCurrentRunRequested =
			m_Enabled && m_StopAfterCurrentRunDefault;
		m_DecisionDelayFrames = (std::max)(
			1,
			config.value("decision_delay_frames", 20));
		m_MaximumRuns =
			(std::max)(0, config.value("max_runs", 0));
		m_MinimumShotPower = std::clamp(
			config.value("min_shot_power", 4.0f),
			1.0f,
			8.0f);
		m_MaximumShotPower = std::clamp(
			config.value("max_shot_power", 8.0f),
			m_MinimumShotPower,
			8.0f);
		m_AimJitterDegrees = std::clamp(
			config.value("aim_jitter_degrees", 1.5f),
			0.0f,
			15.0f);

		m_RandomSeed =
			config.value("random_seed", 20260727u);
		m_RandomEngine.seed(m_RandomSeed);

		std::cout
			<< "[BalanceAutoPlay] "
			<< (m_Enabled ? "Enabled" : "Disabled")
			<< " / F8 toggles auto play / F9 stops at run end"
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr
			<< "[BalanceAutoPlay] Invalid config: "
			<< error.what() << std::endl;
	}
}

void Game::ApplyBalanceAutoBallSelection(int offerIndex)
{
	m_SelectedOfferIndex = offerIndex;
	if (m_SelectedHoldIndex == offerIndex)
	{
		m_SelectedHoldIndex = -1;
	}
	ApplySelectedBallPreview();
}

void Game::MarkBalanceAutoRewardChosen(
	int rewardIndex,
	int rewardBallIndex,
	const std::string& message)
{
	m_SelectedRewardIndex = rewardIndex;
	m_SelectedRewardBallIndex = rewardBallIndex;
	m_RewardMessage = message;
	m_IsClearRewardChosen = true;
}

bool Game::IsBallAdjustmentCandidate(
	std::uint64_t instanceId) const
{
	return m_BalanceAutoPlayer.IsBallAdjustmentCandidate(instanceId);
}

bool Game::HasAvailableRestBenefit() const
{
	return m_BalanceAutoPlayer.HasAvailableRestBenefit(*this);
}

void Game::RemoveBalanceAutoPendingBall(
	std::uint64_t instanceId)
{
	m_BalanceAutoPlayer.RemovePendingBall(instanceId);
}

void Game::PruneBalanceAutoPendingBalls()
{
	m_BalanceAutoPlayer.PrunePendingBalls(*this);
}

// Balance Auto Playを更新
bool BalanceAutoPlayer::Update(Game& game)
{
	if (game.IsDebugMode() || game.m_DebugController.IsEditorOpen()) return false;
	if (Input::GetKeyTrigger(VK_F8))
	{
		const bool startAutoPlay = !m_Enabled;
		m_Enabled = startAutoPlay;
		m_StopAfterCurrentRunRequested =
			startAutoPlay && m_StopAfterCurrentRunDefault;
		m_DecisionFrame = 0;

		std::cout
			<< "[BalanceAutoPlay] "
			<< (m_Enabled ? "ON" : "OFF")
			<< std::endl;
	}
	if (m_Enabled && Input::GetKeyTrigger(VK_F9))
	{
		m_StopAfterCurrentRunRequested =
			!m_StopAfterCurrentRunRequested;
		std::cout
			<< "[BalanceAutoPlay] Stop at run end: "
			<< (m_StopAfterCurrentRunRequested ? "ON" : "OFF")
			<< std::endl;
	}

	if (!m_Enabled || game.m_SceneManager.Get() == nullptr)
	{
		return false;
	}

	auto isDecisionReady = [this]()
	{
		m_DecisionFrame++;
		if (m_DecisionFrame < m_DecisionDelayFrames)
		{
			return false;
		}

		m_DecisionFrame = 0;
		return true;
	};

	if (dynamic_cast<TitleScene*>(game.m_SceneManager.Get()) != nullptr)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (m_MaximumRuns > 0 &&
			m_RunCount >= m_MaximumRuns)
		{
			m_Enabled = false;
			std::cout
				<< "[BalanceAutoPlay] Max runs reached"
				<< std::endl;
			return false;
		}

		game.StartNewRun();
		m_RunCount++;
		game.ChangeScene(SceneType::Select);
		return true;
	}

	if (dynamic_cast<ResultScene*>(game.m_SceneManager.Get()) != nullptr)
	{
		if (m_StopAfterCurrentRunRequested)
		{
			m_Enabled = false;
			m_StopAfterCurrentRunRequested = false;
			std::cout
				<< "[BalanceAutoPlay] Stopped at terminal result"
				<< std::endl;
			return false;
		}
		if (!m_RestartAfterGameOver)
		{
			m_Enabled = false;
			return false;
		}

		if (!isDecisionReady())
		{
			return false;
		}

		game.ChangeScene(SceneType::Title);
		return true;
	}

	if (StageSelectScene* stageSelect =
		dynamic_cast<StageSelectScene*>(game.m_SceneManager.Get()))
	{
		if (!isDecisionReady())
		{
			return false;
		}

		PrunePendingBalls(game);
		int selectedRoute = -1;
		auto findRoute = [stageSelect](const char* routeId)
		{
			for (int index = 0;
				index < stageSelect->GetRouteNodeCount();
				index++)
			{
				if (std::string(stageSelect->GetRouteIdAt(index)) == routeId)
				{
					return index;
				}
			}
			return -1;
		};

		if (game.IsFinalBossRoute())
		{
			selectedRoute = 0;
		}
		else if (IsHealNeeded(game))
		{
			selectedRoute = findRoute("rest");
		}
		if (selectedRoute < 0 && HasShopAction(game))
		{
			selectedRoute = findRoute("shop");
		}
		if (selectedRoute < 0 &&
			FindUpgradeTarget(game) >= 0)
		{
			selectedRoute = findRoute("rest");
		}
		if (selectedRoute < 0)
		{
			selectedRoute = findRoute("battle");
		}
		if (selectedRoute < 0)
		{
			selectedRoute = findRoute("midboss");
		}
		if (selectedRoute < 0)
		{
			selectedRoute = 0;
		}
		stageSelect->ChooseRoute(selectedRoute, "autoplay");
		return true;
	}

	if (dynamic_cast<RestSiteScene*>(game.m_SceneManager.Get()) != nullptr)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (IsHealNeeded(game) && game.RestHeal())
		{
			std::cout
				<< "[BalanceAutoPlay] RestSite: recovered "
				<< game.GetRestHealPercent()
				<< "% of max HP"
				<< std::endl;
		}
		else
		{
			const int ballIndex =
				FindUpgradeTarget(game);
			const PlayerBallData* ball =
				game.m_RunController.Deck().GetRewardTarget(ballIndex);
			if (ball != nullptr)
			{
				const std::uint64_t instanceId = ball->instanceId;
				const std::string definitionId = ball->definitionId;
				if (game.RestUpgradeBall(ballIndex))
				{
					std::cout
						<< "[BalanceAutoPlay] RestSite: upgraded "
						<< definitionId
						<< " (instance " << instanceId << ")"
						<< std::endl;
				}
			}
		}

		game.LeaveRestSite();
		return true;
	}

	if (dynamic_cast<ShopScene*>(game.m_SceneManager.Get()) != nullptr)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		const int relicIndex = FindRelicToBuy(game);
		if (relicIndex >= 0 && game.BuyShopRelic(relicIndex))
		{
			const RelicDefinition* relic = game.GetRelic(relicIndex);
			std::cout
				<< "[BalanceAutoPlay] Shop: purchased relic "
				<< (relic != nullptr ? relic->name : "unknown")
				<< std::endl;
		}
		else
		{
			const int ballIndex = FindWeakestBall(game);
			const PlayerBallData* ball =
				game.m_RunController.Deck().GetRewardTarget(ballIndex);
			if (ball != nullptr &&
				game.m_RunController.Status().money >= kAutoShopRemoveCost &&
				game.m_RunController.Deck().GetRewardTargetCount() >
					PlayerDeck::MinimumDeckSize)
			{
				const std::uint64_t instanceId = ball->instanceId;
				const std::string definitionId = ball->definitionId;
				if (game.RemoveShopBall(ballIndex, kAutoShopRemoveCost))
				{
					std::cout
						<< "[BalanceAutoPlay] Shop: removed "
						<< definitionId
						<< " (instance " << instanceId << ")"
						<< std::endl;
				}
			}
			else
			{
				const int catalogIndex =
					FindMissingCatalogBall(game);
				if (catalogIndex >= 0 &&
					game.BuyShopBall(catalogIndex, kAutoShopBallCost))
				{
					std::cout
						<< "[BalanceAutoPlay] Shop: purchased ball"
						<< std::endl;
				}
			}
		}

		game.LeaveShop();
		return true;
	}

	if (game.m_IsClearRewardActive)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (game.m_IsMidBossRelicSelectionActive)
		{
			game.AcquireMidBossRelicOffer(0);
		}
		else if (!game.m_IsClearRewardChosen)
		{
			ApplyReward(game);
		}
		else
		{
			game.ContinueAfterClearReward();
		}
		return true;
	}

	if (dynamic_cast<BattleScene*>(game.m_SceneManager.Get()) != nullptr &&
		game.GetBattleState() == BattleState::AimingDirection)
	{
		if (game.AreAllEnemiesDefeated())
		{
			m_DecisionFrame = 0;
			return false;
		}
		std::vector<PlayerBall*> players =
			game.GetComponents<PlayerBall>();
		if (players.empty() ||
			players[0] == nullptr ||
			!players[0]->IsIdle() ||
			!game.AreAllBallsStopped())
		{
			m_DecisionFrame = 0;
			return false;
		}

		if (!isDecisionReady())
		{
			return false;
		}

		SelectBall(game);
		return FireShot(game);
	}

	return false;
}

// 指定したComponentが現在のGameWorldに属しているかを返す。
bool Game::ContainsComponent(const Component* component) const
{
	return m_World.Contains(component);
}

// Balance Auto Ballを選択
void BalanceAutoPlayer::SelectBall(Game& game)
{
    const auto bossChoices = game.EvaluateBossShots();
    if (bossChoices.contains("recommended") && !bossChoices["recommended"].is_null())
    {
		game.ApplyBalanceAutoBallSelection(
			bossChoices["recommended"]["offer_index"].get<int>());
        return;
    }
	const int offerCount = game.m_RunController.Deck().GetOfferCount();
	if (offerCount <= 0)
	{
		return;
	}

	int bestIndex = 0;
	float bestScore =
		-(std::numeric_limits<float>::max)();
	const auto& shotCounts =
		game.m_RunStatistics.GetState().ballShotCounts;

	for (int index = 0; index < offerCount; index++)
	{
		const PlayerBallData* ball =
			game.m_RunController.Deck().GetOffer(index);
		if (ball == nullptr)
		{
			continue;
		}

		float score = GetAutoBallValue(*ball);
		score += (std::max)(0.0f, ball->status.mass - 2.0f);
		const auto usage = shotCounts.find(ball->definitionId);
		if (usage != shotCounts.end())
		{
			score -= static_cast<float>(usage->second) * 1.5f;
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestIndex = index;
		}
	}

	game.ApplyBalanceAutoBallSelection(bestIndex);
}

// Balance Auto Shotを発射
bool BalanceAutoPlayer::FireShot(Game& game)
{
    const auto bossChoices = game.EvaluateBossShots();
    if (bossChoices.contains("recommended") && !bossChoices["recommended"].is_null())
        return game.FireBossPlannedShot(bossChoices["recommended"]["candidate_id"].get<std::string>(),
            bossChoices["state_key"].get<std::string>());
	std::vector<PlayerBall*> players =
		game.GetComponents<PlayerBall>();
	std::vector<EnemyBall*> enemies =
		game.GetComponents<EnemyBall>();
	std::vector<Pocket*> pockets =
		game.GetComponents<Pocket>();

	if (players.empty() || players[0] == nullptr)
	{
		return false;
	}

	PlayerBall* player = players[0];
	const DirectX::SimpleMath::Vector3 playerPosition =
		player->GetPosition();

	EnemyBall* bestTarget = nullptr;
	DirectX::SimpleMath::Vector3 bestDirection =
		DirectX::SimpleMath::Vector3::UnitZ;
	float bestDistance = 0.0f;
	float bestPocketRisk = 0.0f;
	bool bestTargetIsKillable = false;
	float bestTargetScore =
		-(std::numeric_limits<float>::max)();
	const float playerRadius = player->GetBall() == nullptr
		? 0.0f
		: player->GetBall()->GetRadius();

	for (EnemyBall* target : enemies)
	{
		if (target == nullptr || target->IsDefeated() ||
			target->IsPocketed())
		{
			continue;
		}

		DirectX::SimpleMath::Vector3 direction =
			target->GetPosition() - playerPosition;
		direction.y = 0.0f;
		const float distance = direction.Length();
		if (distance <= 0.0001f)
		{
			continue;
		}
		direction /= distance;

		float chainScore = 0.0f;
		for (EnemyBall* other : enemies)
		{
			if (other == nullptr ||
				other == target ||
				other->IsDefeated() ||
				other->IsPocketed())
			{
				continue;
			}

			DirectX::SimpleMath::Vector3 relative =
				other->GetPosition() - target->GetPosition();
			relative.y = 0.0f;
			const float forward =
				relative.Dot(direction);
			if (forward <= 0.0f)
			{
				continue;
			}

			const float lateralSquared =
				(std::max)(
					0.0f,
					relative.LengthSquared() -
					forward * forward);
			const float targetRadius =
				target->GetBall() == nullptr
				? 0.0f
				: target->GetBall()->GetRadius();
			const float otherRadius =
				other->GetBall() == nullptr
				? 0.0f
				: other->GetBall()->GetRadius();
			const float chainWidth =
				targetRadius + otherRadius + 1.5f;

			if (lateralSquared <= chainWidth * chainWidth)
			{
				chainScore +=
					20.0f / (1.0f + forward * 0.05f);
			}
		}

		float pocketRisk = 0.0f;
		for (Pocket* pocket : pockets)
		{
			if (pocket == nullptr)
			{
				continue;
			}

			const Collision::Sphere pocketSphere = pocket->GetSphere();
			DirectX::SimpleMath::Vector3 relative =
				pocketSphere.center - playerPosition;
			relative.y = 0.0f;
			const float forward = relative.Dot(direction);
			if (forward <= 0.0f)
			{
				continue;
			}

			const float lateralSquared = (std::max)(
				0.0f,
				relative.LengthSquared() - forward * forward);
			const float corridorRadius =
				pocketSphere.radius + playerRadius * 0.65f;
			if (lateralSquared > corridorRadius * corridorRadius)
			{
				continue;
			}

			if (forward <= distance + playerRadius * 2.0f)
			{
				pocketRisk += 100.0f;
			}
			else if (forward <= distance + 30.0f)
			{
				pocketRisk += 160.0f;
			}
		}

		const bool isKillable =
			target->GetHP() <= player->GetAttack();
		const int missingHp = (std::max)(
			0,
			target->GetMaxHP() - target->GetHP());
		const float targetScore =
			chainScore -
			distance * 0.10f -
			pocketRisk +
			static_cast<float>(target->GetAttack()) * 8.0f +
			static_cast<float>(missingHp) * 10.0f +
			(isKillable ? 180.0f : 0.0f);
		if (targetScore > bestTargetScore)
		{
			bestTargetScore = targetScore;
			bestTarget = target;
			bestDirection = direction;
			bestDistance = distance;
			bestPocketRisk = pocketRisk;
			bestTargetIsKillable = isKillable;
		}
	}

	if (bestTarget == nullptr)
	{
		if (game.AreAllEnemiesDefeated())
		{
			m_DecisionFrame = 0;
			return false;
		}
		game.RecordBalanceEvent(
			"autoplay_waiting",
			{
				{ "reason", "no_live_target" },
				{ "battle_state", ToString(game.GetBattleState()) },
			});
		return false;
	}

	constexpr float DegreesToRadians =
		3.14159265358979323846f / 180.0f;
	const bool needsSafeShot =
		bestPocketRisk > 0.0f ||
		game.m_RunController.Status().currentHp * 2 <=
			game.m_RunController.Status().maxHp;
	const float appliedJitterDegrees = needsSafeShot
		? m_AimJitterDegrees * 0.5f
		: m_AimJitterDegrees;
	std::uniform_real_distribution<float> jitterDistribution(
		-appliedJitterDegrees,
		appliedJitterDegrees);
	const float jitter =
		jitterDistribution(m_RandomEngine) *
		DegreesToRadians;
	const float cosine = std::cos(jitter);
	const float sine = std::sin(jitter);
	const DirectX::SimpleMath::Vector3 shotDirection(
		bestDirection.x * cosine +
			bestDirection.z * sine,
		0.0f,
		-bestDirection.x * sine +
			bestDirection.z * cosine);

	const float safeMaximumPower = (std::max)(
		m_MinimumShotPower,
		needsSafeShot
		? (std::min)(m_MaximumShotPower, 5.5f)
		: m_MaximumShotPower);
	const float shotPower = std::clamp(
		3.0f + bestDistance * 0.03f,
		m_MinimumShotPower,
		safeMaximumPower);

	std::cout
		<< "[BalanceAutoPlay] Target="
		<< bestTarget->GetEnemyId()
		<< " Power=" << shotPower
		<< " Killable=" << (bestTargetIsKillable ? "yes" : "no")
		<< " PocketRisk=" << bestPocketRisk
		<< std::endl;

	player->FireAutomatedShot(
		shotDirection * shotPower);
	return true;
}

// Balance Auto Rewardを適用
void BalanceAutoPlayer::ApplyReward(Game& game)
{
	bool hasMissingRelic = false;
	int nextRelicPrice = (std::numeric_limits<int>::max)();
	for (int index = 0; index < game.GetRelicCount(); index++)
	{
		const RelicDefinition* relic = game.GetRelic(index);
		if (relic != nullptr && !game.HasRelic(relic->type))
		{
			hasMissingRelic = true;
			nextRelicPrice = (std::min)(nextRelicPrice, relic->price);
		}
	}

	// 戦闘報酬の資金でレリックを買えるなら、球の過剰増加より先に資金を確保
	if (hasMissingRelic && game.m_RunController.Status().money < nextRelicPrice)
	{
		const int moneyBefore = game.m_RunController.Status().money;
		game.m_RunController.AddMoney(kExtraRewardMoney);
		game.MarkBalanceAutoRewardChosen(
			2,
			0,
			"Auto Play: saved Money for a relic.");
		game.RecordBalanceEvent(
			"clear_reward_choice",
			{
				{ "controller", "autoplay" },
				{ "reward", "extra_money" },
				{ "reason", "save_for_relic" },
				{ "money_before", moneyBefore },
				{ "money_after", game.m_RunController.Status().money },
			});
		return;
	}

	const int missingOfferIndex = FindMissingClearRewardBallOffer(game);
	if (missingOfferIndex >= 0)
	{
		const int catalogIndex =
			game.GetClearRewardBallOfferCatalogIndex(missingOfferIndex);
		const PlayerBallData* offeredBall =
			game.GetClearRewardBallOffer(missingOfferIndex);
		const std::string definitionId = offeredBall != nullptr
			? offeredBall->definitionId
			: std::string();
		if (game.AddClearRewardBallOffer(missingOfferIndex))
		{
			game.PublishGameEvent(BallAcquiredEvent{ definitionId });
			game.MarkBalanceAutoRewardChosen(
				0,
				missingOfferIndex,
				"Auto Play: added an offered missing ball type.");
			game.RecordBalanceEvent(
				"clear_reward_choice",
				{
					{ "controller", "autoplay" },
					{ "reward", "new_ball" },
					{ "reason", "fill_missing_ball_type" },
					{ "offer_index", missingOfferIndex },
					{ "catalog_index", catalogIndex },
					{ "ball_id", definitionId },
				});
			return;
		}
	}

	const int upgradeTarget = FindUpgradeTarget(game);
	const PlayerBallData* ball =
		game.m_RunController.Deck().GetRewardTarget(upgradeTarget);
	if (ball != nullptr)
	{
		const std::uint64_t instanceId = ball->instanceId;
		const std::string definitionId = ball->definitionId;
		const int upgradeLevelBefore = ball->upgradeLevel;
		const int upgradeCost =
			game.GetClearRewardUpgradeCost(upgradeTarget);
		const int moneyBefore = game.m_RunController.Status().money;
		int chargedCost = 0;
		if (upgradeCost >= 0 &&
			game.m_RunController.Status().money >= upgradeCost &&
			game.ApplyClearRewardUpgrade(upgradeTarget, chargedCost))
		{
			game.MarkBalanceAutoRewardChosen(
				1,
				upgradeTarget,
				"Auto Play: paid Money and upgraded a primary ball.");
			game.RecordBalanceEvent(
				"clear_reward_choice",
				{
					{ "controller", "autoplay" },
					{ "reward", "upgrade_ball" },
					{ "reason", "concentrate_primary_ball" },
					{ "instance_id", instanceId },
					{ "ball_id", definitionId },
					{ "upgrade_level_before", upgradeLevelBefore },
					{ "upgrade_cost", chargedCost },
					{ "money_before", moneyBefore },
					{ "money_after", game.m_RunController.Status().money },
				});
			return;
		}

		if (upgradeCost >= 0 &&
			game.m_RunController.Status().money < upgradeCost)
		{
			game.m_RunController.AddMoney(kExtraRewardMoney);
			game.MarkBalanceAutoRewardChosen(
				2,
				0,
				"Auto Play: saved Money for a paid upgrade.");
			game.RecordBalanceEvent(
				"clear_reward_choice",
				{
					{ "controller", "autoplay" },
					{ "reward", "extra_money" },
					{ "reason", "save_for_upgrade" },
					{ "target_instance_id", instanceId },
					{ "target_upgrade_level", upgradeLevelBefore },
					{ "required_upgrade_cost", upgradeCost },
					{ "money_before", moneyBefore },
					{ "money_after", game.m_RunController.Status().money },
				});
			return;
		}
	}

	const int moneyBefore = game.m_RunController.Status().money;
	game.m_RunController.AddMoney(kExtraRewardMoney);
	game.MarkBalanceAutoRewardChosen(
		2,
		0,
		"Auto Play: received extra Money.");
	game.RecordBalanceEvent(
		"clear_reward_choice",
		{
			{ "controller", "autoplay" },
			{ "reward", "extra_money" },
			{ "money_before", moneyBefore },
			{ "money_after", game.m_RunController.Status().money },
		});
}

// Balance Auto Heal Neededかどうかを判定
bool BalanceAutoPlayer::IsHealNeeded(const Game& game) const
{
	if (game.m_RunController.Status().maxHp <= 0 ||
		game.m_RunController.Status().currentHp >= game.m_RunController.Status().maxHp)
	{
		return false;
	}

	const int thresholdPercent = game.IsBossPreparation() ? 75 : 60;
	return game.m_RunController.Status().currentHp * 100 <=
		game.m_RunController.Status().maxHp * thresholdPercent;
}

// Balance Auto Relic To Buyを検索
int BalanceAutoPlayer::FindRelicToBuy(const Game& game) const
{
	const auto isCandidate = [this, &game](int index)
	{
		return dynamic_cast<ShopScene*>(game.m_SceneManager.Get()) == nullptr ||
			game.m_RunController.ShopRelicOffers().empty() || game.IsShopRelicOffered(index);
	};
	const bool needsRecovery =
		game.m_RunController.Status().currentHp * 2 <=
		game.m_RunController.Status().maxHp;
	const std::array<RelicType, 4> priority = needsRecovery
		? std::array<RelicType, 4>{
			RelicType::EmergencyRepairKit,
			RelicType::AllBallAttackUp,
			RelicType::CollisionAttackUp,
			RelicType::BankShot }
		: std::array<RelicType, 4>{
			RelicType::AllBallAttackUp,
			RelicType::CollisionAttackUp,
			RelicType::EmergencyRepairKit,
			RelicType::BankShot };

	for (RelicType type : priority)
	{
		for (int index = 0; index < game.GetRelicCount(); index++)
		{
			const RelicDefinition* relic = game.GetRelic(index);
			if (relic != nullptr &&
				relic->type == type &&
				isCandidate(index) &&
				!game.HasRelic(type) &&
				game.m_RunController.Status().money >= relic->price)
			{
				return index;
			}
		}
	}
	for (int index = 0; index < game.GetRelicCount(); index++)
	{
		const RelicDefinition* relic = game.GetRelic(index);
		if (relic != nullptr && isCandidate(index) &&
			!game.HasRelic(relic->type) &&
			game.m_RunController.Status().money >= relic->price)
		{
			return index;
		}
	}

	return -1;
}

// Balance Auto Weakest Ballを検索
int BalanceAutoPlayer::FindWeakestBall(const Game& game) const
{
	const int ballCount = game.m_RunController.Deck().GetRewardTargetCount();
	if (ballCount <= PlayerDeck::MinimumDeckSize ||
		game.m_RunController.Status().money < kAutoShopRemoveCost)
	{
		return -1;
	}

	int weakestIndex = -1;
	float weakestScore = (std::numeric_limits<float>::max)();
	for (int index = 0; index < ballCount; index++)
	{
		const PlayerBallData* ball = game.m_RunController.Deck().GetRewardTarget(index);
		if (ball == nullptr)
		{
			continue;
		}

		int sameTypeCount = 0;
		for (int otherIndex = 0; otherIndex < ballCount; otherIndex++)
		{
			const PlayerBallData* other =
				game.m_RunController.Deck().GetRewardTarget(otherIndex);
			if (other != nullptr &&
				other->definitionId == ball->definitionId)
			{
				sameTypeCount++;
			}
		}

		// タイプの種類を減らさないよう、重複球から削除
		const float score = GetAutoBallValue(*ball) +
			(sameTypeCount <= 1 ? 1000.0f : 0.0f);
		if (score < weakestScore)
		{
			weakestScore = score;
			weakestIndex = index;
		}
	}

	return weakestIndex;
}

// Balance Auto Missing Catalog Ballを検索
int BalanceAutoPlayer::FindMissingCatalogBall(const Game& game) const
{
	if (game.m_RunController.Deck().GetRewardTargetCount() >= kAutoMaximumDeckSize)
	{
		return -1;
	}

	const std::array<const char*, 10> priority =
	{
		"player_bounce",
		"player_cushion_charge",
		"player_ricochet_finisher",
		"player_anchor",
		"player_anchor_finisher",
		"player_pierce",
		"player_trace_driver",
		"player_pierce_finisher",
		"player_heavy",
		"player_standard",
	};
	for (const char* definitionId : priority)
	{
		bool alreadyOwned = false;
		for (int index = 0;
			index < game.m_RunController.Deck().GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				game.m_RunController.Deck().GetRewardTarget(index);
			if (ball != nullptr && ball->definitionId == definitionId)
			{
				alreadyOwned = true;
				break;
			}
		}
		if (alreadyOwned)
		{
			continue;
		}

		for (int catalogIndex = 0;
			catalogIndex < game.m_RunController.Deck().GetCatalogCount();
			catalogIndex++)
		{
			const PlayerBallData* catalogBall =
				game.m_RunController.Deck().GetCatalogBall(catalogIndex);
			if (catalogBall != nullptr &&
				catalogBall->definitionId == definitionId)
			{
				return catalogIndex;
			}
		}
	}

	return -1;
}

// Balance Auto Clear Reward内の未所持ボール提示を検索
int BalanceAutoPlayer::FindMissingClearRewardBallOffer(const Game& game) const
{
	if (game.m_RunController.Deck().GetRewardTargetCount() >=
		kAutoMaximumDeckSize)
	{
		return -1;
	}

	const std::array<const char*, 10> priority =
	{
		"player_bounce",
		"player_cushion_charge",
		"player_ricochet_finisher",
		"player_anchor",
		"player_anchor_finisher",
		"player_pierce",
		"player_trace_driver",
		"player_pierce_finisher",
		"player_heavy",
		"player_standard",
	};
	for (const char* definitionId : priority)
	{
		bool alreadyOwned = false;
		for (int index = 0;
			index < game.m_RunController.Deck().GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				game.m_RunController.Deck().GetRewardTarget(index);
			if (ball != nullptr && ball->definitionId == definitionId)
			{
				alreadyOwned = true;
				break;
			}
		}
		if (alreadyOwned)
		{
			continue;
		}

		for (int offerIndex = 0;
			offerIndex < game.GetClearRewardBallOfferCount();
			offerIndex++)
		{
			const PlayerBallData* offeredBall =
				game.GetClearRewardBallOffer(offerIndex);
			if (offeredBall != nullptr &&
				offeredBall->definitionId == definitionId)
			{
				return offerIndex;
			}
		}
	}

	return -1;
}

// Balance Auto Upgrade Targetを検索
int BalanceAutoPlayer::FindUpgradeTarget(const Game& game) const
{
	int bestIndex = -1;
	float bestScore = -(std::numeric_limits<float>::max)();
	for (int index = 0;
		index < game.m_RunController.Deck().GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball =
			game.m_RunController.Deck().GetRewardTarget(index);
		if (ball == nullptr || !ball->CanUpgrade())
		{
			continue;
		}

		const float score = GetAutoBallValue(*ball) +
			static_cast<float>(ball->upgradeLevel) * 25.0f;
		if (score > bestScore)
		{
			bestScore = score;
			bestIndex = index;
		}
	}

	return bestIndex;
}

// Balance Auto Shop Actionを保持しているか判定
bool BalanceAutoPlayer::HasShopAction(const Game& game) const
{
	return FindRelicToBuy(game) >= 0 ||
		FindWeakestBall(game) >= 0 ||
		(game.m_RunController.Status().money >= kAutoShopBallCost &&
			FindMissingCatalogBall(game) >= 0);
}

// Balance Auto Pending Upgradeable Ballを検索
int BalanceAutoPlayer::FindPendingUpgradeableBall(const Game& game) const
{
	for (const std::uint64_t instanceId :
		m_PendingBallAdjustments)
	{
		for (int index = 0;
			index < game.m_RunController.Deck().GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				game.m_RunController.Deck().GetRewardTarget(index);
			if (ball != nullptr &&
				ball->instanceId == instanceId &&
				ball->CanUpgrade())
			{
				return index;
			}
		}
	}

	return -1;
}

// Balance Auto Pending Removal Ballを検索
int BalanceAutoPlayer::FindPendingRemovalBall(const Game& game) const
{
	for (const std::uint64_t instanceId :
		m_PendingBallAdjustments)
	{
		for (int index = 0;
			index < game.m_RunController.Deck().GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				game.m_RunController.Deck().GetRewardTarget(index);
			if (ball != nullptr &&
				ball->instanceId == instanceId &&
				!ball->CanUpgrade())
			{
				return index;
			}
		}
	}

	return -1;
}

// Ball Adjustment Candidateかどうかを判定
bool BalanceAutoPlayer::IsBallAdjustmentCandidate(
	std::uint64_t instanceId) const
{
	return instanceId != 0 &&
		std::find(
			m_PendingBallAdjustments.begin(),
			m_PendingBallAdjustments.end(),
			instanceId) !=
		m_PendingBallAdjustments.end();
}

void BalanceAutoPlayer::NotifyFullHpEnemySurvived(
	const PlayerBallData* currentBall)
{
	if (currentBall == nullptr ||
		currentBall->instanceId == 0 ||
		IsBallAdjustmentCandidate(currentBall->instanceId))
	{
		return;
	}

	m_PendingBallAdjustments.push_back(currentBall->instanceId);
	std::cout
		<< "[BalanceAutoPlay] Ball adjustment pending: "
		<< currentBall->definitionId
		<< " (instance " << currentBall->instanceId << ")"
		<< std::endl;
}

// Available Rest Benefitを保持しているか判定
bool BalanceAutoPlayer::HasAvailableRestBenefit(const Game& game) const
{
	if (game.CanRestHeal())
	{
		return true;
	}

	const int ballCount =
		game.m_RunController.Deck().GetRewardTargetCount();
	for (int index = 0; index < ballCount; index++)
	{
		const PlayerBallData* ball =
			game.m_RunController.Deck().GetRewardTarget(index);
		if (ball != nullptr && ball->CanUpgrade())
		{
			return true;
		}
	}

	return false;
}

// Balance Auto Pending Ballを取り除く。
void BalanceAutoPlayer::RemovePendingBall(
	std::uint64_t instanceId)
{
	m_PendingBallAdjustments.erase(
		std::remove(
			m_PendingBallAdjustments.begin(),
			m_PendingBallAdjustments.end(),
			instanceId),
		m_PendingBallAdjustments.end());
}

// Prune Balance Auto Pending Balls の処理を実行
void BalanceAutoPlayer::PrunePendingBalls(const Game& game)
{
	m_PendingBallAdjustments.erase(
		std::remove_if(
			m_PendingBallAdjustments.begin(),
			m_PendingBallAdjustments.end(),
			[this, &game](std::uint64_t instanceId)
			{
				for (int index = 0;
					index < game.m_RunController.Deck().GetRewardTargetCount();
					index++)
				{
					const PlayerBallData* ball =
						game.m_RunController.Deck().GetRewardTarget(index);
					if (ball != nullptr &&
						ball->instanceId == instanceId)
					{
						return false;
					}
				}
				return true;
			}),
		m_PendingBallAdjustments.end());
}

// On Battle Stage Started の処理を実行
void Game::OnBattleStageStarted(const StageData& stage)
{
	m_BattleController.BeginStage(stage.stageType);
	m_CushionCharges = {};
	m_CushionBoostConsumedThisShot = false;
	m_CushionStrongUsesThisShot = 0;
	m_SynergyDamageBonusThisShot = 0;
	HeavyCollisionRules::Reset(m_HeavyCollisions);
	m_PierceTraces = {};
	m_PierceTraceUse = {};
	m_AnchorStacks = {};
	m_PiercedEnemiesThisShot.clear();
	m_TraceSegmentValid = false;
	m_AnchorContactTarget = nullptr;
	m_PlayerShield = 0;
	InvalidateDebugCombatForecast("戦闘開始");
	m_RunStatistics.ReachFloor(m_RunController.Status().progress);
	if (m_GamePresentation != nullptr &&
		!m_BalanceAutoPlayer.IsEnabled())
	{
		m_GamePresentation->OnBattleStarted(*this);
	}
	m_RunStatistics.BeginBattle(
		stage.stageType == StageType::MidBoss,
		stage.stageType == StageType::Boss &&
			m_RunController.Progress().GetPhase() == RunPhase::FinalBoss,
		stage.id);
	std::vector<BalanceEnemySnapshot> enemies;
	enemies.reserve(stage.enemies.size());

	for (const EnemySpawnData& spawn : stage.enemies)
	{
		const BallStatus& status = spawn.enemyData.status;
		BalanceEnemySnapshot snapshot;
		snapshot.id = spawn.enemyData.id;
		snapshot.maxHp = spawn.enemyData.maxHp;
		snapshot.attack = status.attack;
		snapshot.defense = status.defense;
		snapshot.mass = status.mass;
		snapshot.radius = status.radius;
		snapshot.restitution = status.restitution;
		snapshot.friction = status.friction;
		snapshot.positionX = spawn.position.x;
		snapshot.positionY = spawn.position.y;
		snapshot.positionZ = spawn.position.z;
		enemies.push_back(std::move(snapshot));
	}

	nlohmann::json deckJson = nlohmann::json::array();
	for (int index = 0;
		index < m_RunController.Deck().GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball =
			m_RunController.Deck().GetRewardTarget(index);
		if (ball == nullptr)
		{
			continue;
		}
		deckJson.push_back(
			{
				{ "instance_id", ball->instanceId },
				{ "id", ball->definitionId },
				{ "upgrade_level", ball->upgradeLevel },
				{ "attack", GetEffectivePlayerBallAttack(ball) },
			});
	}

	nlohmann::json relicsJson = nlohmann::json::array();
	for (const RelicDefinition& relic : RelicCatalog)
	{
		if (HasRelic(relic.type))
		{
			relicsJson.push_back(relic.name);
		}
	}

	constexpr int appliedHpModifier = 0;
	constexpr int appliedAttackModifier = 0;
	const int progressionAttackModifier =
		m_DynamicBalanceController.CalculateProgressionAttackModifier(
			m_RunController.Status().progress);
	const int progressionHpModifier =
		m_DynamicBalanceController.CalculateProgressionHpModifier(
			m_RunController.Status().progress);
	const std::string layoutSource =
		m_McpCurrentStageOverride.has_value()
		? "mcp_override"
		: (stage.enemies.size() >= 4
			? "dense_auto_layout"
			: "stage_data");
	float baseThreatBudget = 0.0f;
	for (const EnemySpawnData& spawn : stage.enemies)
	{
		const auto cost = m_EnemyThreatCosts.find(spawn.enemyId);
		baseThreatBudget += cost != m_EnemyThreatCosts.end()
			? cost->second
			: 10.0f;
	}
	const float layoutThreatMultiplier =
		layoutSource == "dense_auto_layout"
		? m_DenseLayoutThreatMultiplier
		: (layoutSource == "mcp_override"
			? m_McpLayoutThreatMultiplier
			: m_StageDataLayoutThreatMultiplier);
	const float actualThreatBudget =
		std::round(baseThreatBudget * layoutThreatMultiplier * 100.0f) /
		100.0f;
	const auto targetThreat = m_StageThreatTargets.find(stage.id);
	const nlohmann::json stageContext =
	{
		{ "progress", m_RunController.Status().progress },
		{ "area_progress", m_RunController.Progress().GetAreaProgress() },
		{ "area_goal", kNormalRouteAreaGoal },
		{ "run_phase", ToString(m_RunController.Progress().GetPhase()) },
		{ "par", stage.par },
		{ "money", m_RunController.Status().money },
		{ "layout_source", layoutSource },
		{ "deck", std::move(deckJson) },
		{ "owned_relics", std::move(relicsJson) },
		{
			"baseline_difficulty",
			{
				{ "profile", m_BaselineDifficultyProfile },
				{ "enemy_hp_multiplier", m_BaselineEnemyHpMultiplier },
				{ "enemy_attack_delta", m_BaselineEnemyAttackDelta },
			}
		},
		{
			"encounter_threat",
			{
				{ "base_budget", baseThreatBudget },
				{ "layout_multiplier", layoutThreatMultiplier },
				{ "actual_budget", actualThreatBudget },
				{
					"target_budget",
					targetThreat != m_StageThreatTargets.end()
						? nlohmann::json(targetThreat->second)
						: nlohmann::json(nullptr)
				},
				{
					"deviation_from_target",
					targetThreat != m_StageThreatTargets.end()
						? nlohmann::json(
							actualThreatBudget - targetThreat->second)
						: nlohmann::json(nullptr)
				},
			}
		},
		{
			"dynamic_balance",
			{
				{ "enabled", false },
				{ "applied_level", 0 },
				{ "enemy_hp_modifier", appliedHpModifier },
				{ "enemy_attack_modifier", appliedAttackModifier },
			}
		},
		{
			"progression_scaling",
			{
				{ "enabled", m_DynamicBalanceController.IsProgressionScalingEnabled() },
				{ "progress", m_RunController.Status().progress },
				{ "enemy_hp_modifier", progressionHpModifier },
				{ "hp_start_progress", m_DynamicBalanceController.GetProgressionHpStart() },
				{ "hp_interval", m_DynamicBalanceController.GetProgressionHpInterval() },
				{ "hp_step", m_DynamicBalanceController.GetProgressionHpStep() },
				{ "maximum_hp_delta",
					m_DynamicBalanceController.GetProgressionHpMaximumDelta() },
				{ "enemy_attack_modifier", progressionAttackModifier },
				{ "attack_start_progress", m_DynamicBalanceController.GetProgressionAttackStart() },
				{ "attack_interval", m_DynamicBalanceController.GetProgressionAttackInterval() },
				{ "maximum_attack_delta",
					m_DynamicBalanceController.GetProgressionAttackMaximumDelta() },
			}
		},
		{
			"assist_mode",
			{
				{ "enabled", false },
				{ "applied_level", 0 },
			}
		},
	};

	BalanceLogger::GetInstance().BeginStage(
		stage.id,
		ToString(stage.stageType),
		stage.difficulty,
		m_RunController.Status().currentHp,
		m_RunController.Status().maxHp,
		enemies,
		stageContext);
}
