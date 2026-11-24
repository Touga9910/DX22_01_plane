#pragma execution_character_set("utf-8")
#include "RestSiteScene.h"

#include "GameRuntime.h"
#include "GameSceneCommands.h"
#include "GameView.h"
#include "Input.h"
#include "UiText.h"
#include "imgui/imgui.h"

#include <cstdio>

/// <summary>
/// 休憩所シーンを生成し、初期化処理を呼び出す。
/// </summary>
RestSiteScene::RestSiteScene()
{
	Init();
}

/// <summary>
/// 休憩所シーンの終了処理を呼び出す。
/// </summary>
RestSiteScene::~RestSiteScene()
{
	Uninit();
}

/// <summary>
/// 休憩所シーンの初期化の呼び出し口を提供する。関数本体は空。
/// </summary>
void RestSiteScene::Init()
{

}

/// <summary>
/// 休憩所の選択入力を受け付け、回復や強化などの選択した行動を処理する。
/// </summary>
void RestSiteScene::Update()
{
	if (m_UiState.modalOpen) return;
	const bool confirmed = m_Menu.UpdateVertical(3);

	const RestSiteViewSnapshot view = GameView::CaptureRestSite();
	const int ballCount = view.deckBallCount;
	if (m_Menu.GetIndex() == 1 && ballCount > 0)
	{
		if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A))
		{
			m_SelectedBall = (m_SelectedBall + ballCount - 1) % ballCount;
		}
		if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D))
		{
			m_SelectedBall = (m_SelectedBall + 1) % ballCount;
		}
	}

	if (!confirmed)
	{
		return;
	}

	if (m_Menu.GetIndex() == 2)
	{
		if (!m_ActionUsed &&
			view.availableRestBenefit)
		{
			m_Message = UiText::MustChooseRest;
			return;
		}
		GameSceneCommands::LeaveRestSite();
		return;
	}

	if (m_ActionUsed)
	{
		m_Message = UiText::RestAlreadyUsed;
		return;
	}

	if (m_Menu.GetIndex() == 0)
	{
		if (GameSceneCommands::RestHeal())
		{
			m_ActionUsed = true;
			char message[128]{};
			sprintf_s(
				message,
				UiText::RecoveredFormat,
				view.restHealPercent);
			m_Message = message;
		}
		else
		{
			m_Message = UiText::HpAlreadyFull;
		}
	}
	else if (GameSceneCommands::RestUpgradeBall(m_SelectedBall))
	{
		m_ActionUsed = true;
		m_Message = UiText::BallUpgraded;
	}
	else
	{
		m_Message = UiText::BallAlreadyMax;
	}
}

/// <summary>
/// 休憩所の状態から表示用データを組み立て、利用できるサービスと選択画面を描画する。
/// </summary>
void RestSiteScene::DrawUI()
{
    const auto view=GameView::CaptureRestSite();
    LoungeServiceUi::RestModel model;
    model.hp=view.playerCurrentHp; model.maxHp=view.playerMaxHp;
    model.money=GameView::CaptureShop().playerMoney;
    model.healAmount=view.restHealAmount; model.healPercent=view.restHealPercent;
    model.canHeal=view.canRestHeal; model.availableBenefit=view.availableRestBenefit;
    model.bossPreparation=view.bossPreparation; model.used=m_ActionUsed;
    model.selectedAction=m_Menu.GetIndex(); model.message=m_Message;
    for(int index=0;index<view.deckBallCount;++index) model.deck.push_back(GameView::DeckBall(index));
    const auto intent=LoungeServiceUi::DrawRest(model,m_UiState);
    if(intent.action<0) return;
    if(intent.action==1) m_SelectedBall=intent.index;
    m_Menu.Confirm(intent.action,3);
}


/// <summary>
/// 休憩所シーンが登録したゲームオブジェクトを破棄し、管理一覧を空にする。
/// </summary>
void RestSiteScene::Uninit()
{
	for (GameObject* gameObject : m_SceneGameObjects)
	{
		GameRuntime::DestroyObject(gameObject);
	}
	m_SceneGameObjects.clear();
}
