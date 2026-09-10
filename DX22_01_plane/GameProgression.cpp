#include "Game.h"
#include "BallStatusJson.h"
#include "PlayerBallText.h"
#include "BallMechanics.h"

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
#include "TableConfig.h"
#include "UiText.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>


namespace
{
	constexpr int kBankShotDamageMultiplier = 2;
	constexpr int kEmergencyRepairContactThreshold = 3;
	constexpr int kEmergencyRepairHealAmount = 1;
	constexpr int kBountyRewardMoney = 5;
	constexpr int kBounceRelicMaximumBonus = 3;
	constexpr int kClearRewardFirstUpgradeCost = 15;
	constexpr int kClearRewardSecondUpgradeCost = 30;

	int CountDefeatedEnemies(const std::vector<EnemyBall*>& enemies)
	{
		return static_cast<int>(std::count_if(
			enemies.begin(),
			enemies.end(),
			[](const EnemyBall* enemy)
			{
				return enemy != nullptr && enemy->IsDefeated();
			}));
	}

	int CountAliveEnemies(const std::vector<EnemyBall*>& enemies)
	{
		return static_cast<int>(std::count_if(
			enemies.begin(),
			enemies.end(),
			[](const EnemyBall* enemy)
			{
				return enemy != nullptr && !enemy->IsDefeated();
			}));
	}

	const char* GetSceneDebugName(Scene* scene)
	{
		if (dynamic_cast<TitleScene*>(scene)) return "TITLE";
		if (dynamic_cast<StageSelectScene*>(scene)) return "SELECT";
		if (dynamic_cast<BattleScene*>(scene)) return "BATTLE";
		if (dynamic_cast<RestSiteScene*>(scene)) return "REST_SITE";
		if (dynamic_cast<ShopScene*>(scene)) return "SHOP";
		if (dynamic_cast<ResultScene*>(scene)) return "RESULT";
		return "Unknown";
	}

	void WriteVector3(
		std::ofstream& file,
		const char* label,
		const DirectX::SimpleMath::Vector3& value)
	{
		file << label << " = ("
			<< value.x << ", "
			<< value.y << ", "
			<< value.z << ")\n";
	}

	void WriteBallDebugStatus(
		std::ofstream& file,
		const char* typeName,
		int index,
		BallComponent* ball)
	{
		if (ball == nullptr)
		{
			return;
		}

		file << "[" << typeName << " " << index << "]\n";
		WriteVector3(file, "Position", ball->GetPosition());
		WriteVector3(file, "Velocity", ball->GetVelocity());
		file << "HP = " << ball->GetHP() << " / " << ball->GetMaxHP() << "\n";
		file << "Radius = " << ball->GetRadius() << "\n";
		file << "IsStopped = " << (ball->IsStopped() ? "true" : "false") << "\n";
		file << "IsDefeated = " << (ball->IsDefeated() ? "true" : "false") << "\n";
		file << "\n";
	}
}

// Rest Heal の処理を実行する。
bool Game::RestHeal()
{
	RestSiteScene* restSite =
		dynamic_cast<RestSiteScene*>(m_SceneManager.Get());
	if (restSite != nullptr && restSite->HasUsedAction())
	{
		return false;
	}
	if (!CanRestHeal())
	{
		return false;
	}

	const int hpBefore = m_PlayerRunStatus.currentHp;
	const int configuredHealAmount = GetRestHealAmount();
	m_PlayerRunStatus.currentHp = (std::min)(
		m_PlayerRunStatus.maxHp,
		hpBefore + configuredHealAmount);
	const int actualHealAmount =
		m_PlayerRunStatus.currentHp - hpBefore;
	if (restSite != nullptr)
	{
		restSite->MarkActionUsed();
	}
	RecordBalanceEvent(
		"rest_heal",
		{
			{ "hp_before", hpBefore },
			{ "hp_after", m_PlayerRunStatus.currentHp },
			{ "heal_amount", actualHealAmount },
			{ "configured_heal_amount", configuredHealAmount },
			{ "heal_ratio", m_RestHealRatio },
			{ "boss_preparation", IsBossPreparation() },
			{ "capped_at_max_hp",
				actualHealAmount < configuredHealAmount },
			{ "source_scene", GetSceneDebugName(m_SceneManager.Get()) },
		});
	return true;
}

// Rest Heal Amountを取得する。
int Game::GetRestHealAmount() const
{
	return (std::max)(
		1,
		static_cast<int>(std::ceil(
			static_cast<float>(m_PlayerRunStatus.maxHp) *
			m_RestHealRatio)));
}

// Rest Heal Percentを取得する。
int Game::GetRestHealPercent() const
{
	return static_cast<int>(std::lround(m_RestHealRatio * 100.0f));
}

// Clear Reward Upgrade Costを取得する。
int Game::GetClearRewardUpgradeCost(int ballIndex) const
{
	const PlayerBallData* ball =
		m_PlayerDeck.GetRewardTarget(ballIndex);
	if (ball == nullptr || !ball->CanUpgrade())
	{
		return -1;
	}

	if (ball->upgradeLevel == 0)
	{
		return kClearRewardFirstUpgradeCost;
	}
	if (ball->upgradeLevel == 1)
	{
		return kClearRewardSecondUpgradeCost;
	}
	return -1;
}

// Clear Reward Upgradeを適用する。
bool Game::ApplyClearRewardUpgrade(
	int ballIndex,
	int& chargedCost)
{
	chargedCost = 0;
	if (!m_IsClearRewardActive)
	{
		return false;
	}

	const int cost = GetClearRewardUpgradeCost(ballIndex);
	if (cost < 0 || m_PlayerRunStatus.money < cost)
	{
		return false;
	}

	if (!RestUpgradeBall(ballIndex))
	{
		return false;
	}

	m_PlayerRunStatus.money -= cost;
	chargedCost = cost;
	return true;
}

// Rest Upgrade Ball の処理を実行する。
bool Game::RestUpgradeBall(int ballIndex)
{
	RestSiteScene* restSite =
		dynamic_cast<RestSiteScene*>(m_SceneManager.Get());
	if (restSite != nullptr && restSite->HasUsedAction())
	{
		return false;
	}
	PlayerBallData* ball = m_PlayerDeck.GetRewardTarget(ballIndex);
	if (ball == nullptr ||
		!ball->CanUpgrade())
	{
		return false;
	}

	const std::uint64_t instanceId = ball->instanceId;
	const std::string ballId = ball->definitionId;
	const int upgradeLevelBefore = ball->upgradeLevel;
	const BallUpgradeStep& upgrade = ball->upgradeTable[ball->upgradeLevel];
	ball->status = upgrade;
	ball->upgradeLevel++;
	ball->status = NormalizeBallStatus(ball->status);
	RemoveBalanceAutoPendingBall(instanceId);
	if (restSite != nullptr)
	{
		restSite->MarkActionUsed();
	}
	RecordBalanceEvent(
		"ball_upgraded",
		{
			{ "instance_id", instanceId },
			{ "ball_id", ballId },
			{ "upgrade_level_before", upgradeLevelBefore },
			{ "upgrade_level_after", ball->upgradeLevel },
			{ "attack_after", ball->status.attack },
			{ "defense_after", ball->status.defense },
			{ "status_after", WriteBallStatus(ball->status) },
			{ "source_scene", GetSceneDebugName(m_SceneManager.Get()) },
		});
	return true;
}

// Shop Ballを購入する。
bool Game::BuyShopBall(int catalogIndex, int cost)
{
	cost = (std::max)(0, cost);
	const PlayerBallData* catalogBall =
		m_PlayerDeck.GetCatalogBall(catalogIndex);
	const std::string ballId =
		catalogBall != nullptr ? catalogBall->definitionId : std::string();
	const int moneyBefore = m_PlayerRunStatus.money;
	if (m_PlayerRunStatus.money < cost ||
		!m_PlayerDeck.AddCatalogBall(catalogIndex))
	{
		return false;
	}

	m_PlayerRunStatus.money -= cost;
	PublishGameEvent(BallAcquiredEvent{ ballId });
	RecordBalanceEvent(
		"shop_ball_purchased",
		{
			{ "catalog_index", catalogIndex },
			{ "ball_id", ballId },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
		});
	return true;
}

// Shop Ballを取り除く。
bool Game::RemoveShopBall(int ballIndex, int cost)
{
	cost = (std::max)(0, cost);
	if (m_PlayerRunStatus.money < cost ||
		m_PlayerDeck.GetRewardTargetCount() <=
			PlayerDeck::MinimumDeckSize)
	{
		return false;
	}

	const PlayerBallData* ball =
		m_PlayerDeck.GetRewardTarget(ballIndex);
	if (ball == nullptr)
	{
		return false;
	}
	const std::uint64_t instanceId = ball->instanceId;
	const std::string ballId = ball->definitionId;
	const int moneyBefore = m_PlayerRunStatus.money;

	if (!m_PlayerDeck.RemoveRewardTarget(ballIndex))
	{
		return false;
	}

	m_PlayerRunStatus.money -= cost;
	RemoveBalanceAutoPendingBall(instanceId);
	RecordBalanceEvent(
		"shop_ball_removed",
		{
			{ "instance_id", instanceId },
			{ "ball_id", ballId },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
			{ "deck_count_after", m_PlayerDeck.GetRewardTargetCount() },
		});
	return true;
}

// Relicを購入する。
bool Game::BuyRelic(int relicIndex)
{
	const RelicDefinition* relic = GetRelic(relicIndex);
	if (relic == nullptr || HasRelic(relic->type))
	{
		return false;
	}

	const int cost = (std::max)(0, relic->price);
	if (m_PlayerRunStatus.money < cost)
	{
		return false;
	}

	const int moneyBefore = m_PlayerRunStatus.money;
	m_PlayerRunStatus.money -= cost;
	if (!GrantRelic(relicIndex, "shop"))
	{
		m_PlayerRunStatus.money = moneyBefore;
		return false;
	}
	RecordBalanceEvent(
		"relic_purchased",
		{
			{ "relic_index", relicIndex },
			{ "relic_name", relic->name },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
		});

	return true;
}

// Relicを付与する。
bool Game::GrantRelic(int relicIndex, const char* source)
{
	const RelicDefinition* relic = GetRelic(relicIndex);
	if (relic == nullptr || HasRelic(relic->type))
	{
		return false;
	}

	m_OwnedRelics[static_cast<std::size_t>(relic->type)] = true;
	for (PlayerBall* player : GetComponents<PlayerBall>())
	{
		ApplyRelicModifiersTo(player);
	}
	RecordBalanceEvent(
		"relic_acquired",
		{
			{ "relic_index", relicIndex },
			{ "relic_name", relic->name },
			{ "rarity", ToString(relic->rarity) },
			{ "source", source != nullptr ? source : "unknown" },
		});
	return true;
}

// Relic Offersの候補を抽選する。
std::vector<int> Game::RollRelicOffers(int count, bool midBoss)
{
	std::vector<int> candidates;
	for (int index = 0; index < GetRelicCount(); index++)
	{
		const RelicDefinition* relic = GetRelic(index);
		if (relic != nullptr && !m_ProgressionProfile.IsRelicUnlocked(relic->type)) continue;
		const int weight = relic == nullptr
			? 0
			: (midBoss ? relic->midBossWeight : relic->shopWeight);
		if (relic != nullptr && !HasRelic(relic->type) && weight > 0)
		{
			candidates.push_back(index);
		}
	}

	std::vector<int> offers;
	while (!candidates.empty() && static_cast<int>(offers.size()) < count)
	{
		int totalWeight = 0;
		for (int index : candidates)
		{
			const RelicDefinition* relic = GetRelic(index);
			totalWeight += midBoss
				? relic->midBossWeight
				: relic->shopWeight;
		}
		std::uniform_int_distribution<int> distribution(1, totalWeight);
		int roll = distribution(m_RelicRandomEngine);
		std::size_t selected = 0;
		for (; selected < candidates.size(); selected++)
		{
			const RelicDefinition* relic = GetRelic(candidates[selected]);
			roll -= midBoss ? relic->midBossWeight : relic->shopWeight;
			if (roll <= 0)
			{
				break;
			}
		}
		offers.push_back(candidates[selected]);
		candidates.erase(candidates.begin() + selected);
	}
	return offers;
}

// Shop Relic Offersの候補を抽選する。
void Game::RollShopRelicOffers()
{
	m_ShopRelicOffers = RollRelicOffers(3, false);
}

// Shop Relic Offeredかどうかを判定する。
bool Game::IsShopRelicOffered(int relicIndex) const
{
	return std::find(
		m_ShopRelicOffers.begin(),
		m_ShopRelicOffers.end(),
		relicIndex) != m_ShopRelicOffers.end();
}

// Shop Relic Offerを購入する。
bool Game::BuyShopRelicOffer(int offerIndex)
{
	return BuyShopRelic(GetShopRelicOfferCatalogIndex(offerIndex));
}

// Shop Relicを購入する。
bool Game::BuyShopRelic(int relicIndex)
{
	if (!IsShopRelicOffered(relicIndex))
	{
		return false;
	}
	if (!BuyRelic(relicIndex))
	{
		return false;
	}
	return true;
}

// Mid Boss Relic Offersの候補を抽選する。
void Game::RollMidBossRelicOffers()
{
	m_MidBossRelicOffers = RollRelicOffers(3, true);
	m_SelectedRelicOfferIndex = 0;
	m_IsMidBossRelicSelectionActive = !m_MidBossRelicOffers.empty();
}

// Mid Boss Relic Offerを獲得する。
bool Game::AcquireMidBossRelicOffer(int offerIndex)
{
	return AcquireMidBossRelic(GetMidBossRelicOfferCatalogIndex(offerIndex));
}

// Mid Boss Relicを獲得する。
bool Game::AcquireMidBossRelic(int relicIndex)
{
	if (!m_IsMidBossRelicSelectionActive ||
		std::find(m_MidBossRelicOffers.begin(),
			m_MidBossRelicOffers.end(), relicIndex) ==
			m_MidBossRelicOffers.end() ||
		!GrantRelic(relicIndex, "midboss"))
	{
		return false;
	}
	m_IsMidBossRelicSelectionActive = false;
	return true;
}

// Owned Relic Countを取得する。
int Game::GetOwnedRelicCount() const
{
	return static_cast<int>(std::count(
		m_OwnedRelics.begin(),
		m_OwnedRelics.end(),
		true));
}

// Relic Attack Bonusを取得する。
int Game::GetRelicAttackBonus() const
{
	return HasRelic(RelicType::AllBallAttackUp) ? 1 : 0;
}

// Relic Defense Bonusを取得する。
int Game::GetRelicDefenseBonus() const
{
	return HasRelic(RelicType::AllBallDefenseUp) ? 1 : 0;
}

// Effective Player Ball Attackを取得する。
int Game::GetEffectivePlayerBallAttack(
	const PlayerBallData* ball) const
{
	return ball == nullptr
		? 0
		: ball->status.attack + GetRelicAttackBonus();
}

// Effective Player Ball Defenseを取得する。
int Game::GetEffectivePlayerBallDefense(
	const PlayerBallData* ball) const
{
	return ball == nullptr
		? 0
		: ball->status.defense + GetRelicDefenseBonus();
}

// Player Status Toを適用する。
void Game::ApplyPlayerStatusTo(PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	const PlayerBallData* selectedBall = m_PlayerDeck.GetCurrent();
	if (selectedBall == nullptr)
	{
		selectedBall = m_PlayerDeck.GetOffer(m_SelectedOfferIndex);
	}

	if (selectedBall == nullptr)
	{
		ApplyPlayerRunStatusTo(player);
		return;
	}

	player->SetStatus(selectedBall->status);
	if (auto* render = player->GetGameObject()->GetComponent<BallRenderComponent>())
	{
		const auto color = PlayerBallText::GetColor(selectedBall->definitionId);
		render->SetTint(DirectX::SimpleMath::Color(color[0], color[1], color[2], 1.0f));
	}

	// 物理半径だけでなく、描画モデルの大きさも選択したボールへ合わせる。
	if (selectedBall->status.radius > 0.0f && player->GetBall() != nullptr)
	{
		const float visualScale = selectedBall->status.radius;
		player->GetBall()->SetScale(DirectX::SimpleMath::Vector3(
			visualScale,
			visualScale,
			visualScale));
	}

	ApplyPlayerRunStatusTo(player);
}

// Player Run Status Toを適用する。
void Game::ApplyPlayerRunStatusTo(PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	m_PlayerRunStatus = NormalizePlayerRunStatus(m_PlayerRunStatus);
	player->SetMaxHP(m_PlayerRunStatus.maxHp);
	player->SetHP(m_PlayerRunStatus.currentHp);
	ApplyRelicModifiersTo(player);
	InvalidateDebugCombatForecast("プレイヤー状態変更");
}

// Relic Modifiers Toを適用する。
void Game::ApplyRelicModifiersTo(PlayerBall* player)
{
	if (player == nullptr || player->GetBall() == nullptr)
	{
		return;
	}

	const int defenseBefore = player->GetDefense();
	const int collisionBonus =
		HasRelic(RelicType::CollisionAttackUp)
			? m_CurrentShotCollisionAttackBonus
			: 0;
	player->GetBall()->SetCombatModifiers(
		GetRelicAttackBonus() + collisionBonus,
		GetRelicDefenseBonus());
	if (player->GetDefense() != defenseBefore)
	{
		InvalidateDebugCombatForecast("プレイヤー防御力変更");
	}
}

// Shot Relic Stateを初期状態へ戻す。
void Game::ResetShotRelicState(PlayerBall* player)
{
	m_CurrentShotCollisionAttackBonus = 0;
	m_CurrentShotPlayerEnemyCollisionCount = 0;
	m_CurrentShotEnemyEnemyCollisionCount = 0;
	m_CurrentShotBankShotReady = false;
	m_CurrentShotBankShotConsumed = false;
	m_CurrentShotWallCollisionCount = 0;
	m_CurrentShotBounceDamageBonus = 0;
	m_CurrentShotAnchorStopped = false;
	m_CurrentShotLaunchPower = 0.0f;
	if (player != nullptr)
	{
		ApplyRelicModifiersTo(player);
	}
}

// End Of Shot Relic Effectsを適用する。
void Game::ApplyEndOfShotRelicEffects(PlayerBall* player)
{
	if (player == nullptr ||
		!HasRelic(RelicType::EmergencyRepairKit) ||
		GetCurrentShotBallCollisionCount() <
			kEmergencyRepairContactThreshold)
	{
		return;
	}

	const int hpBefore = player->GetHP();
	const int hpAfter = (std::min)(
		player->GetMaxHP(),
		hpBefore + kEmergencyRepairHealAmount);
	if (hpAfter <= hpBefore)
	{
		return;
	}

	player->SetHP(hpAfter);
	CapturePlayerStatusFrom(player);
	InvalidateDebugCombatForecast("プレイヤーHP回復");
	RecordBalanceEvent(
		"emergency_repair_triggered",
		{
			{ "ball_collision_count", GetCurrentShotBallCollisionCount() },
			{ "heal_amount", hpAfter - hpBefore },
			{ "hp_before", hpBefore },
			{ "hp_after", hpAfter },
		});
}

// Player Status Fromを取得して保持する。
void Game::CapturePlayerStatusFrom(const PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	BallStatus updatedStatus =
		NormalizeBallStatus(player->GetStatus());

	PlayerBallData* currentBall = m_PlayerDeck.GetCurrent();
	if (currentBall != nullptr)
	{
		currentBall->status = updatedStatus;
	}

	m_PlayerRunStatus = NormalizePlayerRunStatus(m_PlayerRunStatus);
	m_PlayerRunStatus.currentHp = std::clamp(
		player->GetHP(),
		0,
		m_PlayerRunStatus.maxHp
	);
}
// Current Player Statusを取得して保持する。
void Game::CaptureCurrentPlayerStatus()
{
	std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	if (players.empty())
	{
		return;
	}
	CapturePlayerStatusFrom(players[0]);
}

// Next Player Ballを描画する。
void Game::DrawNextPlayerBall()
{
	m_PlayerDeck.DrawNext();
}
// Next Player Ballを準備する。
bool Game::PrepareNextPlayerBall()
{
	DiscardCurrentPlayerBall();

	if (m_PlayerDeck.HasCurrent())
	{
		return true;
	}

	return BeginBallSelection();
}

// Stage Reward Moneyを計算する。
int Game::CalculateStageRewardMoney() const
{
	constexpr int BASE_CLEAR_MONEY = 5;
	int totalRewardMoney = BASE_CLEAR_MONEY;

	std::vector<EnemyBall*> enemies =
		m_Instance->GetComponents<EnemyBall>();

	for (EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		// 倒した敵の報酬だけを取得する
		if (!enemy->IsDefeated())
		{
			continue;
		}

		totalRewardMoney +=
			(std::max)(0, enemy->GetRewardMoney());
	}

	return totalRewardMoney;
}

// Collect Stage Reward Money の処理を実行する。
void Game::CollectStageRewardMoney()
{
	// StartClearRewardが複数回呼ばれても二重取得しない
	if (m_IsStageRewardCollected)
	{
		return;
	}

	m_CurrentStageRewardMoney =
		CalculateStageRewardMoney();

	const int moneyBefore = m_PlayerRunStatus.money;
	m_PlayerRunStatus.money +=
		m_CurrentStageRewardMoney;

	m_PlayerRunStatus =
		NormalizePlayerRunStatus(m_PlayerRunStatus);

	m_IsStageRewardCollected = true;

	char rewardMessage[128]{};
	sprintf_s(
		rewardMessage,
		UiText::RewardMoneyFormat,
		m_CurrentStageRewardMoney);
	m_RewardMessage = rewardMessage;
	RecordBalanceEvent(
		"stage_money_reward",
		{
			{ "amount", m_CurrentStageRewardMoney },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
		});
}

// On Player Shot Fired の処理を実行する。
void Game::OnPlayerShotFired(PlayerBall* player)
{
	ResetFrameTiming();
	ResetShotRelicState(player);
    // A new shot starts a new damage episode even for touching enemy pairs.
    for (auto* ball : GetComponents<BallComponent>()) ball->ResetShotAbilityState();
    for (auto* enemy : GetComponents<EnemyBall>()) enemy->BeginBossShot();

	// 選択内容はショットした瞬間に確定する。
	if (!m_PlayerDeck.HasCurrent())
	{
		if (!m_PlayerDeck.SelectOffer(
			m_SelectedOfferIndex,
			m_SelectedHoldIndex))
		{
			return;
		}
	}

	if (player != nullptr)
	{
		CapturePlayerStatusFrom(player);
	}

	const PlayerBallData* currentBall =
		m_PlayerDeck.GetCurrent();
	if (player != nullptr && currentBall != nullptr)
	{
		m_CurrentShotLaunchPower = player->GetVelocity().Length();
		PublishGameEvent(ShotFiredEvent{ currentBall->definitionId });
		if (m_DynamicBalanceStageActive)
		{
			FinishDynamicBalanceShot();
			m_DynamicBalanceStageShots++;
			m_DynamicBalanceShotActive = true;
			m_DynamicBalanceCurrentShotHit = false;
		}

		const DirectX::SimpleMath::Vector3 position =
			player->GetPosition();
		const DirectX::SimpleMath::Vector3 velocity =
			player->GetVelocity();
		const std::vector<EnemyBall*> enemies =
			GetComponents<EnemyBall>();
		nlohmann::json offers = nlohmann::json::array();
		for (int index = 0;
			index < m_PlayerDeck.GetOfferCount();
			index++)
		{
			const PlayerBallData* offer =
				m_PlayerDeck.GetOffer(index);
			if (offer == nullptr)
			{
				continue;
			}
			offers.push_back(
				{
					{ "offer_index", index },
					{ "instance_id", offer->instanceId },
					{ "id", offer->definitionId },
					{ "upgrade_level", offer->upgradeLevel },
					{ "held", m_PlayerDeck.WasHeldOffer(index) },
				});
		}
		nlohmann::json shotContext =
		{
			{ "selected_offer_index", m_SelectedOfferIndex },
			{ "held_offer_index", m_SelectedHoldIndex },
			{ "selected_instance_id", currentBall->instanceId },
			{ "effective_attack", player->GetAttack() },
			{ "effective_defense", player->GetDefense() },
			{ "offers", std::move(offers) },
			{ "mcp_telemetry", m_PendingShotTelemetry },
		};

		BalanceLogger::GetInstance().BeginShot(
			currentBall->definitionId,
			currentBall->upgradeLevel,
			velocity.Length(),
			position.x,
			position.y,
			position.z,
			velocity.x,
			velocity.y,
			velocity.z,
			player->GetHP(),
			CountAliveEnemies(enemies),
			CountDefeatedEnemies(enemies),
			shotContext);
		m_PendingShotTelemetry = nlohmann::json::object();
	}

	m_PlayerDeck.MarkCurrentUsed();
	m_BattleController.NotifyShotFired();
}

// Player Wall Collisionを通知する。
void Game::NotifyPlayerWallCollision()
{
    auto rules = CaptureShotRelicRules();
    rules.Wall();
    CommitShotRelicRules(rules);
}

// Current Ballかどうかを判定する。
bool Game::IsCurrentBall(const char* definitionId) const
{
	const PlayerBallData* currentBall = m_PlayerDeck.GetCurrent();
	return currentBall != nullptr && definitionId != nullptr &&
		currentBall->definitionId == definitionId;
}

// Player Enemy Relic Damage Bonusを消費する。
int Game::ConsumePlayerEnemyRelicDamageBonus()
{
    auto rules = CaptureShotRelicRules();
    const int bonus = rules.ConsumePlayerEnemyRelicDamageBonus();
    CommitShotRelicRules(rules);
    return bonus;
}

// Anchor Stoppedを通知する。
void Game::NotifyAnchorStopped()
{
    auto rules = CaptureShotRelicRules();
    rules.Anchor();
    CommitShotRelicRules(rules);
}

// Pierce Maximum Usesを取得する。
int Game::GetPierceMaximumUses() const
{
	const PlayerBallData* ball = m_PlayerDeck.GetCurrent();
	if (ball == nullptr) ball = m_PlayerDeck.GetOffer(m_SelectedOfferIndex);
	return ball != nullptr ? BallMechanics::PierceUses(ball->status,
		HasRelic(RelicType::PierceBallCharger) && ball->definitionId == "player_pierce") : 0;
}

// Pierce Speed Retentionを取得する。
float Game::GetPierceSpeedRetention() const
{
	const PlayerBallData* ball = m_PlayerDeck.GetCurrent();
	if (ball == nullptr) ball = m_PlayerDeck.GetOffer(m_SelectedOfferIndex);
	return ball != nullptr ? BallMechanics::PierceRetention(ball->status,
		HasRelic(RelicType::PierceBallCharger) && ball->definitionId == "player_pierce") : 0.75f;
}

// Enemy Defeatedを通知する。
void Game::NotifyEnemyDefeated(const std::string& enemyId)
{
	if (!HasRelic(RelicType::BountyList) || m_BountyRewardClaimed)
	{
		return;
	}
	m_BountyRewardClaimed = true;
	const int moneyBefore = m_PlayerRunStatus.money;
	m_PlayerRunStatus.money += kBountyRewardMoney;
	RecordBalanceEvent(
		"bounty_reward",
		{
			{ "enemy_id", enemyId },
			{ "amount", kBountyRewardMoney },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
		});
}

// Bank Shot Damage Multiplierを消費する。
int Game::ConsumeBankShotDamageMultiplier()
{
    auto rules = CaptureShotRelicRules();
    const int multiplier = rules.ConsumeBankShotDamageMultiplier();
    CommitShotRelicRules(rules);
    if (multiplier > 1) RecordBalanceEvent("bank_shot_triggered", {{ "damage_multiplier", multiplier }});
    return multiplier;
}

// Damage Ball Collisionを通知する。
void Game::NotifyDamageBallCollision(
	DamageBallCollisionType collisionType)
{
    auto rules = CaptureShotRelicRules();
    rules.Contact(collisionType == DamageBallCollisionType::PlayerEnemy);
    CommitShotRelicRules(rules);
    if (HasRelic(RelicType::CollisionAttackUp))
        for (PlayerBall* player : GetComponents<PlayerBall>()) ApplyRelicModifiersTo(player);
}

// Combat Feedbackを通知する。
void Game::NotifyCombatFeedback(
	const DirectX::SimpleMath::Vector3& worldPosition,
	int damage,
	bool defeated,
	bool enemyEnemyCollision)
{
	InvalidateDebugCombatForecast("エネミー被ダメージ");
	PublishGameEvent(EnemyDamageEvent{
		worldPosition,
		damage,
		defeated,
		enemyEnemyCollision,
	});
}

// Pocket Feedbackを通知する。
void Game::NotifyPocketFeedback(
	const DirectX::SimpleMath::Vector3& worldPosition,
	bool playerPocket,
	bool finisher,
	int damage)
{
	InvalidateDebugCombatForecast(
		playerPocket ? "プレイヤーポケット" : "エネミーポケット");
	PublishGameEvent(PocketFeedbackEvent{
		worldPosition,
		playerPocket,
		finisher,
		damage,
	});
}

// Player Damageを通知する。
void Game::NotifyPlayerDamage(
	const std::string& source,
	int damage,
	const std::string& sourceId,
	int hpBefore,
	int hpAfter)
{
	if (damage > 0)
	{
		InvalidateDebugCombatForecast("プレイヤー被ダメージ");
		RecordDebugPlayerDamage(
			source,
			sourceId,
			damage,
			hpBefore,
			hpAfter);
	}
	PublishGameEvent(PlayerDamageEvent{ source, damage, sourceId });
}

// Game Eventを公開する。
void Game::PublishGameEvent(const GameEvent& event)
{
	std::visit(
		[this](const auto& value)
		{
			using EventType = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<EventType, ShotFiredEvent>)
			{
				m_RunStatistics.BeginShot(value.ballId);
				if (m_GamePresentation != nullptr)
				{
					m_GamePresentation->OnShotFired();
				}
			}
			else if constexpr (std::is_same_v<EventType, EnemyDamageEvent>)
			{
				m_RunStatistics.RecordEnemyDamage(value.damage);
				if (m_GamePresentation != nullptr &&
					!m_BalanceAutoPlayEnabled)
				{
					m_GamePresentation->OnCombatFeedback(
						value.worldPosition,
						value.damage,
						value.defeated,
						value.enemyEnemyCollision);
				}
			}
			else if constexpr (std::is_same_v<EventType, PocketFeedbackEvent>)
			{
				if (!value.playerPocket && value.finisher)
				{
					m_RunStatistics.RecordEnemyDamage(value.damage);
				}
				if (m_GamePresentation != nullptr &&
					!m_BalanceAutoPlayEnabled)
				{
					m_GamePresentation->OnPocketFeedback(
						value.worldPosition,
						value.playerPocket,
						value.finisher,
						value.damage);
				}
			}
			else if constexpr (std::is_same_v<EventType, PlayerDamageEvent>)
			{
				m_RunStatistics.RecordPlayerDamage(value.damage);
				BalanceLogger::GetInstance().RecordPlayerDamage(
					value.source,
					value.damage,
					value.sourceId);
				if (m_GamePresentation != nullptr &&
					!m_BalanceAutoPlayEnabled)
				{
					m_GamePresentation->OnPlayerDamage(
						value.damage,
						value.source);
				}
			}
			else if constexpr (std::is_same_v<EventType, BallAcquiredEvent>)
			{
				m_RunStatistics.RecordBallAcquired(value.ballId);
			}
		},
		event);
}

// Balance Eventを記録する。
void Game::RecordBalanceEvent(
	const std::string& eventType,
	const nlohmann::json& details)
{
	BalanceLogger::GetInstance().RecordEvent(eventType, details);
}

// Dynamic Balance Hitを通知する。
void Game::NotifyDynamicBalanceHit()
{
	if (m_DynamicBalanceStageActive &&
		m_DynamicBalanceShotActive)
	{
		m_DynamicBalanceCurrentShotHit = true;
	}
}

// Dynamic Balance To Enemy Dataを適用する。
void Game::ApplyDynamicBalanceToEnemyData(
	EnemyData& enemyData) const
{
	if (m_DebugMode) return; // 指定した実験値へ難易度補正を重ねない。
	const bool armorBoss = enemyData.id == "enemy_boss_core";
	if (!armorBoss) enemyData.maxHp = std::clamp(
		static_cast<int>(std::lround(
			static_cast<double>(enemyData.maxHp) *
			static_cast<double>(m_BaselineEnemyHpMultiplier))) +
			CalculateProgressionHpModifier(),
		m_DynamicBalanceMinEnemyHp,
		m_DynamicBalanceMaxEnemyHp);
	if (!armorBoss) enemyData.status.attack = std::clamp(
		enemyData.status.attack +
			m_BaselineEnemyAttackDelta +
			CalculateProgressionAttackModifier(),
		m_DynamicBalanceMinEnemyAttack,
		m_DynamicBalanceMaxEnemyAttack);

	const bool effectiveEnabled = !armorBoss && (
		m_DynamicBalanceStageActive
			? m_DynamicBalanceAppliedEnabled
			: m_DynamicBalanceEnabled);

	if (effectiveEnabled)
	{
		const int effectiveLevel =
			m_DynamicBalanceStageActive
				? m_DynamicBalanceAppliedLevel
				: m_DynamicBalanceLevel;
		const int hpDelta =
			effectiveLevel *
			m_DynamicBalanceHpStep;
		const int attackDelta =
			CalculateDynamicBalanceAttackModifier(effectiveLevel);

		enemyData.maxHp = std::clamp(
			enemyData.maxHp + hpDelta,
			m_DynamicBalanceMinEnemyHp,
			m_DynamicBalanceMaxEnemyHp);
		enemyData.status.attack = std::clamp(
			enemyData.status.attack + attackDelta,
			m_DynamicBalanceMinEnemyAttack,
			m_DynamicBalanceMaxEnemyAttack);
	}

	// アセンションは救済補正の後に適用し、選択した難易度を保証する。
	enemyData.maxHp = std::clamp(
		static_cast<int>(std::lround(enemyData.maxHp * ProgressionProfile::EnemyHpMultiplier(m_ActiveAscension))),
		m_DynamicBalanceMinEnemyHp, m_DynamicBalanceMaxEnemyHp);
	enemyData.status.attack = std::clamp(
		enemyData.status.attack + ProgressionProfile::EnemyAttackBonus(m_ActiveAscension),
		m_DynamicBalanceMinEnemyAttack, m_DynamicBalanceMaxEnemyAttack);
}

// Progression Hp Modifierを計算する。
int Game::CalculateProgressionHpModifier() const
{
	if (!m_ProgressionScalingEnabled ||
		m_PlayerRunStatus.progress < m_ProgressionHpStart ||
		m_ProgressionHpStep <= 0)
	{
		return 0;
	}
	const int tier = 1 +
		(m_PlayerRunStatus.progress - m_ProgressionHpStart) /
		m_ProgressionHpInterval;
	return (std::min)(
		m_ProgressionHpMaximumDelta,
		tier * m_ProgressionHpStep);
}

// Progression Attack Modifierを計算する。
int Game::CalculateProgressionAttackModifier() const
{
	if (!m_ProgressionScalingEnabled ||
		m_PlayerRunStatus.progress < m_ProgressionAttackStart ||
		m_ProgressionAttackStep <= 0)
	{
		return 0;
	}
	const int tier = 1 +
		(m_PlayerRunStatus.progress - m_ProgressionAttackStart) /
		m_ProgressionAttackInterval;
	return (std::min)(
		m_ProgressionAttackMaximumDelta,
		tier * m_ProgressionAttackStep);
}

// Dynamic Balance Attack Modifierを計算する。
int Game::CalculateDynamicBalanceAttackModifier(int level) const
{
	if (level > 0 && !m_DynamicBalancePositiveAttackScalingEnabled)
	{
		return 0;
	}
	return (level / m_DynamicBalanceLevelsPerAttackStep) *
		m_DynamicBalanceAttackStep;
}

// Dynamic Balanceを設定する。
void Game::SetDynamicBalance(
	bool enabled,
	bool resetLevel,
	int requestedLevel,
	bool hasRequestedLevel)
{
	if (m_BalanceValidationEnabled &&
		m_BalanceValidationCurrentDisableDynamicBalance)
	{
		m_DynamicBalanceEnabled = false;
		m_DynamicBalanceLevel = 0;
		m_DynamicBalanceLastReason =
			"Assist mode is locked off by balance validation mode.";
		m_DynamicBalanceLastLevelChange = 0;
		return;
	}
	m_DynamicBalanceEnabled = enabled;
	if (hasRequestedLevel)
	{
		m_DynamicBalanceLevel = std::clamp(
			requestedLevel,
			m_DynamicBalanceMinLevel,
			m_DynamicBalanceMaxLevel);
		m_DynamicBalanceLastReason =
			"Difficulty level was set through MCP.";
	}
	else if (resetLevel)
	{
		m_DynamicBalanceLevel = 0;
		m_DynamicBalanceLastReason =
			"Difficulty level was reset through MCP.";
	}
	else
	{
		m_DynamicBalanceLastReason =
			enabled
				? "Automatic adjustment was enabled through MCP."
				: "Automatic adjustment was disabled through MCP.";
	}
	m_DynamicBalanceLastLevelChange = 0;
}

// Balance Auto Full Hp Enemy Survivedを通知する。
void Game::NotifyBalanceAutoFullHpEnemySurvived()
{
	const PlayerBallData* currentBall =
		m_PlayerDeck.GetCurrent();
	if (currentBall == nullptr ||
		currentBall->instanceId == 0)
	{
		return;
	}

	if (std::find(
		m_AutoPendingBallAdjustments.begin(),
		m_AutoPendingBallAdjustments.end(),
		currentBall->instanceId) !=
		m_AutoPendingBallAdjustments.end())
	{
		return;
	}

	m_AutoPendingBallAdjustments.push_back(
		currentBall->instanceId);
	std::cout
		<< "[BalanceAutoPlay] Ball adjustment pending: "
		<< currentBall->definitionId
		<< " (instance " << currentBall->instanceId << ")"
		<< std::endl;
}

// Current Player Ballを破棄する。
void Game::DiscardCurrentPlayerBall()
{
	if (!m_PlayerDeck.HasCurrent())
	{
		m_PlayerDeck.ClearCurrentUsed();
		return;
	}

	if (!m_PlayerDeck.IsCurrentUsed())
	{
		return;
	}

	std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	if (!players.empty() && players[0] != nullptr)
	{
		CapturePlayerStatusFrom(players[0]);
	}

	m_PlayerDeck.DiscardCurrentIfUsed();
}

// Debug Snapshotを保存する。
void Game::SaveDebugSnapshot()
{
	std::ofstream file("debug_state_snapshot.txt");

	if (!file)
	{
		return;
	}

	file << std::fixed << std::setprecision(3);

	file << "[Scene]\n";
	file << "Scene = " << GetSceneDebugName(m_SceneManager.Get()) << "\n";
	file << "[Scene]\n";
	file << "Scene = " << GetSceneDebugName(m_SceneManager.Get()) << "\n";

	file << "BattleState = "
		<< ToString(GetBattleState())
		<< " (" << static_cast<int>(GetBattleState()) << ")\n";

	file << "ClearReward = "
		<< (m_IsClearRewardActive ? "true" : "false")
		<< "\n";

	file << "LastBattleResult = "
		<< ToString(m_LastBattleResult)
		<< "\n";

	file << "AreAllBallsStopped = "
		<< (AreAllBallsStopped() ? "true" : "false") << "\n";

	file << "AreAllEnemiesDefeated = "
		<< (AreAllEnemiesDefeated() ? "true" : "false") << "\n";
	file << "AreAllBallsStopped = " << (AreAllBallsStopped() ? "true" : "false") << "\n";
	file << "AreAllEnemiesDefeated = " << (AreAllEnemiesDefeated() ? "true" : "false") << "\n";
	file << "\n";

	std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	std::vector<EnemyBall*> enemies = GetComponents<EnemyBall>();

	file << "[BallCounts]\n";
	file << "PlayerRunCurrentHp = "
		<< m_PlayerRunStatus.currentHp << "\n";

	file << "PlayerRunMaxHp = "
		<< m_PlayerRunStatus.maxHp << "\n";

	file << "[Deck]\n";
	file << "DrawPile = "
		<< m_PlayerDeck.GetDrawPileCount() << "\n";

	file << "DiscardPile = "
		<< m_PlayerDeck.GetDiscardPileCount() << "\n";

	file << "OfferCount = "
		<< m_PlayerDeck.GetOfferCount() << "\n";

	file << "TotalDeckCount = "
		<< m_PlayerDeck.GetRewardTargetCount() << "\n";

	const PlayerBallData* currentBall = m_PlayerDeck.GetCurrent();
	if (currentBall != nullptr)
	{
		file << "CurrentBallId = "
			<< currentBall->definitionId
			<< "\n";

		file << "CurrentBallAttack = "
			<< currentBall->status.attack
			<< "\n";

		file << "CurrentBallUsed = "
			<< (m_PlayerDeck.IsCurrentUsed() ? "true" : "false")
			<< "\n";
	}
	else
	{
		file << "CurrentBallId = none\n";
	}

	file << "\n";
	file << "PlayerBall = " << players.size() << "\n";
	file << "EnemyBall = " << enemies.size() << "\n";
	file << "\n";

	file << "PlayerMoney = "
		<< m_PlayerRunStatus.money
		<< "\n";

	for (int i = 0; i < static_cast<int>(players.size()); i++)
	{
		WriteBallDebugStatus(file, "PlayerBall", i, players[i]->GetBall());
	}

	for (int i = 0; i < static_cast<int>(enemies.size()); i++)
	{
		WriteBallDebugStatus(file, "EnemyBall", i, enemies[i]->GetBall());
	}
}

// Prediction Shot Rulesを生成する。
ShotRelicRules Game::MakePredictionShotRules(float launchPower) const
{
    ShotRelicRules rules;
    rules.relics = m_OwnedRelics;
    const auto* ball = m_PlayerDeck.GetCurrent();
    if (ball == nullptr) ball = m_PlayerDeck.GetOffer(m_SelectedOfferIndex);
    if (ball != nullptr) rules.ballId = ball->definitionId;
    rules.launchPower = launchPower;
    return rules;
}

// Shot Relic Rulesを取得して保持する。
ShotRelicRules Game::CaptureShotRelicRules() const
{
    auto rules = MakePredictionShotRules(m_CurrentShotLaunchPower);
    // Live build effects require a committed deck ball, preserving shot lifecycle semantics.
    if (!m_PlayerDeck.GetCurrent()) rules.ballId.clear();
    rules.collisionBonus = m_CurrentShotCollisionAttackBonus;
    rules.playerEnemyContacts = m_CurrentShotPlayerEnemyCollisionCount;
    rules.enemyEnemyContacts = m_CurrentShotEnemyEnemyCollisionCount;
    rules.bankReady = m_CurrentShotBankShotReady;
    rules.bankConsumed = m_CurrentShotBankShotConsumed;
    rules.wallContacts = m_CurrentShotWallCollisionCount;
    rules.bounceBonus = m_CurrentShotBounceDamageBonus;
    rules.anchorStopped = m_CurrentShotAnchorStopped;
    rules.launchPower = m_CurrentShotLaunchPower;
    return rules;
}

// Shot Relic Rulesを確定する。
void Game::CommitShotRelicRules(const ShotRelicRules& rules)
{
    m_CurrentShotCollisionAttackBonus = rules.collisionBonus;
    m_CurrentShotPlayerEnemyCollisionCount = rules.playerEnemyContacts;
    m_CurrentShotEnemyEnemyCollisionCount = rules.enemyEnemyContacts;
    m_CurrentShotBankShotReady = rules.bankReady;
    m_CurrentShotBankShotConsumed = rules.bankConsumed;
    m_CurrentShotWallCollisionCount = rules.wallContacts;
    m_CurrentShotBounceDamageBonus = rules.bounceBonus;
    m_CurrentShotAnchorStopped = rules.anchorStopped;
    m_CurrentShotLaunchPower = rules.launchPower;
}
