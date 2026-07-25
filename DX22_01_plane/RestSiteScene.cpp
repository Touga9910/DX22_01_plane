#include "RestSiteScene.h"

#include "Game.h"
#include "Input.h"
#include "Texture2D.h"
#include "imgui/imgui.h"

RestSiteScene::RestSiteScene()
{
	Init();
}

RestSiteScene::~RestSiteScene()
{
	Uninit();
}

void RestSiteScene::Init()
{
	Texture2D* background = Game::GetInstance()->AddObject<Texture2D>();
	background->SetTexture("assets/texture/background1.png");
	background->SetScale(1280.0f, 720.0f, 0.0f);
	m_MySceneObjects.emplace_back(background);
}

void RestSiteScene::Update()
{
	if (Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W))
	{
		m_SelectedAction = (m_SelectedAction + 2) % 3;
	}
	if (Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S))
	{
		m_SelectedAction = (m_SelectedAction + 1) % 3;
	}

	const int ballCount = Game::GetInstance()->GetDeckBallCount();
	if (m_SelectedAction == 1 && ballCount > 0)
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

	if (!Input::GetKeyTrigger(VK_RETURN) && !Input::GetKeyTrigger(VK_SPACE))
	{
		return;
	}

	if (m_SelectedAction == 2)
	{
		Game::GetInstance()->StartNextBattle();
		return;
	}

	if (m_ActionUsed)
	{
		m_Message = "A rest action has already been used.";
		return;
	}

	if (m_SelectedAction == 0)
	{
		if (Game::GetInstance()->RestHeal())
		{
			m_ActionUsed = true;
			m_Message = "HP fully restored.";
		}
		else
		{
			m_Message = "HP is already full.";
		}
	}
	else if (Game::GetInstance()->RestUpgradeBall(m_SelectedBall))
	{
		m_ActionUsed = true;
		m_Message = "Ball upgraded to its next fixed level.";
	}
	else
	{
		m_Message = "This ball is already MAX +2.";
	}
}

void RestSiteScene::DrawUI()
{
	ImGui::SetNextWindowPos(ImVec2(300.0f, 100.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(680.0f, 500.0f), ImGuiCond_Always);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
	ImGui::Begin("Rest Site", nullptr, flags);

	ImGui::Text("HP %d / %d", Game::GetInstance()->GetPlayerCurrentHp(), Game::GetInstance()->GetPlayerMaxHp());
	ImGui::TextUnformatted("Choose one free action during this visit.");
	ImGui::Separator();
	ImGui::Text("%s Rest - Fully restore HP", m_SelectedAction == 0 ? ">" : " ");
	ImGui::Text("%s Upgrade - Next fixed level (max +2)", m_SelectedAction == 1 ? ">" : " ");
	ImGui::Text("%s Leave", m_SelectedAction == 2 ? ">" : " ");

	if (m_SelectedAction == 1)
	{
		ImGui::Separator();
		ImGui::TextUnformatted("Upgrade target (LEFT / RIGHT)");
		for (int index = 0; index < Game::GetInstance()->GetDeckBallCount(); index++)
		{
			const PlayerBallData* ball = Game::GetInstance()->GetDeckBall(index);
			if (ball != nullptr)
			{
				ImGui::Text("%s [%d] %s  +%d  ATK:%d DEF:%d",
					index == m_SelectedBall ? ">" : " ", index,
					ball->definitionId.c_str(), ball->upgradeLevel,
					ball->status.attack, ball->status.defense);
				if (ball->CanUpgrade())
				{
					const BallUpgradeStep& next = ball->upgradeTable[ball->upgradeLevel];
					ImGui::Text("    Next: ATK:%d DEF:%d", next.attack, next.defense);
				}
				else
				{
					ImGui::TextUnformatted("    MAX +2");
				}
			}
		}
	}

	if (!m_Message.empty())
	{
		ImGui::Separator();
		ImGui::TextUnformatted(m_Message.c_str());
	}
	ImGui::Separator();
	ImGui::TextUnformatted("W/S or UP/DOWN : Select    ENTER/SPACE : Confirm");
	ImGui::End();
}

void RestSiteScene::Uninit()
{
	for (Component* component : m_MySceneObjects)
	{
		Game::GetInstance()->DeleteComponent(component);
	}
	m_MySceneObjects.clear();
}
