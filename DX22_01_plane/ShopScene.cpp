#include "ShopScene.h"

#include "Game.h"
#include "Input.h"
#include "Texture2D.h"
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
	Texture2D* background = Game::GetInstance()->AddObject<Texture2D>();
	background->SetTexture("assets/texture/background2.png");
	background->SetScale(1280.0f, 720.0f, 0.0f);
	m_MySceneObjects.emplace_back(background);
}

void ShopScene::Update()
{
	if (Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W))
	{
		m_SelectedAction = (m_SelectedAction + 3) % 4;
	}
	if (Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S))
	{
		m_SelectedAction = (m_SelectedAction + 1) % 4;
	}

	const bool moveLeft = Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A);
	const bool moveRight = Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D);
	if (m_SelectedAction == 0 && Game::GetInstance()->GetShopBallCount() > 0)
	{
		const int count = Game::GetInstance()->GetShopBallCount();
		if (moveLeft) m_SelectedBuyBall = (m_SelectedBuyBall + count - 1) % count;
		if (moveRight) m_SelectedBuyBall = (m_SelectedBuyBall + 1) % count;
	}
	else if (m_SelectedAction == 1 && Game::GetInstance()->GetDeckBallCount() > 0)
	{
		const int count = Game::GetInstance()->GetDeckBallCount();
		if (moveLeft) m_SelectedRemoveBall = (m_SelectedRemoveBall + count - 1) % count;
		if (moveRight) m_SelectedRemoveBall = (m_SelectedRemoveBall + 1) % count;
	}

	if (!Input::GetKeyTrigger(VK_RETURN) && !Input::GetKeyTrigger(VK_SPACE))
	{
		return;
	}

	if (m_SelectedAction == 3)
	{
		Game::GetInstance()->StartNextBattle();
		return;
	}
	if (m_SelectedAction == 2)
	{
		m_Message = "Relics are not implemented yet.";
		return;
	}

	if (m_SelectedAction == 0)
	{
		if (Game::GetInstance()->BuyShopBall(m_SelectedBuyBall, kBallPrice))
		{
			m_Message = "Ball purchased.";
		}
		else
		{
			m_Message = "Purchase failed. Check your Money.";
		}
	}
	else
	{
		if (Game::GetInstance()->RemoveShopBall(m_SelectedRemoveBall, kRemovePrice))
		{
			m_Message = "Ball removed from the deck.";
			const int count = Game::GetInstance()->GetDeckBallCount();
			if (count > 0) m_SelectedRemoveBall %= count;
		}
		else
		{
			m_Message = "Removal failed. Keep at least one ball and check your Money.";
		}
	}
}

void ShopScene::DrawUI()
{
	ImGui::SetNextWindowPos(ImVec2(260.0f, 70.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(760.0f, 580.0f), ImGuiCond_Always);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
	ImGui::Begin("Shop", nullptr, flags);

	ImGui::Text("Money %d    Deck %d", Game::GetInstance()->GetPlayerMoney(), Game::GetInstance()->GetDeckBallCount());
	ImGui::Separator();
	ImGui::Text("%s Buy Ball - %d Money", m_SelectedAction == 0 ? ">" : " ", kBallPrice);
	ImGui::Text("%s Remove Ball - %d Money", m_SelectedAction == 1 ? ">" : " ", kRemovePrice);
	ImGui::Text("%s Buy Relic - Coming Soon", m_SelectedAction == 2 ? ">" : " ");
	ImGui::Text("%s Leave", m_SelectedAction == 3 ? ">" : " ");

	ImGui::Separator();
	if (m_SelectedAction == 0)
	{
		ImGui::TextUnformatted("Ball catalog (LEFT / RIGHT)");
		for (int index = 0; index < Game::GetInstance()->GetShopBallCount(); index++)
		{
			const PlayerBallData* ball = Game::GetInstance()->GetShopBall(index);
			if (ball != nullptr)
			{
				ImGui::Text("%s %s  ATK:%d DEF:%d",
					index == m_SelectedBuyBall ? ">" : " ",
					ball->definitionId.c_str(), ball->status.attack, ball->status.defense);
			}
		}
	}
	else if (m_SelectedAction == 1)
	{
		ImGui::TextUnformatted("Deck ball to remove (LEFT / RIGHT)");
		for (int index = 0; index < Game::GetInstance()->GetDeckBallCount(); index++)
		{
			const PlayerBallData* ball = Game::GetInstance()->GetDeckBall(index);
			if (ball != nullptr)
			{
				ImGui::Text("%s [%d] %s  ATK:%d DEF:%d",
					index == m_SelectedRemoveBall ? ">" : " ", index,
					ball->definitionId.c_str(), ball->status.attack, ball->status.defense);
			}
		}
	}
	else if (m_SelectedAction == 2)
	{
		ImGui::TextUnformatted("Relic purchases will be implemented later.");
	}

	if (!m_Message.empty())
	{
		ImGui::Separator();
		ImGui::TextUnformatted(m_Message.c_str());
	}
	ImGui::Separator();
	ImGui::TextUnformatted("W/S or UP/DOWN : Select service");
	ImGui::TextUnformatted("A/D or LEFT/RIGHT : Select ball    ENTER/SPACE : Confirm");
	ImGui::End();
}

void ShopScene::Uninit()
{
	for (Component* component : m_MySceneObjects)
	{
		Game::GetInstance()->DeleteComponent(component);
	}
	m_MySceneObjects.clear();
}
