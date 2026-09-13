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
	// 実績画面を開いている間は、背面のタイトルメニューへキー入力を渡さない。
	if (m_ShowProgression)
	{
		return;
	}

	const bool confirmed = m_Menu.UpdateVertical(3, false);
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
		case 2:
			Game::GetInstance()->OpenDebugMode();
			return;
		default:
			break;
		}
	}
}

void TitleScene::DrawUI()
{
	if (m_ShowProgression) { DrawProgressionUI(); return; }
	GameUi::PrepareWindow("title", ImVec2(390, 285), ImVec2(500, 405));
	ImGui::Begin(UiText::TitleWindow, nullptr, ImGuiWindowFlags_NoCollapse);
	if (ImGui::Button(m_ConfirmNewRun ? "上書きして新しく始める" : UiText::NewRun, ImVec2(-1, 40)))
		m_Menu.Confirm(0, 3);
	if (m_ConfirmNewRun && ImGui::Button("キャンセル", ImVec2(-1, 32)))
	{
		m_ConfirmNewRun = false;
		m_Message.clear();
	}
	ImGui::BeginDisabled(!m_CanContinue);
	if (ImGui::Button(UiText::ContinueRun, ImVec2(-1, 40))) m_Menu.Confirm(1, 3);
	ImGui::EndDisabled();
	if (ImGui::Button("デバッグモード：デッキ・戦闘設定", ImVec2(-1, 40))) m_Menu.Confirm(2, 3);
	if (ImGui::Button("アセンション・実績", ImVec2(-1, 40))) m_ShowProgression = true;
	ImGui::Text("次の通常ラン：アセンション%d", Game::GetInstance()->GetProgressionProfile().selectedAscension);
	ImGui::TextWrapped("%s", m_SaveSummary.c_str());
	if (!m_Message.empty()) ImGui::TextWrapped("%s", m_Message.c_str());
	ImGui::TextDisabled("ボタンをクリックして進めます。");
	ImGui::TextDisabled("%s", UiText::ManualSaveHint);
	ImGui::End();
}

void TitleScene::DrawProgressionUI()
{
	Game* game = Game::GetInstance();
	const auto& profile = game->GetProgressionProfile();
	const bool hasRunSave = game->HasValidRunSave();
	GameUi::PrepareWindow("progression", ImVec2(270, 60), ImVec2(740, 610));
	ImGui::Begin("アセンション・実績", nullptr, ImGuiWindowFlags_NoCollapse);
	ImGui::Text("クリア %d回 / 挑戦 %d回 / 最高到達エリア %d", profile.totalClears, profile.totalRuns, profile.highestArea);
	ImGui::SeparatorText("アセンション");
	ImGui::Text("選択中 %d / 解放済み %d", profile.selectedAscension, profile.highestUnlockedAscension);
	ImGui::TextWrapped("現在の累積ルール：");
	for (int level = 1; level <= profile.selectedAscension; ++level) ImGui::BulletText("A%d  %s", level, ProgressionProfile::AscensionRule(level));
	if (profile.selectedAscension == 0) ImGui::BulletText("A0  %s", ProgressionProfile::AscensionRule(0));
	ImGui::BeginDisabled(hasRunSave || profile.selectedAscension <= 0);
	if (ImGui::Button("難易度を下げる", ImVec2(180, 34))) game->SetSelectedAscension(profile.selectedAscension - 1);
	ImGui::EndDisabled(); ImGui::SameLine();
	ImGui::BeginDisabled(hasRunSave || profile.selectedAscension >= profile.highestUnlockedAscension);
	if (ImGui::Button("難易度を上げる", ImVec2(180, 34))) game->SetSelectedAscension(profile.selectedAscension + 1);
	ImGui::EndDisabled();
	if (hasRunSave) ImGui::TextDisabled("中断ランがある間は難易度を変更できません。");
	if (profile.highestUnlockedAscension < ProgressionProfile::MaximumAscension)
		ImGui::TextWrapped("A%dをクリアするとA%dを解放します。", profile.highestUnlockedAscension, profile.highestUnlockedAscension + 1);
	else ImGui::TextUnformatted("最高アセンションまで解放済みです。");
	ImGui::SeparatorText("実績と解放");
	ImGui::BeginChild("achievement_list", ImVec2(0, -55), ImGuiChildFlags_Borders);
	for (const auto& achievement : AchievementCatalog)
	{
		const bool done = profile.IsAchievementUnlocked(achievement.id);
		ImGui::TextColored(done ? ImVec4(.35f,.95f,.55f,1) : ImVec4(.72f,.72f,.72f,1), "%s  %s", done ? "達成" : "未達成", achievement.name);
		ImGui::TextWrapped("条件：%s", achievement.condition);
		ImGui::TextWrapped("解放：%s", achievement.reward);
		ImGui::Separator();
	}
	ImGui::EndChild();
	if (ImGui::Button("タイトルへ戻る", ImVec2(-1, 40))) m_ShowProgression = false;
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
