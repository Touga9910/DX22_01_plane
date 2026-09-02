#include "ShopScene.h"

#include "Game.h"
#include "Input.h"
#include "PlayerBallText.h"
#include "Texture2D.h"
#include "Texture2DFactory.h"
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
	Game::GetInstance()->RollShopRelicOffers();
	Texture2D* background = Texture2DFactory::Create(*Game::GetInstance());
	background->SetTexture("assets/texture/background2.png");
	background->SetScale(1280.0f, 720.0f, 0.0f);
	m_SceneGameObjects.emplace_back(background->GetGameObject());
}

void ShopScene::Update()
{
	const bool confirmed = m_Menu.UpdateVertical(4);

	const bool moveLeft = Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A);
	const bool moveRight = Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D);
	if (m_Menu.GetIndex() == 0 && Game::GetInstance()->GetShopBallCount() > 0)
	{
		const int count = Game::GetInstance()->GetShopBallCount();
		if (moveLeft) m_SelectedBuyBall = (m_SelectedBuyBall + count - 1) % count;
		if (moveRight) m_SelectedBuyBall = (m_SelectedBuyBall + 1) % count;
	}
	else if (m_Menu.GetIndex() == 1 && Game::GetInstance()->GetDeckBallCount() > 0)
	{
		const int count = Game::GetInstance()->GetDeckBallCount();
		if (moveLeft) m_SelectedRemoveBall = (m_SelectedRemoveBall + count - 1) % count;
		if (moveRight) m_SelectedRemoveBall = (m_SelectedRemoveBall + 1) % count;
	}
	else if (m_Menu.GetIndex() == 2 && Game::GetInstance()->GetShopRelicOfferCount() > 0)
	{
		const int count = Game::GetInstance()->GetShopRelicOfferCount();
		if (moveLeft) m_SelectedRelic = (m_SelectedRelic + count - 1) % count;
		if (moveRight) m_SelectedRelic = (m_SelectedRelic + 1) % count;
	}

	if (!confirmed)
	{
		return;
	}

	if (m_Menu.GetIndex() == 3)
	{
		Game::GetInstance()->LeaveShop();
		return;
	}
	if (m_Menu.GetIndex() == 2)
	{
		const RelicDefinition* relic =
			Game::GetInstance()->GetShopRelicOffer(m_SelectedRelic);
		if (Game::GetInstance()->BuyShopRelicOffer(m_SelectedRelic))
		{
			m_Message = std::string(relic != nullptr ? relic->name : "Relic") +
				UiText::RelicPurchasedSuffix;
		}
		else if (Game::GetInstance()->HasPurchasedShopRelic())
		{
			m_Message = RelicUtf8(u8"\u3053\u306e\u30b7\u30e7\u30c3\u30d7\u3067\u306f\u65e2\u306b\u30ec\u30ea\u30c3\u30af\u3092\u8cfc\u5165\u3057\u3066\u3044\u307e\u3059\u3002");
		}
		else if (relic != nullptr &&
			Game::GetInstance()->HasRelic(relic->type))
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
		if (Game::GetInstance()->BuyShopBall(m_SelectedBuyBall, kBallPrice))
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
		if (Game::GetInstance()->RemoveShopBall(m_SelectedRemoveBall, kRemovePrice))
		{
			m_Message = UiText::BallRemoved;
			const int count = Game::GetInstance()->GetDeckBallCount();
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
	ImGui::SetNextWindowPos(ImVec2(260.0f, 70.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(760.0f, 580.0f), ImGuiCond_Always);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
	ImGui::Begin(UiText::ShopWindow, nullptr, flags);

	ImGui::Text(
		UiText::ShopStatusFormat,
		Game::GetInstance()->GetPlayerMoney(),
		Game::GetInstance()->GetDeckBallCount(),
		Game::GetInstance()->GetOwnedRelicCount(),
		Game::GetInstance()->GetRelicCount());
	ImGui::Separator();
	ImGui::Text(UiText::BuyBallFormat, m_Menu.GetIndex() == 0 ? ">" : " ", kBallPrice);
	ImGui::Text(UiText::RemoveBallFormat, m_Menu.GetIndex() == 1 ? ">" : " ", kRemovePrice);
	ImGui::Text(UiText::BuyRelicFormat, m_Menu.GetIndex() == 2 ? ">" : " ");
	ImGui::Text(UiText::LeaveShopFormat, m_Menu.GetIndex() == 3 ? ">" : " ");

	ImGui::Separator();
	if (m_Menu.GetIndex() == 0)
	{
		ImGui::TextUnformatted(UiText::BallCatalog);
		for (int index = 0; index < Game::GetInstance()->GetShopBallCount(); index++)
		{
			const PlayerBallData* ball = Game::GetInstance()->GetShopBall(index);
			if (ball != nullptr)
			{
				ImGui::Text(UiText::BallStatsFormat,
					index == m_SelectedBuyBall ? ">" : " ",
					PlayerBallText::GetName(ball->definitionId),
					Game::GetInstance()->GetEffectivePlayerBallAttack(ball),
					Game::GetInstance()->GetEffectivePlayerBallDefense(ball),
					ball->status.mass);
				ImGui::Text(UiText::BallPhysicsFormat,
					ball->status.restitution,
					ball->status.friction,
					ball->status.radius);
				ImGui::Text(UiText::BallTraitsFormat,
					ball->status.abilities.pierce ? UiText::Yes : UiText::No,
					ball->status.abilities.anchor ? UiText::Yes : UiText::No);
				if (index == m_SelectedBuyBall)
				{
					ImGui::TextWrapped(
						"    %s",
						PlayerBallText::GetDescription(ball->definitionId));
				}
			}
		}
	}
	else if (m_Menu.GetIndex() == 1)
	{
		ImGui::TextUnformatted(UiText::RemoveCatalog);
		ImGui::Text(
			UiText::MinimumDeckFormat,
			Game::GetInstance()->GetMinimumDeckSize());
		for (int index = 0; index < Game::GetInstance()->GetDeckBallCount(); index++)
		{
			const PlayerBallData* ball = Game::GetInstance()->GetDeckBall(index);
			if (ball != nullptr)
			{
				ImGui::Text(UiText::BallCombatStatsFormat,
					index == m_SelectedRemoveBall ? ">" : " ", index,
					PlayerBallText::GetName(ball->definitionId),
					Game::GetInstance()->GetEffectivePlayerBallAttack(ball),
					Game::GetInstance()->GetEffectivePlayerBallDefense(ball));
				if (index == m_SelectedRemoveBall)
				{
					ImGui::TextWrapped(
						"    %s",
						PlayerBallText::GetDescription(ball->definitionId));
				}
			}
		}
	}
	else if (m_Menu.GetIndex() == 2)
	{
		ImGui::TextUnformatted(RelicUtf8(u8"\u4eca\u56de\u5165\u8377\u3057\u305f\u30ec\u30ea\u30c3\u30af\uff08\u3053\u306e\u5165\u5e97\u30671\u3064\u307e\u3067\uff09"));
		for (int index = 0; index < Game::GetInstance()->GetShopRelicOfferCount(); index++)
		{
			const RelicDefinition* relic = Game::GetInstance()->GetShopRelicOffer(index);
			if (relic == nullptr)
			{
				continue;
			}

			ImGui::Text(
				UiText::RelicLineFormat,
				index == m_SelectedRelic ? ">" : " ",
				relic->name,
				relic->price,
				Game::GetInstance()->HasRelic(relic->type)
					? UiText::OwnedSuffix
					: "");
			ImGui::Text("    rarity: %s", ToString(relic->rarity));
			ImGui::Text("    %s", relic->description);
		}
		if (Game::GetInstance()->GetShopRelicOfferCount() == 0)
		{
			ImGui::TextUnformatted(RelicUtf8(u8"\u672a\u6240\u6301\u306e\u30ec\u30ea\u30c3\u30af\u304c\u3042\u308a\u307e\u305b\u3093\u3002"));
		}
		if (Game::GetInstance()->HasPurchasedShopRelic())
		{
			ImGui::TextUnformatted(RelicUtf8(u8"\u30ec\u30ea\u30c3\u30af\u8cfc\u5165\u6e08\u307f"));
		}
	}

	if (!m_Message.empty())
	{
		ImGui::Separator();
		ImGui::TextUnformatted(m_Message.c_str());
	}
	ImGui::Separator();
	ImGui::TextUnformatted(UiText::ShopSelectControls);
	ImGui::TextUnformatted(UiText::ShopItemControls);
	ImGui::End();
}

void ShopScene::Uninit()
{
	for (GameObject* gameObject : m_SceneGameObjects)
	{
		Game::GetInstance()->DeleteGameObject(gameObject);
	}
	m_SceneGameObjects.clear();
}
