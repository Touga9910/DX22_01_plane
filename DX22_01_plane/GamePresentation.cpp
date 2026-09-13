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
#include "PlayerBallText.h"
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

	const char* PresentationUtf8(const char8_t* text) noexcept
	{
		return reinterpret_cast<const char*>(text);
	}

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

	const char* StatusEffectName(StatusEffectType type)
	{
		switch (type)
		{
		case StatusEffectType::AttackUp: return "攻撃上昇";
		case StatusEffectType::AttackDown: return "攻撃低下";
		case StatusEffectType::DefenseUp: return "防御上昇";
		case StatusEffectType::DefenseDown: return "防御低下";
		default: return "不明な状態効果";
		}
	}

	void DrawStatusArrow(ImDrawList* drawList, const ImVec2& minimum, StatusEffectType type)
	{
		const ImVec2 maximum(minimum.x + 25.0f, minimum.y + 25.0f);
		const ImVec2 center((minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f);
		const ImU32 color = IsAttackStatusEffect(type)
			? IM_COL32(245, 70, 62, 255)
			: IM_COL32(65, 145, 255, 255);
		drawList->AddRectFilled(minimum, maximum, IM_COL32(12, 18, 28, 235), 5.0f);
		drawList->AddRect(minimum, maximum, color, 5.0f, 0, 1.5f);

		const bool pointsUp = IsPositiveStatusEffect(type);
		const float tipY = center.y + (pointsUp ? -7.0f : 7.0f);
		const float tailY = center.y + (pointsUp ? 6.0f : -6.0f);
		drawList->AddLine(ImVec2(center.x, tailY), ImVec2(center.x, tipY), color, 3.0f);
		if (pointsUp)
			drawList->AddTriangleFilled(ImVec2(center.x, tipY - 3.0f), ImVec2(center.x - 5.0f, tipY + 3.0f), ImVec2(center.x + 5.0f, tipY + 3.0f), color);
		else
			drawList->AddTriangleFilled(ImVec2(center.x, tipY + 3.0f), ImVec2(center.x - 5.0f, tipY - 3.0f), ImVec2(center.x + 5.0f, tipY - 3.0f), color);
	}

	bool DrawPileButton(
		const char* id,
		const char* label,
		int count,
		ImU32 accent,
		const ImVec2& size)
	{
		const ImVec2 position = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton(id, size);
		const bool hovered = ImGui::IsItemHovered();
		if (hovered)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 background = hovered
			? IM_COL32(26, 37, 51, 246)
			: IM_COL32(12, 20, 30, 232);
		drawList->AddRectFilled(
			position,
			ImVec2(position.x + size.x, position.y + size.y),
			background,
			10.0f);
		drawList->AddRect(
			position,
			ImVec2(position.x + size.x, position.y + size.y),
			hovered ? accent : IM_COL32(83, 99, 120, 220),
			10.0f,
			0,
			hovered ? 2.0f : 1.0f);

		for (int layer = 2; layer >= 0; --layer)
		{
			const float offset = static_cast<float>(layer) * 3.0f;
			drawList->AddRectFilled(
				ImVec2(position.x + 10.0f + offset, position.y + 12.0f - offset),
				ImVec2(position.x + 43.0f + offset, position.y + 61.0f - offset),
				layer == 0 ? IM_COL32(25, 34, 46, 255) : IM_COL32(48, 59, 73, 255),
				4.0f);
			drawList->AddRect(
				ImVec2(position.x + 10.0f + offset, position.y + 12.0f - offset),
				ImVec2(position.x + 43.0f + offset, position.y + 61.0f - offset),
				layer == 0 ? accent : IM_COL32(96, 108, 126, 220),
				4.0f);
		}

		drawList->AddText(
			ImVec2(position.x + 52.0f, position.y + 13.0f),
			IM_COL32(210, 220, 233, 255),
			label);
		char countText[24]{};
		sprintf_s(countText, PresentationUtf8(u8"%d枚"), count);
		drawList->AddText(
			ImGui::GetFont(),
			ImGui::GetFontSize() * 1.15f,
			ImVec2(position.x + 52.0f, position.y + 36.0f),
			IM_COL32(255, 255, 255, 255),
			countText);
		return ImGui::IsItemClicked(ImGuiMouseButton_Left);
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
	m_PreviousBattleState = static_cast<int>(game.GetBattleState());
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
	for (ChainRangeFeedback& feedback : m_ChainRanges)
	{
		feedback.lifetime -= kFrameSeconds;
	}
	std::erase_if(
		m_ChainRanges,
		[](const ChainRangeFeedback& feedback)
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

	const int currentState = static_cast<int>(game.GetBattleState());
	if (m_TutorialActive && IsBattleScene(game))
	{
		if (m_TutorialStep == TutorialStep::ChooseAndAim &&
			game.GetBattleState() == BattleState::AimingPower)
		{
			m_TutorialStep = TutorialStep::SetPower;
		}
		else if (m_TutorialStep == TutorialStep::WatchShot &&
			m_PreviousBattleState == static_cast<int>(BattleState::BallsMoving) &&
			currentState != static_cast<int>(BattleState::BallsMoving))
		{
			m_TutorialStep = TutorialStep::ReadResult;
		}
	}
	m_PreviousBattleState = currentState;
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
	if (IsBattleScene(game))
	{
		DrawEnemyStatusEffects(game);
		DrawBattleHud(game);
		return;
	}
	GameUi::PrepareWindow("play_hints", ImVec2(805, 35), ImVec2(450, 170));
	if (ImGui::Begin("操作と攻略", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (IsBattleScene(game))
		{
			const auto players = game.GetComponents<PlayerBall>();
			const int currentHp = players.empty() ? game.GetPlayerCurrentHp() : players[0]->GetHP();
			ImGui::Text("HP %d / %d   シールド %d   所持金 %d",
				currentHp, game.GetPlayerMaxHp(), game.GetPlayerShield(), game.GetPlayerMoney());
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
                        if (boss.startedThisShot && game.GetBattleState() == BattleState::BallsMoving)
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

void GamePresentation::DrawEnemyStatusEffects(Game& game)
{
	const BattleState state = game.GetBattleState();
	if (state != BattleState::AimingDirection && state != BattleState::AimingPower &&
		state != BattleState::ConfirmShot)
	{
		return;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoInputs;
	int enemyIndex = 0;
	for (const EnemyBall* enemy : game.GetComponents<EnemyBall>())
	{
		const int currentIndex = enemyIndex++;
		if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed()) continue;
		const auto& effects = enemy->GetStatusEffects();
		const int effectCount = effects.GetActiveCount();
		if (effectCount == 0) continue;

		ImVec2 center;
		ImVec2 top;
		if (!TryProjectToScreen(enemy->GetPosition(), center) ||
			!TryProjectToScreen(enemy->GetPosition() + Vector3(0.0f, enemy->GetRadius(), 0.0f), top))
		{
			continue;
		}
		const float projectedRadius = (std::max)(14.0f, std::abs(center.y - top.y));
		const float rowWidth = effectCount * 25.0f + (effectCount - 1) * 4.0f;
		const ImVec2 rowPosition(center.x - rowWidth * 0.5f, center.y + projectedRadius + 7.0f);
		if (rowPosition.x + rowWidth < viewport->Pos.x || rowPosition.x > viewport->Pos.x + viewport->Size.x ||
			rowPosition.y + 25.0f < viewport->Pos.y || rowPosition.y > viewport->Pos.y + viewport->Size.y)
		{
			continue;
		}

		char windowId[64]{};
		sprintf_s(windowId, "##enemy_status_effects_%d", currentIndex);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowPos(rowPosition);
		ImGui::SetNextWindowSize(ImVec2(rowWidth, 25.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
		if (ImGui::Begin(windowId, nullptr, flags))
		{
			int drawn = 0;
			for (const StatusEffectType effectType : AllStatusEffectTypes)
			{
				const int magnitude = effects.GetMagnitude(effectType);
				if (magnitude <= 0) continue;
				if (drawn++ > 0) ImGui::SameLine();
				const ImVec2 iconMinimum = ImGui::GetCursorScreenPos();
				const ImVec2 iconMaximum(iconMinimum.x + 25.0f, iconMinimum.y + 25.0f);
				ImGui::Dummy(ImVec2(25.0f, 25.0f));
				DrawStatusArrow(ImGui::GetWindowDrawList(), iconMinimum, effectType);
				const ImVec2 mouse = ImGui::GetIO().MousePos;
				const bool hovered = mouse.x >= iconMinimum.x && mouse.x < iconMaximum.x &&
					mouse.y >= iconMinimum.y && mouse.y < iconMaximum.y;
				if (hovered)
				{
					const bool attack = IsAttackStatusEffect(effectType);
					const int signedMagnitude = IsPositiveStatusEffect(effectType) ? magnitude : -magnitude;
					const int baseValue = attack ? enemy->GetStatus().attack : enemy->GetStatus().defense;
					const int currentValue = attack ? enemy->GetAttack() : enemy->GetDefense();
					ImGui::BeginTooltip();
					ImGui::TextColored(
						attack ? ImVec4(1.0f, 0.35f, 0.31f, 1.0f) : ImVec4(0.32f, 0.62f, 1.0f, 1.0f),
						"%s", StatusEffectName(effectType));
					ImGui::Text("現在の効果：%s %+d", attack ? "攻撃力" : "防御力", signedMagnitude);
					ImGui::Text("基礎値 %d  →  現在値 %d", baseValue, currentValue);
					ImGui::EndTooltip();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}
}

void GamePresentation::DrawBattleHud(Game& game)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 workPosition = viewport->WorkPos;
	const ImVec2 workSize = viewport->WorkSize;
	const ImGuiWindowFlags fixedFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;

	const auto players = game.GetComponents<PlayerBall>();
	const int currentHp = players.empty() || players[0] == nullptr
		? game.GetPlayerCurrentHp()
		: players[0]->GetHP();
	const int maxHp = (std::max)(1, game.GetPlayerMaxHp());
	const float hpRatio = std::clamp(
		static_cast<float>(currentHp) / static_cast<float>(maxHp),
		0.0f,
		1.0f);

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(workPosition.x + 16.0f, workPosition.y + 16.0f));
	ImGui::SetNextWindowSize(ImVec2(278.0f, 112.0f));
	ImGui::SetNextWindowBgAlpha(0.91f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(82, 98, 119, 225));
	if (ImGui::Begin("##battle_status_hud", nullptr, fixedFlags))
	{
		ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "HP  %d / %d", currentHp, maxHp);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.92f, 0.25f, 0.22f, 1.0f));
		ImGui::ProgressBar(hpRatio, ImVec2(-1.0f, 8.0f), "");
		ImGui::PopStyleColor();
		ImGui::TextDisabled("シールド %d    所持金 %d", game.GetPlayerShield(), game.GetPlayerMoney());
		char relicButton[64]{};
		sprintf_s(
			relicButton,
			PresentationUtf8(u8"レリック一覧  %d##relic_list"),
			game.GetOwnedRelicCount());
		if (ImGui::Button(relicButton, ImVec2(-1.0f, 25.0f)))
		{
			m_RelicListOpen = !m_RelicListOpen;
		}
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		workPosition.x + workSize.x - 202.0f,
		workPosition.y + 16.0f));
	ImGui::SetNextWindowSize(ImVec2(186.0f, 51.0f));
	ImGui::SetNextWindowBgAlpha(0.91f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
	if (ImGui::Begin("##deck_button_hud", nullptr, fixedFlags))
	{
		char deckButton[64]{};
		sprintf_s(
			deckButton,
			PresentationUtf8(u8"デッキ確認  %d枚##deck_list"),
			game.GetDeckBallCount());
		if (ImGui::Button(deckButton, ImVec2(-1.0f, 30.0f)))
		{
			m_DeckListView = DeckListView::All;
			m_DeckListOpen = !m_DeckListOpen;
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();

	const ImVec2 pileSize(112.0f, 76.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		workPosition.x + 16.0f,
		workPosition.y + workSize.y - pileSize.y - 12.0f));
	ImGui::SetNextWindowSize(pileSize);
	if (ImGui::Begin("##draw_pile_hud", nullptr, fixedFlags | ImGuiWindowFlags_NoBackground))
	{
		if (DrawPileButton(
			"draw_pile_button",
			"残り札",
			game.GetPlayerDeckCount(),
			IM_COL32(88, 169, 255, 255),
			pileSize))
		{
			m_DeckListView = DeckListView::DrawPile;
			m_DeckListOpen = true;
		}
	}
	ImGui::End();

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		workPosition.x + workSize.x - pileSize.x - 16.0f,
		workPosition.y + workSize.y - pileSize.y - 12.0f));
	ImGui::SetNextWindowSize(pileSize);
	if (ImGui::Begin("##discard_pile_hud", nullptr, fixedFlags | ImGuiWindowFlags_NoBackground))
	{
		if (DrawPileButton(
			"discard_pile_button",
			"捨て札",
			game.GetPlayerDiscardCount(),
			IM_COL32(205, 118, 255, 255),
			pileSize))
		{
			m_DeckListView = DeckListView::DiscardPile;
			m_DeckListOpen = true;
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();

	if (m_DeckListOpen)
	{
		DrawDeckList(game);
	}
	if (m_RelicListOpen)
	{
		DrawRelicList(game);
	}
}

void GamePresentation::DrawDeckList(Game& game)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float overlayHeight = (std::min)(440.0f, viewport->WorkSize.y - 112.0f);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		viewport->WorkPos.x + viewport->WorkSize.x - 388.0f,
		viewport->WorkPos.y + 76.0f));
	ImGui::SetNextWindowSize(ImVec2(372.0f, overlayHeight));
	ImGui::SetNextWindowBgAlpha(0.96f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(88, 113, 143, 255));
	const ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking;
	if (ImGui::Begin("##deck_list_overlay", nullptr, flags))
	{
		ImGui::TextUnformatted("デッキ確認");
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 26.0f);
		if (ImGui::SmallButton("X##close_deck"))
		{
			m_DeckListOpen = false;
		}
		ImGui::Separator();

		if (ImGui::Selectable("すべて", m_DeckListView == DeckListView::All, 0, ImVec2(88.0f, 24.0f)))
			m_DeckListView = DeckListView::All;
		ImGui::SameLine();
		if (ImGui::Selectable("残り札", m_DeckListView == DeckListView::DrawPile, 0, ImVec2(88.0f, 24.0f)))
			m_DeckListView = DeckListView::DrawPile;
		ImGui::SameLine();
		if (ImGui::Selectable("捨て札", m_DeckListView == DeckListView::DiscardPile, 0, ImVec2(88.0f, 24.0f)))
			m_DeckListView = DeckListView::DiscardPile;

		const PlayerDeck& deck = game.m_RunController.Deck();
		int count = 0;
		auto ballAt = [&](int index) -> const PlayerBallData*
		{
			switch (m_DeckListView)
			{
			case DeckListView::DrawPile:
				return index >= 0 && index < deck.GetDrawPileCount()
					? &deck.GetDrawPile()[static_cast<std::size_t>(index)]
					: nullptr;
			case DeckListView::DiscardPile:
				return index >= 0 && index < deck.GetDiscardPileCount()
					? &deck.GetDiscardPile()[static_cast<std::size_t>(index)]
					: nullptr;
			default:
				return deck.GetRewardTarget(index);
			}
		};
		switch (m_DeckListView)
		{
		case DeckListView::DrawPile: count = deck.GetDrawPileCount(); break;
		case DeckListView::DiscardPile: count = deck.GetDiscardPileCount(); break;
		default: count = deck.GetRewardTargetCount(); break;
		}
		ImGui::TextDisabled("%d枚", count);
		ImGui::BeginChild("deck_list_rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
		if (count == 0)
		{
			ImGui::TextDisabled("ここにはまだカードがありません。");
		}
		for (int index = 0; index < count; ++index)
		{
			const PlayerBallData* ball = ballAt(index);
			if (ball == nullptr) continue;
			ImGui::PushID(index);
			const auto color = PlayerBallText::GetColor(*ball);
			ImGui::ColorButton(
				"ball_color",
				ImVec4(color[0], color[1], color[2], 1.0f),
				ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
				ImVec2(18.0f, 18.0f));
			ImGui::SameLine();
			ImGui::Text("%s  Lv.%d", PlayerBallText::GetName(ball->definitionId), ball->upgradeLevel);
			ImGui::TextDisabled("%s", PlayerBallText::GetTrait(*ball));
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", PlayerBallText::GetDescription(ball->definitionId));
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		ImGui::EndChild();
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void GamePresentation::DrawRelicList(Game& game)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float overlayHeight = (std::min)(360.0f, viewport->WorkSize.y - 164.0f);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		viewport->WorkPos.x + 16.0f,
		viewport->WorkPos.y + 138.0f));
	ImGui::SetNextWindowSize(ImVec2(362.0f, overlayHeight));
	ImGui::SetNextWindowBgAlpha(0.96f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(112, 190, 166, 255));
	const ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking;
	if (ImGui::Begin("##relic_list_overlay", nullptr, flags))
	{
		ImGui::Text("レリック一覧  %d", game.GetOwnedRelicCount());
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 26.0f);
		if (ImGui::SmallButton("X##close_relic"))
		{
			m_RelicListOpen = false;
		}
		ImGui::Separator();
		ImGui::BeginChild("relic_list_rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
		bool found = false;
		for (int index = 0; index < game.GetRelicCount(); ++index)
		{
			const RelicDefinition* relic = game.GetRelic(index);
			if (relic == nullptr || !game.HasRelic(relic->type)) continue;
			found = true;
			ImGui::PushID(index);
			ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.80f, 1.0f), "%s", relic->name);
			ImGui::TextWrapped("%s", relic->description);
			ImGui::Separator();
			ImGui::PopID();
		}
		if (!found)
		{
			ImGui::TextDisabled("レリックはまだありません。");
		}
		ImGui::EndChild();
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void GamePresentation::OnBattleStarted(Game&)
{
	m_BallCardExpansion.fill(0.0f);
	m_DeckListOpen = false;
	m_RelicListOpen = false;
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
	m_BallCardExpansion.fill(0.0f);
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

void GamePresentation::OnChainImpact(
	const Vector3& worldPosition,
	float radius,
	int hitCount)
{
	if (radius <= 0.0f)
	{
		return;
	}
	ChainRangeFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.radius = radius;
	feedback.hitCount = (std::max)(0, hitCount);
	feedback.totalLifetime = 1.05f;
	feedback.lifetime = feedback.totalLifetime;
	m_ChainRanges.push_back(feedback);
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

	for (const ChainRangeFeedback& feedback : m_ChainRanges)
	{
		constexpr int segmentCount = 64;
		std::vector<ImVec2> points;
		points.reserve(segmentCount);
		for (int index = 0; index < segmentCount; ++index)
		{
			const float angle = DirectX::XM_2PI *
				static_cast<float>(index) / static_cast<float>(segmentCount);
			const Vector3 worldPoint = feedback.worldPosition + Vector3(
				std::cos(angle) * feedback.radius,
				0.18f,
				std::sin(angle) * feedback.radius);
			ImVec2 screenPoint;
			if (TryProjectToScreen(worldPoint, screenPoint))
			{
				points.push_back(screenPoint);
			}
		}
		if (points.size() != segmentCount)
		{
			continue;
		}
		const float progress = 1.0f - feedback.lifetime / feedback.totalLifetime;
		const float fade = std::clamp(feedback.lifetime / 0.28f, 0.0f, 1.0f);
		const float pulse = 0.65f +
			0.35f * std::sin(progress * DirectX::XM_PI * 3.0f);
		const ImU32 purple = IM_COL32(198, 86, 255, 255);
		foreground->AddConvexPolyFilled(
			points.data(),
			static_cast<int>(points.size()),
			WithAlpha(purple, fade * 0.11f));
		foreground->AddPolyline(
			points.data(),
			static_cast<int>(points.size()),
			WithAlpha(purple, fade * (0.65f + pulse * 0.25f)),
			ImDrawFlags_Closed,
			3.0f + pulse * 2.0f);

		ImVec2 center;
		if (TryProjectToScreen(
			feedback.worldPosition + Vector3(0.0f, 1.0f, 0.0f),
			center))
		{
			std::ostringstream label;
			label << "連鎖範囲 " << std::fixed << std::setprecision(0)
				<< feedback.radius << "  /  " << feedback.hitCount << "体に連鎖";
			const std::string text = label.str();
			const ImVec2 size = ImGui::CalcTextSize(text.c_str());
			const ImVec2 at(
				center.x - size.x * 0.5f,
				center.y - size.y - 18.0f);
			foreground->AddText(
				ImVec2(at.x + 2.0f, at.y + 2.0f),
				IM_COL32(0, 0, 0, static_cast<int>(220.0f * fade)),
				text.c_str());
			foreground->AddText(at, WithAlpha(purple, fade), text.c_str());
		}
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

// Game keeps only the timing decision; presentation owns the UI details.
void Game::DrawPauseUI()
{
	if (m_GamePresentation != nullptr) m_GamePresentation->DrawPause(*this);
}

void Game::DrawBallSelectionUI()
{
	if (m_GamePresentation != nullptr) m_GamePresentation->DrawBallSelection(*this);
}

void Game::DrawClearRewardUI()
{
	if (m_GamePresentation != nullptr) m_GamePresentation->DrawClearReward(*this);
}
