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
#include "BreakBall.h"
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
	if (!m_RunController.CanRestHeal())
	{
		return false;
	}

	const int hpBefore = m_RunController.Status().currentHp;
	const int configuredHealAmount = m_RunController.GetRestHealAmount();
	if (!m_RunController.RestHeal())
	{
		return false;
	}
	const int actualHealAmount =
		m_RunController.Status().currentHp - hpBefore;
	if (restSite != nullptr)
	{
		restSite->MarkActionUsed();
	}
	RecordBalanceEvent(
		"rest_heal",
		{
			{ "hp_before", hpBefore },
			{ "hp_after", m_RunController.Status().currentHp },
			{ "heal_amount", actualHealAmount },
			{ "configured_heal_amount", configuredHealAmount },
			{ "heal_ratio", m_RunController.RestHealRatio() },
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
	return m_RunController.GetRestHealAmount();
}

// Rest Heal Percentを取得する。
int Game::GetRestHealPercent() const
{
	return m_RunController.GetRestHealPercent();
}

// Clear Reward Upgrade Costを取得する。
int Game::GetClearRewardUpgradeCost(int ballIndex) const
{
	const PlayerBallData* ball =
		m_RunController.Deck().GetRewardTarget(ballIndex);
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
	if (cost < 0 || m_RunController.Status().money < cost)
	{
		return false;
	}

	if (!RestUpgradeBall(ballIndex))
	{
		return false;
	}

	if (!m_RunController.SpendMoney(cost))
	{
		return false;
	}
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
	PlayerBallData* ball = m_RunController.Deck().GetRewardTarget(ballIndex);
	if (ball == nullptr ||
		!ball->CanUpgrade())
	{
		return false;
	}

	const std::uint64_t instanceId = ball->instanceId;
	const std::string ballId = ball->definitionId;
	const int upgradeLevelBefore = ball->upgradeLevel;
	if (!m_RunController.UpgradeBall(ballIndex))
	{
		return false;
	}
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
		m_RunController.Deck().GetCatalogBall(catalogIndex);
	const std::string ballId =
		catalogBall != nullptr ? catalogBall->definitionId : std::string();
	const int moneyBefore = m_RunController.Status().money;
	if (!m_RunController.BuyShopBall(catalogIndex, cost))
	{
		return false;
	}

	PublishGameEvent(BallAcquiredEvent{ ballId });
	RecordBalanceEvent(
		"shop_ball_purchased",
		{
			{ "catalog_index", catalogIndex },
			{ "ball_id", ballId },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController.Status().money },
		});
	return true;
}

// Shop Ballを取り除く。
bool Game::RemoveShopBall(int ballIndex, int cost)
{
	cost = (std::max)(0, cost);
	if (m_RunController.Status().money < cost ||
		m_RunController.Deck().GetRewardTargetCount() <=
			PlayerDeck::MinimumDeckSize)
	{
		return false;
	}

	const PlayerBallData* ball =
		m_RunController.Deck().GetRewardTarget(ballIndex);
	if (ball == nullptr)
	{
		return false;
	}
	const std::uint64_t instanceId = ball->instanceId;
	const std::string ballId = ball->definitionId;
	const int moneyBefore = m_RunController.Status().money;

	if (!m_RunController.RemoveShopBall(ballIndex, cost))
	{
		return false;
	}

	RemoveBalanceAutoPendingBall(instanceId);
	RecordBalanceEvent(
		"shop_ball_removed",
		{
			{ "instance_id", instanceId },
			{ "ball_id", ballId },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController.Status().money },
			{ "deck_count_after", m_RunController.Deck().GetRewardTargetCount() },
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
	const int moneyBefore = m_RunController.Status().money;
	if (!m_RunController.BuyRelic(relicIndex))
	{
		return false;
	}
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
			{ "source", "shop" },
		});
	RecordBalanceEvent(
		"relic_purchased",
		{
			{ "relic_index", relicIndex },
			{ "relic_name", relic->name },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController.Status().money },
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

	if (!m_RunController.GrantRelic(relicIndex))
	{
		return false;
	}
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
	return m_RunController.RollRelicOffers(
		count,
		midBoss,
		m_ProgressionProfile);
}

// Shop Relic Offersの候補を抽選する。
void Game::RollShopRelicOffers()
{
	m_RunController.ShopRelicOffers() = RollRelicOffers(3, false);
}

// Shop Relic Offeredかどうかを判定する。
bool Game::IsShopRelicOffered(int relicIndex) const
{
	return std::find(
		m_RunController.ShopRelicOffers().begin(),
		m_RunController.ShopRelicOffers().end(),
		relicIndex) != m_RunController.ShopRelicOffers().end();
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
	m_RunController.MidBossRelicOffers() = RollRelicOffers(3, true);
	m_SelectedRelicOfferIndex = 0;
	m_IsMidBossRelicSelectionActive = !m_RunController.MidBossRelicOffers().empty();
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
		std::find(m_RunController.MidBossRelicOffers().begin(),
			m_RunController.MidBossRelicOffers().end(), relicIndex) ==
			m_RunController.MidBossRelicOffers().end() ||
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
	return m_RunController.GetOwnedRelicCount();
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

	const PlayerBallData* selectedBall = m_RunController.Deck().GetCurrent();
	if (selectedBall == nullptr)
	{
		selectedBall = m_RunController.Deck().GetOffer(m_SelectedOfferIndex);
	}

	if (selectedBall == nullptr)
	{
		ApplyPlayerRunStatusTo(player);
		return;
	}

	player->SetStatus(selectedBall->status);
	if (auto* render = player->GetGameObject()->GetComponent<BallRenderComponent>())
	{
		const auto color = PlayerBallText::GetColor(*selectedBall);
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

	m_RunController.Status() = NormalizePlayerRunStatus(m_RunController.Status());
	player->SetMaxHP(m_RunController.Status().maxHp);
	player->SetHP(m_RunController.Status().currentHp);
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
			? m_BattleController.GetShotRelicRules().collisionBonus
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
	m_BattleController.ResetShotState(MakePredictionShotRules(0.0f));
	if (player != nullptr)
	{
		ApplyRelicModifiersTo(player);
	}
}

// End Of Shot Relic Effectsを適用する。
void Game::ApplyEndOfShotRelicEffects(PlayerBall* player)
{
	const PlayerBallData* currentBall = m_RunController.Deck().GetCurrent();
	if (player != nullptr && currentBall != nullptr && m_TraceSegmentValid &&
		(m_TraceSegmentPierced || m_TraceDriverExpansionSegment))
	{
		const int overwrittenBefore = m_PierceTraces.overwrittenCount;
		if (PierceTraceRules::AddTrace(
			m_PierceTraces,
			m_TraceSegmentStart,
			player->GetPosition(),
			currentBall->status.traceDurability))
		{
			RecordBalanceEvent("pierce_trace_generated", {
				{ "trace_id", m_PierceTraces.traces.back().id },
				{ "durability", m_PierceTraces.traces.back().durability },
				{ "route_expansion", m_TraceDriverExpansionSegment },
				{ "overwrote_oldest", m_PierceTraces.overwrittenCount > overwrittenBefore },
			});
		}
	}
	m_TraceSegmentValid = false;
	m_TraceSegmentPierced = false;
	m_TraceDriverExpansionSegment = false;

	if (player != nullptr && currentBall != nullptr &&
		currentBall->category == BallCategory::Anchor &&
		m_AnchorContactTarget != nullptr &&
		!m_AnchorContactTarget->IsDefeated())
	{
		const BallStatus& status = currentBall->status;
		const int playerGenerated = AnchorStackRules::AddPlayerStacks(
			m_AnchorStacks, status.anchorPlayerStackGenerate, status.anchorStackMax);
		int enemyGenerated = 0;
		const DirectX::SimpleMath::Vector3 center = m_AnchorContactTarget->GetPosition();
		const float radiusSquared = status.anchorStackRadius * status.anchorStackRadius;
		for (EnemyBall* enemy : GetComponents<EnemyBall>())
		{
			if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed()) continue;
			DirectX::SimpleMath::Vector3 delta = enemy->GetPosition() - center;
			delta.y = 0.0f;
			if (enemy != m_AnchorContactTarget && delta.LengthSquared() > radiusSquared) continue;
			enemyGenerated += AnchorStackRules::AddEnemyStacks(
				enemy->MutableAnchorStacks(), status.anchorEnemyStackGenerate, status.anchorStackMax);
		}
		if (playerGenerated > 0 || enemyGenerated > 0)
		{
			RecordBalanceEvent("anchor_stacks_generated", {
				{ "player_amount", playerGenerated },
				{ "enemy_amount", enemyGenerated },
				{ "radius", status.anchorStackRadius },
			});
		}
	}
	m_AnchorContactTarget = nullptr;

	CushionChargeRules::EndPlayerShot(m_CushionCharges);
	m_CushionBoostConsumedThisShot = false;
	if (player != nullptr)
	{
		m_PlayerShield = (std::max)(0, player->GetStatus().stopShieldAmount);
		if (m_PlayerShield > 0)
		{
			InvalidateDebugCombatForecast("停止時シールド付与");
			RecordBalanceEvent("stop_shield_granted", { { "amount", m_PlayerShield } });
		}
	}

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

	PlayerBallData* currentBall = m_RunController.Deck().GetCurrent();
	if (currentBall != nullptr)
	{
		currentBall->status = updatedStatus;
	}

	m_RunController.Status() = NormalizePlayerRunStatus(m_RunController.Status());
	m_RunController.Status().currentHp = std::clamp(
		player->GetHP(),
		0,
		m_RunController.Status().maxHp
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

// Next Player Ballを準備する。
bool Game::PrepareNextPlayerBall()
{
	DiscardCurrentPlayerBall();

	if (m_RunController.Deck().HasCurrent())
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
	if (m_RunController.IsStageRewardCollected())
	{
		return;
	}

	const int moneyBefore = m_RunController.Status().money;
	if (!m_RunController.CollectStageReward(
		CalculateStageRewardMoney()))
	{
		return;
	}
	const int rewardMoney =
		m_RunController.GetCurrentStageRewardMoney();

	char rewardMessage[128]{};
	sprintf_s(
		rewardMessage,
		UiText::RewardMoneyFormat,
		rewardMoney);
	m_RewardMessage = rewardMessage;
	RecordBalanceEvent(
		"stage_money_reward",
		{
			{ "amount", rewardMoney },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController.Status().money },
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
	if (!m_RunController.Deck().HasCurrent())
	{
		if (!m_RunController.Deck().SelectOffer(
			m_SelectedOfferIndex,
			m_SelectedHoldIndex))
		{
			return;
		}
	}

	CushionChargeRules::BeginPlayerShot(m_CushionCharges);
	m_CushionBoostConsumedThisShot = false;
	m_CushionStrongUsesThisShot = 0;
	m_SynergyDamageBonusThisShot = 0;
	m_PierceTraceUse = {};
	m_PiercedEnemiesThisShot.clear();
	m_TraceSegmentStart = player != nullptr
		? player->GetPosition()
		: DirectX::SimpleMath::Vector3::Zero;
	m_TraceSegmentValid = player != nullptr;
	m_TraceSegmentPierced = false;
	m_TraceDriverExpansionArmed = false;
	m_TraceDriverExpansionSegment = false;
	m_TracePierceBenefitActive = false;
	m_HeavyFinisherConsumedThisShot = false;
	m_AnchorFinisherTriggeredThisShot = false;
	m_AnchorContactTarget = nullptr;
	m_PlayerShield = 0;

	if (player != nullptr)
	{
		CapturePlayerStatusFrom(player);
	}

	const PlayerBallData* currentBall =
		m_RunController.Deck().GetCurrent();
	if (player != nullptr && currentBall != nullptr)
	{
		auto shotRules = m_BattleController.GetShotRelicRules();
		shotRules.launchPower = player->GetVelocity().Length();
		m_BattleController.SetShotRelicRules(shotRules);
		PublishGameEvent(ShotFiredEvent{ currentBall->definitionId });

		const DirectX::SimpleMath::Vector3 position =
			player->GetPosition();
		const DirectX::SimpleMath::Vector3 velocity =
			player->GetVelocity();
		const std::vector<EnemyBall*> enemies =
			GetComponents<EnemyBall>();
		nlohmann::json offers = nlohmann::json::array();
		for (int index = 0;
			index < m_RunController.Deck().GetOfferCount();
			index++)
		{
			const PlayerBallData* offer =
				m_RunController.Deck().GetOffer(index);
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
					{ "held", m_RunController.Deck().WasHeldOffer(index) },
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

	m_RunController.Deck().MarkCurrentUsed();
	m_BattleController.NotifyShotFired();
}

// Player Wall Collisionを通知する。
void Game::NotifyPlayerWallCollision(
	int cushionRegion,
	const DirectX::SimpleMath::Vector3& playerPosition,
	DirectX::SimpleMath::Vector3& reflectedVelocity)
{
    auto rules = CaptureShotRelicRules();
    rules.Wall();
    CommitShotRelicRules(rules);
	const PlayerBallData* currentBall = m_RunController.Deck().GetCurrent();
	if (currentBall == nullptr) return;
	const BallStatus& status = currentBall->status;
	if (m_TraceSegmentValid && (m_TraceSegmentPierced || m_TraceDriverExpansionSegment))
	{
		const int overwrittenBefore = m_PierceTraces.overwrittenCount;
		if (PierceTraceRules::AddTrace(
			m_PierceTraces, m_TraceSegmentStart, playerPosition, status.traceDurability))
		{
			RecordBalanceEvent("pierce_trace_generated", {
				{ "trace_id", m_PierceTraces.traces.back().id },
				{ "durability", m_PierceTraces.traces.back().durability },
				{ "route_expansion", m_TraceDriverExpansionSegment },
				{ "overwrote_oldest", m_PierceTraces.overwrittenCount > overwrittenBefore },
			});
		}
	}
	m_TraceSegmentStart = playerPosition;
	m_TraceSegmentValid = true;
	m_TraceSegmentPierced = false;
	m_TraceDriverExpansionSegment =
		currentBall->definitionId == "player_trace_driver" &&
		m_TraceDriverExpansionArmed;
	m_TraceDriverExpansionArmed = false;

	const bool bounceCategory = currentBall->category == BallCategory::Bounce;
	const bool finisher = currentBall->definitionId == "player_ricochet_finisher";
	const auto result = CushionChargeRules::ApplyStackContact(
		m_CushionCharges,
		cushionRegion,
		bounceCategory ? status.cushionStackGenerateAmount : 0,
		status.cushionMaxStack,
		status.cushionStackConsumeAmount,
		bounceCategory,
		finisher,
		status.cushionChargeSpeedMultiplier,
		status.cushionNonBounceSpeedMultiplier,
		status.cushionBounceAttackBonus,
		finisher ? status.ricochetFinisherBonusPerUse : 0,
		m_CushionBoostConsumedThisShot,
		m_CushionStrongUsesThisShot,
		reflectedVelocity);
	if (result.generated > 0)
		RecordBalanceEvent("cushion_stacks_generated", {
			{ "region", cushionRegion }, { "amount", result.generated } });
	if (result.consumed > 0)
	{
		m_SynergyDamageBonusThisShot += result.damageBonus;
		RecordBalanceEvent("cushion_stacks_consumed", {
			{ "region", cushionRegion },
			{ "amount", result.consumed },
			{ "category", BallCategoryId(currentBall->category) },
			{ "strong", result.kind == CushionChargeRules::UseKind::Strong },
			{ "strong_uses_this_shot", m_CushionStrongUsesThisShot },
		});
	}
}

void Game::NotifyEnemyEnemySynergyCollision(
	EnemyBall* first,
	EnemyBall* second,
	const DirectX::SimpleMath::Vector3& firstVelocityBefore,
	const DirectX::SimpleMath::Vector3& secondVelocityBefore)
{
	if (first == nullptr || second == nullptr) return;
	const PlayerBallData* currentBall = m_RunController.Deck().GetCurrent();
	if (HeavyCollisionRules::RecordEnemyEnemyCollision(
		m_HeavyCollisions,
		currentBall != nullptr && currentBall->category == BallCategory::Heavy))
	{
		RecordBalanceEvent("heavy_collision_recorded", {
			{ "count", m_HeavyCollisions.collisionCount },
			{ "ball_id", currentBall->definitionId },
			{ "category", BallCategoryId(currentBall->category) },
		});
	}

	int* source = nullptr;
	int* target = nullptr;
	if (first->GetAnchorStacks() > 0 && second->GetAnchorStacks() == 0)
	{
		source = &first->MutableAnchorStacks();
		target = &second->MutableAnchorStacks();
	}
	else if (second->GetAnchorStacks() > 0 && first->GetAnchorStacks() == 0)
	{
		source = &second->MutableAnchorStacks();
		target = &first->MutableAnchorStacks();
	}
	else if (first->GetAnchorStacks() > 0 && second->GetAnchorStacks() > 0)
	{
		const bool firstIsSource = firstVelocityBefore.LengthSquared() >=
			secondVelocityBefore.LengthSquared();
		source = firstIsSource
			? &first->MutableAnchorStacks()
			: &second->MutableAnchorStacks();
		target = firstIsSource
			? &second->MutableAnchorStacks()
			: &first->MutableAnchorStacks();
	}
	if (source != nullptr && target != nullptr)
	{
		const int sourceBefore = *source;
		const int moved = AnchorStackRules::TransferEnemyToEnemy(
			*source, *target, 9999);
		RecordBalanceEvent("anchor_enemy_transfer", {
			{ "direction", "enemy_to_enemy" },
			{ "amount", moved },
			{ "source_before", sourceBefore },
			{ "source_after", *source },
		});
	}
}

void Game::NotifyPlayerPiercedEnemy(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& playerPosition,
	bool refracted)
{
	if (enemy == nullptr) return;
	m_PiercedEnemiesThisShot.insert(enemy);
	m_TraceSegmentPierced = true;
	if (!m_TraceSegmentValid)
	{
		m_TraceSegmentStart = playerPosition;
		m_TraceSegmentValid = true;
	}
	if (!refracted) return;

	const PlayerBallData* currentBall = m_RunController.Deck().GetCurrent();
	if (currentBall != nullptr)
	{
		const int overwrittenBefore = m_PierceTraces.overwrittenCount;
		if (PierceTraceRules::AddTrace(
			m_PierceTraces, m_TraceSegmentStart, playerPosition,
			currentBall->status.traceDurability))
		{
			RecordBalanceEvent("pierce_trace_generated", {
				{ "trace_id", m_PierceTraces.traces.back().id },
				{ "durability", m_PierceTraces.traces.back().durability },
				{ "route_expansion", false },
				{ "overwrote_oldest", m_PierceTraces.overwrittenCount > overwrittenBefore },
			});
		}
	}
	m_TraceSegmentStart = playerPosition;
	m_TraceSegmentPierced = false;
}

void Game::NotifyPlayerDirectionChange(
	const DirectX::SimpleMath::Vector3& playerPosition)
{
	const PlayerBallData* currentBall = m_RunController.Deck().GetCurrent();
	if (currentBall == nullptr) return;
	if (m_TraceSegmentValid && (m_TraceSegmentPierced || m_TraceDriverExpansionSegment))
	{
		const int overwrittenBefore = m_PierceTraces.overwrittenCount;
		if (PierceTraceRules::AddTrace(
			m_PierceTraces, m_TraceSegmentStart, playerPosition,
			currentBall->status.traceDurability))
		{
			RecordBalanceEvent("pierce_trace_generated", {
				{ "trace_id", m_PierceTraces.traces.back().id },
				{ "durability", m_PierceTraces.traces.back().durability },
				{ "route_expansion", m_TraceDriverExpansionSegment },
				{ "overwrote_oldest", m_PierceTraces.overwrittenCount > overwrittenBefore },
			});
		}
	}
	m_TraceSegmentStart = playerPosition;
	m_TraceSegmentValid = true;
	m_TraceSegmentPierced = false;
	m_TraceDriverExpansionSegment =
		currentBall->definitionId == "player_trace_driver" &&
		m_TraceDriverExpansionArmed;
	m_TraceDriverExpansionArmed = false;
}

void Game::NotifyTraceMovement(
	BallComponent& ball,
	const DirectX::SimpleMath::Vector3& from,
	const DirectX::SimpleMath::Vector3& to)
{
	if (m_BattleController.GetState() != BattleState::BallsMoving ||
		m_PierceTraces.traces.empty()) return;
	GameObject* owner = ball.GetGameObject();
	if (owner == nullptr) return;
	const bool playerMovement = owner->GetComponent<PlayerBall>() != nullptr;
	const bool breakMovement = owner->GetComponent<BreakBall>() != nullptr;
	if (!playerMovement && !breakMovement) return;

	const PlayerBallData* currentBall = m_RunController.Deck().GetCurrent();
	if (currentBall == nullptr) return;
	const BallStatus& status = currentBall->status;
	const bool strongUse = playerMovement && currentBall->category == BallCategory::Pierce;
	PierceTraceRules::UseConfig config;
	config.angleToleranceDegrees = status.traceUseAngleTolerance;
	config.requiredDistance = status.traceUseDistance;
	config.width = status.traceWidth;
	config.nonPierceSpeedMultiplier = status.traceNonPierceSpeedMultiplier;
	const auto result = PierceTraceRules::AccumulateMovement(
		m_PierceTraces,
		m_PierceTraceUse,
		from,
		to,
		config,
		strongUse,
		ball.GetMutableVelocity());
	if (!result.activated) return;
	if (strongUse)
	{
		m_TracePierceBenefitActive = true;
		if (currentBall->definitionId == "player_trace_driver")
			m_TraceDriverExpansionArmed = true;
	}
	RecordBalanceEvent("pierce_trace_used", {
		{ "trace_id", result.traceId },
		{ "remaining_durability", result.remainingDurability },
		{ "strong", strongUse },
		{ "break_ball", breakMovement },
	});
}

int Game::NotifyPlayerEnemySynergyCollision(EnemyBall* enemy)
{
	if (enemy == nullptr) return m_SynergyDamageBonusThisShot;
	const PlayerBallData* currentBall = m_RunController.Deck().GetCurrent();
	if (currentBall == nullptr) return m_SynergyDamageBonusThisShot;
	const BallStatus& status = currentBall->status;
	if (currentBall->category == BallCategory::Anchor)
		m_AnchorContactTarget = enemy;

	if (enemy->GetAnchorStacks() > 0)
	{
		const int enemyBefore = enemy->GetAnchorStacks();
		const int moved = AnchorStackRules::TransferEnemyToPlayer(
			enemy->MutableAnchorStacks(), m_AnchorStacks, status.anchorStackMax);
		RecordBalanceEvent("anchor_enemy_transfer", {
			{ "direction", "enemy_to_player" },
			{ "amount", moved },
			{ "enemy_before", enemyBefore },
			{ "enemy_after", enemy->GetAnchorStacks() },
			{ "player_after", m_AnchorStacks.playerStacks },
		});
	}

	int bonus = m_SynergyDamageBonusThisShot;
	if (currentBall->definitionId == "player_pierce_finisher" &&
		m_PierceTraceUse.usedAnyTrace)
	{
		const int uniqueEnemies = static_cast<int>(m_PiercedEnemiesThisShot.size());
		bonus += status.pierceFinisherBaseBonus +
			(std::max)(0, uniqueEnemies - 1) * status.pierceFinisherMultiTargetBonus;
		RecordBalanceEvent("pierce_finisher_hit", {
			{ "unique_enemies", uniqueEnemies }, { "damage_bonus", bonus },
		});
	}

	if (currentBall->definitionId == "player_anchor_finisher" &&
		!m_AnchorFinisherTriggeredThisShot)
	{
		m_AnchorFinisherTriggeredThisShot = true;
		const int consumed = AnchorStackRules::ConsumePlayerStacks(
			m_AnchorStacks, status.anchorFinisherStackConsume);
		const int finisherBonus = consumed * status.anchorFinisherDamagePerStack;
		bonus += finisherBonus;
		int aoeHits = 0;
		if (consumed >= status.anchorFinisherAoeThreshold &&
			status.anchorFinisherAoeRadius > 0.0f)
		{
			const float radiusSquared = status.anchorFinisherAoeRadius * status.anchorFinisherAoeRadius;
			const int aoeDamage = finisherBonus / 2;
			for (EnemyBall* nearby : GetComponents<EnemyBall>())
			{
				if (nearby == nullptr || nearby == enemy || nearby->IsDefeated() || nearby->IsPocketed()) continue;
				DirectX::SimpleMath::Vector3 delta = nearby->GetPosition() - enemy->GetPosition();
				delta.y = 0.0f;
				if (delta.LengthSquared() > radiusSquared) continue;
				const int hpBefore = nearby->GetHP();
				nearby->TakeDamage(aoeDamage);
				const int applied = (std::max)(0, hpBefore - nearby->GetHP());
				if (applied > 0)
				{
					++aoeHits;
					NotifyCombatFeedback(nearby->GetPosition(), applied, nearby->IsDefeated(), true);
				}
			}
		}
		RecordBalanceEvent("anchor_finisher_triggered", {
			{ "consumed", consumed }, { "damage_bonus", finisherBonus },
			{ "aoe_hits", aoeHits }, { "player_stacks_after", m_AnchorStacks.playerStacks },
		});
	}
	return bonus;
}

int Game::AbsorbPlayerShieldDamage(int damage)
{
	const int incoming = (std::max)(0, damage);
	const int absorbed = (std::min)(m_PlayerShield, incoming);
	m_PlayerShield -= absorbed;
	if (absorbed > 0)
	{
		InvalidateDebugCombatForecast("停止時シールド消費");
		RecordBalanceEvent(
			"stop_shield_absorbed",
			{ { "absorbed", absorbed }, { "remaining", m_PlayerShield } });
	}
	return incoming - absorbed;
}

void Game::NotifyPlayerChainImpact(
	EnemyBall* directTarget,
	int attackDamage,
	float radius)
{
	if (directTarget == nullptr)
	{
		return;
	}
	NotifyPlayerChainImpact(
		directTarget->GetPosition(), directTarget, attackDamage, radius);
}

void Game::NotifyPlayerChainImpact(
	const DirectX::SimpleMath::Vector3& center,
	const EnemyBall* excludedTarget,
	int attackDamage,
	float radius)
{
	const PlayerBallData* currentBall = m_RunController.Deck().GetCurrent();
	if (currentBall != nullptr &&
		currentBall->definitionId == "player_chain_impact" &&
		!m_HeavyFinisherConsumedThisShot)
	{
		m_HeavyFinisherConsumedThisShot = true;
		const auto finisher = HeavyCollisionRules::ConsumeForFinisher(
			m_HeavyCollisions,
			currentBall->status.heavyFinisherDamagePerCollision,
			currentBall->status.heavyCollisionConsumeAmount);
		attackDamage += finisher.bonusDamage;
		RecordBalanceEvent("heavy_finisher_triggered", {
			{ "referenced_collision_count", finisher.referenced },
			{ "consumed_collision_count", finisher.consumed },
			{ "damage_bonus", finisher.bonusDamage },
			{ "remaining_collision_count", m_HeavyCollisions.collisionCount },
		});
	}
	if (attackDamage <= 0 || radius <= 0.0f) return;
	const float radiusSquared = radius * radius;
	int hitCount = 0;
	int totalDamage = 0;
	for (EnemyBall* enemy : GetComponents<EnemyBall>())
	{
		if (enemy == nullptr || enemy == excludedTarget || enemy->IsDefeated() ||
			enemy->IsPocketed())
		{
			continue;
		}
		DirectX::SimpleMath::Vector3 offset = enemy->GetPosition() - center;
		offset.y = 0.0f;
		if (offset.LengthSquared() > radiusSquared)
		{
			continue;
		}
		const int hpBefore = enemy->GetHP();
		const DirectX::SimpleMath::Vector3 feedbackPosition = enemy->GetPosition();
		enemy->TakeDamage(attackDamage);
		const int applied = (std::max)(0, hpBefore - enemy->GetHP());
		if (applied <= 0 && !enemy->IsDefeated())
		{
			continue;
		}
		++hitCount;
		totalDamage += applied;
		NotifyCombatFeedback(feedbackPosition, applied, enemy->IsDefeated(), true);
	}
	if (hitCount > 0)
	{
		RecordBalanceEvent(
			"chain_impact_triggered",
			{ { "hit_count", hitCount }, { "damage", totalDamage },
			  { "radius", radius }, { "attack_damage", attackDamage } });
	}
	PublishGameEvent(ChainImpactEvent{ center, radius, hitCount });
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
void Game::NotifyAnchorStopped(EnemyBall* directTarget)
{
    auto rules = CaptureShotRelicRules();
    rules.Anchor();
    CommitShotRelicRules(rules);
	if (directTarget != nullptr) m_AnchorContactTarget = directTarget;
}

// Pierce Maximum Usesを取得する。
int Game::GetPierceMaximumUses() const
{
	const PlayerBallData* ball = m_RunController.Deck().GetCurrent();
	if (ball == nullptr) ball = m_RunController.Deck().GetOffer(m_SelectedOfferIndex);
	if (ball == nullptr) return 0;
	const int base = BallMechanics::PierceUses(ball->status,
		HasRelic(RelicType::PierceBallCharger) && ball->definitionId == "player_pierce");
	return base + (m_TracePierceBenefitActive ? ball->status.tracePierceMaxUsesBonus : 0);
}

// Pierce Speed Retentionを取得する。
float Game::GetPierceSpeedRetention() const
{
	const PlayerBallData* ball = m_RunController.Deck().GetCurrent();
	if (ball == nullptr) ball = m_RunController.Deck().GetOffer(m_SelectedOfferIndex);
	if (ball == nullptr) return 0.75f;
	const float base = BallMechanics::PierceRetention(ball->status,
		HasRelic(RelicType::PierceBallCharger) && ball->definitionId == "player_pierce");
	return std::clamp(base + (m_TracePierceBenefitActive
		? ball->status.tracePierceSpeedRetentionBonus : 0.0f), 0.0f, 1.0f);
}

// Enemy Defeatedを通知する。
void Game::NotifyEnemyDefeated(const std::string& enemyId)
{
	if (!HasRelic(RelicType::BountyList) ||
		!m_BattleController.ClaimBountyReward())
	{
		return;
	}
	const int moneyBefore = m_RunController.Status().money;
	m_RunController.AddMoney(kBountyRewardMoney);
	RecordBalanceEvent(
		"bounty_reward",
		{
			{ "enemy_id", enemyId },
			{ "amount", kBountyRewardMoney },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController.Status().money },
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
					!m_BalanceAutoPlayer.IsEnabled())
				{
					m_GamePresentation->OnCombatFeedback(
						value.worldPosition,
						value.damage,
						value.defeated,
						value.enemyEnemyCollision);
				}
			}
			else if constexpr (std::is_same_v<EventType, ChainImpactEvent>)
			{
				if (m_GamePresentation != nullptr &&
					!m_BalanceAutoPlayer.IsEnabled())
				{
					m_GamePresentation->OnChainImpact(
						value.worldPosition,
						value.radius,
						value.hitCount);
				}
			}
			else if constexpr (std::is_same_v<EventType, PocketFeedbackEvent>)
			{
				if (!value.playerPocket && value.finisher)
				{
					m_RunStatistics.RecordEnemyDamage(value.damage);
				}
				if (m_GamePresentation != nullptr &&
					!m_BalanceAutoPlayer.IsEnabled())
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
					!m_BalanceAutoPlayer.IsEnabled())
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

// Enemy Dataへ基準難易度と進行度の固定補正を適用する。
void Game::ApplyEnemyDifficultyScaling(
	EnemyData& enemyData) const
{
	m_DynamicBalanceController.ApplyToEnemyData(
		enemyData,
		m_BaselineEnemyHpMultiplier,
		m_BaselineEnemyAttackDelta,
		m_RunController.Status().progress,
		m_ActiveAscension,
		IsDebugMode());
}
// Balance Auto Full Hp Enemy Survivedを通知する。
void Game::NotifyBalanceAutoFullHpEnemySurvived()
{
	const PlayerBallData* currentBall =
		m_RunController.Deck().GetCurrent();
	m_BalanceAutoPlayer.NotifyFullHpEnemySurvived(currentBall);
}

// Current Player Ballを破棄する。
void Game::DiscardCurrentPlayerBall()
{
	if (!m_RunController.Deck().HasCurrent())
	{
		m_RunController.Deck().ClearCurrentUsed();
		return;
	}

	if (!m_RunController.Deck().IsCurrentUsed())
	{
		return;
	}

	std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	if (!players.empty() && players[0] != nullptr)
	{
		CapturePlayerStatusFrom(players[0]);
	}

	m_RunController.Deck().DiscardCurrentIfUsed();
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
		<< m_RunController.Status().currentHp << "\n";

	file << "PlayerRunMaxHp = "
		<< m_RunController.Status().maxHp << "\n";

	file << "[Deck]\n";
	file << "DrawPile = "
		<< m_RunController.Deck().GetDrawPileCount() << "\n";

	file << "DiscardPile = "
		<< m_RunController.Deck().GetDiscardPileCount() << "\n";

	file << "OfferCount = "
		<< m_RunController.Deck().GetOfferCount() << "\n";

	file << "TotalDeckCount = "
		<< m_RunController.Deck().GetRewardTargetCount() << "\n";

	const PlayerBallData* currentBall = m_RunController.Deck().GetCurrent();
	if (currentBall != nullptr)
	{
		file << "CurrentBallId = "
			<< currentBall->definitionId
			<< "\n";

		file << "CurrentBallAttack = "
			<< currentBall->status.attack
			<< "\n";

		file << "CurrentBallUsed = "
			<< (m_RunController.Deck().IsCurrentUsed() ? "true" : "false")
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
		<< m_RunController.Status().money
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
    rules.relics = m_RunController.Relics();
    const auto* ball = m_RunController.Deck().GetCurrent();
    if (ball == nullptr) ball = m_RunController.Deck().GetOffer(m_SelectedOfferIndex);
    if (ball != nullptr) rules.ballId = ball->definitionId;
    rules.launchPower = launchPower;
    return rules;
}

// Shot Relic Rulesを取得して保持する。
ShotRelicRules Game::CaptureShotRelicRules() const
{
    auto rules = m_BattleController.GetShotRelicRules();
    // Live build effects require a committed deck ball, preserving shot lifecycle semantics.
    if (!m_RunController.Deck().GetCurrent()) rules.ballId.clear();
    return rules;
}

// Shot Relic Rulesを確定する。
void Game::CommitShotRelicRules(const ShotRelicRules& rules)
{
    m_BattleController.SetShotRelicRules(rules);
}
