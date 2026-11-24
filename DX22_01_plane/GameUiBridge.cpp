#include "Game.h"
#include "GamePresentation.h"
#include "GamePresentationPrimitives.h"
#include "TableConfig.h"
#include "imgui/imgui.h"
#include <array>
#include "GameSettingsRuntime.h"
#include "GameRuntimeState.h"
#include "GameRewardState.h"
#include "GameBallSelectionState.h"
#include "GamePauseState.h"
#include "RunController.h"
#include "PlayerRunStatus.h"
#include "GameDebugController.h"
#include "input.h"

#pragma execution_character_set("utf-8")

void Game::DrawPauseUI()
{
	if (m_GamePresentation == nullptr)
	{
		return;
	}

	PausePresentationModel model{};
	model.settings = m_SettingsRuntime->Current();
	model.debugMode = IsDebugMode();
	model.confirmReturnToTitle =
		m_PauseState->IsReturnToTitleConfirmationActive();
	model.battleScene = GetCurrentSceneType() == SceneType::Battle;
    model.titleScene = GetCurrentSceneType() == SceneType::Title;
	model.clearRewardActive = IsClearRewardActive();
	const PausePresentationIntent intent = m_GamePresentation->DrawPause(model);

	if (intent.settingsChanged)
	{
		if (m_SettingsRuntime->Replace(intent.settings))
		{
			Input::SetVibration(0, 0.0f);
		}
	}
	if (intent.previewSceneTransition)
	{
		m_RuntimeState->Transition().RequestPreview(intent.settings.sceneTransition);
	}
	if (intent.applyDisplay)
	{
		m_PauseState->RequestDisplayApply();
		m_SettingsRuntime->SaveIfDirty();
	}
	if (intent.resume)
	{
		m_SettingsRuntime->SaveIfDirty();
		m_PauseState->ResumeAndDismissConfirmation();
	}
	if (intent.requestDebugExit)
	{
		m_DebugController->RequestExit();
	}
	if (intent.requestReturnConfirmation)
	{
		m_PauseState->RequestReturnToTitleConfirmation();
	}
	if (intent.saveAndReturnToTitle)
	{
		SaveAndReturnToTitle();
	}
	if (intent.cancelReturnToTitle)
	{
		m_PauseState->CancelReturnToTitleConfirmation();
	}
}

void Game::DrawBallSelectionUI()
{
	if (m_GamePresentation == nullptr)
	{
		return;
	}

	BallSelectionPresentationModel model{};
	model.selectedOfferIndex = GetSelectedBallOfferIndex();
	model.selectedHoldIndex = GetHeldBallOfferIndex();
	model.handHiddenRatio = m_BallSelectionState->HandHiddenRatio();
	model.selectionInteractive =
		m_BallSelectionState->IsSelectionInteractive();
	const int offerCount = GetBallOfferCount();
	model.offers.reserve(static_cast<std::size_t>(offerCount));
	model.effectiveAttacks.reserve(static_cast<std::size_t>(offerCount));
	for (int index = 0; index < offerCount; ++index)
	{
		const PlayerBallData* ball = GetBallOffer(index);
		model.offers.push_back(ball);
		model.effectiveAttacks.push_back(GetEffectivePlayerBallAttack(ball));
	}

	const BallSelectionPresentationIntent intent =
		m_GamePresentation->DrawBallSelection(model);
	m_BallSelectionState->ApplyPresentationSelection(
		intent.selectedOfferIndex,
		intent.selectedHoldIndex,
		offerCount);
	if (intent.selectionChanged)
	{
		ApplySelectedBallPreview();
	}
	if (intent.confirmSelection)
	{
		m_BallSelectionState->ConfirmSelection(offerCount);
	}
}

void Game::DrawClearRewardUI()
{
	if (m_GamePresentation == nullptr)
	{
		return;
	}

	ClearRewardPresentationModel model{};
    // Project an inset cloth rectangle and fit every reward control inside it.
    std::array<RewardFieldLayout::Point,4> cloth{};
    const float x=TableConfig::GetFieldWidth()*0.5f-5.0f;
    const float z=TableConfig::GetFieldDepth()*0.5f-5.0f;
    const DirectX::SimpleMath::Vector3 corners[]={{-x,TableConfig::FIELD_HEIGHT,z},
        {x,TableConfig::FIELD_HEIGHT,z},{x,TableConfig::FIELD_HEIGHT,-z},{-x,TableConfig::FIELD_HEIGHT,-z}};
    bool projected=true;
    for(int i=0;i<4;++i)
    {
        ImVec2 point;
        if (!GamePresentationPrimitives::TryProjectToScreen(corners[i],point)) { projected=false; break; }
        cloth[i]={point.x,point.y};
    }
    if(projected) model.fieldBounds=RewardFieldLayout::Fit(cloth);

	model.stageRewardMoney = m_RunController->GetCurrentStageRewardMoney();
	model.playerMoney = m_RunController->Status().money;
	model.playerCurrentHp = m_RunController->Status().currentHp;
	model.playerMaxHp = m_RunController->Status().maxHp;
	model.midBossRelicSelectionActive =
		m_RewardState->IsMidBossRelicSelectionActive();
	model.clearRewardChosen = m_RewardState->IsChosen();
	model.rewardBallAcquired = IsClearRewardBallAcquired();
	model.rewardBallCost = GetClearRewardBallCost();
	model.selectedRelicOfferIndex = m_RewardState->SelectedRelicOfferIndex();
	model.selectedRewardIndex = m_RewardState->SelectedRewardIndex();
	model.selectedRewardBallIndex = m_RewardState->SelectedRewardBallIndex();
	model.extraMoneyAmount = kClearRewardExtraMoney;
	model.rewardMessage = m_RewardState->Message();
	for (int index = 0; index < GetMidBossRelicOfferCount(); ++index)
	{
		model.relicOffers.push_back(GetMidBossRelicOffer(index));
	}
	for (int index = 0; index < GetClearRewardBallOfferCount(); ++index)
	{
		model.newBallOffers.push_back(GetClearRewardBallOffer(index));
	}
	const int targetCount = GetDeckBallCount();
	model.upgradeTargets.reserve(static_cast<std::size_t>(targetCount));
	for (int index = 0; index < targetCount; ++index)
	{
		model.upgradeTargets.push_back({
			GetDeckBall(index),
			GetClearRewardUpgradeCost(index),
			});
	}

	const ClearRewardPresentationIntent intent =
		m_GamePresentation->DrawClearReward(model);
	m_RewardState->ApplyPresentationSelection(
		intent.selectedRelicOfferIndex,
		intent.selectedRewardIndex,
		intent.selectedRewardBallIndex);
	if (intent.confirm)
	{
		m_RewardState->RequestConfirmation();
	}
	if (intent.skip) m_RewardState->RequestSkip();
	m_RewardState->SetModalOpen(intent.modalOpen);
}
