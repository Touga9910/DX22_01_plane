#include "RestSiteScene.h"

#include "Game.h"
#include "Input.h"
#include "PlayerBallText.h"
#include "Texture2D.h"
#include "Texture2DFactory.h"
#include "UiText.h"
#include "imgui/imgui.h"

#include <cstdio>

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
	Texture2D* background = Texture2DFactory::Create(*Game::GetInstance());
	background->SetTexture("assets/texture/background1.png");
	background->SetScale(1280.0f, 720.0f, 0.0f);
	m_SceneGameObjects.emplace_back(background->GetGameObject());
}

void RestSiteScene::Update()
{
	const bool confirmed = m_Menu.UpdateVertical(3);

	const int ballCount = Game::GetInstance()->GetDeckBallCount();
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
			Game::GetInstance()->HasAvailableRestBenefit())
		{
			m_Message = UiText::MustChooseRest;
			return;
		}
		Game::GetInstance()->LeaveRestSite();
		return;
	}

	if (m_ActionUsed)
	{
		m_Message = UiText::RestAlreadyUsed;
		return;
	}

	if (m_Menu.GetIndex() == 0)
	{
		if (Game::GetInstance()->RestHeal())
		{
			m_ActionUsed = true;
			char message[128]{};
			sprintf_s(
				message,
				UiText::RecoveredFormat,
				Game::GetInstance()->GetRestHealPercent());
			m_Message = message;
		}
		else
		{
			m_Message = UiText::HpAlreadyFull;
		}
	}
	else if (Game::GetInstance()->RestUpgradeBall(m_SelectedBall))
	{
		m_ActionUsed = true;
		m_Message = UiText::BallUpgraded;
	}
	else
	{
		m_Message = UiText::BallAlreadyMax;
	}
}

void RestSiteScene::DrawUI()
{
	ImGui::SetNextWindowPos(ImVec2(300.0f, 100.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(680.0f, 500.0f), ImGuiCond_Always);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
	ImGui::Begin(
		Game::GetInstance()->IsBossPreparation()
		? RelicUtf8(u8"\u6700\u7d42\u6e96\u5099")
		: UiText::RestWindow,
		nullptr,
		flags);
	if (Game::GetInstance()->IsBossPreparation())
	{
		ImGui::TextColored(
			ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
			RelicUtf8(u8"\u3053\u306e\u4f11\u61a9\u306e\u5f8c\u3001\u6700\u7d42\u30dc\u30b9\u3078\u9032\u307f\u307e\u3059"));
	}

	ImGui::Text("HP %d / %d", Game::GetInstance()->GetPlayerCurrentHp(), Game::GetInstance()->GetPlayerMaxHp());
	ImGui::TextUnformatted(UiText::ChooseRestAction);
	ImGui::Separator();
	ImGui::Text(
		UiText::RestActionFormat,
		m_Menu.GetIndex() == 0 ? ">" : " ",
		Game::GetInstance()->GetRestHealPercent(),
		Game::GetInstance()->GetRestHealAmount());
	ImGui::Text(UiText::UpgradeActionFormat, m_Menu.GetIndex() == 1 ? ">" : " ");
	ImGui::Text(UiText::LeaveRestFormat, m_Menu.GetIndex() == 2 ? ">" : " ");

	if (m_Menu.GetIndex() == 1)
	{
		ImGui::Separator();
		ImGui::TextUnformatted(UiText::UpgradeTarget);
		for (int index = 0; index < Game::GetInstance()->GetDeckBallCount(); index++)
		{
			const PlayerBallData* ball = Game::GetInstance()->GetDeckBall(index);
			if (ball != nullptr)
			{
				ImGui::Text(UiText::BallUpgradeStatsFormat,
					index == m_SelectedBall ? ">" : " ", index,
					PlayerBallText::GetName(ball->definitionId), ball->upgradeLevel,
					Game::GetInstance()->GetEffectivePlayerBallAttack(ball),
					Game::GetInstance()->GetEffectivePlayerBallDefense(ball));
				if (index == m_SelectedBall)
				{
					ImGui::TextWrapped(
						"    %s",
						PlayerBallText::GetDescription(ball->definitionId));
				}
				if (ball->CanUpgrade())
				{
					const BallUpgradeStep& next = ball->upgradeTable[ball->upgradeLevel];
					ImGui::Text(UiText::NextStatsFormat, next.attack, next.defense);
				}
				else
				{
					ImGui::TextUnformatted(UiText::MaxUpgrade);
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
	ImGui::TextUnformatted(UiText::RestControls);
	ImGui::End();
}

void RestSiteScene::Uninit()
{
	for (GameObject* gameObject : m_SceneGameObjects)
	{
		Game::GetInstance()->DeleteGameObject(gameObject);
	}
	m_SceneGameObjects.clear();
}
