#include "Game.h"
#include "GameBattleState.h"
#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameRewardState.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "RunController.h"
#include "PlayerRunStatus.h"
#include "RunProgressController.h"
#include "RunProgressConstants.h"
#include "PlayerDeck.h"
#include "PlayerBallData.h"
#include "EnemyBall.h"
#include "BalanceLogger.h"
#include "GameSaveManager.h"
#include "UiText.h"
#include "input.h"
#include "json/json.hpp"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <utility>

namespace
{
constexpr int kClearRewardCount = 3;
	/// <summary>
	/// リストに含まれる撃破前の敵球を数える。
	/// </summary>
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
}

/// <summary>
/// ステージクリア報酬を受け取れる状態かを返す。
/// </summary>
bool Game::IsClearRewardActive() const
{
	return m_RewardState->IsActive();
}

/// <summary>
/// クリア報酬で提示している球候補の件数を返す。
/// </summary>
int Game::GetClearRewardBallOfferCount() const
{
	return m_RewardState->BallOfferCount();
}

/// <summary>
/// 指定した報酬候補に対応する球カタログ上の位置を返す。
/// </summary>
int Game::GetClearRewardBallOfferCatalogIndex(int offerIndex) const
{
	return m_RewardState->BallOfferCatalogIndex(offerIndex);
}

/// <summary>
/// クリア報酬の指定候補に対応する球データを取得する。
/// </summary>
const PlayerBallData* Game::GetClearRewardBallOffer(int offerIndex) const
{
	return m_RunController->Deck().GetCatalogBall(
		GetClearRewardBallOfferCatalogIndex(offerIndex));
}

/// <summary>
/// 購入条件を満たしていればクリア報酬の球をデッキに追加し、代金を差し引く。
/// </summary>
bool Game::AddClearRewardBallOffer(int offerIndex)
{
    const int cost = GetClearRewardBallCost();
    if (!IsClearRewardActive() ||
        IsClearRewardBallAcquired() || IsMidBossRelicSelectionActive() ||
        m_RunController->Status().money < cost) return false;
    if (!m_RunController->Deck().AddCatalogBall(GetClearRewardBallOfferCatalogIndex(offerIndex))) return false;
    m_RunController->SpendMoney(cost);
    m_RewardState->MarkBallAcquired();
    return true;
}

/// <summary>
/// ステージクリア報酬で球をすでに獲得したかを返す。
/// </summary>
bool Game::IsClearRewardBallAcquired() const { return m_RewardState->IsBallAcquired(); }

/// <summary>
/// 中ボス撃破後のレリックを選択中かを返す。
/// </summary>
bool Game::IsMidBossRelicSelectionActive() const
{
	return m_RewardState->IsMidBossRelicSelectionActive();
}

/// <summary>
/// ステージクリア報酬が確定済みかを返す。
/// </summary>
bool Game::IsClearRewardChosen() const
{
	return m_RewardState->IsChosen();
}

/// <summary>
/// 球カタログから抽選した球をクリア報酬候補として設定する。
/// </summary>
void Game::RollClearRewardBallOffers()
{
	m_RewardState->SetBallOffers(
		m_RunController->Deck().RollCatalogOfferIndices(
			kClearRewardBallOfferSize));
}

/// <summary>
/// 戦闘の終了結果を反映し、ランの進行状況に応じてクリア報酬の受け取りを開始する。
/// </summary>
void Game::StartClearReward()
{
	if (IsDebugMode() && !IsDebugRunMode()) { FinishDebugBattle(true); return; }
	DiscardCurrentPlayerBall();
	if (m_BattleState->controller.IsPhasePrototype())
	{
		RecordBalanceEvent("phase_ended", {
			{ "phase", m_BattleState->controller.GetCurrentPhase() },
			{ "total_phases", m_BattleState->controller.GetTotalPhases() },
			{ "reason", "battle_victory" },
			{ "living_enemy_count", 0 },
		});
	}
	RecordBalanceEvent("battle_victory", {
		{ "stage_id", m_RunController->Status().GetSelectedStageId() },
		{ "floor", m_RunController->Status().progress },
		{ "combat_mode", ToString(
			m_BattleState->controller.GetCombatMode()) },
		{ "total_phases", m_BattleState->controller.GetTotalPhases() },
		{ "defeated_enemy_count",
			m_BattleState->controller.GetDefeatedEnemyCount() },
		{ "pocket_defeated_enemy_count",
			m_BattleState->controller.GetPocketDefeatedEnemyCount() },
	});

	const std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();
	BalanceLogger& logger = BalanceLogger::GetInstance();
	logger.EndShot(
		m_RunController->Status().currentHp,
		CountAliveEnemies(enemies),
		m_BattleState->controller.GetDefeatedEnemyCount());
	logger.EndStage(
		"clear",
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		m_BattleState->controller.GetDefeatedEnemyCount());
	if (m_RunController->Progress().GetPhase() == RunPhase::FinalBoss &&
		m_BattleState->controller.GetStageType() == StageType::Boss)
	{
		// 最終ボス後は、以後使えないMoneyや取得物を選ばせない。
		CompleteFinalBossRun();
		return;
	}

	// 敵全滅報酬を所持Moneyへ加算する
	CollectStageRewardMoney();

	// 全滅時は攻撃フェーズへ進まないため、報酬計算後に撃破済みの敵を取り除く。
	for (EnemyBall* enemy : enemies)
	{
		if (enemy != nullptr && enemy->IsDefeated())
		{
			DeleteGameObject(enemy->GetGameObject());
		}
	}

	m_RewardState->Prepare(UiText::ChooseClearReward);
	m_RunController->MidBossRelicOffers().clear();
	if (m_BattleState->controller.GetStageType() == StageType::MidBoss)
	{
		RollMidBossRelicOffers();
	}
	m_RunController->Progress().RecordBattleCleared();
	if (m_BattleState->controller.GetStageType() == StageType::MidBoss)
	{
		m_PersistenceRuntime->Statistics().DefeatMidBoss();
	}
	CompleteNormalRouteArea(
		m_BattleState->controller.GetStageType() == StageType::MidBoss
		? "midboss_battle"
		: "normal_battle");
	if (m_BalanceRuntime->validation.HasReachedMaximumClearedStages(
		m_RunController->Progress().GetClearedBattleCount(),
		kNormalRouteAreaGoal))
	{
		RecordBalanceEvent(
			"balance_validation_run_completed",
			{
				{ "reason", "maximum_cleared_stages_reached" },
				{ "cleared_stage_count", m_RunController->Progress().GetClearedBattleCount() },
				{ "maximum_cleared_stages",
					m_BalanceRuntime->validation.GetMaximumClearedStages() },
			});
		logger.EndRun(
			"validation_complete",
			m_RunController->Status().currentHp,
			m_RunController->Status().maxHp,
			m_RunController->Progress().GetClearedBattleCount());
		FinalizeRunResult(true);
		if (!IsDebugMode()) GameSaveManager::Remove();
		ChangeScene(SceneType::Result);
		return;
	}
	RollClearRewardBallOffers();
	nlohmann::json newBallCandidates = nlohmann::json::array();
	for (int offerIndex = 0;
		offerIndex < GetClearRewardBallOfferCount();
		offerIndex++)
	{
		const int catalogIndex =
			GetClearRewardBallOfferCatalogIndex(offerIndex);
		const PlayerBallData* ball =
			GetClearRewardBallOffer(offerIndex);
		if (ball != nullptr)
		{
			newBallCandidates.push_back(
				{
					{ "offer_index", offerIndex },
					{ "catalog_index", catalogIndex },
					{ "ball_id", ball->definitionId },
				});
		}
	}
	nlohmann::json upgradeCandidates = nlohmann::json::array();
	for (int index = 0;
		index < m_RunController->Deck().GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball = m_RunController->Deck().GetRewardTarget(index);
		if (ball != nullptr && ball->CanUpgrade())
		{
			const int upgradeCost =
				GetClearRewardUpgradeCost(index);
			upgradeCandidates.push_back(
				{
					{ "deck_index", index },
					{ "instance_id", ball->instanceId },
					{ "ball_id", ball->definitionId },
					{ "upgrade_level", ball->upgradeLevel },
					{ "upgrade_cost", upgradeCost },
					{ "affordable",
						upgradeCost >= 0 &&
						m_RunController->Status().money >= upgradeCost },
				});
		}
	}
	RecordBalanceEvent(
		"clear_reward_offered",
		{
			{
				"reward_types",
				{ "new_ball", "upgrade_ball", "extra_money" }
			},
			{ "new_ball_candidates", std::move(newBallCandidates) },
			{ "upgrade_candidates", std::move(upgradeCandidates) },
			{
				"upgrade_cost_by_current_level",
				{
					{ "0", 15 },
					{ "1", 30 },
				}
			},
			{ "extra_money_amount", kClearRewardExtraMoney },
		});
	m_RewardState->Activate();
}

/// <summary>
/// クリア報酬が始まっていなければ、現在のステージのクリア処理を開始する。
/// </summary>
void Game::CompleteCurrentStage()
{
	if (!IsClearRewardActive())
	{
		StartClearReward();
	}
}

/// <summary>
/// 報酬画面の入力を処理し、レリックの取得、球の購入・強化、所持金の獲得や次への進行を行う。
/// </summary>
void Game::UpdateClearReward()
{
	if (!IsClearRewardActive()) return;
	const bool confirmed = m_RewardState->ConsumeConfirmationRequest();
	if (m_RewardState->ConsumeSkipRequest() && !IsMidBossRelicSelectionActive())
	{
		RecordBalanceEvent("clear_reward_skipped", {
			{ "ball_acquired", IsClearRewardBallAcquired() },
			{ "controller", "human" },
		});
		ContinueAfterClearReward();
		return;
	}
	if (m_RewardState->IsMidBossRelicSelectionActive())
	{
		const int offerCount = GetMidBossRelicOfferCount();
		if (offerCount <= 0)
		{
			m_RewardState->FinishMidBossRelicSelection();
			return;
		}
		if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A) ||
			Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W))
		{
			m_RewardState->SelectPreviousRelic(offerCount);
		}
		if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D) ||
			Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S))
		{
			m_RewardState->SelectNextRelic(offerCount);
		}
		if (confirmed || Input::GetKeyTrigger(VK_RETURN) || Input::GetKeyTrigger(VK_SPACE))
		{
			AcquireMidBossRelicOffer(m_RewardState->SelectedRelicOfferIndex());
		}
		return;
	}

	if (m_RewardState->IsReadyToContinue())
	{
		if (confirmed)
		{
			ContinueAfterClearReward();
		}
		return;
	}
	// Arrow keys must not change the pending purchase/upgrade behind a modal.
	if (m_RewardState->IsModalOpen() && !confirmed) return;

	if (!confirmed)
	{
		if (Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W))
		{
			m_RewardState->SelectPreviousReward(kClearRewardCount);
		}
		if (Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S))
		{
			m_RewardState->SelectNextReward(kClearRewardCount);
		}

		int targetCount = 0;
		if (m_RewardState->SelectedRewardIndex() == 0)
		{
			targetCount = GetClearRewardBallOfferCount();
		}
		else if (m_RewardState->SelectedRewardIndex() == 1)
		{
			targetCount = m_RunController->Deck().GetRewardTargetCount();
		}

		if (targetCount > 0)
		{
			if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A))
			{
				m_RewardState->SelectPreviousBall(targetCount);
			}
			if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D))
			{
				m_RewardState->SelectNextBall(targetCount);
			}
		}
		return;
	}

	bool rewardApplied = false;
	if (m_RewardState->IsChosen() && m_RewardState->SelectedRewardIndex() != 0)
		return; // Bonus already received; a ball may still be purchased.
	nlohmann::json rewardDetails =
	{
		{ "money_before", m_RunController->Status().money },
	};
	switch (m_RewardState->SelectedRewardIndex())
	{
	case 0:
	{
		std::string acquiredBallId;
		const int catalogIndex =
			GetClearRewardBallOfferCatalogIndex(
				m_RewardState->SelectedRewardBallIndex());
		if (const PlayerBallData* selected =
			GetClearRewardBallOffer(
				m_RewardState->SelectedRewardBallIndex()))
		{
			acquiredBallId = selected->definitionId;
			rewardDetails["reward"] = "new_ball";
			rewardDetails["ball_id"] = selected->definitionId;
			rewardDetails["offer_index"] =
				m_RewardState->SelectedRewardBallIndex();
			rewardDetails["catalog_index"] = catalogIndex;
		}
		rewardApplied = AddClearRewardBallOffer(
			m_RewardState->SelectedRewardBallIndex());
		if (rewardApplied)
		{
			PublishGameEvent(BallAcquiredEvent{ acquiredBallId });
		}
		m_RewardState->SetMessage(rewardApplied
			? UiText::NewBallAdded
			: UiText::NoBallAvailable);
		break;
	}
	case 1:
	{
		const PlayerBallData* selected =
			m_RunController->Deck().GetRewardTarget(
				m_RewardState->SelectedRewardBallIndex());
		const int upgradeCost =
			GetClearRewardUpgradeCost(
				m_RewardState->SelectedRewardBallIndex());
		if (selected != nullptr)
		{
			rewardDetails["reward"] = "upgrade_ball";
			rewardDetails["ball_id"] = selected->definitionId;
			rewardDetails["instance_id"] = selected->instanceId;
			rewardDetails["upgrade_level_before"] = selected->upgradeLevel;
			rewardDetails["upgrade_cost"] = upgradeCost;
		}
		if (upgradeCost < 0)
		{
			m_RewardState->SetMessage(UiText::BallAlreadyMax);
		}
		else if (m_RunController->Status().money < upgradeCost)
		{
			m_RewardState->SetMessage(UiText::UpgradeMoneyShortage);
		}
		else
		{
			int chargedCost = 0;
			rewardApplied = ApplyClearRewardUpgrade(
				m_RewardState->SelectedRewardBallIndex(),
				chargedCost);
			rewardDetails["upgrade_cost"] = chargedCost;
			m_RewardState->SetMessage(rewardApplied
				? UiText::UpgradeApplied
				: UiText::UpgradeUnavailable);
		}
		break;
	}
	case 2:
		rewardDetails["reward"] = "extra_money";
		m_RunController->AddMoney(kClearRewardExtraMoney);
		rewardApplied = true;
		m_RewardState->SetMessage(UiText::ExtraMoneyReceived);
		break;
	default:
		break;
	}

	if (rewardApplied)
	{
		const bool ballOnly = m_RewardState->SelectedRewardIndex() == 0;
		if (ballOnly) rewardDetails["ball_cost"] = GetClearRewardBallCost();
		rewardDetails["money_after"] = m_RunController->Status().money;
		rewardDetails["controller"] = "human";
		RecordBalanceEvent("clear_reward_choice", rewardDetails);
		if (!ballOnly) m_RewardState->MarkChosen();
		if (m_RewardState->IsReadyToContinue())
			ContinueAfterClearReward();
		else
			m_RewardState->ApplyPresentationSelection(
				m_RewardState->SelectedRelicOfferIndex(), ballOnly ? 2 : 0, 0);
	}
}
