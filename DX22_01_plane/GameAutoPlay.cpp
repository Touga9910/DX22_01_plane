#include "Game.h"

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
			static_cast<float>(ball.status.defense) * 2.0f +
			static_cast<float>(ball.upgradeLevel) * 5.0f;
		if (ball.status.abilities.pierce)
		{
			value += 6.0f;
		}
		if (ball.status.abilities.anchor)
		{
			value += 5.0f;
		}
		if (ball.definitionId == "player_bounce")
		{
			value += 4.0f;
		}
		return value;
	}
}

// Balance Auto Playを更新する。
bool Game::UpdateBalanceAutoPlay()
{
	if (m_DebugMode || m_DebugEditorOpen) return false;
	if (Input::GetKeyTrigger(VK_F8))
	{
		const bool startAutoPlay = !m_BalanceAutoPlayEnabled;
		m_BalanceAutoPlayEnabled = startAutoPlay;
		m_AutoStopAfterCurrentRunRequested =
			startAutoPlay && m_AutoStopAfterCurrentRunDefault;
		m_AutoDecisionFrame = 0;

		std::cout
			<< "[BalanceAutoPlay] "
			<< (m_BalanceAutoPlayEnabled ? "ON" : "OFF")
			<< std::endl;
	}
	if (m_BalanceAutoPlayEnabled && Input::GetKeyTrigger(VK_F9))
	{
		m_AutoStopAfterCurrentRunRequested =
			!m_AutoStopAfterCurrentRunRequested;
		std::cout
			<< "[BalanceAutoPlay] Stop at run end: "
			<< (m_AutoStopAfterCurrentRunRequested ? "ON" : "OFF")
			<< std::endl;
	}

	if (!m_BalanceAutoPlayEnabled || m_SceneManager.Get() == nullptr)
	{
		return false;
	}

	auto isDecisionReady = [this]()
	{
		m_AutoDecisionFrame++;
		if (m_AutoDecisionFrame < m_AutoDecisionDelayFrames)
		{
			return false;
		}

		m_AutoDecisionFrame = 0;
		return true;
	};

	if (dynamic_cast<TitleScene*>(m_SceneManager.Get()) != nullptr)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (m_AutoMaxRuns > 0 &&
			m_AutoRunCount >= m_AutoMaxRuns)
		{
			m_BalanceAutoPlayEnabled = false;
			std::cout
				<< "[BalanceAutoPlay] Max runs reached"
				<< std::endl;
			return false;
		}

		StartNewRun();
		m_AutoRunCount++;
		ChangeScene(SceneType::Select);
		return true;
	}

	if (dynamic_cast<ResultScene*>(m_SceneManager.Get()) != nullptr)
	{
		if (m_AutoStopAfterCurrentRunRequested)
		{
			m_BalanceAutoPlayEnabled = false;
			m_AutoStopAfterCurrentRunRequested = false;
			std::cout
				<< "[BalanceAutoPlay] Stopped at terminal result"
				<< std::endl;
			return false;
		}
		if (!m_AutoRestartAfterGameOver)
		{
			m_BalanceAutoPlayEnabled = false;
			return false;
		}

		if (!isDecisionReady())
		{
			return false;
		}

		ChangeScene(SceneType::Title);
		return true;
	}

	if (StageSelectScene* stageSelect =
		dynamic_cast<StageSelectScene*>(m_SceneManager.Get()))
	{
		if (!isDecisionReady())
		{
			return false;
		}

		PruneBalanceAutoPendingBalls();
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

		if (IsFinalBossRoute())
		{
			selectedRoute = 0;
		}
		else if (IsBalanceAutoHealNeeded())
		{
			selectedRoute = findRoute("rest");
		}
		if (selectedRoute < 0 && HasBalanceAutoShopAction())
		{
			selectedRoute = findRoute("shop");
		}
		if (selectedRoute < 0 &&
			FindBalanceAutoUpgradeTarget() >= 0)
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

	if (dynamic_cast<RestSiteScene*>(m_SceneManager.Get()) != nullptr)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (IsBalanceAutoHealNeeded() && RestHeal())
		{
			std::cout
				<< "[BalanceAutoPlay] RestSite: recovered "
				<< GetRestHealPercent()
				<< "% of max HP"
				<< std::endl;
		}
		else
		{
			const int ballIndex =
				FindBalanceAutoUpgradeTarget();
			const PlayerBallData* ball =
				m_PlayerDeck.GetRewardTarget(ballIndex);
			if (ball != nullptr)
			{
				const std::uint64_t instanceId = ball->instanceId;
				const std::string definitionId = ball->definitionId;
				if (RestUpgradeBall(ballIndex))
				{
					std::cout
						<< "[BalanceAutoPlay] RestSite: upgraded "
						<< definitionId
						<< " (instance " << instanceId << ")"
						<< std::endl;
				}
			}
		}

		LeaveRestSite();
		return true;
	}

	if (dynamic_cast<ShopScene*>(m_SceneManager.Get()) != nullptr)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		const int relicIndex = FindBalanceAutoRelicToBuy();
		if (relicIndex >= 0 && BuyShopRelic(relicIndex))
		{
			const RelicDefinition* relic = GetRelic(relicIndex);
			std::cout
				<< "[BalanceAutoPlay] Shop: purchased relic "
				<< (relic != nullptr ? relic->name : "unknown")
				<< std::endl;
		}
		else
		{
			const int ballIndex = FindBalanceAutoWeakestBall();
			const PlayerBallData* ball =
				m_PlayerDeck.GetRewardTarget(ballIndex);
			if (ball != nullptr &&
				m_PlayerRunStatus.money >= kAutoShopRemoveCost &&
				m_PlayerDeck.GetRewardTargetCount() >
					PlayerDeck::MinimumDeckSize)
			{
				const std::uint64_t instanceId = ball->instanceId;
				const std::string definitionId = ball->definitionId;
				if (RemoveShopBall(ballIndex, kAutoShopRemoveCost))
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
					FindBalanceAutoMissingCatalogBall();
				if (catalogIndex >= 0 &&
					BuyShopBall(catalogIndex, kAutoShopBallCost))
				{
					std::cout
						<< "[BalanceAutoPlay] Shop: purchased ball"
						<< std::endl;
				}
			}
		}

		LeaveShop();
		return true;
	}

	if (m_IsClearRewardActive)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (m_IsMidBossRelicSelectionActive)
		{
			AcquireMidBossRelicOffer(0);
		}
		else if (!m_IsClearRewardChosen)
		{
			ApplyBalanceAutoReward();
		}
		else
		{
			ContinueAfterClearReward();
		}
		return true;
	}

	if (dynamic_cast<BattleScene*>(m_SceneManager.Get()) != nullptr &&
		GetBattleState() == BattleState::AimingDirection)
	{
		if (AreAllEnemiesDefeated())
		{
			m_AutoDecisionFrame = 0;
			return false;
		}
		std::vector<PlayerBall*> players =
			GetComponents<PlayerBall>();
		if (players.empty() ||
			players[0] == nullptr ||
			!players[0]->IsIdle() ||
			!AreAllBallsStopped())
		{
			m_AutoDecisionFrame = 0;
			return false;
		}

		if (!isDecisionReady())
		{
			return false;
		}

		SelectBalanceAutoBall();
		return FireBalanceAutoShot();
	}

	return false;
}

// 指定したComponentが現在のGameWorldに属しているかを返す。
bool Game::ContainsComponent(const Component* component) const
{
	return m_World.Contains(component);
}

// Balance Auto Ballを選択する。
void Game::SelectBalanceAutoBall()
{
    const auto bossChoices = EvaluateBossShots();
    if (bossChoices.contains("recommended") && !bossChoices["recommended"].is_null())
    {
        m_SelectedOfferIndex = bossChoices["recommended"]["offer_index"].get<int>();
        if (m_SelectedHoldIndex == m_SelectedOfferIndex) m_SelectedHoldIndex = -1;
        ApplySelectedBallPreview();
        return;
    }
	const int offerCount = m_PlayerDeck.GetOfferCount();
	if (offerCount <= 0)
	{
		return;
	}

	int bestIndex = 0;
	float bestScore =
		-(std::numeric_limits<float>::max)();
	const bool needsDefense =
		m_PlayerRunStatus.currentHp * 2 <=
		m_PlayerRunStatus.maxHp;
	const auto& shotCounts =
		m_RunStatistics.GetState().ballShotCounts;

	for (int index = 0; index < offerCount; index++)
	{
		const PlayerBallData* ball =
			m_PlayerDeck.GetOffer(index);
		if (ball == nullptr)
		{
			continue;
		}

		float score = GetAutoBallValue(*ball) +
			static_cast<float>(ball->status.defense) *
				(needsDefense ? 2.0f : 0.0f);
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

	m_SelectedOfferIndex = bestIndex;
	if (m_SelectedHoldIndex == bestIndex)
	{
		m_SelectedHoldIndex = -1;
	}
	ApplySelectedBallPreview();
}

// Balance Auto Shotを発射する。
bool Game::FireBalanceAutoShot()
{
    const auto bossChoices = EvaluateBossShots();
    if (bossChoices.contains("recommended") && !bossChoices["recommended"].is_null())
        return FireBossPlannedShot(bossChoices["recommended"]["candidate_id"].get<std::string>(),
            bossChoices["state_key"].get<std::string>());
	std::vector<PlayerBall*> players =
		GetComponents<PlayerBall>();
	std::vector<EnemyBall*> enemies =
		GetComponents<EnemyBall>();
	std::vector<Pocket*> pockets =
		GetComponents<Pocket>();

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
		if (AreAllEnemiesDefeated())
		{
			m_AutoDecisionFrame = 0;
			return false;
		}
		RecordBalanceEvent(
			"autoplay_waiting",
			{
				{ "reason", "no_live_target" },
				{ "battle_state", ToString(GetBattleState()) },
			});
		return false;
	}

	constexpr float DegreesToRadians =
		3.14159265358979323846f / 180.0f;
	const bool needsSafeShot =
		bestPocketRisk > 0.0f ||
		m_PlayerRunStatus.currentHp * 2 <=
			m_PlayerRunStatus.maxHp;
	const float appliedJitterDegrees = needsSafeShot
		? m_AutoAimJitterDegrees * 0.5f
		: m_AutoAimJitterDegrees;
	std::uniform_real_distribution<float> jitterDistribution(
		-appliedJitterDegrees,
		appliedJitterDegrees);
	const float jitter =
		jitterDistribution(m_AutoRandomEngine) *
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
		m_AutoMinShotPower,
		needsSafeShot
		? (std::min)(m_AutoMaxShotPower, 5.5f)
		: m_AutoMaxShotPower);
	const float shotPower = std::clamp(
		3.0f + bestDistance * 0.03f,
		m_AutoMinShotPower,
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

// Balance Auto Rewardを適用する。
void Game::ApplyBalanceAutoReward()
{
	bool hasMissingRelic = false;
	int nextRelicPrice = (std::numeric_limits<int>::max)();
	for (int index = 0; index < GetRelicCount(); index++)
	{
		const RelicDefinition* relic = GetRelic(index);
		if (relic != nullptr && !HasRelic(relic->type))
		{
			hasMissingRelic = true;
			nextRelicPrice = (std::min)(nextRelicPrice, relic->price);
		}
	}

	// 戦闘報酬の資金でレリックを買えるなら、球の過剰増加より先に資金を確保する。
	if (hasMissingRelic && m_PlayerRunStatus.money < nextRelicPrice)
	{
		const int moneyBefore = m_PlayerRunStatus.money;
		m_SelectedRewardIndex = 2;
		m_PlayerRunStatus.money += kExtraRewardMoney;
		m_RewardMessage = "Auto Play: saved Money for a relic.";
		m_IsClearRewardChosen = true;
		RecordBalanceEvent(
			"clear_reward_choice",
			{
				{ "controller", "autoplay" },
				{ "reward", "extra_money" },
				{ "reason", "save_for_relic" },
				{ "money_before", moneyBefore },
				{ "money_after", m_PlayerRunStatus.money },
			});
		return;
	}

	const int missingCatalogIndex = FindBalanceAutoMissingCatalogBall();
	if (missingCatalogIndex >= 0 &&
		m_PlayerDeck.AddCatalogBall(missingCatalogIndex))
	{
		const PlayerBallData* catalogBall =
			m_PlayerDeck.GetCatalogBall(missingCatalogIndex);
		m_SelectedRewardIndex = 0;
		m_SelectedRewardBallIndex = missingCatalogIndex;
		m_RewardMessage = "Auto Play: added a missing ball type.";
		m_IsClearRewardChosen = true;
		RecordBalanceEvent(
			"clear_reward_choice",
			{
				{ "controller", "autoplay" },
				{ "reward", "new_ball" },
				{ "reason", "fill_missing_ball_type" },
				{ "catalog_index", missingCatalogIndex },
				{ "ball_id", catalogBall != nullptr
					? catalogBall->definitionId
					: std::string() },
			});
		return;
	}

	const int upgradeTarget = FindBalanceAutoUpgradeTarget();
	const PlayerBallData* ball =
		m_PlayerDeck.GetRewardTarget(upgradeTarget);
	if (ball != nullptr)
	{
		const std::uint64_t instanceId = ball->instanceId;
		const std::string definitionId = ball->definitionId;
		const int upgradeLevelBefore = ball->upgradeLevel;
		const int upgradeCost =
			GetClearRewardUpgradeCost(upgradeTarget);
		const int moneyBefore = m_PlayerRunStatus.money;
		int chargedCost = 0;
		if (upgradeCost >= 0 &&
			m_PlayerRunStatus.money >= upgradeCost &&
			ApplyClearRewardUpgrade(upgradeTarget, chargedCost))
		{
			m_SelectedRewardIndex = 1;
			m_SelectedRewardBallIndex = upgradeTarget;
			m_RewardMessage =
				"Auto Play: paid Money and upgraded a primary ball.";
			m_IsClearRewardChosen = true;
			RecordBalanceEvent(
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
					{ "money_after", m_PlayerRunStatus.money },
				});
			return;
		}

		if (upgradeCost >= 0 &&
			m_PlayerRunStatus.money < upgradeCost)
		{
			m_SelectedRewardIndex = 2;
			m_PlayerRunStatus.money += kExtraRewardMoney;
			m_RewardMessage =
				"Auto Play: saved Money for a paid upgrade.";
			m_IsClearRewardChosen = true;
			RecordBalanceEvent(
				"clear_reward_choice",
				{
					{ "controller", "autoplay" },
					{ "reward", "extra_money" },
					{ "reason", "save_for_upgrade" },
					{ "target_instance_id", instanceId },
					{ "target_upgrade_level", upgradeLevelBefore },
					{ "required_upgrade_cost", upgradeCost },
					{ "money_before", moneyBefore },
					{ "money_after", m_PlayerRunStatus.money },
				});
			return;
		}
	}

	const int moneyBefore = m_PlayerRunStatus.money;
	m_SelectedRewardIndex = 2;
	m_PlayerRunStatus.money += kExtraRewardMoney;
	m_RewardMessage =
		"Auto Play: received extra Money.";
	m_IsClearRewardChosen = true;
	RecordBalanceEvent(
		"clear_reward_choice",
		{
			{ "controller", "autoplay" },
			{ "reward", "extra_money" },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
		});
}

// Balance Auto Stage Typeを取得する。
StageType Game::GetBalanceAutoStageType() const
{
	return GetScheduledStageType();
}

// Balance Auto Heal Neededかどうかを判定する。
bool Game::IsBalanceAutoHealNeeded() const
{
	if (m_PlayerRunStatus.maxHp <= 0 ||
		m_PlayerRunStatus.currentHp >= m_PlayerRunStatus.maxHp)
	{
		return false;
	}

	const int thresholdPercent = IsBossPreparation() ? 75 : 60;
	return m_PlayerRunStatus.currentHp * 100 <=
		m_PlayerRunStatus.maxHp * thresholdPercent;
}

// Balance Auto Relic To Buyを検索する。
int Game::FindBalanceAutoRelicToBuy() const
{
	const auto isCandidate = [this](int index)
	{
		return dynamic_cast<ShopScene*>(m_SceneManager.Get()) == nullptr ||
			m_ShopRelicOffers.empty() || IsShopRelicOffered(index);
	};
	const bool needsDefense =
		m_PlayerRunStatus.currentHp * 2 <=
		m_PlayerRunStatus.maxHp;
	const std::array<RelicType, 5> priority = needsDefense
		? std::array<RelicType, 5>{
			RelicType::EmergencyRepairKit,
			RelicType::AllBallDefenseUp,
			RelicType::AllBallAttackUp,
			RelicType::CollisionAttackUp,
			RelicType::BankShot }
		: std::array<RelicType, 5>{
			RelicType::AllBallAttackUp,
			RelicType::CollisionAttackUp,
			RelicType::EmergencyRepairKit,
			RelicType::AllBallDefenseUp,
			RelicType::BankShot };

	for (RelicType type : priority)
	{
		for (int index = 0; index < GetRelicCount(); index++)
		{
			const RelicDefinition* relic = GetRelic(index);
			if (relic != nullptr &&
				relic->type == type &&
				isCandidate(index) &&
				!HasRelic(type) &&
				m_PlayerRunStatus.money >= relic->price)
			{
				return index;
			}
		}
	}
	for (int index = 0; index < GetRelicCount(); index++)
	{
		const RelicDefinition* relic = GetRelic(index);
		if (relic != nullptr && isCandidate(index) &&
			!HasRelic(relic->type) &&
			m_PlayerRunStatus.money >= relic->price)
		{
			return index;
		}
	}

	return -1;
}

// Balance Auto Weakest Ballを検索する。
int Game::FindBalanceAutoWeakestBall() const
{
	const int ballCount = m_PlayerDeck.GetRewardTargetCount();
	if (ballCount <= PlayerDeck::MinimumDeckSize ||
		m_PlayerRunStatus.money < kAutoShopRemoveCost)
	{
		return -1;
	}

	int weakestIndex = -1;
	float weakestScore = (std::numeric_limits<float>::max)();
	for (int index = 0; index < ballCount; index++)
	{
		const PlayerBallData* ball = m_PlayerDeck.GetRewardTarget(index);
		if (ball == nullptr)
		{
			continue;
		}

		int sameTypeCount = 0;
		for (int otherIndex = 0; otherIndex < ballCount; otherIndex++)
		{
			const PlayerBallData* other =
				m_PlayerDeck.GetRewardTarget(otherIndex);
			if (other != nullptr &&
				other->definitionId == ball->definitionId)
			{
				sameTypeCount++;
			}
		}

		// タイプの種類を減らさないよう、重複球から削除する。
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

// Balance Auto Missing Catalog Ballを検索する。
int Game::FindBalanceAutoMissingCatalogBall() const
{
	if (m_PlayerDeck.GetRewardTargetCount() >= kAutoMaximumDeckSize)
	{
		return -1;
	}

	const std::array<const char*, 5> priority =
	{
		"player_bounce",
		"player_anchor",
		"player_pierce",
		"player_heavy",
		"player_standard",
	};
	for (const char* definitionId : priority)
	{
		bool alreadyOwned = false;
		for (int index = 0;
			index < m_PlayerDeck.GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				m_PlayerDeck.GetRewardTarget(index);
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
			catalogIndex < m_PlayerDeck.GetCatalogCount();
			catalogIndex++)
		{
			const PlayerBallData* catalogBall =
				m_PlayerDeck.GetCatalogBall(catalogIndex);
			if (catalogBall != nullptr &&
				catalogBall->definitionId == definitionId)
			{
				return catalogIndex;
			}
		}
	}

	return -1;
}

// Balance Auto Upgrade Targetを検索する。
int Game::FindBalanceAutoUpgradeTarget() const
{
	int bestIndex = -1;
	float bestScore = -(std::numeric_limits<float>::max)();
	for (int index = 0;
		index < m_PlayerDeck.GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball =
			m_PlayerDeck.GetRewardTarget(index);
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

// Balance Auto Shop Actionを保持しているか判定する。
bool Game::HasBalanceAutoShopAction() const
{
	return FindBalanceAutoRelicToBuy() >= 0 ||
		FindBalanceAutoWeakestBall() >= 0 ||
		(m_PlayerRunStatus.money >= kAutoShopBallCost &&
			FindBalanceAutoMissingCatalogBall() >= 0);
}

// Balance Auto Pending Upgradeable Ballを検索する。
int Game::FindBalanceAutoPendingUpgradeableBall() const
{
	for (const std::uint64_t instanceId :
		m_AutoPendingBallAdjustments)
	{
		for (int index = 0;
			index < m_PlayerDeck.GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				m_PlayerDeck.GetRewardTarget(index);
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

// Balance Auto Pending Removal Ballを検索する。
int Game::FindBalanceAutoPendingRemovalBall() const
{
	for (const std::uint64_t instanceId :
		m_AutoPendingBallAdjustments)
	{
		for (int index = 0;
			index < m_PlayerDeck.GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				m_PlayerDeck.GetRewardTarget(index);
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

// Ball Adjustment Candidateかどうかを判定する。
bool Game::IsBallAdjustmentCandidate(
	std::uint64_t instanceId) const
{
	return instanceId != 0 &&
		std::find(
			m_AutoPendingBallAdjustments.begin(),
			m_AutoPendingBallAdjustments.end(),
			instanceId) !=
		m_AutoPendingBallAdjustments.end();
}

// Available Rest Benefitを保持しているか判定する。
bool Game::HasAvailableRestBenefit() const
{
	if (CanRestHeal())
	{
		return true;
	}

	const int ballCount =
		m_PlayerDeck.GetRewardTargetCount();
	for (int index = 0; index < ballCount; index++)
	{
		const PlayerBallData* ball =
			m_PlayerDeck.GetRewardTarget(index);
		if (ball != nullptr && ball->CanUpgrade())
		{
			return true;
		}
	}

	return false;
}

// Balance Auto Pending Ballを取り除く。
void Game::RemoveBalanceAutoPendingBall(
	std::uint64_t instanceId)
{
	m_AutoPendingBallAdjustments.erase(
		std::remove(
			m_AutoPendingBallAdjustments.begin(),
			m_AutoPendingBallAdjustments.end(),
			instanceId),
		m_AutoPendingBallAdjustments.end());
}

// Prune Balance Auto Pending Balls の処理を実行する。
void Game::PruneBalanceAutoPendingBalls()
{
	m_AutoPendingBallAdjustments.erase(
		std::remove_if(
			m_AutoPendingBallAdjustments.begin(),
			m_AutoPendingBallAdjustments.end(),
			[this](std::uint64_t instanceId)
			{
				for (int index = 0;
					index < m_PlayerDeck.GetRewardTargetCount();
					index++)
				{
					const PlayerBallData* ball =
						m_PlayerDeck.GetRewardTarget(index);
					if (ball != nullptr &&
						ball->instanceId == instanceId)
					{
						return false;
					}
				}
				return true;
			}),
		m_AutoPendingBallAdjustments.end());
}

// On Battle Stage Started の処理を実行する。
void Game::OnBattleStageStarted(const StageData& stage)
{
	m_BountyRewardClaimed = false;
	InvalidateDebugCombatForecast("戦闘開始");
	m_RunStatistics.ReachFloor(m_PlayerRunStatus.progress);
	if (m_GamePresentation != nullptr && !m_BalanceAutoPlayEnabled)
	{
		m_GamePresentation->OnBattleStarted(*this);
	}
	m_CurrentBattleStageType = stage.stageType;
	m_RunStatistics.BeginBattle(
		stage.stageType == StageType::MidBoss,
		stage.stageType == StageType::Boss &&
			m_RunProgress.GetPhase() == RunPhase::FinalBoss,
		stage.id);
	m_PocketedEnemyQueue.clear();
	m_DynamicBalanceAppliedEnabled = m_DynamicBalanceEnabled;
	m_DynamicBalanceAppliedLevel = m_DynamicBalanceLevel;
	m_DynamicBalanceStageActive = true;
	m_DynamicBalanceShotActive = false;
	m_DynamicBalanceCurrentShotHit = false;
	m_DynamicBalanceStageShots = 0;
	m_DynamicBalanceStageNoHitShots = 0;
	m_DynamicBalanceStageEnemyCount =
		static_cast<int>(stage.enemies.size());

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
		index < m_PlayerDeck.GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball =
			m_PlayerDeck.GetRewardTarget(index);
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
				{ "defense", GetEffectivePlayerBallDefense(ball) },
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

	const int appliedHpModifier =
		m_DynamicBalanceAppliedEnabled
		? m_DynamicBalanceAppliedLevel * m_DynamicBalanceHpStep
		: 0;
	const int appliedAttackModifier =
		m_DynamicBalanceAppliedEnabled
		? CalculateDynamicBalanceAttackModifier(
			m_DynamicBalanceAppliedLevel)
		: 0;
	const int progressionAttackModifier =
		CalculateProgressionAttackModifier();
	const int progressionHpModifier =
		CalculateProgressionHpModifier();
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
		{ "progress", m_PlayerRunStatus.progress },
		{ "area_progress", m_RunProgress.GetAreaProgress() },
		{ "area_goal", kNormalRouteAreaGoal },
		{ "run_phase", ToString(m_RunProgress.GetPhase()) },
		{ "par", stage.par },
		{ "money", m_PlayerRunStatus.money },
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
				{ "enabled", m_DynamicBalanceAppliedEnabled },
				{ "applied_level", m_DynamicBalanceAppliedLevel },
				{ "enemy_hp_modifier", appliedHpModifier },
				{ "enemy_attack_modifier", appliedAttackModifier },
			}
		},
		{
			"progression_scaling",
			{
				{ "enabled", m_ProgressionScalingEnabled },
				{ "progress", m_PlayerRunStatus.progress },
				{ "enemy_hp_modifier", progressionHpModifier },
				{ "hp_start_progress", m_ProgressionHpStart },
				{ "hp_interval", m_ProgressionHpInterval },
				{ "hp_step", m_ProgressionHpStep },
				{ "maximum_hp_delta",
					m_ProgressionHpMaximumDelta },
				{ "enemy_attack_modifier", progressionAttackModifier },
				{ "attack_start_progress", m_ProgressionAttackStart },
				{ "attack_interval", m_ProgressionAttackInterval },
				{ "maximum_attack_delta",
					m_ProgressionAttackMaximumDelta },
			}
		},
		{
			"assist_mode",
			{
				{ "enabled", m_DynamicBalanceAppliedEnabled },
				{ "applied_level", m_DynamicBalanceAppliedLevel },
			}
		},
	};

	BalanceLogger::GetInstance().BeginStage(
		stage.id,
		ToString(stage.stageType),
		stage.difficulty,
		m_PlayerRunStatus.currentHp,
		m_PlayerRunStatus.maxHp,
		enemies,
		stageContext);
}
