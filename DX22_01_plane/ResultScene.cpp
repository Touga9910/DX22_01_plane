#include "ResultScene.h"
#include "GameRuntime.h"
#include "GameSceneCommands.h"
#include "GameView.h"
#include "GameTypes.h"
#include "GameObject.h"
#include "GameUi.h"
#include "Input.h"
#include "SceneObjectFactory.h"
#include "Texture2D.h"
#include "PlayerBallText.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#pragma execution_character_set("utf-8")

namespace
{
	struct RunHighlight
	{
		std::string title;
		std::string description;
		ImVec4 color;
	};

	std::string FormatPlayTime(std::uint64_t activeFrames)
	{
		const std::uint64_t totalSeconds = activeFrames / 60;
		const std::uint64_t hours = totalSeconds / 3600;
		const std::uint64_t minutes = (totalSeconds / 60) % 60;
		const std::uint64_t seconds = totalSeconds % 60;
		char text[16]{};
		sprintf_s(
			text,
			"%02llu:%02llu:%02llu",
			static_cast<unsigned long long>(hours),
			static_cast<unsigned long long>(minutes),
			static_cast<unsigned long long>(seconds));
		return text;
	}

	std::vector<RunHighlight> BuildRunHighlights(
		const RunResultSnapshot& result,
		const PlayerRunAnalysis& analysis)
	{
		const ImVec4 gold(1.0f, 0.78f, 0.28f, 1.0f);
		const ImVec4 blue(0.35f, 0.88f, 1.0f, 1.0f);
		const ImVec4 green(0.35f, 0.95f, 0.55f, 1.0f);
		std::vector<RunHighlight> highlights;

		if (result.completed && result.finalBossDefeated)
		{
			highlights.push_back({
				"Armor突破",
				"最終ボスを撃破し、ランを制覇した。",
				gold });
		}
		else if (result.midBossDefeats > 0)
		{
			highlights.push_back({
				"中ボス撃破",
				"中ボスを" + std::to_string(result.midBossDefeats) + "体撃破した。",
				gold });
		}
		else if (result.areaProgress >= 5)
		{
			highlights.push_back({
				"道半ば",
				"通常ルートのエリア" + std::to_string(result.areaProgress) + "へ到達した。",
				blue });
		}
		else
		{
			highlights.push_back({
				"挑戦の足跡",
				std::to_string(result.clearedBattles) + "戦を勝ち抜いた。",
				blue });
		}

		if (result.maximumTurnDamage >= 15)
		{
			highlights.push_back({
				"決定打",
				"1ターンで" + std::to_string(result.maximumTurnDamage) + "ダメージを与えた。",
				gold });
		}
		else if (result.maximumTurnDamage >= 8)
		{
			highlights.push_back({
				"連鎖の一打",
				"1ターンで" + std::to_string(result.maximumTurnDamage) + "ダメージを与えた。",
				green });
		}
		else if (!analysis.mostUsedBallId.empty())
		{
			highlights.push_back({
				std::string(PlayerBallText::GetName(analysis.mostUsedBallId)) + "使い",
				std::to_string(analysis.mostUsedBallCount) + "回のショットでランを組み立てた。",
				green });
		}
		else
		{
			highlights.push_back({
				"ショットメーカー",
				std::to_string(result.totalShots) + "回のショットを放った。",
				green });
		}

		if (result.acquiredBallIds.size() >= 3)
		{
			highlights.push_back({
				"ボール収集家",
				"新しいボールを" + std::to_string(result.acquiredBallIds.size()) + "個獲得した。",
				gold });
		}
		else if (result.damageTaken == 0 && result.clearedBattles > 0)
		{
			highlights.push_back({
				"ノーダメージ",
				"一度もダメージを受けずに戦い抜いた。",
				green });
		}
		else if (!analysis.mostUsedBallId.empty())
		{
			highlights.push_back({
				"愛用の一球",
				std::string("最も多く使ったのは「") +
					PlayerBallText::GetName(analysis.mostUsedBallId) + "」。",
				blue });
		}
		else
		{
			highlights.push_back({
				"ビルドの記録",
				"ボールとレリックを携えて挑戦した。",
				blue });
		}

		return highlights;
	}

	void DrawRunHighlight(const RunHighlight& highlight, int index)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.10f, 0.14f, 0.88f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(
			highlight.color.x,
			highlight.color.y,
			highlight.color.z,
			0.75f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
		const std::string id = "##run_highlight_" + std::to_string(index);
		ImGui::BeginChild(id.c_str(), ImVec2(0.0f, 105.0f), ImGuiChildFlags_Borders);
		ImGui::TextColored(highlight.color, "ACHIEVEMENT");
		ImGui::TextUnformatted(highlight.title.c_str());
		ImGui::Spacing();
		ImGui::TextWrapped("%s", highlight.description.c_str());
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);
	}
}

// コンストラクタ
ResultScene::ResultScene()
{
	Init();
}

// デストラクタ
ResultScene::~ResultScene()
{
	Uninit();
}

// 初期化
void ResultScene::Init()
{
	m_Result = GameView::LastRunResult();
	m_Analysis = AnalyzeRun(m_Result);
	//背景画像オブジェクトを作成
	Texture2D* pt = SceneObjectFactory::CreateTexture2D();
	pt->SetTexture("assets/texture/background2.png");
	pt->SetScale(1280.0f, 720.0f, 0.0f);
	m_SceneGameObjects.emplace_back(pt->GetGameObject());

	//リザルト文字列オブジェクトを作成
	Texture2D* pt2 = SceneObjectFactory::CreateTexture2D();
	pt2->SetTexture("assets/texture/resultString.png");
	pt2->SetScale(700.0f, 100.0f, 0.0f);
	pt2->SetUV(1, 1, 1, 13);//縦1横13分割の、左から1番目上から5番目を指定
	m_SceneGameObjects.emplace_back(pt2->GetGameObject());

	/*
	// 人オブジェクトを作成
	Texture2D* pt3 = SceneObjectFactory::CreateTexture2D();
	pt3->SetTexture("assets/texture/golf_jou_man.png");
	pt3->SetPosition(-300.0f, 0.0f, 0.0f);
	pt3->SetScale(361.0f, 400.0f, 0.0f);
	m_SceneGameObjects.emplace_back(pt3->GetGameObject());
	*/

}

// 更新
void ResultScene::Update()
{
	if (m_ShowAnalysis)
	{
		if (Input::GetKeyTrigger(VK_ESCAPE) ||
			Input::GetKeyTrigger(VK_BACK))
		{
			m_ShowAnalysis = false;
		}
		return;
	}

	if (!m_Menu.UpdateVertical(3))
	{
		return;
	}

	if (m_Menu.GetIndex() == 0)
	{
		m_ShowAnalysis = true;
	}
	else if (m_Menu.GetIndex() == 1)
	{
		GameSceneCommands::StartNewRun();
		GameSceneCommands::ChangeScene(SceneType::Select);
	}
	else
	{
		GameSceneCommands::ChangeScene(SceneType::Title);
	}
}

void ResultScene::DrawUI()
{
	GameUi::PrepareWindow(m_ShowAnalysis ? "result_analysis" : "result", ImVec2(230, 95), ImVec2(820, 590));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	ImGui::Begin(
		m_ShowAnalysis ? "ラン分析" : "ランリザルト",
		nullptr,
		flags);
	if (m_ShowAnalysis)
	{
		DrawAnalysis();
	}
	else
	{
		DrawSummary();
	}
	ImGui::End();
}

void ResultScene::DrawSummary()
{
	const RunResultSnapshot& statistics = m_Result;
	const std::string playTime = FormatPlayTime(statistics.activeFrames);

	ImGui::TextColored(
		statistics.completed
			? ImVec4(0.35f, 0.95f, 0.55f, 1.0f)
			: ImVec4(1.0f, 0.48f, 0.38f, 1.0f),
		statistics.completed ? "ラン完了！" : "ラン終了");
	ImGui::SameLine();
	ImGui::TextDisabled("アセンション%d", GameView::CaptureResult().activeAscension);
	ImGui::Separator();
	if (ImGui::BeginTable("result_summary", 3, ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextColumn();
		ImGui::TextDisabled("到達エリア");
		ImGui::Text("%d / 15", statistics.areaProgress);

		ImGui::TableNextColumn();
		ImGui::TextDisabled("プレイ時間");
		ImGui::TextUnformatted(playTime.c_str());

		ImGui::TableNextColumn();
		ImGui::TextDisabled("ショット数");
		ImGui::Text("%d", statistics.totalShots);
		ImGui::EndTable();
	}

	const auto& progressionUnlocks = GameView::LastProgressionUnlocks();
	if (!progressionUnlocks.empty())
	{
		ImGui::SeparatorText("NEW UNLOCK");
		ImGui::TextColored(
			ImVec4(1.0f, 0.78f, 0.28f, 1.0f),
			"%s",
			progressionUnlocks.front().c_str());
		if (progressionUnlocks.size() > 1)
		{
			ImGui::TextDisabled(
				"ほか %d 件の解放（詳細で確認）",
				static_cast<int>(progressionUnlocks.size() - 1));
		}
	}

	ImGui::SeparatorText("今回の実績");
	const std::vector<RunHighlight> highlights =
		BuildRunHighlights(statistics, m_Analysis);
	if (ImGui::BeginTable("run_highlights", 3, ImGuiTableFlags_SizingStretchSame))
	{
		for (int index = 0; index < static_cast<int>(highlights.size()); ++index)
		{
			ImGui::TableNextColumn();
			DrawRunHighlight(highlights[index], index);
		}
		ImGui::EndTable();
	}

	ImGui::Separator();
	const char* actions[] = { "詳細を見る", "もう一度", "タイトルへ" };
	for (int index = 0; index < 3; ++index)
	{
		if (ImGui::Button(actions[index], ImVec2(-1, 34))) m_Menu.Confirm(index, 3);
	}
}

void ResultScene::DrawAnalysis()
{
	const RunResultSnapshot& statistics = m_Result;
	const std::string playTime = FormatPlayTime(statistics.activeFrames);
	ImGui::TextColored(
		ImVec4(0.35f, 0.88f, 1.0f, 1.0f),
		"ランの詳細");
	ImGui::Separator();
	ImGui::BeginChild("result_detail_scroll", ImVec2(0.0f, -55.0f));
	ImGui::SeparatorText("戦績");
	if (ImGui::BeginTable("result_detail_stats", 2, ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextColumn();
		ImGui::TextDisabled("通常ルート通過");
		ImGui::Text("%d / 15 エリア", statistics.areaProgress);
		ImGui::TextDisabled("総戦闘数");
		ImGui::Text("%d（クリア %d）", statistics.totalBattles, statistics.clearedBattles);
		ImGui::TextDisabled("ショット数");
		ImGui::Text("%d", statistics.totalShots);
		ImGui::TextDisabled("プレイ時間");
		ImGui::TextUnformatted(playTime.c_str());

		ImGui::TableNextColumn();
		ImGui::TextDisabled("総ダメージ");
		ImGui::Text("%d", statistics.totalDamage);
		ImGui::TextDisabled("被ダメージ");
		ImGui::Text("%d", statistics.damageTaken);
		ImGui::TextDisabled("1ターン最大ダメージ");
		ImGui::Text("%d", statistics.maximumTurnDamage);
		ImGui::TextDisabled("終了時HP");
		ImGui::Text("%d / %d", statistics.currentHp, statistics.maxHp);
		ImGui::EndTable();
	}

	ImGui::SeparatorText("ボス戦績");
	ImGui::Text(
		"中ボス：挑戦 %d回 / 撃破 %d回",
		statistics.midBossChallenges,
		statistics.midBossDefeats);
	if (statistics.finalBossReached)
	{
		ImGui::Text(
			"最終ボス：%s（%s）",
			statistics.finalBossId.empty() ? "不明" : statistics.finalBossId.c_str(),
			statistics.finalBossDefeated ? "撃破" : "未撃破");
	}
	else
	{
		ImGui::TextDisabled("最終ボス：未到達");
	}

	ImGui::SeparatorText("効率");
	ImGui::Text(
		"1ショット平均ダメージ：%.1f",
		m_Analysis.averageDamagePerShot);
	ImGui::ProgressBar(
		(std::min)(1.0f, m_Analysis.averageDamagePerShot / 20.0f),
		ImVec2(-1.0f, 16.0f),
		"");
	ImGui::Text("1ターン最大ダメージ：%d", statistics.maximumTurnDamage);
	ImGui::Text("終了時HP：%d / %d", statistics.currentHp, statistics.maxHp);
	ImGui::ProgressBar(
		m_Analysis.remainingHpRatio,
		ImVec2(-1.0f, 16.0f),
		"");

	ImGui::SeparatorText("ボール使用割合");
	if (m_Analysis.ballUsage.empty())
	{
		ImGui::TextDisabled("ショットデータがありません");
	}
	for (const auto& [ballId, shotCount] : m_Analysis.ballUsage)
	{
		const float share = statistics.totalShots > 0
			? static_cast<float>(shotCount) /
				static_cast<float>(statistics.totalShots)
			: 0.0f;
		ImGui::Text(
			"%s  %d回（%.1f%%）",
			PlayerBallText::GetName(ballId),
			shotCount,
			share * 100.0f);
		ImGui::ProgressBar(share, ImVec2(-1.0f, 13.0f), "");
	}

	ImGui::SeparatorText("振り返り");
	if (!m_Analysis.mostUsedBallId.empty())
	{
		ImGui::TextWrapped(
			"最も多く使ったボールは「%s」（%d回）でした。",
			PlayerBallText::GetName(m_Analysis.mostUsedBallId),
			m_Analysis.mostUsedBallCount);
	}
	ImGui::TextWrapped(
		"獲得ボール%d個、レリック%d個で通常エリア%d地点まで到達しました。",
		m_Analysis.acquiredBallCount,
		m_Analysis.acquiredRelicCount,
		statistics.areaProgress);

	ImGui::SeparatorText("獲得ボール");
	std::map<std::string, int> acquiredCounts;
	for (const std::string& ballId : statistics.acquiredBallIds)
	{
		++acquiredCounts[ballId];
	}
	if (acquiredCounts.empty())
	{
		ImGui::TextDisabled("なし");
	}
	for (const auto& [ballId, count] : acquiredCounts)
	{
		ImGui::BulletText(
			"%s%s",
			PlayerBallText::GetName(ballId),
			count > 1 ? ("  x" + std::to_string(count)).c_str() : "");
	}

	ImGui::SeparatorText("獲得レリック");
	bool hasRelic = false;
	for (const RelicType relicType : statistics.acquiredRelics)
	{
		const auto relic = std::find_if(
			RelicCatalog.begin(),
			RelicCatalog.end(),
			[relicType](const RelicDefinition& definition)
			{
				return definition.type == relicType;
			});
		if (relic != RelicCatalog.end())
		{
			ImGui::BulletText("%s", relic->name);
			hasRelic = true;
		}
	}
	if (!hasRelic)
	{
		ImGui::TextDisabled("なし");
	}

	const auto& progressionUnlocks = GameView::LastProgressionUnlocks();
	if (!progressionUnlocks.empty())
	{
		ImGui::SeparatorText("新しい解放");
		for (const auto& message : progressionUnlocks)
		{
			ImGui::BulletText("%s", message.c_str());
		}
	}
	ImGui::EndChild();
	ImGui::Separator();
	if (ImGui::Button("リザルトへ戻る", ImVec2(-1.0f, 40.0f)))
	{
		m_ShowAnalysis = false;
	}
	ImGui::TextDisabled("Esc・Backspaceでも戻れます");
}

// 終了処理
void ResultScene::Uninit()
{
	// このシーンのオブジェクトを削除する
	for (GameObject* gameObject : m_SceneGameObjects) {
		GameRuntime::DestroyObject(gameObject);
	}
	m_SceneGameObjects.clear();
}

// スコアを設定
void ResultScene::SetScore(int c)
{
	// リザルト文字列オブジェクト
	Texture2D* stringObj =
		m_SceneGameObjects[1]->GetComponent<Texture2D>();

	switch (c)
	{
	case -4:
		stringObj->SetUV(1, 2, 1, 13);	// -4 コンドル
		break;
	case -3:
		stringObj->SetUV(1, 3, 1, 13);	// -3 アルバトロス
		break;
	case -2:
		stringObj->SetUV(1, 4, 1, 13);	// -2 イーグル
		break;
	case -1:
		stringObj->SetUV(1, 5, 1, 13);	// -1 バーディ
		break;
	case 0:
		stringObj->SetUV(1, 6, 1, 13);	// 0 パー
		break;
	case 1:
		stringObj->SetUV(1, 7, 1, 13);	// +1 ボギー
		break;
	case 2:
		stringObj->SetUV(1, 8, 1, 13);	// +2 ダブルボギー
		break;
	case 3:
		stringObj->SetUV(1, 9, 1, 13);	// +3 トリプルボギー
		break;
	case 4:
		stringObj->SetUV(1, 10, 1, 13);	// +4
		break;
	case 5:
		stringObj->SetUV(1, 11, 1, 13);	// +5
		break;
	case 6:
		stringObj->SetUV(1, 12, 1, 13);	// +6
		break;
	default:
		stringObj->SetUV(1, 13, 1, 13);	// +7
		break;
	}
}
