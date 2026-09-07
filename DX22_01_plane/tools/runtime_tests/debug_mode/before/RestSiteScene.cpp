#pragma execution_character_set("utf-8")
#include "RestSiteScene.h"

#include "Game.h"
#include "GameUi.h"
#include "Input.h"
#include "PlayerBallText.h"
#include "PlayerBallUI.h"
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
	GameUi::PrepareWindow("rest", ImVec2(300, 100), ImVec2(680, 520));
	Game* game = Game::GetInstance();
	ImGui::Begin(game->IsBossPreparation() ? "最終準備" : UiText::RestWindow, nullptr, ImGuiWindowFlags_NoCollapse);
	if (game->IsBossPreparation()) ImGui::TextUnformatted("この休憩の後、最終ボスへ進みます。");
	ImGui::Text("HP %d / %d", game->GetPlayerCurrentHp(), game->GetPlayerMaxHp());
	ImGui::TextUnformatted(UiText::ChooseRestAction);
	ImGui::BeginDisabled(m_ActionUsed || !game->CanRestHeal());
	const std::string heal = "回復する (最大HPの" + std::to_string(game->GetRestHealPercent()) + "% / " + std::to_string(game->GetRestHealAmount()) + " HP)";
	if (ImGui::Button(heal.c_str(), ImVec2(-1, 36))) m_Menu.Confirm(0, 3);
	ImGui::EndDisabled();
	if (ImGui::RadioButton("ボールを強化", m_Menu.GetIndex() == 1)) m_Menu.SetIndex(1, 3);
	ImGui::BeginChild("upgrade_targets", ImVec2(0, -95), ImGuiChildFlags_Borders);
	for (int index = 0; index < game->GetDeckBallCount(); ++index)
	{
		const auto* ball = game->GetDeckBall(index);
		if (ball == nullptr) continue;
		ImGui::PushID(index);
		if (PlayerBallUI::Select(*ball, index == m_SelectedBall))
		{
			m_SelectedBall = index;
			m_Menu.SetIndex(1, 3);
		}
		if (index == m_SelectedBall)
		{
			ImGui::TextWrapped("%s", PlayerBallText::GetDescription(ball->definitionId));
			ImGui::TextWrapped("%s", PlayerBallText::GetUpgradePreview(*ball).c_str());
			ImGui::BeginDisabled(m_ActionUsed || !ball->CanUpgrade());
			if (ImGui::Button("この個体を強化する", ImVec2(-1, 34))) m_Menu.Confirm(1, 3);
			ImGui::EndDisabled();
		}
		ImGui::Separator();
		ImGui::PopID();
	}
	ImGui::EndChild();
	if (!m_Message.empty()) ImGui::TextWrapped("%s", m_Message.c_str());
	ImGui::BeginDisabled(!m_ActionUsed && game->HasAvailableRestBenefit());
	if (ImGui::Button("次へ進む", ImVec2(-1, 36))) m_Menu.Confirm(2, 3);
	ImGui::EndDisabled();
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
