#pragma execution_character_set("utf-8")

#include "TitleScene.h"
#include "Game.h"
#include "GameUi.h"
#include "Input.h"
#include "Texture2D.h"
#include "Texture2DFactory.h"
#include "UiText.h"
#include "imgui/imgui.h"

// コンストラクタ
TitleScene::TitleScene()
{
	Init();
}

// デストラクタ
TitleScene::~TitleScene()
{
	Uninit();
}

// 初期化
void TitleScene::Init()
{
	m_CanContinue = Game::GetInstance()->HasValidRunSave();
	m_SaveSummary = Game::GetInstance()->GetRunSaveSummary();
	//背景画像オブジェクトを作成
	Texture2D* pt = Texture2DFactory::Create(*Game::GetInstance());
	pt->SetTexture("assets/texture/background1.png");
	pt->SetPosition(0.0f, 0.0f, 0.0f);
	pt->SetRotation(0.0f, 0.0f, 0.0f);
	pt->SetScale(1280.0f, 720.0f, 0.0f);
	m_SceneGameObjects.emplace_back(pt->GetGameObject());

	Texture2D* pt2 = Texture2DFactory::Create(*Game::GetInstance());
	pt2->SetTexture("assets/texture/titlerogo.png");
	pt2->SetPosition(0.0f, 100.0f, 0.0f);
	pt2->SetRotation(0.0f, 0.0f, 0.0f);
	pt2->SetScale(700.0f, 150.0f, 0.0f);
	m_SceneGameObjects.emplace_back(pt2->GetGameObject());
}

// 更新
void TitleScene::Update()
{
	const bool confirmed = m_Menu.UpdateVertical(2, false);
	if (m_Menu.WasMoved())
	{
		m_ConfirmNewRun = false;
	}

	// エンターキーを押してステージセレクトへ
	if (confirmed)
	{
		switch (m_Menu.GetIndex())
		{
		case 0:
			if (m_CanContinue && !m_ConfirmNewRun)
			{
				m_ConfirmNewRun = true;
				m_Message = UiText::OverwriteConfirm;
				return;
			}
			Game::GetInstance()->StartNewRun();
			Game::GetInstance()->ChangeScene(SceneType::Select);
			return;
		case 1:
			if (!m_CanContinue)
			{
				m_Message = m_SaveSummary;
				return;
			}
			if (Game::GetInstance()->LoadSavedRun())
			{
				return;
			}
			m_Message = Game::GetInstance()->GetSaveLoadMessage();
			return;
		default:
			break;
		}
	}
}

void TitleScene::DrawUI()
{
	GameUi::PrepareWindow("title", ImVec2(390, 360), ImVec2(500, 280));
	ImGui::Begin(UiText::TitleWindow, nullptr, ImGuiWindowFlags_NoCollapse);
	if (ImGui::Button(m_ConfirmNewRun ? "上書きして新しく始める" : UiText::NewRun, ImVec2(-1, 40)))
		m_Menu.Confirm(0, 2);
	if (m_ConfirmNewRun && ImGui::Button("キャンセル", ImVec2(-1, 32)))
	{
		m_ConfirmNewRun = false;
		m_Message.clear();
	}
	ImGui::BeginDisabled(!m_CanContinue);
	if (ImGui::Button(UiText::ContinueRun, ImVec2(-1, 40))) m_Menu.Confirm(1, 2);
	ImGui::EndDisabled();
	ImGui::TextWrapped("%s", m_SaveSummary.c_str());
	if (!m_Message.empty()) ImGui::TextWrapped("%s", m_Message.c_str());
	ImGui::TextDisabled("ボタンをクリックして進めます。");
	ImGui::TextDisabled("%s", UiText::ManualSaveHint);
	ImGui::End();
}


// 終了処理
void TitleScene::Uninit()
{
	// このシーンのオブジェクトを削除する
	for (GameObject* gameObject : m_SceneGameObjects) {
		Game::GetInstance()->DeleteGameObject(gameObject);
	}
	m_SceneGameObjects.clear();
}
