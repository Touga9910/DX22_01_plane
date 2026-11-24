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
#include "GameShotTraceView.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "GameUi.h"
#include "EnemyBall.h"
#include "EnemyText.h"
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

/// <summary>
/// 戦闘UIのアイコンとダッシュボードの設定を読み込む。
/// </summary>
void GamePresentation::Initialize(Game& game)
{
	// Upload every combat HUD icon once after the renderer is available. Draw
	// calls only reuse the cached shader-resource views.
	CombatOverlayDrawing::LoadIcons();
	m_PreviousBattleState = static_cast<int>(game.GetBattleState());
	LoadDashboard();
}

/// <summary>
/// 経過時間をもとに戦闘演出と自球のオーラを更新し、入力とチュートリアルの状態を処理する。
/// </summary>
void GamePresentation::Update(Game& game, float deltaSeconds)
{
	const float presentationDelta = std::clamp(deltaSeconds, 0.0f, 0.10f);
	const bool battleScene = IsBattleScene(game);
	UpdatePlayerAura(game, presentationDelta);
	m_PresentationTimeSeconds += presentationDelta;
    if (m_AttackFlightAge >= 0.0f) m_AttackFlightAge += presentationDelta;
	if (!battleScene && !m_TutorialOpenedManually)
	{
		m_TutorialActive = false;
	}
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
		battleScene);
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
	if (m_TutorialActive && m_TutorialStep == TutorialStep::WatchShot &&
		battleScene && currentState != static_cast<int>(BattleState::BallsMoving))
	{
		m_TutorialStep = TutorialStep::ReadResult;
	}
	m_PreviousBattleState = currentState;
	++m_PresentationFrame;
}

/// <summary>
/// 戦闘中の攻撃演出、各種オーバーレイ、HUDとチュートリアルを描画する。
/// </summary>
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
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.6f, 1.0f), "%s  HP %d / %d", EnemyLabel(enemy->GetEnemyId()), enemy->GetHP(), enemy->GetMaxHP());
					ImGui::Text("装甲 %d / %d", boss.armor, BossCombatRules::MaxArmor);
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
					ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "%s（水色）  HP %d / %d", EnemyLabel(enemy->GetEnemyId()), enemy->GetHP(), enemy->GetMaxHP());
					ImGui::Text("テーブル手前側からの衝突ダメージを%d%%軽減（最低1）。", static_cast<int>(std::lround((1.0f - enemy->GetFrontalDamageMultiplier()) * 100.0f)));
					ImGui::TextUnformatted("側面・奥側から狙おう。ガードの向きは固定。");
				}
				if (enemy->GetPocketDamageRatio() > 0.0f)
				{
					ImGui::Separator();
					ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "%s（緑）  HP %d / %d", EnemyLabel(enemy->GetEnemyId()), enemy->GetHP(), enemy->GetMaxHP());
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

/// <summary>
/// 有効な貫通痕を盤面に投影し、動きや明滅を付けて描画する。
/// </summary>
void GamePresentation::DrawPierceTraces(Game& game)
{
    if (game.IsPaused() || game.IsClearRewardActive() ||
        game.GetLastBattleResult() != BattleResult::None)
        return;

    const auto& traces = GameShotTraceView::Current().traces;
    if (traces.empty()) return;

    const std::size_t firstHighlighted = traces.size() - GameShotTraceView::SelectedMinionTraceCount();
    const float pulse = std::fmod(m_PresentationTimeSeconds, 1.25f) / 1.25f;
    auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetBackgroundDrawList(viewport);
    const float scale = std::clamp(viewport->Size.y / 900.0f, 0.85f, 1.75f);
    const Vector3 height(0.0f, 0.55f, 0.0f);
    for (std::size_t index = 0; index < traces.size(); ++index)
    {
        ImVec2 start, end;
        if (!TryProjectToScreen(traces[index].start + height, start) ||
            !TryProjectToScreen(traces[index].end + height, end))
            continue;

        const bool highlighted = index >= firstHighlighted;
        draw->AddLine(start, end, IM_COL32(115, 52, 201, 110), 6.0f * scale);
        draw->AddLine(start, end,
            highlighted ? IM_COL32(220, 175, 255, 255) : IM_COL32(200, 145, 255, 218),
            (highlighted ? 3.0f : 2.5f) * scale);
        draw->AddCircle(start, 5.0f * scale,
            highlighted ? IM_COL32(249, 223, 255, 255) : IM_COL32(212, 165, 255, 240),
            0, 2.0f * scale);

        if (highlighted)
        {
            const auto alongTrace = [&](float t) {
                return ImVec2(start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t);
            };
            const float head = (std::min)(1.0f, pulse * 1.14f);
            const float tail = (std::max)(0.0f, pulse * 1.14f - 0.14f);
            draw->AddLine(alongTrace(tail), alongTrace(head), IM_COL32(252, 235, 255, 255),
                4.0f * scale);
        }
    }
}

/// <summary>
/// 戦闘状態と自球の位置、球種に応じてオーラの表示状態を更新する。
/// </summary>
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

/// <summary>
/// 戦闘中の自球に付随するオーラを画面へ投影して描画する。
/// </summary>
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

/// <summary>
/// 現在のシーンが戦闘シーンかを判定する。
/// </summary>
bool GamePresentation::IsBattleScene(const Game& game) const
{
	return dynamic_cast<const BattleScene*>(game.GetCurrentScene()) != nullptr;
}
