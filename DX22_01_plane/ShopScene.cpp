#pragma execution_character_set("utf-8")
#include "ShopScene.h"

#include "GameRuntime.h"
#include "GameSceneCommands.h"
#include "GameView.h"
#include "GameTypes.h"
#include "Input.h"
#include "UiText.h"
#include "imgui/imgui.h"

/// <summary>
/// ショップシーンを生成し、商品の初期化を呼び出す。
/// </summary>
ShopScene::ShopScene()
{
	Init();
}

/// <summary>
/// ショップシーンの終了処理を呼び出す。
/// </summary>
ShopScene::~ShopScene()
{
	Uninit();
}

/// <summary>
/// ショップに並べる球とレリックの候補を抽選する。
/// </summary>
void ShopScene::Init()
{
	GameSceneCommands::RollShopBallOffers();
	GameSceneCommands::RollShopRelicOffers();

}

/// <summary>
/// ショップの入力を処理し、商品の購入や球の除去に関する選択を進める。
/// </summary>
void ShopScene::Update()
{
	if (m_UiState.modalOpen) return;
	const bool confirmed = m_Menu.UpdateVertical(4);
	const ShopViewSnapshot view = GameView::CaptureShop();

	const bool moveLeft = Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A);
	const bool moveRight = Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D);
	if (m_Menu.GetIndex() == 0 && view.shopBallOfferCount > 0)
	{
		const int count = view.shopBallOfferCount;
		if (moveLeft) m_SelectedBuyBall = (m_SelectedBuyBall + count - 1) % count;
		if (moveRight) m_SelectedBuyBall = (m_SelectedBuyBall + 1) % count;
	}
	else if (m_Menu.GetIndex() == 1 && view.deckBallCount > 0)
	{
		const int count = view.deckBallCount;
		if (moveLeft) m_SelectedRemoveBall = (m_SelectedRemoveBall + count - 1) % count;
		if (moveRight) m_SelectedRemoveBall = (m_SelectedRemoveBall + 1) % count;
	}
	else if (m_Menu.GetIndex() == 2 && view.shopRelicOfferCount > 0)
	{
		const int count = view.shopRelicOfferCount;
		if (moveLeft) m_SelectedRelic = (m_SelectedRelic + count - 1) % count;
		if (moveRight) m_SelectedRelic = (m_SelectedRelic + 1) % count;
	}

	if (!confirmed)
	{
		return;
	}

	if (m_Menu.GetIndex() == 3)
	{
		GameSceneCommands::LeaveShop();
		return;
	}
	if (m_Menu.GetIndex() == 2)
	{
		const RelicDefinition* relic =
			GameView::ShopRelicOffer(m_SelectedRelic);
		if (GameSceneCommands::BuyShopRelicOffer(m_SelectedRelic))
		{
			m_Message = std::string(relic != nullptr ? relic->name : "Relic") +
				UiText::RelicPurchasedSuffix;
		}
		else if (relic != nullptr &&
			GameView::IsShopRelicOfferOwned(m_SelectedRelic))
		{
			m_Message = UiText::RelicAlreadyOwned;
		}
		else
		{
			m_Message = UiText::PurchaseFailed;
		}
		return;
	}

	if (m_Menu.GetIndex() == 0)
	{
		if (GameSceneCommands::BuyShopBallOffer(m_SelectedBuyBall, kBallPrice))
		{
			m_Message = UiText::BallPurchased;
		}
		else
		{
			m_Message = UiText::PurchaseFailed;
		}
	}
	else
	{
		if (GameSceneCommands::RemoveShopBall(m_SelectedRemoveBall, kRemovePrice))
		{
			m_Message = UiText::BallRemoved;
			const int count = GameView::CaptureShop().deckBallCount;
			if (count > 0) m_SelectedRemoveBall %= count;
		}
		else
		{
			m_Message = UiText::RemovalFailed;
		}
	}
}

/// <summary>
/// ショップの状態から価格や候補の表示用データを作成し、選択画面を描画する。
/// </summary>
void ShopScene::DrawUI()
{
    const auto view=GameView::CaptureShop();
    LoungeServiceUi::ShopModel model;
    model.money=view.playerMoney; model.ballPrice=kBallPrice;
    model.removePrice=kRemovePrice; model.minimumDeck=view.minimumDeckSize;
    model.selectedAction=m_Menu.GetIndex(); model.selectedBall=m_SelectedBuyBall;
    model.selectedRelic=m_SelectedRelic; model.message=m_Message;
    for(int index=0;index<view.shopBallOfferCount;++index) model.balls.push_back(GameView::ShopBallOffer(index));
    for(int index=0;index<view.deckBallCount;++index) model.deck.push_back(GameView::DeckBall(index));
    for(int index=0;index<view.shopRelicOfferCount;++index)
    {
        model.relics.push_back(GameView::ShopRelicOffer(index));
        model.relicOwned.push_back(GameView::IsShopRelicOfferOwned(index));
    }
    const auto intent=LoungeServiceUi::DrawShop(model,m_UiState);
    if(intent.action<0) return;
    if(intent.action==0) m_SelectedBuyBall=intent.index;
    if(intent.action==1) m_SelectedRemoveBall=intent.index;
    if(intent.action==2) m_SelectedRelic=intent.index;
    m_Menu.Confirm(intent.action,4);
}


/// <summary>
/// ショップが登録したゲームオブジェクトを破棄し、管理一覧を空にする。
/// </summary>
void ShopScene::Uninit()
{
	for (GameObject* gameObject : m_SceneGameObjects)
	{
		GameRuntime::DestroyObject(gameObject);
	}
	m_SceneGameObjects.clear();
}
