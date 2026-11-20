#include "GamePresentation.h"
#include "GamePresentationPrimitives.h"
#include "CombatOverlayDrawing.h"
#include "EnemyAttackTiming.h"
#include "Application.h"
#include "ApplicationLifecycle.h"
#include "BattleScene.h"
#include "Camera.h"
#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "GameUi.h"
#include "EnemyBall.h"
#include "BreakBall.h"
#include "PlayerBall.h"
#include "PlayerBallData.h"
#include "PlayerBallText.h"
#include "input.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <array>
#include <cmath>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;
using namespace GamePresentationPrimitives;

void GamePresentation::Initialize(Game& game)
{
	// Upload every combat HUD icon once after the renderer is available. Draw
	// calls only reuse the cached shader-resource views.
	CombatOverlayDrawing::LoadIcons();
	LoadTutorialProgress();
	m_PreviousBattleState = static_cast<int>(game.GetBattleState());
	LoadDashboard();
}

void GamePresentation::Update(Game& game, float deltaSeconds)
{
	const float presentationDelta = std::clamp(deltaSeconds, 0.0f, 0.10f);
	UpdatePlayerAura(game, presentationDelta);
	m_PresentationTimeSeconds += presentationDelta;
    if (m_AttackFlightAge >= 0.0f) m_AttackFlightAge += presentationDelta;
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

	const auto liveEnemies = GameRuntime::World().GetComponents<EnemyBall>();
	for (FloatingFeedback& feedback : m_Feedback)
	{
		if (feedback.followsDamageTarget &&
			feedback.targetFollowLifetime > 0.0f &&
			feedback.targetKey != 0)
		{
			// targetKey is only compared with keys produced by the current enemy
			// list. It is never converted back into a pointer and dereferenced.
			const auto target = std::find_if(
				liveEnemies.begin(),
				liveEnemies.end(),
				[&feedback](const EnemyBall* enemy)
				{
					return enemy != nullptr &&
						reinterpret_cast<std::uintptr_t>(enemy) == feedback.targetKey;
				});
			if (target != liveEnemies.end())
			{
				feedback.worldPosition = (*target)->GetPosition();
			}
			feedback.targetFollowLifetime = (std::max)(
				0.0f,
				feedback.targetFollowLifetime - presentationDelta);
		}
		feedback.lifetime -= presentationDelta;
	}
	UpdateHitReactions(game, presentationDelta);
	UpdateEnemyHpVisuals(presentationDelta);
	m_CollisionVfx.Update(
		presentationDelta,
		(game.IsCollisionImpactEffectsEnabled() || game.IsDefeatVfxEnabled()) &&
		IsBattleScene(game));
	std::erase_if(
		m_Feedback,
		[](const FloatingFeedback& feedback)
		{
			return feedback.lifetime <= 0.0f;
		});
	for (ChainRangeFeedback& feedback : m_ChainRanges)
	{
		feedback.lifetime -= presentationDelta;
	}
	std::erase_if(
		m_ChainRanges,
		[](const ChainRangeFeedback& feedback)
		{
			return feedback.lifetime <= 0.0f;
		});
	m_ImpactFlashLifetime = (std::max)(
		0.0f,
		m_ImpactFlashLifetime - presentationDelta);
	m_DamageFlashLifetime = (std::max)(
		0.0f,
		m_DamageFlashLifetime - presentationDelta);
	m_HitSummaryLifetime = (std::max)(
		0.0f,
		m_HitSummaryLifetime - presentationDelta);
	m_HeavyChainDisplayLifetime = (std::max)(
		0.0f,
		m_HeavyChainDisplayLifetime - presentationDelta);
	m_MultiDefeatDisplayLifetime = (std::max)(
		0.0f,
		m_MultiDefeatDisplayLifetime - presentationDelta);
	m_ImpactFlashCooldown = (std::max)(
		0.0f,
		m_ImpactFlashCooldown - presentationDelta);
	m_CameraShakeCooldown = (std::max)(
		0.0f,
		m_CameraShakeCooldown - presentationDelta);
	if (game.IsCameraShakeEnabled() && m_CameraShakeLifetime > 0.0f)
	{
		m_CameraShakeLifetime = (std::max)(
			0.0f,
			m_CameraShakeLifetime - presentationDelta);
		m_CameraShakePhase += 1.65f;
        const auto shake=CollisionPresentationRules::ShakeOffset(m_CameraShakePhase,m_CameraShakeLifetime,m_CameraShakeStrength);
        Camera::GetInstance().SetTarget(Vector3(shake.x,0,shake.z));
	}
	else
	{
		m_CameraShakeLifetime = 0.0f;
		Camera::GetInstance().SetTarget(Vector3::Zero);
	}

	const int currentState = static_cast<int>(game.GetBattleState());
	if (m_PreviousBattleState == static_cast<int>(BattleState::BallsMoving) &&
		currentState != static_cast<int>(BattleState::BallsMoving))
	{
		ClearHitReactions();
	}
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
	++m_PresentationFrame;
}

void GamePresentation::Draw(Game& game)
{
    // Reward artwork owns the screen, including the explanation of its choices.
    if (game.IsClearRewardActive()) return;
	DrawPlayerAura(game);
    if (m_AttackFlightAge >= 0.0f)
    {
        const float t = std::clamp(m_AttackFlightAge / static_cast<float>(EnemyAttackTiming::FlightSeconds), 0.0f, 1.0f);
        const Vector3 position = m_AttackStart + (m_AttackTarget - m_AttackStart) * t;
        ImVec2 center, edge;
        if (TryProjectToScreen(position, center) &&
            TryProjectToScreen(position + Vector3(1.4f, 0, 0), edge))
        {
            const float radius = (std::max)(5.0f, std::abs(edge.x - center.x));
            auto* draw = ImGui::GetForegroundDrawList();
            draw->AddCircleFilled(center, radius * 1.35f, IM_COL32(255, 148, 110, 32));
            draw->AddCircleFilled(center, radius, IM_COL32(255, 190, 150, 115));
            draw->AddCircle(center, radius, IM_COL32(255, 230, 210, 190), 24, 1.5f);
            draw->AddCircleFilled(ImVec2(center.x-radius*.25f, center.y-radius*.25f),
                radius*.28f, IM_COL32(255, 255, 255, 150));
        }
    }
	DrawFeedback(game);
	if (GameUi::showDebugger && IsBattleScene(game))
	{
		m_CollisionVfx.DrawDebugTuning();
	}
	if (m_DashboardOpen)
	{
		DrawDashboard();
	}

	const bool battleScene = IsBattleScene(game);
	if (!battleScene && ImGui::BeginMainMenuBar())
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
		if (ImGui::Button("終了"))
		{
			ApplicationLifecycle::Record("main_menu_exit_requested", Application::GetWindow(), WM_CLOSE, 0, 0);
			const BOOL sent = PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
			ApplicationLifecycle::Record("main_menu_exit_post_result", Application::GetWindow(), WM_CLOSE, 0, 0, sent);
		}
		ImGui::EndMainMenuBar();
	}
	if (battleScene)
	{
		DrawPierceTraces(game);
		DrawEnemyStatusEffects(game);
		DrawBattleHud(game);
		if (m_TutorialActive)
		{
			DrawTutorial(game);
		}
		return;
	}
    // The illustrated map already includes its own controls and route legend.
    if (game.GetCurrentSceneType() == SceneType::Select) return;
	GameUi::PrepareWindow("play_hints", ImVec2(805, 35), ImVec2(450, 170));
	if (ImGui::Begin("操作と攻略", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (IsBattleScene(game))
		{
			const auto players =
				GameRuntime::World().GetComponents<PlayerBall>();
			const int currentHp = players.empty() ? game.GetPlayerCurrentHp() : players[0]->GetHP();
			ImGui::Text("HP %d / %d   シールド %d   所持金 %d",
				currentHp, game.GetPlayerMaxHp(), game.GetPlayerShield(), game.GetPlayerMoney());
		}
		ImGui::TextUnformatted("盤面を左ドラッグして離す：発射 / 右クリック：取消");
		ImGui::TextUnformatted("ホイール：拡大縮小 / タイトルバーをドラッグ：UI移動");
		if (IsBattleScene(game))
		{
			for (const EnemyBall* enemy :
				GameRuntime::World().GetComponents<EnemyBall>())
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
					else ImGui::TextUnformatted("通常ダメージ25%（切り上げ・最低1） / ポケットでArmor -1");
                    ImGui::Text("BREAK中のポケット：最大HPの%d%%ダメージ（切り上げ）", BossCombatRules::PocketDamagePercent);
                    ImGui::TextUnformatted("生存時は盤面内へ復帰 / ポケット効果は1ショット1回");
					ImGui::TextUnformatted("黄色の球をボスへ押し込もう：Armor -1 / HP -4");
					ImGui::TextUnformatted("黄色の球は命中・落下後、ショット終了時に再配置");
					for (auto* neutral :
						GameRuntime::World().GetComponents<BreakBall>())
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
	if (m_TutorialActive)
	{
		DrawTutorial(game);
	}
}

void GamePresentation::DrawPierceTraces(Game& game)
{
	if (game.IsPaused() || game.GetLastBattleResult() != BattleResult::None) return;
	const auto& traces = game.GetPierceTraceState().traces;
	if (traces.empty()) return;
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImDrawList* foreground = ImGui::GetForegroundDrawList(viewport);
	const ImU32 purple = IM_COL32(205, 91, 255, 255);
	for (const auto& trace : traces)
	{
		ImVec2 start;
		ImVec2 end;
		ImVec2 directionBase;
		const Vector3 height(0.0f, 0.55f, 0.0f);
		if (!TryProjectToScreen(trace.start + height, start) ||
			!TryProjectToScreen(trace.end + height, end) ||
			!TryProjectToScreen(trace.end - trace.direction * 2.0f + height, directionBase))
			continue;
		foreground->AddLine(start, end, IM_COL32(18, 6, 28, 235), 9.0f);
		foreground->AddLine(start, end, purple, 5.0f);
		ImVec2 direction(end.x - directionBase.x, end.y - directionBase.y);
		const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
		if (length > 0.001f)
		{
			direction.x /= length;
			direction.y /= length;
			const ImVec2 perpendicular(-direction.y, direction.x);
			const ImVec2 base(end.x - direction.x * 15.0f, end.y - direction.y * 15.0f);
			foreground->AddTriangleFilled(
				end,
				ImVec2(base.x + perpendicular.x * 7.0f, base.y + perpendicular.y * 7.0f),
				ImVec2(base.x - perpendicular.x * 7.0f, base.y - perpendicular.y * 7.0f),
				purple);
		}
		char label[64]{};
		sprintf_s(label, PresentationUtf8(u8"貫通痕 #%llu"),
			static_cast<unsigned long long>(trace.id));
		const ImVec2 midpoint((start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f);
		const ImVec2 textSize = ImGui::CalcTextSize(label);
		const ImVec2 textAt(midpoint.x - textSize.x * 0.5f, midpoint.y - textSize.y - 9.0f);
		foreground->AddText(ImVec2(textAt.x + 2.0f, textAt.y + 2.0f), IM_COL32(0, 0, 0, 235), label);
		foreground->AddText(textAt, purple, label);
	}
}

void GamePresentation::UpdatePlayerAura(Game& game, float deltaSeconds)
{
	if (!IsBattleScene(game) || game.GetBattleState() == BattleState::Finished ||
		game.GetBattleState() == BattleState::Inactive) { m_PlayerAura.Reset(); return; }
	const auto players = GameRuntime::World().GetComponents<PlayerBall>();
	const auto* player = players.empty() ? nullptr : players.front();
	if (player == nullptr || player->IsDefeated() || player->IsPocketed())
	{
		m_PlayerAura.Reset();
		return;
	}
	const auto position = player->GetPosition();
	const auto* ball = game.GetCurrentPlayerBallData();
	const auto color = ball != nullptr ? PlayerBallText::GetColor(*ball)
		: std::array<float, 3>{0.25f, 0.63f, 1.0f};
	m_PlayerAura.Update({position.x, position.y, position.z}, player->GetBall()->GetRadius(),
		color, reinterpret_cast<std::uintptr_t>(player), deltaSeconds);
}

void GamePresentation::DrawPlayerAura(Game& game)
{
	if (!IsBattleScene(game) || !m_PlayerAura.IsActive() ||
		game.GetBattleState() == BattleState::Finished || game.GetBattleState() == BattleState::Inactive) return;
	const auto players = GameRuntime::World().GetComponents<PlayerBall>();
	const auto* player = players.empty() ? nullptr : players.front();
	if (player == nullptr || player->IsDefeated() || player->IsPocketed()) return;
	const auto position = player->GetPosition();
	const Matrix inverseView = Camera::GetInstance().GetViewMatrix().Invert();
	const Vector3 right = Vector3::TransformNormal(Vector3::UnitX, inverseView);
	ImVec2 center, edge;
	if (!TryProjectToScreen(position, center) ||
		!TryProjectToScreen(position + right * player->GetBall()->GetRadius(), edge)) return;
	const float radius = std::hypot(edge.x - center.x, edge.y - center.y);
	m_PlayerAura.Draw({position.x, position.y, position.z}, radius,
		[](const PlayerAuraEffect::Position& p, ImVec2& screen)
		{ return TryProjectToScreen(Vector3(p.x, p.y, p.z), screen); });
}

bool GamePresentation::IsBattleScene(const Game& game) const
{
	return dynamic_cast<const BattleScene*>(game.GetCurrentScene()) != nullptr;
}
