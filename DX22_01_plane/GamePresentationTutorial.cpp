#include "GamePresentation.h"
#include "Game.h"
#include "GameUi.h"
#include "json/json.hpp"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <filesystem>
#include <fstream>

using nlohmann::json;

namespace
{
constexpr const char* kTutorialProgressPath = "runtime/tutorial_progress.json";
}

/// <summary>
/// 通常開始か再生かを記録して、チュートリアルを最初の手順から始める。
/// </summary>
void GamePresentation::StartTutorial(bool replay)
{
	m_TutorialReplay = replay;
	m_TutorialActive = true;
	m_TutorialStep = TutorialStep::Welcome;
}

/// <summary>
/// チュートリアルを閉じ、初回の完了状態をファイルに保存する。
/// </summary>
void GamePresentation::CompleteTutorial()
{
	m_TutorialStep = TutorialStep::Complete;
	m_TutorialActive = false;
	if (!m_TutorialReplay)
	{
		m_TutorialCompleted = true;
		SaveTutorialProgress();
	}
}

/// <summary>
/// チュートリアルの完了状態をJSONファイルに保存する。
/// </summary>
void GamePresentation::SaveTutorialProgress() const
{
	std::error_code error;
	std::filesystem::create_directories("runtime", error);
	std::ofstream destination(kTutorialProgressPath, std::ios::trunc);
	if (!destination)
	{
		return;
	}
	destination << json{
		{ "schema_version", 1 },
		{ "completed", m_TutorialCompleted },
	}.dump(2) << '\n';
}

/// <summary>
/// 現在の手順に対応するチュートリアルを描画し、ガイドの操作を処理する。
/// </summary>
void GamePresentation::DrawTutorial(Game& game)
{
	GameUi::PrepareWindow("tutorial", ImVec2(305, 400), ImVec2(670, 270));
	ImGui::SetNextWindowBgAlpha(0.94f);
	// Keep the guide above the battle HUD and selection cards while it is open.
	ImGui::SetNextWindowFocus();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
	if (!ImGui::Begin("プレイガイド", nullptr, flags))
	{
		ImGui::End();
		return;
	}

	const int stepNumber = static_cast<int>(m_TutorialStep) + 1;
	ImGui::TextColored(
		ImVec4(0.35f, 0.88f, 1.0f, 1.0f),
		"チュートリアル  %d / 6",
		stepNumber);
	ImGui::SameLine();
	ImGui::ProgressBar(
		static_cast<float>(stepNumber) / 6.0f,
		ImVec2(220.0f, 0.0f),
		"");
	ImGui::Separator();

	switch (m_TutorialStep)
	{
	case TutorialStep::Welcome:
		ImGui::TextUnformatted("ボールを弾いて敵を倒す、ビリヤード型ローグライトです。");
		ImGui::TextUnformatted("各ボールにはカテゴリと「生成・利用・決着」の役割があります。");
		ImGui::TextUnformatted("まずは1ショットを操作して、基本の流れを確認します。");
		if (ImGui::Button("ガイドを開始", ImVec2(170.0f, 0.0f)))
		{
			m_TutorialStep = TutorialStep::ChooseAndAim;
		}
		break;
	case TutorialStep::ChooseAndAim:
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "1. ボールを選び、狙う");
		ImGui::TextUnformatted("カードにはカテゴリ・役割・攻撃力・特性が表示されます。");
		ImGui::TextUnformatted("未選択カードをクリックすると選択します。");
		ImGui::TextUnformatted("選択中のカードを再クリックすると詳細を開きます。");
		ImGui::TextUnformatted("マウスを動かして軌道予測線を敵へ合わせ、左ボタンを押してください。");
		break;
	case TutorialStep::SetPower:
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "2. パワーを決める");
		ImGui::TextUnformatted("左ボタンを押したまま手前へドラッグするとパワーが上がります。");
		ImGui::TextUnformatted("離すとショット。右クリックで狙い直せます。");
		break;
	case TutorialStep::WatchShot:
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "3. 衝突結果を見る");
		ImGui::TextUnformatted("赤い数値は与えたダメージ、「撃破！」は敵を倒した印です。");
		ImGui::TextUnformatted("カテゴリ固有資源は、対応カテゴリの生成球だけが蓄積できます。");
		ImGui::TextUnformatted("対象：重量カウント / 貫通痕 / クッションスタック / 錨スタック");
		ImGui::TextUnformatted("貫通痕とクッションは全カテゴリで利用でき、カテゴリ一致時だけ強い効果になります。");
		break;
	case TutorialStep::ReadResult:
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "4. ターンの流れ");
		ImGui::TextUnformatted("ボールが止まると敵が反撃し、次の使用球を選びます。");
		ImGui::TextUnformatted("生成球で資源を作り、利用球・決着球へつないで敵を倒しましょう。");
		ImGui::TextUnformatted("敵を全滅させると報酬を選び、次のルートへ進めます。");
		if (ImGui::Button("理解した", ImVec2(150.0f, 0.0f)))
		{
			m_TutorialStep = TutorialStep::Complete;
		}
		break;
	case TutorialStep::Complete:
		ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "基本操作は完了です！");
		ImGui::TextUnformatted("F1でガイドを再表示、F2でプレイログの分析画面を開けます。");
		if (ImGui::Button("ゲームへ戻る", ImVec2(170.0f, 0.0f)))
		{
			CompleteTutorial();
		}
		break;
	}

	ImGui::Separator();
	if (ImGui::SmallButton("スキップ"))
	{
		CompleteTutorial();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("F1: 表示切替");
	if (!IsBattleScene(game))
	{
		ImGui::SameLine();
		ImGui::TextDisabled("（戦闘開始後に操作できます）");
	}
	ImGui::End();
}

/// <summary>
/// 保存されたJSONからチュートリアルの完了状態を読み込む。
/// </summary>
void GamePresentation::LoadTutorialProgress()
{
	std::ifstream tutorialFile(kTutorialProgressPath);
	if (tutorialFile)
	{
		try
		{
			json progress;
			tutorialFile >> progress;
			m_TutorialCompleted = progress.value("completed", false);
		}
		catch (const json::exception&)
		{
			m_TutorialCompleted = false;
		}
	}
}
