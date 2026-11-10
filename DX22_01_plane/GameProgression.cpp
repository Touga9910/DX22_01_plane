#include "Game.h"
#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameBattleState.h"
#include "GameRewardState.h"
#include "GameBallSelectionState.h"
#include "GameRunDefaults.h"
#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "ShotRelicRules.h"
#include "BallStatusJson.h"
#include "GameObject.h"
#include "PlayerBallText.h"
#include "BallMechanics.h"
#include "json/json.hpp"
#include "RunProgressController.h"

#pragma execution_character_set("utf-8")
#include "Renderer.h"
#include "BalanceLogger.h"
#include "GameMcpBridge.h"
#include "GamePresentation.h"
#include "AudioFeedback.h"
#include "GameShotPrediction.h"
#include "BattleScene.h"
#include "ResultScene.h"
#include "RestSiteScene.h"
#include "ShopScene.h"
#include "StageSelectScene.h"
#include "TitleScene.h"
#include "BallPhysicsComponent.h"
#include "BallFactory.h"
#include "input.h"

#include "PlayerBall.h"  // DrawImGui呼び出しに必要
#include "EnemyBall.h"   // DrawImGui呼び出しに必要
#include "EnemyAttackComponent.h"
#include "BallComponent.h"
#include "BreakBall.h"
#include "PierceMinion.h"
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

int Game::GetAreaProgress() const
{
	return m_RunController->Progress().GetAreaProgress();
}

const RunMap& Game::GetRunMap() const
{
	return m_RunController->Progress().GetMap();
}

bool Game::ChooseMapNode(int nodeId)
{
	return m_RunController->Progress().ChooseNode(nodeId);
}

RunPhase Game::GetRunPhase() const
{
	return m_RunController->Progress().GetPhase();
}

bool Game::IsBossPreparation() const
{
	return m_RunController->Progress().IsBossPreparation();
}

bool Game::IsFinalBossRoute() const
{
	return m_RunController->Progress().IsFinalBossReady();
}

// Rest Heal の処理を実行する。
bool Game::RestHeal()
{
	RestSiteScene* restSite =
		dynamic_cast<RestSiteScene*>(GetCurrentScene());
	if (restSite != nullptr && restSite->HasUsedAction())
	{
		return false;
	}
	if (!m_RunController->CanRestHeal())
	{
		return false;
	}

	const int hpBefore = m_RunController->Status().currentHp;
	const int configuredHealAmount = m_RunController->GetRestHealAmount();
	if (!m_RunController->RestHeal())
	{
		return false;
	}
	const int actualHealAmount =
		m_RunController->Status().currentHp - hpBefore;
	if (restSite != nullptr)
	{
		restSite->MarkActionUsed();
	}
	RecordBalanceEvent(
		"rest_heal",
		{
			{ "hp_before", hpBefore },
			{ "hp_after", m_RunController->Status().currentHp },
			{ "heal_amount", actualHealAmount },
			{ "configured_heal_amount", configuredHealAmount },
			{ "heal_ratio", m_RunController->RestHealRatio() },
			{ "boss_preparation", IsBossPreparation() },
			{ "capped_at_max_hp",
				actualHealAmount < configuredHealAmount },
			{ "source_scene", GetSceneDebugName(GetCurrentScene()) },
		});
	return true;
}

// Rest Heal Amountを取得する。
int Game::GetRestHealAmount() const
{
	return m_RunController->GetRestHealAmount();
}

// Rest Heal Percentを取得する。
int Game::GetRestHealPercent() const
{
	return m_RunController->GetRestHealPercent();
}

// Clear Reward Upgrade Costを取得する。
int Game::GetClearRewardUpgradeCost(int ballIndex) const
{
	const PlayerBallData* ball =
		m_RunController->Deck().GetRewardTarget(ballIndex);
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
	if (!IsClearRewardActive())
	{
		return false;
	}

	const int cost = GetClearRewardUpgradeCost(ballIndex);
	if (cost < 0 || m_RunController->Status().money < cost)
	{
		return false;
	}

	if (!RestUpgradeBall(ballIndex))
	{
		return false;
	}

	if (!m_RunController->SpendMoney(cost))
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
		dynamic_cast<RestSiteScene*>(GetCurrentScene());
	if (restSite != nullptr && restSite->HasUsedAction())
	{
		return false;
	}
	PlayerBallData* ball = m_RunController->Deck().GetRewardTarget(ballIndex);
	if (ball == nullptr ||
		!ball->CanUpgrade())
	{
		return false;
	}

	const std::uint64_t instanceId = ball->instanceId;
	const std::string ballId = ball->definitionId;
	const int upgradeLevelBefore = ball->upgradeLevel;
	if (!m_RunController->UpgradeBall(ballIndex))
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
			{ "status_after", WritePlayerBallStatus(ball->status) },
			{ "source_scene", GetSceneDebugName(GetCurrentScene()) },
		});
	return true;
}

// Shop Ballを購入する。
bool Game::BuyShopBall(int catalogIndex, int cost)
{
	cost = (std::max)(0, cost);
	const PlayerBallData* catalogBall =
		m_RunController->Deck().GetCatalogBall(catalogIndex);
	const std::string ballId =
		catalogBall != nullptr ? catalogBall->definitionId : std::string();
	const int moneyBefore = m_RunController->Status().money;
	if (!m_RunController->BuyShopBall(catalogIndex, cost))
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
			{ "money_after", m_RunController->Status().money },
		});
	return true;
}

// Shop入店中に提示するBallを、重複なしで最大3枠抽選する。
void Game::RollShopBallOffers()
{
	m_RunController->ShopBallOffers() =
		m_RunController->Deck().RollDistinctCatalogOfferIndices(kShopBallOfferSize);
}

// Shopの提示位置から対応するCatalog Ballを購入する。
bool Game::BuyShopBallOffer(int offerIndex, int cost)
{
	return BuyShopBall(GetShopBallOfferCatalogIndex(offerIndex), cost);
}

// Shop Ballを取り除く。
bool Game::RemoveShopBall(int ballIndex, int cost)
{
	cost = (std::max)(0, cost);
	if (m_RunController->Status().money < cost ||
		m_RunController->Deck().GetRewardTargetCount() <=
			PlayerDeck::MinimumDeckSize)
	{
		return false;
	}

	const PlayerBallData* ball =
		m_RunController->Deck().GetRewardTarget(ballIndex);
	if (ball == nullptr)
	{
		return false;
	}
	const std::uint64_t instanceId = ball->instanceId;
	const std::string ballId = ball->definitionId;
	const int moneyBefore = m_RunController->Status().money;

	if (!m_RunController->RemoveShopBall(ballIndex, cost))
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
			{ "money_after", m_RunController->Status().money },
			{ "deck_count_after", m_RunController->Deck().GetRewardTargetCount() },
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
	const int moneyBefore = m_RunController->Status().money;
	if (!m_RunController->BuyRelic(relicIndex))
	{
		return false;
	}
	for (PlayerBall* player : GameRuntime::World().GetComponents<PlayerBall>())
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
			{ "money_after", m_RunController->Status().money },
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

	if (!m_RunController->GrantRelic(relicIndex))
	{
		return false;
	}
	for (PlayerBall* player : GameRuntime::World().GetComponents<PlayerBall>())
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
	return m_RunController->RollRelicOffers(
		count,
		midBoss,
		m_PersistenceRuntime->Profile());
}

// Shop Relic Offersの候補を抽選する。
void Game::RollShopRelicOffers()
{
	m_RunController->ShopRelicOffers() = RollRelicOffers(3, false);
}

// Shop Relic Offeredかどうかを判定する。
bool Game::IsShopRelicOffered(int relicIndex) const
{
	return std::find(
		m_RunController->ShopRelicOffers().begin(),
		m_RunController->ShopRelicOffers().end(),
		relicIndex) != m_RunController->ShopRelicOffers().end();
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
	m_RunController->MidBossRelicOffers() = RollRelicOffers(3, true);
	m_RewardState->StartMidBossRelicSelection(
		!m_RunController->MidBossRelicOffers().empty());
}

// Mid Boss Relic Offerを獲得する。
bool Game::AcquireMidBossRelicOffer(int offerIndex)
{
	return AcquireMidBossRelic(GetMidBossRelicOfferCatalogIndex(offerIndex));
}

// Mid Boss Relicを獲得する。
bool Game::AcquireMidBossRelic(int relicIndex)
{
	if (!m_RewardState->IsMidBossRelicSelectionActive() ||
		std::find(m_RunController->MidBossRelicOffers().begin(),
			m_RunController->MidBossRelicOffers().end(), relicIndex) ==
			m_RunController->MidBossRelicOffers().end() ||
		!GrantRelic(relicIndex, "midboss"))
	{
		return false;
	}
	m_RewardState->FinishMidBossRelicSelection();
	return true;
}

// Owned Relic Countを取得する。
int Game::GetOwnedRelicCount() const
{
	return m_RunController->GetOwnedRelicCount();
}

// Relic Attack Bonusを取得する。
int Game::GetRelicAttackBonus() const
{
	return HasRelic(RelicType::AllBallAttackUp) ? 1 : 0;
}

// Effective Player Ball Attackを取得する。
int Game::GetEffectivePlayerBallAttack(
	const PlayerBallData* ball) const
{
	return ball == nullptr
		? 0
		: ball->status.attack + GetRelicAttackBonus();
}

// Player Status Toを適用する。
void Game::ApplyPlayerStatusTo(PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	const PlayerBallData* selectedBall = m_RunController->Deck().GetCurrent();
	if (selectedBall == nullptr)
	{
		selectedBall = m_RunController->Deck().GetOffer(
			GetSelectedBallOfferIndex());
	}

	if (selectedBall == nullptr)
	{
		ApplyPlayerRunStatusTo(player);
		return;
	}

	BallStatus playerStatus = selectedBall->status;
	playerStatus.defense = 0;
	player->SetStatus(playerStatus);
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

	m_RunController->Status() = NormalizePlayerRunStatus(m_RunController->Status());
	player->SetMaxHP(m_RunController->Status().maxHp);
	player->SetHP(m_RunController->Status().currentHp);
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

	const int collisionBonus =
		HasRelic(RelicType::CollisionAttackUp)
			? m_BattleState->controller.GetShotRelicRules().collisionBonus
			: 0;
	player->GetBall()->SetCombatModifiers(
		GetRelicAttackBonus() + collisionBonus,
		0);
}

// Shot Relic Stateを初期状態へ戻す。
void Game::ResetShotRelicState(PlayerBall* player)
{
	m_BattleState->controller.ResetShotState(MakePredictionShotRules(0.0f));
	if (player != nullptr)
	{
		ApplyRelicModifiersTo(player);
	}
}

// End Of Shot Relic Effectsを適用する。
void Game::ApplyEndOfShotRelicEffects(PlayerBall* player)
{
	for (PierceMinion* minion :
		GameRuntime::World().GetComponents<PierceMinion>())
	{
		if (minion != nullptr) minion->Destroy();
	}
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	const int oneShotAnchorBonus = currentBall != nullptr &&
		currentBall->category == BallCategory::Anchor
		? m_BattleState->anchorStacks.nextAnchorGrantBonus
		: 0;
	if (currentBall != nullptr && currentBall->category == BallCategory::Anchor)
		m_BattleState->anchorStacks.nextAnchorGrantBonus = 0;
	const int heavyStacksBeforeEnd =
		m_BattleState->heavyCollisions.stackCount;
	const auto heavyGain = HeavyCollisionRules::EndShot(
		m_BattleState->heavyCollisions);
	if (heavyGain.requested > 0)
	{
		RecordBalanceEvent("heavy_distance_stacks_generated", {
			{ "amount", heavyGain.granted },
			{ "uncapped_amount", heavyGain.requested },
			{ "granted_amount", heavyGain.granted },
			{ "limited_amount", heavyGain.discardedByTurnLimit },
			{ "storage_limited_amount", heavyGain.discardedByStorageLimit },
			{ "gain_limit", heavyGain.turnLimit },
			{ "turn_gained_after", m_BattleState->heavyCollisions.gainedThisTurn },
			{ "stack_after", m_BattleState->heavyCollisions.stackCount },
		});
	}
	NotifyAxisResourceChanged(
		AxisResourceKind::Heavy,
		AxisResourceOwner::Player,
		heavyStacksBeforeEnd,
		m_BattleState->heavyCollisions.stackCount,
		AxisResourceChangeReason::TravelDistance,
		currentBall != nullptr ? currentBall->definitionId : std::string(),
		false,
		player != nullptr
			? player->GetPosition()
			: DirectX::SimpleMath::Vector3::Zero);

	if (player != nullptr && currentBall != nullptr &&
		currentBall->status.anchorEnemyStackGenerate > 0 &&
		m_BattleState->anchorContactTarget != nullptr &&
		!m_BattleState->anchorContactTarget->IsDefeated())
	{
		const BallStatus& status = currentBall->status;
		const int grant = AnchorStackRules::CalculateEnemyGrant(
			status.anchorEnemyStackGenerate,
			m_BattleState->anchorStacks.playerStacks,
			status.anchorPlayerStacksPerBonus,
			oneShotAnchorBonus);
		const int enemyStacksBefore =
			m_BattleState->anchorContactTarget->GetAnchorStacks();
		const int enemyGenerated = AnchorStackRules::AddEnemyStacks(
			m_BattleState->anchorContactTarget->MutableAnchorStacks(),
			grant,
			status.anchorStackMax);
		if (enemyGenerated > 0)
		{
			RecordBalanceEvent("anchor_stacks_generated", {
				{ "enemy_amount", enemyGenerated },
				{ "player_anchor", m_BattleState->anchorStacks.playerStacks },
				{ "one_shot_bonus", oneShotAnchorBonus },
			});
		}
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Enemy,
			enemyStacksBefore,
			m_BattleState->anchorContactTarget->GetAnchorStacks(),
			AxisResourceChangeReason::EndOfShot,
			currentBall->definitionId,
			false,
			m_BattleState->anchorContactTarget->GetPosition(),
			m_BattleState->anchorContactTarget);
		if (enemyGenerated > 0)
		{
			const DirectX::SimpleMath::Vector3 sourcePosition = player != nullptr
				? player->GetPosition()
				: m_BattleState->anchorContactTarget->GetPosition();
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Anchor,
				AxisAbilityAction::AnchorApplied,
				sourcePosition,
				m_BattleState->anchorContactTarget->GetPosition(),
				enemyGenerated,
				m_BattleState->anchorContactTarget->GetAnchorStacks(),
				0.0f,
				m_BattleState->anchorContactTarget->GetAnchorStacks() >= 5,
				false,
				reinterpret_cast<std::uintptr_t>(player),
				reinterpret_cast<std::uintptr_t>(m_BattleState->anchorContactTarget),
				currentBall->definitionId,
			});
		}
	}
	m_BattleState->anchorContactTarget = nullptr;
	if (currentBall != nullptr &&
		currentBall->definitionId == "player_origin_anchor" &&
		currentBall->status.nextAnchorGrantBonus > 0)
	{
		m_BattleState->anchorStacks.nextAnchorGrantBonus =
			currentBall->status.nextAnchorGrantBonus;
	}

	if (currentBall != nullptr && currentBall->status.abilities.reflectionSetup)
	{
		CushionChargeRules::ArmSetupForNextShot(
			m_BattleState->cushionCharges,
			currentBall->status.reflectionSetupAmount);
	}
	CushionChargeRules::EndPlayerShot(m_BattleState->cushionCharges);
	m_BattleState->cushionBoostConsumedThisShot = false;
	PublishGameEvent(ShotResolvedEvent{
		m_BattleState->presentationHitCount,
		m_BattleState->presentationDamage,
		m_BattleState->presentationDefeatCount,
		m_BattleState->presentationChargeInterruptions,
		m_BattleState->presentationResourceGained,
		m_BattleState->presentationResourceConsumed,
		m_BattleState->presentationFinisherMask,
	});
	// The first verification snapshot is taken at physics stop. Overwrite it
	// after end-of-shot resource generation so the debug comparison covers the
	// same final Heavy state as the predictor.
	GameShotPrediction::WriteVerificationActual();

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

	PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall != nullptr)
	{
		currentBall->status = updatedStatus;
	}

	m_RunController->Status() = NormalizePlayerRunStatus(m_RunController->Status());
	m_RunController->Status().currentHp = std::clamp(
		player->GetHP(),
		0,
		m_RunController->Status().maxHp
	);
}
// Current Player Statusを取得して保持する。
void Game::CaptureCurrentPlayerStatus()
{
	std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
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
	DirectX::SimpleMath::Vector3 returnPosition;
	if (AnchorStackRules::ConsumeReturnPosition(
		m_BattleState->anchorStacks, returnPosition))
	{
		const auto players = GameRuntime::World().GetComponents<PlayerBall>();
		if (!players.empty() && players.front() != nullptr &&
			players.front()->GetBall() != nullptr)
		{
			players.front()->GetBall()->ResetAtPosition(returnPosition);
		}
	}

	if (m_RunController->Deck().HasCurrent())
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
		GameRuntime::World().GetComponents<EnemyBall>();

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
	if (m_RunController->IsStageRewardCollected())
	{
		return;
	}

	const int moneyBefore = m_RunController->Status().money;
	if (!m_RunController->CollectStageReward(
		CalculateStageRewardMoney()))
	{
		return;
	}
	const int rewardMoney =
		m_RunController->GetCurrentStageRewardMoney();

	char rewardMessage[128]{};
	sprintf_s(
		rewardMessage,
		UiText::RewardMoneyFormat,
		rewardMoney);
	m_RewardState->SetMessage(rewardMessage);
	RecordBalanceEvent(
		"stage_money_reward",
		{
			{ "amount", rewardMoney },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController->Status().money },
		});
}

// On Player Shot Fired の処理を実行する。
void Game::OnPlayerShotFired(PlayerBall* player)
{
	ResetFrameTiming();
	ResetShotRelicState(player);
    // A new shot starts a new damage episode even for touching enemy pairs.
    for (auto* ball : GameRuntime::World().GetComponents<BallComponent>()) ball->ResetShotAbilityState();
	for (auto* enemy : GameRuntime::World().GetComponents<EnemyBall>())
	{
		enemy->BeginBossShot();
		enemy->BeginLightweightWindow();
	}

	// 選択内容はショットした瞬間に確定する。
	if (!m_RunController->Deck().HasCurrent())
	{
		if (!m_RunController->Deck().SelectOffer(
			GetSelectedBallOfferIndex(),
			GetHeldBallOfferIndex()))
		{
			return;
		}
	}
	if (IsFrameTimeMeasurementEnabled())
	{
		m_RuntimeState->BeginFrameTimingMeasurement();
	}

	CushionChargeRules::BeginPlayerShot(m_BattleState->cushionCharges);
	HeavyCollisionRules::BeginShot(m_BattleState->heavyCollisions);
	PierceTraceRules::BeginShot(m_BattleState->pierceTraces);
	m_BattleState->cushionBoostConsumedThisShot = false;
	m_BattleState->cushionStrongUsesThisShot = 0;
	m_BattleState->synergyDamageBonusThisShot = 0;
	m_BattleState->piercedEnemiesThisShot.clear();
	m_BattleState->heavyFinisherConsumedThisShot = false;
	m_BattleState->pierceFinisherPresentedThisShot = false;
	m_BattleState->anchorFinisherTriggeredThisShot = false;
	m_BattleState->anchorContactTarget = nullptr;
	m_BattleState->playerShield = 0;
	m_BattleState->presentationHitCount = 0;
	m_BattleState->presentationDamage = 0;
	m_BattleState->presentationDefeatCount = 0;
	m_BattleState->presentationChargeInterruptions = 0;
	m_BattleState->presentationResourceGained.fill(0);
	m_BattleState->presentationResourceConsumed.fill(0);
	m_BattleState->presentationFinisherMask = 0;

	if (player != nullptr)
	{
		CapturePlayerStatusFrom(player);
	}

	const PlayerBallData* currentBall =
		m_RunController->Deck().GetCurrent();
	const int heavyStacksAtShotStart =
		m_BattleState->heavyCollisions.stackCount;
	const int pierceStacksAtShotStart =
		m_BattleState->pierceTraces.stackCount;
	const int reflectionStacksAtShotStart =
		m_BattleState->cushionCharges.stackCount;
	const int pierceTraceCountAtShotStart = static_cast<int>(
		m_BattleState->pierceTraces.traces.size());
	int heavyStacksConsumedOnStart = 0;
	int pierceStacksConsumedOnStart = 0;
	bool pierceMinionActivationAttempted = false;
	bool pierceMinionFinisher = false;
	std::vector<PierceTraceRules::MinionLaunch> pierceMinionLaunches;
	if (currentBall != nullptr &&
		currentBall->definitionId == "player_collision_boost" &&
		currentBall->status.heavySetupStackConsume > 0 &&
		m_BattleState->heavyCollisions.stackCount >=
			currentBall->status.heavySetupStackConsume)
	{
		heavyStacksConsumedOnStart = HeavyCollisionRules::Consume(
			m_BattleState->heavyCollisions,
			currentBall->status.heavySetupStackConsume);
		HeavyCollisionRules::ArmNextEnemyCollisionMultiplier(
			m_BattleState->heavyCollisions,
			currentBall->status.heavyEnemyCollisionMultiplier);
		RecordBalanceEvent("heavy_collision_boost_armed", {
			{ "consumed", heavyStacksConsumedOnStart },
			{ "remaining", m_BattleState->heavyCollisions.stackCount },
			{ "multiplier", currentBall->status.heavyEnemyCollisionMultiplier },
		});
	}
	if (currentBall != nullptr &&
		(currentBall->definitionId == "player_trace_driver" ||
		 currentBall->definitionId == "player_minion_finisher"))
	{
		pierceMinionActivationAttempted = true;
		pierceMinionFinisher =
			currentBall->definitionId == "player_minion_finisher";
		const int consumeLimit = pierceMinionFinisher
			? PierceTraceRules::MaxStackCount
			: currentBall->status.pierceStackConsumeLimit;
		pierceStacksConsumedOnStart = PierceTraceRules::ConsumeStacks(
			m_BattleState->pierceTraces,
			consumeLimit);
		pierceMinionLaunches = PierceTraceRules::BuildMinionLaunches(
			m_BattleState->pierceTraces,
			pierceStacksConsumedOnStart,
			pierceMinionFinisher);
	}
	if (player != nullptr && currentBall != nullptr)
	{
		if (currentBall->status.abilities.returnToShotStart)
		{
			AnchorStackRules::SaveReturnPosition(
				m_BattleState->anchorStacks,
				player->GetPosition());
		}
		auto shotRules = m_BattleState->controller.GetShotRelicRules();
		shotRules.launchPower = player->GetVelocity().Length();
		m_BattleState->controller.SetShotRelicRules(shotRules);
		PublishGameEvent(ShotFiredEvent{
			currentBall->definitionId,
			static_cast<int>(currentBall->category),
		});

		const DirectX::SimpleMath::Vector3 position =
			player->GetPosition();
		if (heavyStacksConsumedOnStart > 0)
		{
			NotifyAxisResourceChanged(
				AxisResourceKind::Heavy,
				AxisResourceOwner::Player,
				heavyStacksAtShotStart,
				m_BattleState->heavyCollisions.stackCount,
				AxisResourceChangeReason::AbilityCost,
				currentBall->definitionId,
				false,
				position);
		}
		if (pierceStacksConsumedOnStart > 0 ||
			(pierceMinionFinisher && !pierceMinionLaunches.empty()))
		{
			NotifyAxisResourceChanged(
				AxisResourceKind::Pierce,
				AxisResourceOwner::Player,
				pierceStacksAtShotStart,
				m_BattleState->pierceTraces.stackCount,
				pierceMinionFinisher
					? AxisResourceChangeReason::Finisher
					: AxisResourceChangeReason::AbilityCost,
				currentBall->definitionId,
				pierceMinionFinisher && !pierceMinionLaunches.empty(),
				position);
		}
		const DirectX::SimpleMath::Vector3 velocity =
			player->GetVelocity();
		const std::vector<EnemyBall*> enemies =
			GameRuntime::World().GetComponents<EnemyBall>();
		nlohmann::json offers = nlohmann::json::array();
		for (int index = 0;
			index < m_RunController->Deck().GetOfferCount();
			index++)
		{
			const PlayerBallData* offer =
				m_RunController->Deck().GetOffer(index);
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
					{ "held", m_RunController->Deck().WasHeldOffer(index) },
				});
		}
		nlohmann::json shotContext =
		{
			{ "selected_offer_index", GetSelectedBallOfferIndex() },
			{ "held_offer_index", GetHeldBallOfferIndex() },
			{ "selected_instance_id", currentBall->instanceId },
			{ "ball_category", BallCategoryId(currentBall->category) },
			{ "effective_attack", player->GetAttack() },
			{ "heavy_stacks_before", heavyStacksAtShotStart },
			{ "heavy_stacks_consumed_on_start",
				heavyStacksConsumedOnStart },
			{ "heavy_stack_gain_limit_per_turn",
				m_BattleState->heavyCollisions.gainLimitPerTurn },
			{ "heavy_stack_distance_per_stack",
				m_BattleState->heavyCollisions.distancePerStack },
			{ "pierce_stacks_before", pierceStacksAtShotStart },
			{ "pierce_stacks_consumed_on_start",
				pierceStacksConsumedOnStart },
			{ "pierce_stack_max", PierceTraceRules::MaxStackCount },
			{ "pierce_trace_count_before", pierceTraceCountAtShotStart },
			{ "pierce_minion_activation_attempted",
				pierceMinionActivationAttempted },
			{ "pierce_minions_planned",
				static_cast<int>(pierceMinionLaunches.size()) },
			{ "pierce_minion_finisher", pierceMinionFinisher },
			{ "reflection_stacks_before", reflectionStacksAtShotStart },
			{ "offers", std::move(offers) },
			{ "mcp_telemetry", *m_PendingShotTelemetry },
		};
		std::vector<BalanceShotEnemySnapshot> shotEnemies;
		shotEnemies.reserve(enemies.size());
		for (std::size_t index = 0; index < enemies.size(); ++index)
		{
			EnemyBall* enemy = enemies[index];
			if (enemy == nullptr || enemy->IsDefeated())
			{
				continue;
			}
			const DirectX::SimpleMath::Vector3 enemyPosition =
				enemy->GetPosition();
			shotEnemies.push_back({
				reinterpret_cast<std::uintptr_t>(enemy),
				index,
				enemy->GetEnemyId(),
				enemy->GetHP(),
				enemy->GetMaxHP(),
				enemyPosition.x,
				enemyPosition.z,
			});
		}

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
			shotEnemies,
			shotContext);
		if (pierceMinionActivationAttempted)
		{
			for (std::size_t launchIndex = 0;
				launchIndex < pierceMinionLaunches.size();
				++launchIndex)
			{
				const auto& launch = pierceMinionLaunches[launchIndex];
				BallStatus minionStatus = currentBall->status;
				minionStatus.abilities = {};
				minionStatus.attack = launch.attack;
				minionStatus.knockbackTransfer = launch.knockbackMultiplier;
				BallFactory::CreatePierceMinion(
					*this,
					minionStatus,
					launch.start,
					launch.direction,
					PierceTraceRules::MinionLaunchSpeed);
				const bool overcharged = launch.attack >
					PierceTraceRules::MinionBaseAttack ||
					launch.knockbackMultiplier > 1.0f;
				PublishGameEvent(AxisAbilityResolvedEvent{
					AxisResourceKind::Pierce,
					AxisAbilityAction::PierceMinionLaunched,
					launch.start,
					launch.start + launch.direction * 8.0f,
					launch.attack,
					static_cast<int>(launchIndex) + 1,
					0.0f,
					overcharged,
					pierceMinionFinisher,
					0,
					0,
					currentBall->definitionId,
				});
			}
			RecordBalanceEvent("pierce_minions_launched", {
				{ "consumed", pierceStacksConsumedOnStart },
				{ "remaining", m_BattleState->pierceTraces.stackCount },
				{ "minion_count", static_cast<int>(pierceMinionLaunches.size()) },
				{ "finisher", pierceMinionFinisher },
			});
		}
		*m_PendingShotTelemetry = nlohmann::json::object();
	}

	m_RunController->Deck().MarkCurrentUsed();
	m_BattleState->controller.NotifyShotFired(
		currentBall != nullptr &&
		currentBall->category == BallCategory::Heavy);
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
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall == nullptr) return;
	const BallStatus& status = currentBall->status;
	(void)playerPosition;

	(void)cushionRegion;
	CushionChargeRules::ReflectionConfig config;
	config.generatesStacks = currentBall->definitionId == "player_cushion_charge";
	config.generationLimit = status.cushionReflectionLimit;
	config.stacksPerReflection = status.cushionStackGenerateAmount;
	config.consumesStacks = currentBall->definitionId == "player_bounce";
	config.consumptionLimit = status.cushionConsumptionLimit;
	config.reflectedSpeedMultiplier = status.cushionReflectionSpeedMultiplier;
	const int stacksBefore = m_BattleState->cushionCharges.stackCount;
	const int activeSetupBonusBefore =
		m_BattleState->cushionCharges.activeSetupBonus;
	const int uncappedGenerated =
		(config.generatesStacks
			? (std::max)(0, config.stacksPerReflection)
			: 0) +
		(m_BattleState->cushionCharges.setupTriggeredThisShot
			? 0
			: (std::max)(0, activeSetupBonusBefore));
	const auto result = CushionChargeRules::ApplyValidReflection(
		m_BattleState->cushionCharges, config, reflectedVelocity);
	const int stacksAfter = m_BattleState->cushionCharges.stackCount;
	const int stacksAfterGeneration = stacksBefore + result.generated;
	RecordBalanceEvent("cushion_reflection_resolved", {
		{ "cushion_region", cushionRegion },
		{ "position_xz", { playerPosition.x, playerPosition.z } },
		{ "valid_reflections_this_shot",
			m_BattleState->cushionCharges.validReflectionsThisShot },
		{ "generated", result.generated },
		{ "uncapped_generated", uncappedGenerated },
		{ "consumed", result.consumed },
		{ "stacks_before", stacksBefore },
		{ "stacks_after", m_BattleState->cushionCharges.stackCount },
		{ "setup_triggered", result.setupTriggered },
		{ "generation_limit", config.generationLimit },
		{ "consumption_limit", config.consumptionLimit },
		{ "reflected_speed_multiplier", config.reflectedSpeedMultiplier },
		{ "velocity_after_effects_xz",
			{ reflectedVelocity.x, reflectedVelocity.z } },
	});
	if (result.generated > 0 || result.consumed > 0 || result.setupTriggered)
	{
		DirectX::SimpleMath::Vector3 reflectionDirection = reflectedVelocity;
		reflectionDirection.y = 0.0f;
		if (reflectionDirection.LengthSquared() > 0.0001f)
			reflectionDirection.Normalize();
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Reflection,
			AxisAbilityAction::ReflectionWallBounce,
			playerPosition,
			playerPosition + reflectionDirection * 8.0f,
			result.generated > 0 ? result.generated : result.consumed,
			m_BattleState->cushionCharges.validReflectionsThisShot,
			0.0f,
			result.consumed > 0 || result.setupTriggered,
			false,
			0,
			0,
			currentBall->definitionId,
		});
	}
	if (result.generated > 0)
	{
		RecordBalanceEvent("cushion_stacks_generated", {
			{ "amount", result.generated },
			{ "setup", result.setupTriggered } });
		NotifyAxisResourceChanged(
			AxisResourceKind::Reflection,
			AxisResourceOwner::Player,
			stacksBefore,
			stacksAfterGeneration,
			AxisResourceChangeReason::WallReflection,
			currentBall->definitionId,
			false,
			playerPosition);
	}
	if (result.consumed > 0)
	{
		m_BattleState->cushionBoostConsumedThisShot = true;
		m_BattleState->cushionStrongUsesThisShot =
			m_BattleState->cushionCharges.consumedThisShot;
		RecordBalanceEvent("cushion_stacks_consumed", {
			{ "amount", result.consumed },
			{ "strong_uses_this_shot", m_BattleState->cushionStrongUsesThisShot },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Reflection,
			AxisResourceOwner::Player,
			stacksAfterGeneration,
			stacksAfter,
			AxisResourceChangeReason::AbilityCost,
			currentBall->definitionId,
			false,
			playerPosition);
	}
}

int Game::NotifyEnemyEnemySynergyCollision(
	EnemyBall* first,
	EnemyBall* second,
	const DirectX::SimpleMath::Vector3& firstVelocityBefore,
	const DirectX::SimpleMath::Vector3& secondVelocityBefore)
{
	if (first == nullptr || second == nullptr) return 0;
	const std::uintptr_t firstId = reinterpret_cast<std::uintptr_t>(first);
	const std::uintptr_t secondId = reinterpret_cast<std::uintptr_t>(second);
	EnemyBall* target = nullptr;
	const DirectX::SimpleMath::Vector3* velocityBefore = nullptr;
	if (CushionChargeRules::IsPinballTarget(
		m_BattleState->cushionCharges, firstId))
	{
		target = first;
		velocityBefore = &firstVelocityBefore;
	}
	else if (CushionChargeRules::IsPinballTarget(
		m_BattleState->cushionCharges, secondId))
	{
		target = second;
		velocityBefore = &secondVelocityBefore;
	}
	if (target == nullptr || velocityBefore == nullptr ||
		target->GetBall() == nullptr)
	{
		return 0;
	}
	const int damageStage = CushionChargeRules::RegisterPinballCollision(
		m_BattleState->cushionCharges,
		reinterpret_cast<std::uintptr_t>(target));
	CushionChargeRules::PreservePinballSpeed(
		m_BattleState->cushionCharges,
		reinterpret_cast<std::uintptr_t>(target),
		*velocityBefore,
		target->GetBall()->GetMutableVelocity());
	EnemyBall* source = target == first ? second : first;
	PublishGameEvent(AxisAbilityResolvedEvent{
		AxisResourceKind::Reflection,
		AxisAbilityAction::ReflectionPinballCollision,
		source != nullptr ? source->GetPosition() : target->GetPosition(),
		target->GetPosition(),
		damageStage,
		m_BattleState->cushionCharges.pinballCollisionCount,
		0.0f,
		damageStage > 0,
		true,
		reinterpret_cast<std::uintptr_t>(source),
		reinterpret_cast<std::uintptr_t>(target),
		"player_ricochet_finisher",
	});
	return damageStage;
}

void Game::NotifyEnemyWallCollision(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& velocityBefore,
	DirectX::SimpleMath::Vector3& velocityAfter)
{
	if (enemy == nullptr) return;
	const std::uintptr_t enemyId = reinterpret_cast<std::uintptr_t>(enemy);
	if (!CushionChargeRules::IsPinballTarget(
		m_BattleState->cushionCharges, enemyId))
	{
		return;
	}
	CushionChargeRules::RegisterPinballCollision(
		m_BattleState->cushionCharges, enemyId);
	CushionChargeRules::PreservePinballSpeed(
		m_BattleState->cushionCharges,
		enemyId,
		velocityBefore,
		velocityAfter);
	DirectX::SimpleMath::Vector3 direction = velocityAfter;
	direction.y = 0.0f;
	if (direction.LengthSquared() > 0.0001f) direction.Normalize();
	PublishGameEvent(AxisAbilityResolvedEvent{
		AxisResourceKind::Reflection,
		AxisAbilityAction::ReflectionWallBounce,
		enemy->GetPosition(),
		enemy->GetPosition() + direction * 6.0f,
		0,
		m_BattleState->cushionCharges.pinballCollisionCount,
		0.0f,
		true,
		true,
		enemyId,
		0,
		"player_ricochet_finisher",
	});
}

void Game::NotifyPlayerPiercedEnemy(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& playerPosition,
	const DirectX::SimpleMath::Vector3& direction)
{
	if (enemy == nullptr) return;
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall == nullptr || currentBall->category != BallCategory::Pierce) return;
	m_BattleState->piercedEnemiesThisShot.insert(enemy);
	const int stacksBefore = m_BattleState->pierceTraces.stackCount;
	const bool uniquePierce = PierceTraceRules::RecordPierce(
		m_BattleState->pierceTraces,
		reinterpret_cast<std::uintptr_t>(enemy));
	const int stackGranted = (std::max)(
		0,
		m_BattleState->pierceTraces.stackCount - stacksBefore);
	DirectX::SimpleMath::Vector3 traceDirection = direction;
	traceDirection.y = 0.0f;
	if (traceDirection.LengthSquared() > 0.0001f)
	{
		traceDirection.Normalize();
		const int overwrittenBefore = m_BattleState->pierceTraces.overwrittenCount;
		const bool traceCreated = PierceTraceRules::AddTrace(
			m_BattleState->pierceTraces,
			playerPosition,
			playerPosition + traceDirection * currentBall->status.traceUseDistance);
		const DirectX::SimpleMath::Vector3 traceEnd = playerPosition +
			traceDirection * currentBall->status.traceUseDistance;
		RecordBalanceEvent("pierce_trace_generated", {
			{ "overwrote_oldest", m_BattleState->pierceTraces.overwrittenCount > overwrittenBefore },
			{ "unique_pierce", uniquePierce },
			{ "requested_amount", uniquePierce ? 1 : 0 },
			{ "granted_amount", stackGranted },
			{ "discarded_by_stack_max", uniquePierce ? 1 - stackGranted : 0 },
			{ "stack_generated", stackGranted > 0 },
			{ "stacks_before", stacksBefore },
			{ "stacks", m_BattleState->pierceTraces.stackCount },
			{ "trace_count", static_cast<int>(m_BattleState->pierceTraces.traces.size()) },
			{ "trace_start_xz", { playerPosition.x, playerPosition.z } },
			{ "trace_direction_xz", { traceDirection.x, traceDirection.z } },
			{ "trace_length", currentBall->status.traceUseDistance },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Pierce,
			AxisResourceOwner::Player,
			stacksBefore,
			m_BattleState->pierceTraces.stackCount,
			AxisResourceChangeReason::PierceHit,
			currentBall->definitionId,
			false,
			enemy->GetPosition(),
			enemy);
		if (traceCreated)
		{
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Pierce,
				AxisAbilityAction::PierceTraceCreated,
				playerPosition,
				traceEnd,
				stackGranted,
				static_cast<int>(m_BattleState->pierceTraces.traces.size()),
				0.0f,
				uniquePierce,
				false,
				0,
				reinterpret_cast<std::uintptr_t>(enemy),
				currentBall->definitionId,
			});
		}
	}
}

void Game::NotifyPlayerDirectionChange(
	const DirectX::SimpleMath::Vector3& playerPosition)
{
	(void)playerPosition;
}

void Game::NotifyTraceMovement(
	BallComponent& ball,
	const DirectX::SimpleMath::Vector3& from,
	const DirectX::SimpleMath::Vector3& to)
{
	if (m_BattleState->controller.GetState() != BattleState::BallsMoving) return;
	GameObject* owner = ball.GetGameObject();
	if (owner == nullptr) return;
	DirectX::SimpleMath::Vector3 movement = to - from;
	movement.y = 0.0f;
	const float distance = movement.Length();
	if (PierceMinion* minion = owner->GetComponent<PierceMinion>())
	{
		BalanceLogger::GetInstance().RecordPierceMinionMovement(
			reinterpret_cast<std::uintptr_t>(minion),
			distance);
		if (distance > 0.01f)
		{
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Pierce,
				AxisAbilityAction::PierceMinionTravel,
				from,
				to,
				0,
				0,
				0.0f,
				false,
				false,
				reinterpret_cast<std::uintptr_t>(minion),
				0,
				"pierce_minion",
			});
		}
		return;
	}
	EnemyBall* enemy = owner->GetComponent<EnemyBall>();
	if (enemy == nullptr) return;
	BalanceLogger::GetInstance().RecordEnemyMovement(
		reinterpret_cast<std::uintptr_t>(enemy),
		enemy->GetEnemyId(),
		distance);
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall == nullptr) return;
	HeavyCollisionRules::RecordEnemyMovement(
		m_BattleState->heavyCollisions,
		reinterpret_cast<std::uintptr_t>(enemy),
		distance,
		currentBall->status.abilities.heavyDistanceStacks);
}

PlayerEnemySynergyResult Game::NotifyPlayerEnemySynergyCollision(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& enemyVelocityBefore)
{
	PlayerEnemySynergyResult result;
	result.damageBonus = m_BattleState->synergyDamageBonusThisShot;
	if (enemy == nullptr) return result;
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall == nullptr) return result;
	const BallStatus& status = currentBall->status;
	if (status.anchorEnemyStackGenerate > 0)
	{
		if (currentBall->definitionId != "player_origin_anchor" ||
			m_BattleState->anchorContactTarget == nullptr)
		{
			m_BattleState->anchorContactTarget = enemy;
		}
	}

	if (status.abilities.lightweight)
	{
		enemy->ApplyLightweight(status.lightweightKnockbackMultiplier);
		const int heavyStacksBefore =
			m_BattleState->heavyCollisions.stackCount;
		const auto heavyGain = HeavyCollisionRules::GenerateOncePerEnemy(
			m_BattleState->heavyCollisions,
			reinterpret_cast<std::uintptr_t>(enemy));
		if (heavyGain.requested > 0)
		{
			RecordBalanceEvent("heavy_micro_stack_generated", {
				{ "amount", heavyGain.granted },
				{ "uncapped_amount", heavyGain.requested },
				{ "granted_amount", heavyGain.granted },
				{ "limited_amount", heavyGain.discardedByTurnLimit },
				{ "storage_limited_amount", heavyGain.discardedByStorageLimit },
				{ "gain_limit", heavyGain.turnLimit },
				{ "turn_gained_after", m_BattleState->heavyCollisions.gainedThisTurn },
				{ "stack_after", m_BattleState->heavyCollisions.stackCount },
			});
		}
		NotifyAxisResourceChanged(
			AxisResourceKind::Heavy,
			AxisResourceOwner::Player,
			heavyStacksBefore,
			m_BattleState->heavyCollisions.stackCount,
			AxisResourceChangeReason::Collision,
			currentBall->definitionId,
			false,
			enemy->GetPosition(),
			enemy);
	}

	if (currentBall->definitionId == "player_bounce" &&
		CushionChargeRules::ConsumeEmpoweredHit(
			m_BattleState->cushionCharges,
			reinterpret_cast<std::uintptr_t>(enemy)))
	{
		result.damageBonus += status.cushionBounceAttackBonus;
		RecordBalanceEvent("cushion_empowered_hit", {
			{ "target_id", reinterpret_cast<std::uintptr_t>(enemy) },
			{ "damage_bonus", status.cushionBounceAttackBonus },
			{ "empowered_hits_remaining",
				m_BattleState->cushionCharges.empoweredHitsRemaining },
		});
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Reflection,
			AxisAbilityAction::ReflectionEmpoweredHit,
			enemy->GetPosition(),
			enemy->GetPosition(),
			status.cushionBounceAttackBonus,
			m_BattleState->cushionStrongUsesThisShot,
			0.0f,
			true,
			false,
			0,
			reinterpret_cast<std::uintptr_t>(enemy),
			currentBall->definitionId,
		});
	}
	if (currentBall->definitionId == "player_pierce_finisher" &&
		!m_BattleState->piercedEnemiesThisShot.empty())
	{
		const int uniqueEnemies = static_cast<int>(m_BattleState->piercedEnemiesThisShot.size());
		result.damageBonus += status.pierceFinisherBaseBonus +
			(std::max)(0, uniqueEnemies - 1) * status.pierceFinisherMultiTargetBonus;
		RecordBalanceEvent("pierce_finisher_hit", {
			{ "unique_enemies", uniqueEnemies },
			{ "damage_bonus", result.damageBonus },
		});
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Pierce,
			AxisAbilityAction::PierceFinisherHit,
			enemy->GetPosition(),
			enemy->GetPosition(),
			result.damageBonus,
			uniqueEnemies,
			0.0f,
			uniqueEnemies >= 2,
			true,
			0,
			reinterpret_cast<std::uintptr_t>(enemy),
			currentBall->definitionId,
		});
		if (!m_BattleState->pierceFinisherPresentedThisShot)
		{
			m_BattleState->pierceFinisherPresentedThisShot = true;
			NotifyAxisResourceChanged(
				AxisResourceKind::Pierce,
				AxisResourceOwner::Player,
				m_BattleState->pierceTraces.stackCount,
				m_BattleState->pierceTraces.stackCount,
				AxisResourceChangeReason::Finisher,
				currentBall->definitionId,
				true,
				enemy->GetPosition(),
				enemy);
		}
	}

	if (currentBall->definitionId == "player_ricochet_finisher")
	{
		const std::uintptr_t enemyId = reinterpret_cast<std::uintptr_t>(enemy);
		const int stacksBefore = m_BattleState->cushionCharges.stackCount;
		const bool activated = CushionChargeRules::ActivatePinball(
			m_BattleState->cushionCharges,
			enemyId,
			status.ricochetStackConsume,
			status.pinballCollisionsPerStage,
			status.pinballSpeedRetention);
		RecordBalanceEvent("pinball_activation_attempted", {
			{ "activated", activated },
			{ "required", status.ricochetStackConsume },
			{ "consumed", activated ? status.ricochetStackConsume : 0 },
			{ "stacks_before", stacksBefore },
			{ "stacks_after", m_BattleState->cushionCharges.stackCount },
			{ "target_id", enemyId },
			{ "collisions_per_damage_stage",
				status.pinballCollisionsPerStage },
			{ "speed_retention", status.pinballSpeedRetention },
		});
		if (CushionChargeRules::IsPinballTarget(
			m_BattleState->cushionCharges, enemyId))
		{
			CushionChargeRules::RegisterPinballCollision(
				m_BattleState->cushionCharges, enemyId);
			if (enemy->GetBall() != nullptr)
			{
				CushionChargeRules::PreservePinballSpeed(
					m_BattleState->cushionCharges,
					enemyId,
					enemyVelocityBefore,
					enemy->GetBall()->GetMutableVelocity());
			}
		}
		result.stopPlayer = activated;
		if (activated)
		{
			RecordBalanceEvent("pinball_activated", {
				{ "consumed", status.ricochetStackConsume },
				{ "remaining", m_BattleState->cushionCharges.stackCount },
			});
			NotifyAxisResourceChanged(
				AxisResourceKind::Reflection,
				AxisResourceOwner::Player,
				stacksBefore,
				m_BattleState->cushionCharges.stackCount,
				AxisResourceChangeReason::Finisher,
				currentBall->definitionId,
				true,
				enemy->GetPosition(),
				enemy);
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Reflection,
				AxisAbilityAction::ReflectionPinballActivated,
				enemy->GetPosition(),
				enemy->GetPosition(),
				status.ricochetStackConsume,
				0,
				0.0f,
				true,
				true,
				0,
				enemyId,
				currentBall->definitionId,
			});
		}
	}

	if (currentBall->definitionId == "player_stop_shield" &&
		enemy->GetAnchorStacks() > 0)
	{
		const int enemyStacksBefore = enemy->GetAnchorStacks();
		const int playerStacksBefore =
			m_BattleState->anchorStacks.playerStacks;
		const auto consumed = AnchorStackRules::ConsumeEnemyStacks(
			enemy->MutableAnchorStacks(),
			m_BattleState->anchorStacks,
			status.anchorEnemyConsumeLimit,
			status.anchorStackMax);
		const int shieldGain = AnchorStackRules::CalculateShield(
			consumed.consumed,
			status.stopShieldAmount);
		m_BattleState->playerShield = (std::min)(
			status.stopShieldAmount,
			m_BattleState->playerShield + shieldGain);
		RecordBalanceEvent("stop_shield_granted", {
			{ "consumed", consumed.consumed },
			{ "shield_gain", shieldGain },
			{ "shield", m_BattleState->playerShield },
			{ "player_anchor_generated", consumed.playerStacksGenerated },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Enemy,
			enemyStacksBefore,
			enemy->GetAnchorStacks(),
			AxisResourceChangeReason::ShieldConversion,
			currentBall->definitionId,
			false,
			enemy->GetPosition(),
			enemy);
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Player,
			playerStacksBefore,
			m_BattleState->anchorStacks.playerStacks,
			AxisResourceChangeReason::ShieldConversion,
			currentBall->definitionId,
			false,
			enemy->GetPosition());
		const auto players = GameRuntime::World().GetComponents<PlayerBall>();
		const PlayerBall* player = players.empty() ? nullptr : players.front();
		const DirectX::SimpleMath::Vector3 playerPosition = player != nullptr
			? player->GetPosition()
			: enemy->GetPosition();
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Anchor,
			AxisAbilityAction::AnchorRecovered,
			enemy->GetPosition(),
			playerPosition,
			consumed.consumed,
			consumed.playerStacksGenerated,
			0.0f,
			consumed.playerStacksGenerated > 0,
			false,
			reinterpret_cast<std::uintptr_t>(enemy),
			reinterpret_cast<std::uintptr_t>(player),
			currentBall->definitionId,
		});
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Anchor,
			AxisAbilityAction::AnchorShieldGranted,
			playerPosition,
			playerPosition,
			shieldGain,
			m_BattleState->playerShield,
			0.0f,
			shieldGain >= status.stopShieldAmount,
			false,
			reinterpret_cast<std::uintptr_t>(player),
			reinterpret_cast<std::uintptr_t>(player),
			currentBall->definitionId,
		});
		if (consumed.playerStacksGenerated > 0)
		{
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Anchor,
				AxisAbilityAction::AnchorPlayerEmpowered,
				playerPosition,
				playerPosition,
				consumed.playerStacksGenerated,
				m_BattleState->anchorStacks.playerStacks,
				0.0f,
				true,
				false,
				reinterpret_cast<std::uintptr_t>(player),
				reinterpret_cast<std::uintptr_t>(player),
				currentBall->definitionId,
			});
		}
	}

	if (currentBall->definitionId == "player_anchor_finisher" &&
		!m_BattleState->anchorFinisherTriggeredThisShot &&
		enemy->GetAnchorStacks() > 0)
	{
		m_BattleState->anchorFinisherTriggeredThisShot = true;
		const int playerStacksBefore =
			m_BattleState->anchorStacks.playerStacks;
		const int damagePerStack =
			AnchorStackRules::FinisherDamagePerEnemyStack(playerStacksBefore);
		const int available = enemy->GetAnchorStacks();
		const int requested = status.anchorFinisherStackConsume <= 0
			? available
			: (std::min)(available, status.anchorFinisherStackConsume);
		const int directBonus = requested * damagePerStack;
		result.damageBonus += directBonus;
		const auto consumed = AnchorStackRules::ConsumeEnemyStacks(
			enemy->MutableAnchorStacks(),
			m_BattleState->anchorStacks,
			requested,
			status.anchorStackMax);

		int aoeHitCount = 0;
		const int aoeDamage = directBonus / 2;
		if (consumed.consumed >= status.anchorFinisherAoeThreshold &&
			aoeDamage > 0 && status.anchorFinisherAoeRadius > 0.0f)
		{
			const float radiusSquared = status.anchorFinisherAoeRadius *
				status.anchorFinisherAoeRadius;
			for (EnemyBall* other :
				GameRuntime::World().GetComponents<EnemyBall>())
			{
				if (other == nullptr || other == enemy || other->IsDefeated() ||
					other->IsPocketed())
				{
					continue;
				}
				DirectX::SimpleMath::Vector3 offset =
					other->GetPosition() - enemy->GetPosition();
				offset.y = 0.0f;
				if (offset.LengthSquared() > radiusSquared) continue;
				const int hpBefore = other->GetHP();
				const bool wasDefeated = other->IsDefeated();
				const DirectX::SimpleMath::Vector3 feedbackPosition =
					other->GetPosition();
				other->TakeDamage(aoeDamage);
				const int applied = (std::max)(0, hpBefore - other->GetHP());
				if (!wasDefeated && (applied > 0 || other->IsDefeated()))
				{
					NotifyCombatFeedback(
						feedbackPosition,
						applied,
						other->IsDefeated(),
						false,
						other);
				}
				++aoeHitCount;
			}
		}
		RecordBalanceEvent("anchor_finisher_triggered", {
			{ "consumed", consumed.consumed },
			{ "player_anchor_before", playerStacksBefore },
			{ "damage_per_stack", damagePerStack },
			{ "direct_damage_bonus", directBonus },
			{ "aoe_hit_count", aoeHitCount },
			{ "player_anchor_generated", consumed.playerStacksGenerated },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Enemy,
			available,
			enemy->GetAnchorStacks(),
			AxisResourceChangeReason::Finisher,
			currentBall->definitionId,
			true,
			enemy->GetPosition(),
			enemy);
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Player,
			playerStacksBefore,
			m_BattleState->anchorStacks.playerStacks,
			AxisResourceChangeReason::Finisher,
			currentBall->definitionId,
			true,
			enemy->GetPosition());
		const auto players = GameRuntime::World().GetComponents<PlayerBall>();
		const PlayerBall* player = players.empty() ? nullptr : players.front();
		const DirectX::SimpleMath::Vector3 playerPosition = player != nullptr
			? player->GetPosition()
			: enemy->GetPosition();
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Anchor,
			AxisAbilityAction::AnchorRecovered,
			enemy->GetPosition(),
			playerPosition,
			consumed.consumed,
			consumed.playerStacksGenerated,
			0.0f,
			true,
			true,
			reinterpret_cast<std::uintptr_t>(enemy),
			reinterpret_cast<std::uintptr_t>(player),
			currentBall->definitionId,
		});
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Anchor,
			AxisAbilityAction::AnchorFinisherArea,
			enemy->GetPosition(),
			enemy->GetPosition(),
			consumed.consumed,
			aoeHitCount,
			status.anchorFinisherAoeRadius,
			true,
			true,
			reinterpret_cast<std::uintptr_t>(enemy),
			reinterpret_cast<std::uintptr_t>(enemy),
			currentBall->definitionId,
		});
		if (consumed.playerStacksGenerated > 0)
		{
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Anchor,
				AxisAbilityAction::AnchorPlayerEmpowered,
				playerPosition,
				playerPosition,
				consumed.playerStacksGenerated,
				m_BattleState->anchorStacks.playerStacks,
				0.0f,
				true,
				true,
				reinterpret_cast<std::uintptr_t>(player),
				reinterpret_cast<std::uintptr_t>(player),
				currentBall->definitionId,
			});
		}
	}

	return result;
}

int Game::AbsorbPlayerShieldDamage(int damage)
{
	const int incoming = (std::max)(0, damage);
	const int absorbed = (std::min)(m_BattleState->playerShield, incoming);
	m_BattleState->playerShield -= absorbed;
	if (absorbed > 0)
	{
		InvalidateDebugCombatForecast("停止時シールド消費");
		RecordBalanceEvent(
			"stop_shield_absorbed",
			{ { "absorbed", absorbed }, { "remaining", m_BattleState->playerShield } });
		const auto players = GameRuntime::World().GetComponents<PlayerBall>();
		const PlayerBall* player = players.empty() ? nullptr : players.front();
		const DirectX::SimpleMath::Vector3 position = player != nullptr
			? player->GetPosition()
			: DirectX::SimpleMath::Vector3::Zero;
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Anchor,
			AxisAbilityAction::AnchorShieldAbsorbed,
			position,
			position,
			absorbed,
			m_BattleState->playerShield,
			0.0f,
			absorbed == incoming,
			false,
			reinterpret_cast<std::uintptr_t>(player),
			reinterpret_cast<std::uintptr_t>(player),
			"player_stop_shield",
		});
	}
	return incoming - absorbed;
}

int Game::ScaleEnemyCollisionDamage(int damage) const
{
	return HeavyCollisionRules::ScaleEnemyCollisionDamage(
		m_BattleState->heavyCollisions,
		damage);
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
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall != nullptr &&
		currentBall->definitionId == "player_chain_impact" &&
		!m_BattleState->heavyFinisherConsumedThisShot)
	{
		m_BattleState->heavyFinisherConsumedThisShot = true;
		const int stacksBefore =
			m_BattleState->heavyCollisions.stackCount;
		const auto finisher = HeavyCollisionRules::ConsumeForFinisher(
			m_BattleState->heavyCollisions,
			currentBall->status.heavyFinisherDamagePerCollision,
			currentBall->status.heavyCollisionConsumeAmount);
		attackDamage += finisher.bonusDamage;
		RecordBalanceEvent("heavy_finisher_triggered", {
			{ "referenced_collision_count", finisher.referenced },
			{ "consumed_collision_count", finisher.consumed },
			{ "damage_bonus", finisher.bonusDamage },
			{ "remaining_collision_count", m_BattleState->heavyCollisions.stackCount },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Heavy,
			AxisResourceOwner::Player,
			stacksBefore,
			m_BattleState->heavyCollisions.stackCount,
			AxisResourceChangeReason::Finisher,
			currentBall->definitionId,
			true,
			center,
			excludedTarget);
	}
	if (attackDamage <= 0 || radius <= 0.0f) return;
	const float radiusSquared = radius * radius;
	struct ChainTarget
	{
		EnemyBall* enemy = nullptr;
		float distanceSquared = 0.0f;
		std::size_t stableOrder = 0;
	};
	std::vector<ChainTarget> targets;
	const auto enemies = GameRuntime::World().GetComponents<EnemyBall>();
	for (std::size_t index = 0; index < enemies.size(); ++index)
	{
		EnemyBall* enemy = enemies[index];
		if (enemy == nullptr || enemy == excludedTarget || enemy->IsDefeated() ||
			enemy->IsPocketed())
		{
			continue;
		}
		DirectX::SimpleMath::Vector3 offset = enemy->GetPosition() - center;
		offset.y = 0.0f;
		const float distanceSquared = offset.LengthSquared();
		if (distanceSquared <= radiusSquared)
		{
			targets.push_back({ enemy, distanceSquared, index });
		}
	}
	std::sort(
		targets.begin(),
		targets.end(),
		[](const ChainTarget& left, const ChainTarget& right)
		{
			if (left.distanceSquared != right.distanceSquared)
				return left.distanceSquared < right.distanceSquared;
			return left.stableOrder < right.stableOrder;
		});
	if (targets.size() > 2) targets.resize(2);
	int hitCount = 0;
	int totalDamage = 0;
	for (const ChainTarget& target : targets)
	{
		EnemyBall* enemy = target.enemy;
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
		NotifyCombatFeedback(
			feedbackPosition,
			applied,
			enemy->IsDefeated(),
			true,
			enemy);
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
	if (directTarget != nullptr) m_BattleState->anchorContactTarget = directTarget;
}

// Pierce Maximum Usesを取得する。
int Game::GetPierceMaximumUses() const
{
	const PlayerBallData* ball = m_RunController->Deck().GetCurrent();
	if (ball == nullptr)
		ball = m_RunController->Deck().GetOffer(GetSelectedBallOfferIndex());
	if (ball == nullptr) return 0;
	const int base = BallMechanics::PierceUses(ball->status,
		HasRelic(RelicType::PierceBallCharger) && ball->definitionId == "player_pierce");
	return base;
}

// Pierce Speed Retentionを取得する。
float Game::GetPierceSpeedRetention() const
{
	const PlayerBallData* ball = m_RunController->Deck().GetCurrent();
	if (ball == nullptr)
		ball = m_RunController->Deck().GetOffer(GetSelectedBallOfferIndex());
	if (ball == nullptr) return 0.75f;
	const float base = BallMechanics::PierceRetention(ball->status,
		HasRelic(RelicType::PierceBallCharger) && ball->definitionId == "player_pierce");
	return std::clamp(base, 0.0f, 1.0f);
}

// Enemy Defeatedを通知する。
void Game::NotifyEnemyDefeated(EnemyBall* enemy)
{
	if (enemy == nullptr) return;
	const std::string enemyId = enemy->GetEnemyId();
	const auto enemies = m_RuntimeState->World().GetComponents<EnemyBall>();
	const auto found = std::find(enemies.begin(), enemies.end(), enemy);
	const std::string targetId = found != enemies.end()
		? "enemy:" + std::to_string(
			static_cast<std::size_t>(std::distance(enemies.begin(), found)))
		: std::string();
	const EnemyActionType plannedAction = enemy->GetPlannedAction();
	const bool attacksNextTurn =
		plannedAction == EnemyActionType::NormalAttack ||
		plannedAction == EnemyActionType::ChargeAttack;
	const int plannedDamage = attacksNextTurn
		? PlayerBall::CalculateIncomingDamage(
			static_cast<int>(std::lround(
				enemy->GetAttack() * enemy->GetPlannedDamageMultiplier())))
		: 0;
	const int futureChargeDamage =
		!attacksNextTurn &&
		(plannedAction == EnemyActionType::ChargeStart || enemy->IsCharging())
			? PlayerBall::CalculateIncomingDamage(
				static_cast<int>(std::lround(
					enemy->GetAttack() *
					GetEnemyActionConfig().chargeDamageMultiplier)))
			: 0;
	RecordBalanceEvent("enemy_defeated", {
		{ "enemy_id", enemyId },
		{ "target_id", targetId },
		{ "board_ability", ToString(enemy->GetBoardAbilityType()) },
		{ "planned_action", ToString(plannedAction) },
		{ "prevented_next_turn_damage_before_defense", plannedDamage },
		{ "prevented_future_charge_damage_before_defense", futureChargeDamage },
	});
	const int enemyAnchorBefore = enemy->GetAnchorStacks();
	const int playerAnchorBefore =
		m_BattleState->anchorStacks.playerStacks;
	const int automaticConsume = AnchorStackRules::DefeatAutoConsumeAmount(
		enemy->GetAnchorStacks());
	if (automaticConsume > 0)
	{
		const auto consumed = AnchorStackRules::ConsumeEnemyStacks(
			enemy->MutableAnchorStacks(),
			m_BattleState->anchorStacks,
			automaticConsume,
			m_RunDefaults->PlayerBallStatus().anchorStackMax);
		RecordBalanceEvent("defeated_enemy_anchor_converted", {
			{ "enemy_id", enemyId },
			{ "consumed", consumed.consumed },
			{ "player_anchor_generated", consumed.playerStacksGenerated },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Enemy,
			enemyAnchorBefore,
			enemy->GetAnchorStacks(),
			AxisResourceChangeReason::DefeatConversion,
			enemyId,
			false,
			enemy->GetPosition(),
			enemy);
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Player,
			playerAnchorBefore,
			m_BattleState->anchorStacks.playerStacks,
			AxisResourceChangeReason::DefeatConversion,
			enemyId,
			false,
			enemy->GetPosition());
		const auto players = GameRuntime::World().GetComponents<PlayerBall>();
		const PlayerBall* player = players.empty() ? nullptr : players.front();
		const DirectX::SimpleMath::Vector3 playerPosition = player != nullptr
			? player->GetPosition()
			: enemy->GetPosition();
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Anchor,
			AxisAbilityAction::AnchorRecovered,
			enemy->GetPosition(),
			playerPosition,
			consumed.consumed,
			consumed.playerStacksGenerated,
			0.0f,
			consumed.playerStacksGenerated > 0,
			false,
			reinterpret_cast<std::uintptr_t>(enemy),
			reinterpret_cast<std::uintptr_t>(player),
			enemyId,
		});
		if (consumed.playerStacksGenerated > 0)
		{
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Anchor,
				AxisAbilityAction::AnchorPlayerEmpowered,
				playerPosition,
				playerPosition,
				consumed.playerStacksGenerated,
				m_BattleState->anchorStacks.playerStacks,
				0.0f,
				true,
				false,
				reinterpret_cast<std::uintptr_t>(player),
				reinterpret_cast<std::uintptr_t>(player),
				enemyId,
			});
		}
	}
	enemy->MutableAnchorStacks() = 0;
	if (!HasRelic(RelicType::BountyList) ||
		!m_BattleState->controller.ClaimBountyReward())
	{
		return;
	}
	const int moneyBefore = m_RunController->Status().money;
	m_RunController->AddMoney(kBountyRewardMoney);
	RecordBalanceEvent(
		"bounty_reward",
		{
			{ "enemy_id", enemyId },
			{ "amount", kBountyRewardMoney },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController->Status().money },
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
        for (PlayerBall* player : GameRuntime::World().GetComponents<PlayerBall>()) ApplyRelicModifiersTo(player);
}

// Combat Feedbackを通知する。
void Game::NotifyCombatFeedback(
	const DirectX::SimpleMath::Vector3& worldPosition,
	int damage,
	bool defeated,
	bool enemyEnemyCollision,
	const EnemyBall* target)
{
	InvalidateDebugCombatForecast("エネミー被ダメージ");
	PublishGameEvent(EnemyDamageEvent{
		worldPosition,
		damage,
		defeated,
		enemyEnemyCollision,
		reinterpret_cast<std::uintptr_t>(target),
		target != nullptr ? target->GetEnemyId() : std::string(),
	});
}

// Pocket Feedbackを通知する。
void Game::NotifyCollisionImpact(
	const DirectX::SimpleMath::Vector3& worldPosition,
	const DirectX::SimpleMath::Vector3& firstTargetPosition,
	const DirectX::SimpleMath::Vector3& secondTargetPosition,
	float relativeApproachSpeed,
	CombatCollisionKind collisionKind,
	int damage,
	bool defeated,
	const EnemyBall* firstTarget,
	const EnemyBall* secondTarget)
{
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	PublishGameEvent(CollisionImpactEvent{
		worldPosition,
		firstTargetPosition,
		secondTargetPosition,
		relativeApproachSpeed,
		collisionKind,
		damage,
		defeated,
		reinterpret_cast<std::uintptr_t>(firstTarget),
		reinterpret_cast<std::uintptr_t>(secondTarget),
		firstTarget != nullptr ? firstTarget->GetEnemyId() : std::string(),
		secondTarget != nullptr ? secondTarget->GetEnemyId() : std::string(),
		currentBall != nullptr
			? static_cast<int>(currentBall->category)
			: static_cast<int>(BallCategory::Standard),
	});
}

void Game::NotifyWallImpact(
	const DirectX::SimpleMath::Vector3& worldPosition,
	float relativeApproachSpeed,
	const void* target)
{
	PublishGameEvent(WallImpactEvent{
		worldPosition,
		relativeApproachSpeed,
		reinterpret_cast<std::uintptr_t>(target),
	});
}

void Game::NotifyPocketFeedback(
	const DirectX::SimpleMath::Vector3& worldPosition,
	bool playerPocket,
	bool finisher,
	int damage,
	const EnemyBall* target)
{
	InvalidateDebugCombatForecast(
		playerPocket ? "プレイヤーポケット" : "エネミーポケット");
	PublishGameEvent(PocketFeedbackEvent{
		worldPosition,
		playerPocket,
		finisher,
		damage,
		reinterpret_cast<std::uintptr_t>(target),
		target != nullptr ? target->GetEnemyId() : std::string(),
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

void Game::NotifyChargeInterrupted(
	const EnemyBall& enemy,
	const ChargeInterruptionResult& result)
{
	const DirectX::SimpleMath::Vector3 position =
		result.reason == ChargeInterruptReason::Pocketed
			? enemy.GetPocketEntryPosition()
			: enemy.GetPosition();
	InvalidateDebugCombatForecast("Charge妨害");
	const auto enemies = m_RuntimeState->World().GetComponents<EnemyBall>();
	const auto found = std::find(enemies.begin(), enemies.end(), &enemy);
	const std::string targetId = found != enemies.end()
		? "enemy:" + std::to_string(
			static_cast<std::size_t>(std::distance(enemies.begin(), found)))
		: std::string();
	RecordBalanceEvent("enemy_charge_interrupted", {
		{ "enemy_id", enemy.GetEnemyId() },
		{ "target_id", targetId },
		// This callback is emitted only after a currently charging enemy has
		// been interrupted. The EnemyBall has already cleared its internal
		// planned action by this point, so reading it here incorrectly reports
		// Wait and classifies an immediate prevented Charge as a future benefit.
		{ "planned_action", ToString(EnemyActionType::ChargeAttack) },
		{ "prevented_attack_damage",
			static_cast<int>(std::ceil(
				enemy.GetAttack() *
				GetEnemyActionConfig().chargeDamageMultiplier)) },
		{ "reason", ToString(result.reason) },
		{ "movement_distance", result.movementDistance },
		{ "movement_threshold", result.movementThreshold },
		{ "pierce_damage", result.pierceDamage },
		{ "pierce_damage_threshold", result.pierceDamageThreshold },
	});
	PublishGameEvent(ChargeInterruptedEvent{
		position,
		static_cast<int>(result.reason),
		reinterpret_cast<std::uintptr_t>(&enemy),
		enemy.GetEnemyId(),
	});
}

void Game::NotifyAxisResourceChanged(
	AxisResourceKind axis,
	AxisResourceOwner owner,
	int valueBefore,
	int valueAfter,
	AxisResourceChangeReason reason,
	const std::string& abilityId,
	bool finisher,
	const DirectX::SimpleMath::Vector3& worldPosition,
	const EnemyBall* target)
{
	if (valueBefore == valueAfter && !finisher)
	{
		return;
	}
	PublishGameEvent(AxisResourceChangedEvent{
		axis,
		owner,
		valueBefore,
		valueAfter,
		reason,
		abilityId,
		finisher,
		worldPosition,
		reinterpret_cast<std::uintptr_t>(target),
	});
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
				m_PersistenceRuntime->Statistics().BeginShot(value.ballId);
				if (m_AudioFeedback != nullptr)
				{
					m_AudioFeedback->OnShotFired(value);
				}
				if (m_GamePresentation != nullptr)
				{
					m_GamePresentation->OnShotFired(value.ballCategory);
				}
			}
			else if constexpr (std::is_same_v<EventType, CollisionImpactEvent>)
			{
				if (m_GamePresentation != nullptr &&
					!m_BalanceRuntime->autoPlayer.IsEnabled())
				{
					m_GamePresentation->OnCollisionImpact(value);
				}
				if (m_AudioFeedback != nullptr)
				{
					const int existingHeavyChainCount =
						m_GamePresentation != nullptr
						? m_GamePresentation->GetHeavyPhysicsChainCount()
						: 0;
					m_AudioFeedback->OnCollisionImpact(
						value,
						existingHeavyChainCount);
				}
			}
			else if constexpr (std::is_same_v<EventType, WallImpactEvent>)
			{
				if (m_AudioFeedback != nullptr)
				{
					m_AudioFeedback->OnWallImpact(value);
				}
			}
			else if constexpr (std::is_same_v<EventType, EnemyDamageEvent>)
			{
				if (m_BattleState->controller.GetState() ==
					BattleState::BallsMoving)
				{
					++m_BattleState->presentationHitCount;
					m_BattleState->presentationDamage +=
						(std::max)(0, value.damage);
					if (value.defeated)
						++m_BattleState->presentationDefeatCount;
				}
				if (m_AudioFeedback != nullptr)
				{
					m_AudioFeedback->OnEnemyDamage(value);
				}
				m_PersistenceRuntime->Statistics().RecordEnemyDamage(value.damage);
				if (m_GamePresentation != nullptr &&
					!m_BalanceRuntime->autoPlayer.IsEnabled())
				{
					m_GamePresentation->OnCombatFeedback(
						value.worldPosition,
						value.damage,
						value.defeated,
						value.enemyEnemyCollision,
						value.targetKey,
						value.targetEnemyId);
				}
			}
			else if constexpr (std::is_same_v<EventType, ChainImpactEvent>)
			{
				if (m_GamePresentation != nullptr &&
					!m_BalanceRuntime->autoPlayer.IsEnabled())
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
					m_PersistenceRuntime->Statistics().RecordEnemyDamage(value.damage);
				}
				if (m_GamePresentation != nullptr &&
					!m_BalanceRuntime->autoPlayer.IsEnabled())
				{
					m_GamePresentation->OnPocketFeedback(
						value.worldPosition,
						value.playerPocket,
						value.finisher,
						value.damage,
						value.targetKey,
						value.targetEnemyId);
				}
			}
			else if constexpr (std::is_same_v<EventType, PlayerDamageEvent>)
			{
				m_PersistenceRuntime->Statistics().RecordPlayerDamage(value.damage);
				BalanceLogger::GetInstance().RecordPlayerDamage(
					value.source,
					value.damage,
					value.sourceId);
				if (m_GamePresentation != nullptr &&
					!m_BalanceRuntime->autoPlayer.IsEnabled())
				{
					m_GamePresentation->OnPlayerDamage(
						value.damage,
						value.source);
				}
			}
			else if constexpr (std::is_same_v<EventType, AxisResourceChangedEvent>)
			{
				const std::size_t axis = static_cast<std::size_t>(value.axis);
				if (axis < m_BattleState->presentationResourceGained.size())
				{
					const int delta = value.valueAfter - value.valueBefore;
					if (delta > 0)
						m_BattleState->presentationResourceGained[axis] += delta;
					else
						m_BattleState->presentationResourceConsumed[axis] += -delta;
					if (value.finisher)
						m_BattleState->presentationFinisherMask |=
							(1u << static_cast<unsigned int>(axis));
				}
				if (m_GamePresentation != nullptr &&
					(!m_BalanceRuntime->autoPlayer.IsEnabled() || IsDebugMode()))
				{
					m_GamePresentation->OnAxisResourceChanged(value);
				}
			}
			else if constexpr (std::is_same_v<EventType, AxisAbilityResolvedEvent>)
			{
				if (m_GamePresentation != nullptr &&
					(!m_BalanceRuntime->autoPlayer.IsEnabled() || IsDebugMode()))
				{
					m_GamePresentation->OnAxisAbilityResolved(value);
				}
			}
			else if constexpr (std::is_same_v<EventType, ChargeInterruptedEvent>)
			{
				++m_BattleState->presentationChargeInterruptions;
				if (m_GamePresentation != nullptr &&
					(!m_BalanceRuntime->autoPlayer.IsEnabled() || IsDebugMode()))
				{
					m_GamePresentation->OnChargeInterrupted(
						value.worldPosition,
						static_cast<ChargeInterruptReason>(value.reason));
				}
			}
			else if constexpr (std::is_same_v<EventType, EnemyAbilityStateChangedEvent>)
			{
				if (m_GamePresentation != nullptr &&
					(!m_BalanceRuntime->autoPlayer.IsEnabled() || IsDebugMode()))
				{
					m_GamePresentation->OnEnemyAbilityStateChanged(value);
				}
			}
			else if constexpr (std::is_same_v<EventType, ShotResolvedEvent>)
			{
				if (m_GamePresentation != nullptr &&
					(!m_BalanceRuntime->autoPlayer.IsEnabled() || IsDebugMode()))
				{
					m_GamePresentation->OnShotResolved(value);
				}
			}
			else if constexpr (std::is_same_v<EventType, BallAcquiredEvent>)
			{
				m_PersistenceRuntime->Statistics().RecordBallAcquired(value.ballId);
			}
		},
		event);
}

void Game::NotifyEnemyAbilityStateChanged(
	const EnemyAbilityStateChangedEvent& event)
{
	const auto enemies = m_RuntimeState->World().GetComponents<EnemyBall>();
	const EnemyBall* changedEnemy = reinterpret_cast<const EnemyBall*>(
		event.targetKey);
	const auto found = std::find(
		enemies.begin(), enemies.end(), changedEnemy);
	const std::string targetId = found != enemies.end()
		? "enemy:" + std::to_string(
			static_cast<std::size_t>(std::distance(enemies.begin(), found)))
		: std::string();
	const EnemyActionType plannedAction = changedEnemy != nullptr
		? changedEnemy->GetPlannedAction()
		: EnemyActionType::Wait;
	BalanceLogger::GetInstance().RecordEvent(
		"enemy_board_ability_state_changed",
		{
			{ "enemy_id", event.targetEnemyId },
			{ "target_id", targetId },
			{ "ability_id", event.abilityId },
			{ "reason", event.reason },
			{ "previous_stage", event.previousStage },
			{ "current_stage", event.currentStage },
			{ "previous_attack", event.previousAttack },
			{ "current_attack", event.currentAttack },
			{ "nearby_living_allies", event.nearbyLivingAllies },
			{ "collision_speed", event.collisionSpeed },
			{ "planned_action", ToString(plannedAction) },
			{ "affects_next_turn",
				plannedAction == EnemyActionType::NormalAttack ||
				plannedAction == EnemyActionType::ChargeAttack },
		});
	PublishGameEvent(event);
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
	m_BalanceRuntime->scaling.ApplyToEnemyData(
		enemyData,
		m_BalanceRuntime->baselineEnemyHpMultiplier,
		m_BalanceRuntime->baselineEnemyAttackDelta,
		m_RunController->Status().progress,
		m_PersistenceRuntime->ActiveAscension(),
		IsDebugMode());
}
// Balance Auto Full Hp Enemy Survivedを通知する。
void Game::NotifyBalanceAutoFullHpEnemySurvived()
{
	const PlayerBallData* currentBall =
		m_RunController->Deck().GetCurrent();
	m_BalanceRuntime->autoPlayer.NotifyFullHpEnemySurvived(currentBall);
}

// Current Player Ballを破棄する。
void Game::DiscardCurrentPlayerBall()
{
	if (!m_RunController->Deck().HasCurrent())
	{
		m_RunController->Deck().ClearCurrentUsed();
		return;
	}

	if (!m_RunController->Deck().IsCurrentUsed())
	{
		return;
	}

	std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	if (!players.empty() && players[0] != nullptr)
	{
		CapturePlayerStatusFrom(players[0]);
	}

	m_RunController->Deck().DiscardCurrentIfUsed();
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
	file << "Scene = " << GetSceneDebugName(GetCurrentScene()) << "\n";
	file << "[Scene]\n";
	file << "Scene = " << GetSceneDebugName(GetCurrentScene()) << "\n";

	file << "BattleState = "
		<< ToString(GetBattleState())
		<< " (" << static_cast<int>(GetBattleState()) << ")\n";

	file << "ClearReward = "
		<< (IsClearRewardActive() ? "true" : "false")
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

	std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();

	file << "[BallCounts]\n";
	file << "PlayerRunCurrentHp = "
		<< m_RunController->Status().currentHp << "\n";

	file << "PlayerRunMaxHp = "
		<< m_RunController->Status().maxHp << "\n";

	file << "[Deck]\n";
	file << "DrawPile = "
		<< m_RunController->Deck().GetDrawPileCount() << "\n";

	file << "DiscardPile = "
		<< m_RunController->Deck().GetDiscardPileCount() << "\n";

	file << "OfferCount = "
		<< m_RunController->Deck().GetOfferCount() << "\n";

	file << "TotalDeckCount = "
		<< m_RunController->Deck().GetRewardTargetCount() << "\n";

	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall != nullptr)
	{
		file << "CurrentBallId = "
			<< currentBall->definitionId
			<< "\n";

		file << "CurrentBallAttack = "
			<< currentBall->status.attack
			<< "\n";

		file << "CurrentBallUsed = "
			<< (m_RunController->Deck().IsCurrentUsed() ? "true" : "false")
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
		<< m_RunController->Status().money
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
    rules.relics = m_RunController->Relics();
    const auto* ball = m_RunController->Deck().GetCurrent();
    if (ball == nullptr)
        ball = m_RunController->Deck().GetOffer(GetSelectedBallOfferIndex());
    if (ball != nullptr) rules.ballId = ball->definitionId;
    rules.launchPower = launchPower;
    return rules;
}

// Shot Relic Rulesを取得して保持する。
ShotRelicRules Game::CaptureShotRelicRules() const
{
    auto rules = m_BattleState->controller.GetShotRelicRules();
    // Live build effects require a committed deck ball, preserving shot lifecycle semantics.
    if (!m_RunController->Deck().GetCurrent()) rules.ballId.clear();
    return rules;
}

// Shot Relic Rulesを確定する。
void Game::CommitShotRelicRules(const ShotRelicRules& rules)
{
    m_BattleState->controller.SetShotRelicRules(rules);
}
