#include "GamePresentation.h"

#pragma execution_character_set("utf-8")

#include "Application.h"
#include "BattleScene.h"
#include "Camera.h"
#include "Game.h"
#include "GameUi.h"
#include "EnemyBall.h"
#include "BreakBall.h"
#include "PlayerBall.h"
#include "input.h"
#include "imgui/imgui.h"
#include "json/json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;
using nlohmann::json;

namespace
{
	constexpr float kFrameSeconds = 1.0f / 60.0f;
	constexpr const char* kTutorialProgressPath =
		"runtime/tutorial_progress.json";
	constexpr const char* kBalanceReportPath =
		"logs/balance/balance_report.json";

	ImU32 WithAlpha(ImU32 color, float alpha)
	{
		const unsigned int clamped = static_cast<unsigned int>(
			std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
		return (color & 0x00ffffffu) | (clamped << 24u);
	}

	const char* JudgementLabel(const std::string& judgement)
	{
		if (judgement == "balanced") return "目標範囲内";
		if (judgement == "too_easy") return "易しすぎる傾向";
		if (judgement == "too_difficult") return "難しすぎる傾向";
		if (judgement == "insufficient_data") return "データ不足";
		return "評価待ち";
	}

	ImVec4 JudgementColor(const std::string& judgement)
	{
		if (judgement == "balanced") return ImVec4(0.25f, 0.85f, 0.48f, 1.0f);
		if (judgement == "too_easy") return ImVec4(0.30f, 0.72f, 1.0f, 1.0f);
		if (judgement == "too_difficult") return ImVec4(1.0f, 0.43f, 0.30f, 1.0f);
		return ImVec4(0.95f, 0.78f, 0.28f, 1.0f);
	}

	bool TryProjectToScreen(
		const Vector3& worldPosition,
		ImVec2& screenPosition)
	{
		const float width = static_cast<float>(Application::GetWidth());
		const float height = static_cast<float>(Application::GetHeight());
		if (width <= 0.0f || height <= 0.0f)
		{
			return false;
		}

		const Matrix projection = DirectX::XMMatrixPerspectiveFovLH(
			DirectX::XMConvertToRadians(45.0f),
			width / height,
			1.0f,
			1000.0f);
		const Matrix view = Camera::GetInstance().GetViewMatrix();
		const DirectX::XMVECTOR projected = DirectX::XMVector3Project(
			worldPosition,
			0.0f,
			0.0f,
			width,
			height,
			0.0f,
			1.0f,
			projection,
			view,
			Matrix::Identity);
		Vector3 result;
		DirectX::XMStoreFloat3(&result, projected);
		if (!std::isfinite(result.x) || !std::isfinite(result.y) ||
			result.z < 0.0f || result.z > 1.0f)
		{
			return false;
		}
		const ImVec2 origin = ImGui::GetMainViewport()->Pos;
		screenPosition = ImVec2(origin.x + result.x, origin.y + result.y);
		return true;
	}

	float ReadNumber(const json& value, const char* key, float fallback = 0.0f)
	{
		if (!value.is_object() || !value.contains(key) ||
			!value[key].is_number())
		{
			return fallback;
		}
		return value[key].get<float>();
	}

	std::string ReadString(
		const json& value,
		const char* key,
		const std::string& fallback = std::string())
	{
		if (!value.is_object() || !value.contains(key) ||
			!value[key].is_string())
		{
			return fallback;
		}
		return value[key].get<std::string>();
	}
}

void GamePresentation::Initialize(Game& game)
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
	m_PreviousGameState = static_cast<int>(game.GetGameState());
	LoadDashboard();
}

void GamePresentation::Update(Game& game)
{
	if (Input::GetKeyTrigger(VK_F1))
	{
		if (m_TutorialActive)
		{
			m_TutorialActive = false;
		}
		else
		{
			StartTutorial(true);
		}
	}
	if (Input::GetKeyTrigger(VK_F2))
	{
		m_DashboardOpen = !m_DashboardOpen;
		if (m_DashboardOpen)
		{
			LoadDashboard();
		}
	}

	for (FloatingFeedback& feedback : m_Feedback)
	{
		feedback.lifetime -= kFrameSeconds;
	}
	std::erase_if(
		m_Feedback,
		[](const FloatingFeedback& feedback)
		{
			return feedback.lifetime <= 0.0f;
		});
	m_ImpactFlashLifetime = (std::max)(
		0.0f,
		m_ImpactFlashLifetime - kFrameSeconds);
	m_DamageFlashLifetime = (std::max)(
		0.0f,
		m_DamageFlashLifetime - kFrameSeconds);
	m_HitSummaryLifetime = (std::max)(
		0.0f,
		m_HitSummaryLifetime - kFrameSeconds);
	if (game.IsCameraShakeEnabled() && m_CameraShakeLifetime > 0.0f)
	{
		m_CameraShakeLifetime = (std::max)(
			0.0f,
			m_CameraShakeLifetime - kFrameSeconds);
		m_CameraShakePhase += 1.65f;
		const float fade = std::clamp(
			m_CameraShakeLifetime / 0.24f,
			0.0f,
			1.0f);
		Camera::GetInstance().SetTarget(Vector3(
			std::sin(m_CameraShakePhase) * m_CameraShakeStrength * fade,
			0.0f,
			std::cos(m_CameraShakePhase * 1.31f) *
				m_CameraShakeStrength * fade));
	}
	else
	{
		m_CameraShakeLifetime = 0.0f;
		Camera::GetInstance().SetTarget(Vector3::Zero);
	}

	const int currentState = static_cast<int>(game.GetGameState());
	if (m_TutorialActive && IsBattleScene(game))
	{
		if (m_TutorialStep == TutorialStep::ChooseAndAim &&
			game.GetGameState() == GameState::AimingPower)
		{
			m_TutorialStep = TutorialStep::SetPower;
		}
		else if (m_TutorialStep == TutorialStep::WatchShot &&
			m_PreviousGameState == static_cast<int>(GameState::BallsMoving) &&
			currentState != static_cast<int>(GameState::BallsMoving))
		{
			m_TutorialStep = TutorialStep::ReadResult;
		}
	}
	m_PreviousGameState = currentState;
}

void GamePresentation::Draw(Game& game)
{
	DrawFeedback(game);
	if (m_DashboardOpen)
	{
		DrawDashboard();
	}
	if (m_TutorialActive)
	{
		DrawTutorial(game);
	}

	if (ImGui::BeginMainMenuBar())
	{
		ImGui::BeginDisabled(!game.CanOpenPauseMenu());
		if (ImGui::Button(game.IsPaused() ? "再開" : "ポーズ / 設定")) game.RequestPauseToggle();
		if (ImGui::Button("セーブ")) game.RequestManualSave();
		ImGui::EndDisabled();
		if (ImGui::Button("ガイド")) { if (m_TutorialActive) m_TutorialActive = false; else StartTutorial(true); }
		if (ImGui::Button("分析")) { m_DashboardOpen = !m_DashboardOpen; if (m_DashboardOpen) LoadDashboard(); }
		ImGui::Checkbox("デバッグ", &GameUi::showDebugger);
		if (ImGui::Button("UIを画面内へ戻す")) GameUi::ResetWindows();
		if (ImGui::Button("全画面切替")) game.RequestFullscreenToggle();
		if (ImGui::Button("終了")) PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
		ImGui::EndMainMenuBar();
	}
	GameUi::PrepareWindow("play_hints", ImVec2(805, 35), ImVec2(450, 170));
	if (ImGui::Begin("操作と攻略", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (IsBattleScene(game))
		{
			const auto players = game.GetComponents<PlayerBall>();
			const int currentHp = players.empty() ? game.GetPlayerCurrentHp() : players[0]->GetHP();
			ImGui::Text("HP %d / %d   所持金 %d", currentHp, game.GetPlayerMaxHp(), game.GetPlayerMoney());
		}
		ImGui::TextUnformatted("盤面を左ドラッグして離す：発射 / 右クリック：取消");
		ImGui::TextUnformatted("ホイール：拡大縮小 / タイトルバーをドラッグ：UI移動");
		if (IsBattleScene(game))
		{
			for (const EnemyBall* enemy : game.GetComponents<EnemyBall>())
			{
				if (enemy == nullptr || enemy->IsDefeated()) continue;
                if (enemy->IsArmorBoss())
                {
                    const auto& boss = enemy->GetBossState();
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.6f, 1.0f), "最終ボス  HP %d / %d", enemy->GetHP(), enemy->GetMaxHP());
                    ImGui::Text("Armor %d / %d", boss.armor, BossCombatRules::MaxArmor);
                    if (boss.IsBroken())
                    {
                        ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "BREAK！ 通常ダメージ100%% / 残り%dショット", boss.shotsRemaining);
                        if (boss.startedThisShot && game.GetGameState() == GameState::BallsMoving)
                            ImGui::TextUnformatted("このショットの残り ＋ 次の2ショットが有効");
                    }
                    else ImGui::TextUnformatted("通常ダメージ25%（切り上げ・最低1） / ポケット無効");
                    ImGui::TextUnformatted("黄色の球をボスへ押し込もう：Armor -1 / HP -4");
                    ImGui::TextUnformatted("黄色の球は命中・落下後、ショット終了時に再配置");
                    for (auto* neutral : game.GetComponents<BreakBall>())
                        ImGui::Text("ブレイク球%d：%s", neutral->GetIndex() + 1,
                            neutral->GetGameObject()->IsActive() ? "使用可能" : "再配置待ち");
                }
				if (enemy->GetFrontalDamageMultiplier() < 1.0f)
				{
					ImGui::Separator();
					ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "中ボス：ガード球（水色）  HP %d / %d", enemy->GetHP(), enemy->GetMaxHP());
					ImGui::Text("テーブル手前側からの衝突ダメージを%d%%軽減（最低1）。", static_cast<int>(std::lround((1.0f - enemy->GetFrontalDamageMultiplier()) * 100.0f)));
					ImGui::TextUnformatted("側面・奥側から狙おう。ガードの向きは固定。");
				}
				if (enemy->GetPocketDamageRatio() > 0.0f)
				{
					ImGui::Separator();
					ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "中ボス：ポケット球（緑）  HP %d / %d", enemy->GetHP(), enemy->GetMaxHP());
					ImGui::Text("ポケットに落とすと最大HPの%d%%ダメージ。", static_cast<int>(std::lround(enemy->GetPocketDamageRatio() * 100.0f)));
					ImGui::TextUnformatted("重い球で押し出そう。生存時は待機列に入り、後で復帰する。");
				}
			}
		}
	}
	ImGui::End();
}

void GamePresentation::OnBattleStarted(Game&)
{
	if (!m_TutorialCompleted)
	{
		StartTutorial(false);
	}
}

void GamePresentation::OnShotFired()
{
	m_CurrentShotHitCount = 0;
	m_CurrentShotDamage = 0;
	m_HitSummaryLifetime = 0.0f;
	if (m_TutorialActive &&
		(m_TutorialStep == TutorialStep::ChooseAndAim ||
			m_TutorialStep == TutorialStep::SetPower))
	{
		m_TutorialStep = TutorialStep::WatchShot;
	}
}

void GamePresentation::OnCombatFeedback(
	const Vector3& worldPosition,
	int damage,
	bool defeated,
	bool enemyEnemyCollision)
{
	FloatingFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.totalLifetime = defeated ? 1.35f : 0.95f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.drawRing = true;
	feedback.color = defeated
		? IM_COL32(255, 205, 70, 255)
		: enemyEnemyCollision
			? IM_COL32(110, 220, 255, 255)
			: IM_COL32(255, 118, 80, 255);
	if (defeated)
	{
		feedback.text = "撃破！  -" + std::to_string(damage);
	}
	else if (damage > 0)
	{
		feedback.text = "-" + std::to_string(damage) + " HP";
	}
	else
	{
		feedback.text = "防御";
	}
	m_Feedback.push_back(std::move(feedback));
	m_ImpactFlashLifetime = (std::max)(m_ImpactFlashLifetime, 0.11f);
	m_CurrentShotHitCount++;
	m_CurrentShotDamage += (std::max)(0, damage);
	m_HitSummaryLifetime = 1.15f;
	if (Game::GetInstance()->IsCameraShakeEnabled())
	{
		m_CameraShakeLifetime = (std::max)(
			m_CameraShakeLifetime,
			defeated ? 0.24f : 0.12f);
		m_CameraShakeStrength = defeated ? 1.25f : 0.55f;
	}
	if (Game::GetInstance()->IsVibrationEnabled())
	{
		Input::SetVibration(defeated ? 7 : 3, defeated ? 0.78f : 0.36f);
	}
}

void GamePresentation::OnPocketFeedback(
	const Vector3& worldPosition,
	bool playerPocket,
	bool finisher,
	int damage)
{
	FloatingFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.totalLifetime = 1.5f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.drawRing = true;
	if (playerPocket)
	{
		feedback.text = "スクラッチ  -" + std::to_string(damage) + " HP";
		feedback.color = IM_COL32(255, 88, 96, 255);
		m_DamageFlashLifetime = 0.28f;
	}
	else if (finisher)
	{
		feedback.text = "ポケット撃破！";
		feedback.color = IM_COL32(255, 215, 64, 255);
	}
	else
	{
		feedback.text = "ポケット制圧";
		feedback.color = IM_COL32(92, 224, 255, 255);
	}
	m_Feedback.push_back(std::move(feedback));
	if (Game::GetInstance()->IsCameraShakeEnabled())
	{
		m_CameraShakeLifetime = (std::max)(m_CameraShakeLifetime, 0.24f);
		m_CameraShakeStrength = finisher ? 1.45f : 0.85f;
	}
	if (Game::GetInstance()->IsVibrationEnabled())
	{
		Input::SetVibration(finisher ? 9 : 5, finisher ? 0.9f : 0.55f);
	}
}

void GamePresentation::OnPlayerDamage(
	int damage,
	const std::string& source)
{
	if (damage <= 0)
	{
		return;
	}
	m_DamageFlashLifetime = (std::max)(m_DamageFlashLifetime, 0.22f);
	if (source != "pocket")
	{
		FloatingFeedback feedback;
		feedback.worldPosition = Vector3::Zero;
		feedback.text = "プレイヤー  -" + std::to_string(damage) + " HP";
		feedback.playerHudMessage = true;
		feedback.totalLifetime = 1.1f;
		feedback.lifetime = feedback.totalLifetime;
		feedback.color = IM_COL32(255, 86, 92, 255);
		m_Feedback.push_back(std::move(feedback));
	}
	if (Game::GetInstance()->IsCameraShakeEnabled())
	{
		m_CameraShakeLifetime = (std::max)(m_CameraShakeLifetime, 0.20f);
		m_CameraShakeStrength = 1.0f;
	}
	if (Game::GetInstance()->IsVibrationEnabled())
	{
		Input::SetVibration(5, 0.62f);
	}
}

void GamePresentation::StartTutorial(bool replay)
{
	m_TutorialReplay = replay;
	m_TutorialActive = true;
	m_TutorialStep = TutorialStep::Welcome;
}

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

void GamePresentation::DrawTutorial(Game& game)
{
	GameUi::PrepareWindow("tutorial", ImVec2(305, 400), ImVec2(670, 270));
	ImGui::SetNextWindowBgAlpha(0.94f);
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
		ImGui::TextUnformatted("まずは1ショットを一緒に操作して、基本の流れを確認します。");
		if (ImGui::Button("ガイドを開始", ImVec2(170.0f, 0.0f)))
		{
			m_TutorialStep = TutorialStep::ChooseAndAim;
		}
		break;
	case TutorialStep::ChooseAndAim:
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "1. ボールを選び、狙う");
		ImGui::TextUnformatted("ボール選択画面で、使いたいボールの「使用」をクリックします。");
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
		ImGui::TextUnformatted("壁反射や敵同士の衝突を使うと、1ショットの効果を伸ばせます。");
		break;
	case TutorialStep::ReadResult:
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "4. ターンの流れ");
		ImGui::TextUnformatted("ボールが止まると敵が反撃し、次の使用球を選びます。");
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

void GamePresentation::DrawFeedback(Game& game)
{
	ImDrawList* foreground = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 topLeft = viewport->Pos;
	const ImVec2 bottomRight(
		viewport->Pos.x + viewport->Size.x,
		viewport->Pos.y + viewport->Size.y);
	if (game.IsScreenFlashEnabled() && m_ImpactFlashLifetime > 0.0f)
	{
		const float alpha = m_ImpactFlashLifetime / 0.11f;
		foreground->AddRectFilled(
			topLeft,
			bottomRight,
			IM_COL32(255, 238, 190, static_cast<int>(36.0f * alpha)));
	}
	if (game.IsScreenFlashEnabled() && m_DamageFlashLifetime > 0.0f)
	{
		const float alpha = std::clamp(m_DamageFlashLifetime / 0.28f, 0.0f, 1.0f);
		const ImU32 red = IM_COL32(255, 45, 55, static_cast<int>(80.0f * alpha));
		const float border = 24.0f + 24.0f * alpha;
		foreground->AddRectFilled(topLeft, ImVec2(bottomRight.x, topLeft.y + border), red);
		foreground->AddRectFilled(ImVec2(topLeft.x, bottomRight.y - border), bottomRight, red);
		foreground->AddRectFilled(topLeft, ImVec2(topLeft.x + border, bottomRight.y), red);
		foreground->AddRectFilled(ImVec2(bottomRight.x - border, topLeft.y), bottomRight, red);
	}

	for (const FloatingFeedback& feedback : m_Feedback)
	{
		ImVec2 position;
		const bool playerHudMessage = feedback.playerHudMessage;
		if (playerHudMessage)
		{
			position = ImVec2(
				viewport->Pos.x + viewport->Size.x * 0.5f,
				viewport->Pos.y + 105.0f);
		}
		else if (!TryProjectToScreen(feedback.worldPosition, position))
		{
			continue;
		}

		const float progress = 1.0f -
			feedback.lifetime / feedback.totalLifetime;
		position.y -= 20.0f + progress * 58.0f;
		const float fadeIn = std::clamp(progress * 8.0f, 0.0f, 1.0f);
		const float fadeOut = std::clamp(
			feedback.lifetime / 0.28f,
			0.0f,
			1.0f);
		const float alpha = fadeIn * fadeOut;
		const ImVec2 textSize = ImGui::CalcTextSize(feedback.text.c_str());
		const ImVec2 textPosition(
			position.x - textSize.x * 0.5f,
			position.y - textSize.y * 0.5f);
		foreground->AddText(
			ImVec2(textPosition.x + 2.0f, textPosition.y + 2.0f),
			IM_COL32(0, 0, 0, static_cast<int>(210.0f * alpha)),
			feedback.text.c_str());
		foreground->AddText(
			textPosition,
			WithAlpha(feedback.color, alpha),
			feedback.text.c_str());
		if (feedback.drawRing)
		{
			const float radius = 16.0f + progress * 48.0f;
			foreground->AddCircle(
				position,
				radius,
				WithAlpha(feedback.color, alpha * 0.7f),
				32,
				3.0f);
		}
	}

	if (m_HitSummaryLifetime > 0.0f && m_CurrentShotHitCount > 0)
	{
		std::ostringstream summary;
		summary << m_CurrentShotHitCount << "ヒット"
			<< "   合計ダメージ " << m_CurrentShotDamage;
		const std::string text = summary.str();
		const ImVec2 size = ImGui::CalcTextSize(text.c_str());
		const ImVec2 position(
			viewport->Pos.x + viewport->Size.x - size.x - 32.0f,
			viewport->Pos.y + 56.0f);
		foreground->AddText(
			position,
			IM_COL32(255, 224, 100, 255),
			text.c_str());
	}
}

void GamePresentation::DrawDashboard()
{
	GameUi::PrepareWindow("dashboard", ImVec2(120, 35), ImVec2(1040, 650));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (!ImGui::Begin("バランス分析ダッシュボード", &m_DashboardOpen, flags))
	{
		ImGui::End();
		return;
	}

	if (ImGui::Button("レポートを再読込"))
	{
		LoadDashboard();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("F2で開閉 / %s", kBalanceReportPath);
	ImGui::Separator();
	if (!m_Dashboard.loaded)
	{
		ImGui::TextColored(
			ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
			"%s",
			m_Dashboard.error.c_str());
		ImGui::TextWrapped(
			"tools\\analyze_balance_logs.py を実行してレポートを生成してください。");
		ImGui::End();
		return;
	}

	if (ImGui::BeginTable("summary_cards", 4, ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextColumn();
		ImGui::TextDisabled("総合スコア");
		ImGui::Text("%.1f / 100", m_Dashboard.balanceScore);
		ImGui::TableNextColumn();
		ImGui::TextDisabled("判定");
		ImGui::TextColored(
			JudgementColor(m_Dashboard.judgement),
			"%s",
			JudgementLabel(m_Dashboard.judgement));
		ImGui::TableNextColumn();
		ImGui::TextDisabled("分析ログ");
		ImGui::Text("%dラン", m_Dashboard.analyzedRuns);
		ImGui::TableNextColumn();
		ImGui::TextDisabled("完了ラン母数");
		ImGui::Text("%d件", m_Dashboard.balanceSamples);
		ImGui::EndTable();
	}
	ImGui::TextDisabled("生成: %s", m_Dashboard.generatedAt.c_str());
	if (!m_Dashboard.warning.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.28f, 1.0f));
		ImGui::TextWrapped("注意: %s", m_Dashboard.warning.c_str());
		ImGui::PopStyleColor();
	}

	if (ImGui::BeginTabBar("dashboard_tabs"))
	{
		if (ImGui::BeginTabItem("ラン進行"))
		{
			ImGui::Spacing();
			for (const DashboardMetric& metric : m_Dashboard.metrics)
			{
				ImGui::PushID(metric.id.c_str());
				const float displayValue = metric.percentage
					? metric.value * 100.0f
					: metric.value;
				const float displayMin = metric.percentage
					? metric.targetMin * 100.0f
					: metric.targetMin;
				const float displayMax = metric.percentage
					? metric.targetMax * 100.0f
					: metric.targetMax;
				ImGui::TextColored(
					metric.inRange
						? ImVec4(0.35f, 0.9f, 0.52f, 1.0f)
						: ImVec4(1.0f, 0.48f, 0.32f, 1.0f),
					"%s",
					metric.inRange ? "OK" : "要確認");
				ImGui::SameLine(86.0f);
				if (metric.percentage)
				{
					ImGui::Text(
						"%-24s %6.1f%%   目標 %.1f–%.1f%%",
						metric.label.c_str(),
						displayValue,
						displayMin,
						displayMax);
				}
				else
				{
					ImGui::Text(
						"%-24s %6.1f   目標 %.1f–%.1f",
						metric.label.c_str(),
						displayValue,
						displayMin,
						displayMax);
				}
				const float scaleMax = (std::max)(
					metric.targetMax * 1.35f,
					metric.value * 1.1f);
				const float fraction = scaleMax > 0.0f
					? std::clamp(metric.value / scaleMax, 0.0f, 1.0f)
					: 0.0f;
				ImGui::PushStyleColor(
					ImGuiCol_PlotHistogram,
					metric.inRange
						? ImVec4(0.25f, 0.78f, 0.45f, 1.0f)
						: ImVec4(0.92f, 0.35f, 0.25f, 1.0f));
				ImGui::ProgressBar(fraction, ImVec2(-1.0f, 12.0f), "");
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("ステージ別"))
		{
			const ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable;
			if (ImGui::BeginTable(
				"stage_table",
				7,
				tableFlags,
				ImVec2(0.0f, 390.0f)))
			{
				ImGui::TableSetupColumn("ステージ");
				ImGui::TableSetupColumn("種別");
				ImGui::TableSetupColumn("母数");
				ImGui::TableSetupColumn("勝率");
				ImGui::TableSetupColumn("中央値打数");
				ImGui::TableSetupColumn("残HP");
				ImGui::TableSetupColumn("スコア / 判定");
				ImGui::TableHeadersRow();
				for (const StageSummary& stage : m_Dashboard.stages)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(stage.id.c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted(stage.type.c_str());
					ImGui::TableNextColumn(); ImGui::Text("%d", stage.sampleCount);
					ImGui::TableNextColumn(); ImGui::Text("%.1f%%", stage.clearRate * 100.0f);
					ImGui::TableNextColumn(); ImGui::Text("%.1f", stage.medianShots);
					ImGui::TableNextColumn(); ImGui::Text("%.1f%%", stage.remainingHpRate * 100.0f);
					ImGui::TableNextColumn();
					ImGui::TextColored(
						JudgementColor(stage.judgement),
						"%.1f  %s",
						stage.balanceScore,
						JudgementLabel(stage.judgement));
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("ビルド・経済・ポケット"))
		{
			ImGui::Spacing();
			ImGui::TextUnformatted("使用ボール構成");
			const float shares[2] = {
				m_Dashboard.standardShotShare,
				m_Dashboard.heavyShotShare,
			};
			ImGui::Text("スタンダード球  %.1f%%", shares[0] * 100.0f);
			ImGui::ProgressBar(shares[0], ImVec2(-1.0f, 16.0f), "");
			ImGui::Text("ヘビー球        %.1f%%", shares[1] * 100.0f);
			ImGui::ProgressBar(shares[1], ImVec2(-1.0f, 16.0f), "");
			ImGui::Separator();
			ImGui::Text(
				"戦術的ポケット使用ラン率  %.1f%%",
				m_Dashboard.tacticalPocketRate * 100.0f);
			ImGui::Text(
				"1ラン平均獲得マネー        %.1f",
				m_Dashboard.averageMoneyPerRun);
			ImGui::TextWrapped(
				"これらは因果関係ではなく記述統計です。使用率が高いボールが強いとは限らないため、プレイヤープロフィールと設定コホートを揃えて比較してください。");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void GamePresentation::LoadDashboard()
{
	m_Dashboard = DashboardData{};
	std::ifstream source(kBalanceReportPath);
	if (!source)
	{
		m_Dashboard.error =
			"分析レポートが見つかりません: " +
			std::string(kBalanceReportPath);
		return;
	}

	try
	{
		json report;
		source >> report;
		m_Dashboard.generatedAt = report.value("generated_at", "unknown");
		m_Dashboard.analyzedRuns = report.value("analyzed_file_count", 0);
		const json& runSelection = report.value(
			"run_selection",
			json::object());
		m_Dashboard.warning = runSelection.value("warning", "");

		const json& run = report.value("run_balance", json::object());
		m_Dashboard.balanceSamples = run.value("balance_sample_count", 0);
		m_Dashboard.balanceScore = ReadNumber(run, "balance_score");
		m_Dashboard.judgement = ReadString(run, "judgement");
		const json& metrics = run.value("metrics", json::object());
		const json& statuses = run.value("metric_status", json::object());
		const auto addMetric = [&](
			const char* id,
			const char* label,
			bool percentage)
		{
			DashboardMetric metric;
			metric.id = id;
			metric.label = label;
			metric.value = ReadNumber(metrics, id);
			if (statuses.contains(id) && statuses[id].is_object())
			{
				metric.targetMin = ReadNumber(statuses[id], "target_min");
				metric.targetMax = ReadNumber(statuses[id], "target_max", 1.0f);
				metric.inRange = statuses[id].value("in_range", false);
			}
			metric.percentage = percentage;
			m_Dashboard.metrics.push_back(std::move(metric));
		};
		addMetric("first_boss_reach_rate", "第1ボス到達率", true);
		addMetric("first_boss_clear_rate", "第1ボス撃破率", true);
		addMetric("second_boss_reach_rate", "第2ボス到達率", true);
		addMetric("median_reached_progress", "到達進行度中央値", false);
		addMetric("terminal_run_rate", "正常終了ラン率", true);

		const json& system = report.value("system_metrics", json::object());
		const json& build = system.value("build", json::object());
		const json& usage = build.value("ball_usage", json::object());
		const json& shares = usage.value(
			"shot_share_by_ball_id",
			json::object());
		m_Dashboard.standardShotShare = ReadNumber(
			shares,
			"player_standard");
		m_Dashboard.heavyShotShare = ReadNumber(
			shares,
			"player_heavy");
		const json& pockets = system.value("pockets", json::object());
		m_Dashboard.tacticalPocketRate = ReadNumber(
			pockets,
			"tactical_pocket_run_rate");
		const json& economy = system.value("economy", json::object());
		m_Dashboard.averageMoneyPerRun = ReadNumber(
			economy,
			"average_money_earned_per_run");

		if (report.contains("stages") && report["stages"].is_array())
		{
			for (const json& item : report["stages"])
			{
				StageSummary stage;
				stage.id = ReadString(item, "stage_id");
				stage.type = ReadString(item, "stage_type");
				stage.sampleCount = item.value("sample_count", 0);
				const json& stageMetrics = item.value(
					"metrics",
					json::object());
				stage.clearRate = ReadNumber(stageMetrics, "clear_rate");
				stage.medianShots = ReadNumber(stageMetrics, "median_shots");
				stage.remainingHpRate = ReadNumber(
					stageMetrics,
					"average_remaining_hp_ratio");
				stage.balanceScore = ReadNumber(item, "balance_score");
				stage.judgement = ReadString(item, "judgement");
				m_Dashboard.stages.push_back(std::move(stage));
			}
		}
		m_Dashboard.loaded = true;
	}
	catch (const json::exception& exception)
	{
		m_Dashboard.error =
			"分析レポートを読み込めません: " +
			std::string(exception.what());
	}
}

bool GamePresentation::IsBattleScene(const Game& game) const
{
	return dynamic_cast<const BattleScene*>(game.GetCurrentScene()) != nullptr;
}
