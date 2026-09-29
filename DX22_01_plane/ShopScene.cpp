#pragma execution_character_set("utf-8")
#include "ShopScene.h"

#include "GameRuntime.h"
#include "GameSceneCommands.h"
#include "GameView.h"
#include "GameTypes.h"
#include "GameUi.h"
#include "Input.h"
#include "PlayerBallText.h"
#include "PlayerBallUI.h"
#include "SceneObjectFactory.h"
#include "Texture2D.h"
#include "UiText.h"
#include "imgui/imgui.h"

ShopScene::ShopScene()
{
	Init();
}

ShopScene::~ShopScene()
{
	Uninit();
}

void ShopScene::Init()
{
	GameSceneCommands::RollShopRelicOffers();
	Texture2D* background = SceneObjectFactory::CreateTexture2D();
	background->SetTexture("assets/texture/background2.png");
	background->SetScale(1280.0f, 720.0f, 0.0f);
	m_SceneGameObjects.emplace_back(background->GetGameObject());
}

void ShopScene::Update()
{
	const bool confirmed = m_Menu.UpdateVertical(4);
	const ShopViewSnapshot view = GameView::CaptureShop();

	const bool moveLeft = Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A);
	const bool moveRight = Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D);
	if (m_Menu.GetIndex() == 0 && view.shopBallCount > 0)
	{
		const int count = view.shopBallCount;
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
		if (GameSceneCommands::BuyShopBall(m_SelectedBuyBall, kBallPrice))
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

void ShopScene::DrawUI()
{
	GameUi::PrepareWindow("shop", ImVec2(260, 70), ImVec2(760, 580));
	ImGui::Begin(UiText::ShopWindow, nullptr, ImGuiWindowFlags_NoCollapse);
	const ShopViewSnapshot view = GameView::CaptureShop();
	ImGui::Text(UiText::ShopStatusFormat, view.playerMoney, view.deckBallCount, view.ownedRelicCount, view.relicCount);
	const char* categories[] = { "ボール購入", "デッキから削除", "レリック購入" };
	for (int category = 0; category < 3; ++category)
	{
		if (category > 0) ImGui::SameLine();
		if (ImGui::RadioButton(categories[category], m_Menu.GetIndex() == category)) m_Menu.SetIndex(category, 4);
	}
	ImGui::Separator();
	ImGui::BeginChild("shop_items", ImVec2(0, -112), ImGuiChildFlags_Borders);
	if (m_Menu.GetIndex() == 0 || m_Menu.GetIndex() == 1)
	{
		const bool buying = m_Menu.GetIndex() == 0;
		int& selected = buying ? m_SelectedBuyBall : m_SelectedRemoveBall;
		const int count = buying ? view.shopBallCount : view.deckBallCount;
		if (!buying) ImGui::Text(UiText::MinimumDeckFormat, view.minimumDeckSize);
		for (int index = 0; index < count; ++index)
		{
			const auto* ball = buying ? GameView::ShopBall(index) : GameView::DeckBall(index);
			if (ball == nullptr) continue;
			ImGui::PushID(index);
			if (PlayerBallUI::Select(*ball, index == selected)) selected = index;
			if (index == selected)
			{
				ImGui::TextWrapped("%s", PlayerBallText::GetDescription(ball->definitionId));
				ImGui::TextWrapped("%s", PlayerBallText::GetStats(*ball, ball->status).c_str());
				ImGui::Text("レリック込み：攻撃 %d", GameView::EffectivePlayerBallAttack(ball));
				const int cost = buying ? kBallPrice : kRemovePrice;
				ImGui::BeginDisabled(view.playerMoney < cost || (!buying && count <= view.minimumDeckSize));
				const std::string label = std::string(buying ? "このボールを購入" : "この個体を削除") + " (" + std::to_string(cost) + " Money)";
				if (ImGui::Button(label.c_str(), ImVec2(-1, 34))) m_Menu.Confirm(buying ? 0 : 1, 4);
				ImGui::EndDisabled();
			}
			ImGui::Separator();
			ImGui::PopID();
		}
	}
	else if (m_Menu.GetIndex() == 2)
	{
		ImGui::TextWrapped("入荷中のレリックは、所持金の範囲で複数購入できます。");
		for (int index = 0; index < view.shopRelicOfferCount; ++index)
		{
			const auto* relic = GameView::ShopRelicOffer(index);
			if (relic == nullptr) continue;
			ImGui::PushID(index);
			const bool owned = GameView::IsShopRelicOfferOwned(index);
			if (ImGui::Selectable(relic->name, m_SelectedRelic == index)) m_SelectedRelic = index;
			ImGui::TextWrapped("%s", relic->description);
			ImGui::Text("%d Money  %s", relic->price, owned ? UiText::OwnedSuffix : "");
			ImGui::BeginDisabled(owned || view.playerMoney < relic->price);
			if (ImGui::Button(owned ? "購入済み" : "購入する", ImVec2(-1, 32)))
			{
				m_SelectedRelic = index;
				m_Menu.Confirm(2, 4);
			}
			ImGui::EndDisabled();
			ImGui::Separator();
			ImGui::PopID();
		}
		if (view.shopRelicOfferCount == 0) ImGui::TextUnformatted("入荷品はありません。");
	}
	ImGui::EndChild();
	if (!m_Message.empty()) ImGui::TextWrapped("%s", m_Message.c_str());
	if (ImGui::Button("ショップを出る", ImVec2(-1, 38))) m_Menu.Confirm(3, 4);
	ImGui::End();
}


void ShopScene::Uninit()
{
	for (GameObject* gameObject : m_SceneGameObjects)
	{
		GameRuntime::DestroyObject(gameObject);
	}
	m_SceneGameObjects.clear();
}
