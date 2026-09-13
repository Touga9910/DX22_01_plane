#include "ResultScene.h"
#include "Game.h"
#include "GameUi.h"
#include "Input.h"
#include "Texture2D.h"
#include "Texture2DFactory.h"
#include "PlayerBallText.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <map>
#include <string>

#pragma execution_character_set("utf-8")

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
	m_Result = Game::GetInstance()->GetLastRunResult();
	m_Analysis = AnalyzeRun(m_Result);
	//背景画像オブジェクトを作成
	Texture2D* pt = Texture2DFactory::Create(*Game::GetInstance());
	pt->SetTexture("assets/texture/background2.png");
	pt->SetScale(1280.0f, 720.0f, 0.0f);
	m_SceneGameObjects.emplace_back(pt->GetGameObject());

	//リザルト文字列オブジェクトを作成
	Texture2D* pt2 = Texture2DFactory::Create(*Game::GetInstance());
	pt2->SetTexture("assets/texture/resultString.png");
	pt2->SetScale(700.0f, 100.0f, 0.0f);
	pt2->SetUV(1, 1, 1, 13);//縦1横13分割の、左から1番目上から5番目を指定
	m_SceneGameObjects.emplace_back(pt2->GetGameObject());

	/*
	// 人オブジェクトを作成
	Texture2D* pt3 = Texture2DFactory::Create(*Game::GetInstance());
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
		Game::GetInstance()->StartNewRun();
		Game::GetInstance()->ChangeScene(SceneType::Select);
	}
	else
	{
		Game::GetInstance()->ChangeScene(SceneType::Title);
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
	const std::uint64_t totalSeconds = statistics.activeFrames / 60;
	const std::uint64_t hours = totalSeconds / 3600;
	const std::uint64_t minutes = (totalSeconds / 60) % 60;
	const std::uint64_t seconds = totalSeconds % 60;

	ImGui::TextColored(
		statistics.completed
			? ImVec4(0.35f, 0.95f, 0.55f, 1.0f)
			: ImVec4(1.0f, 0.48f, 0.38f, 1.0f),
		statistics.completed ? "ラン完了！" : "ラン終了");
	ImGui::SameLine();
	ImGui::TextDisabled("アセンション%d", Game::GetInstance()->GetActiveAscension());
	ImGui::Separator();
	if (ImGui::BeginTable("result_summary", 2, ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextColumn();
		ImGui::TextDisabled("通常ルート通過");
		ImGui::Text("%d / 15 エリア", statistics.areaProgress);
		ImGui::TextDisabled("総戦闘数");
		ImGui::Text("%d", statistics.totalBattles);
		ImGui::TextDisabled("ショット数");
		ImGui::Text("%d", statistics.totalShots);
		ImGui::TextDisabled("1ターン最大ダメージ");
		ImGui::Text("%d", statistics.maximumTurnDamage);

		ImGui::TableNextColumn();
		ImGui::TextDisabled("プレイ時間");
		ImGui::Text(
			"%02llu:%02llu:%02llu",
			static_cast<unsigned long long>(hours),
			static_cast<unsigned long long>(minutes),
			static_cast<unsigned long long>(seconds));
		ImGui::TextDisabled("総ダメージ");
		ImGui::Text("%d", statistics.totalDamage);
		ImGui::TextDisabled("被ダメージ");
		ImGui::Text("%d", statistics.damageTaken);
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
			statistics.finalBossId.empty()
			? "不明"
			: statistics.finalBossId.c_str(),
			statistics.finalBossDefeated ? "撃破" : "未撃破");
	}
	else
	{
		ImGui::TextDisabled("最終ボス：未到達");
	}

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
	else
	{
		for (const auto& [ballId, count] : acquiredCounts)
		{
			ImGui::BulletText(
				"%s%s",
				PlayerBallText::GetName(ballId),
				count > 1 ? ("  x" + std::to_string(count)).c_str() : "");
		}
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
	const auto& progressionUnlocks = Game::GetInstance()->GetLastProgressionUnlocks();
	if (!progressionUnlocks.empty())
	{
		ImGui::SeparatorText("新しい解放");
		for (const auto& message : progressionUnlocks) ImGui::BulletText("%s", message.c_str());
	}

	ImGui::Separator();
	const char* actions[] = { "ラン分析を見る", "もう一度", "タイトルへ" };
	for (int index = 0; index < 3; ++index)
	{
		if (ImGui::Button(actions[index], ImVec2(-1, 38))) m_Menu.Confirm(index, 3);
	}
	ImGui::TextDisabled("ボタンをクリックして進めます。");
}

void ResultScene::DrawAnalysis()
{
	const RunResultSnapshot& statistics = m_Result;
	ImGui::TextColored(
		ImVec4(0.35f, 0.88f, 1.0f, 1.0f),
		"今回のランを数値で振り返ります");
	ImGui::Separator();
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
		Game::GetInstance()->DeleteGameObject(gameObject);
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
