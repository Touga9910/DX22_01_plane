#include "GamePresentation.h"
#include "GamePresentationPrimitives.h"
#include "Game.h"
#include "GameEvent.h"
#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "EnemyBall.h"
#include "GameUi.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

using DirectX::SimpleMath::Vector3;
using namespace GamePresentationPrimitives;

/// <summary>
/// 戦闘演出、連鎖と撃破の表示、ショット結果をまとめて描画する。
/// </summary>
void GamePresentation::DrawFeedback(Game& game)
{
	DrawScreenFlashes(game);
	DrawWorldFeedback(game);
	DrawHeavyChainBanner(game);
	DrawMultiDefeatBanner(game);
	DrawFloatingFeedback(game);
	DrawFeedbackDiagnostics(game);
	DrawShotSummary();
}

/// <summary>
/// 衝突時の明滅や被弾時の赤い縁取りを画面に描画する。
/// </summary>
void GamePresentation::DrawScreenFlashes(Game& game)
{
	ImDrawList* foreground = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 topLeft = viewport->Pos;
	const ImVec2 bottomRight(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);

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
		// Tint the whole screen on HP damage; the stronger border keeps the
		// reaction visible without hiding the table or damage numbers.
		foreground->AddRectFilled(topLeft, bottomRight,
			IM_COL32(255, 45, 55, static_cast<int>(48.0f * alpha)));
		const ImU32 red = IM_COL32(255, 45, 55, static_cast<int>(80.0f * alpha));
		const float border = 24.0f + 24.0f * alpha;
		foreground->AddRectFilled(topLeft, ImVec2(bottomRight.x, topLeft.y + border), red);
		foreground->AddRectFilled(ImVec2(topLeft.x, bottomRight.y - border), bottomRight, red);
		foreground->AddRectFilled(topLeft, ImVec2(topLeft.x + border, bottomRight.y), red);
		foreground->AddRectFilled(ImVec2(bottomRight.x - border, topLeft.y), bottomRight, red);
	}
}

/// <summary>
/// 連鎖範囲や衝突エフェクトを盤面から画面座標へ投影して描画する。
/// </summary>
void GamePresentation::DrawWorldFeedback(Game& game)
{
	ImDrawList* foreground = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
	ImDrawList* effects = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

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

	m_CollisionVfx.Draw(
		effects,
		&TryProjectToScreen,
		game.IsCollisionImpactEffectsEnabled() || game.IsDefeatVfxEnabled());
}

/// <summary>
/// 重量系の衝突連鎖数と到達した段階をバナーに描画する。
/// </summary>
void GamePresentation::DrawHeavyChainBanner(Game& game)
{
	ImDrawList* foreground = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	if (m_HeavyChainDisplayLifetime > 0.0f &&
		m_HeavyPhysicsChainCount > 0 &&
		game.IsHeavyChainEffectsEnabled())
	{
		const int tier = CollisionPresentationRules::ChainTier(
			m_HeavyPhysicsChainCount);
		const ImU32 colors[5] = {
			IM_COL32(205, 238, 255, 255),
			IM_COL32(255, 224, 92, 255),
			IM_COL32(255, 154, 58, 255),
			IM_COL32(255, 178, 38, 255),
			IM_COL32(255, 238, 150, 255),
		};
		const float progress = 1.0f -
			m_HeavyChainDisplayLifetime / m_HeavyChainDisplayTotalLifetime;
		const float intro = std::clamp(progress / 0.22f, 0.0f, 1.0f);
		const float eased = 1.0f - std::pow(1.0f - intro, 3.0f);
		const float overshoot = std::sin(intro * DirectX::XM_PI) *
			(tier >= 3 ? 0.18f : 0.08f);
		const float baseScale = tier >= 3
			? 2.05f
			: tier == 2 ? 1.72f : tier == 1 ? 1.46f : 1.12f;
		const float scale = baseScale * (0.82f + eased * 0.18f + overshoot);
		const float alpha = std::clamp(
			m_HeavyChainDisplayLifetime / 0.24f,
			0.0f,
			1.0f);
		const std::string chainText =
			"CHAIN " + std::to_string(m_HeavyPhysicsChainCount);
		ImFont* font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize() * scale;
		const ImVec2 size = font->CalcTextSizeA(
			fontSize,
			FLT_MAX,
			0.0f,
			chainText.c_str());
		const bool avoidDebugWindow = game.IsDebugMode() || GameUi::showDebugger;
		const float centerX = avoidDebugWindow
			? viewport->Pos.x + viewport->Size.x * 0.405f
			: viewport->Pos.x + viewport->Size.x * 0.5f;
		const float centerY = viewport->Pos.y +
			(avoidDebugWindow ? 108.0f : 88.0f);
		const float bannerWidth = tier >= 3 ? 360.0f : tier == 2 ? 320.0f : 276.0f;
		const float bannerHeight = tier >= 3 ? 62.0f : 56.0f;
		const ImVec2 bannerMinimum(
			centerX - bannerWidth * 0.5f,
			centerY - bannerHeight * 0.5f);
		const ImVec2 bannerMaximum(
			centerX + bannerWidth * 0.5f,
			centerY + bannerHeight * 0.5f);

		if (tier >= 2)
		{
			foreground->PushClipRect(
				ImVec2(bannerMinimum.x - 42.0f, bannerMinimum.y - 34.0f),
				ImVec2(bannerMaximum.x + 42.0f, bannerMaximum.y + 34.0f),
				true);
			const int rayCount = tier >= 3 ? 12 : 8;
			for (int index = 0; index < rayCount; ++index)
			{
				const float angle = DirectX::XM_2PI *
					static_cast<float>(index) / static_cast<float>(rayCount);
				const float inner = tier >= 3 ? 45.0f : 34.0f;
				const float outer = inner + 58.0f +
					26.0f * std::abs(std::sin(angle * 2.3f));
				foreground->AddLine(
					ImVec2(centerX + std::cos(angle) * inner,
						centerY + std::sin(angle) * inner * 0.45f),
					ImVec2(centerX + std::cos(angle) * outer,
						centerY + std::sin(angle) * outer * 0.45f),
					WithAlpha(colors[tier], alpha * (tier >= 3 ? 0.22f : 0.15f)),
					tier >= 3 ? 1.8f : 1.2f);
			}
			foreground->PopClipRect();
			foreground->AddRectFilled(
				bannerMinimum,
				bannerMaximum,
				IM_COL32(24, 10, 5, static_cast<int>(
					CombatPresentationRules::ChainBackgroundAlpha * alpha)),
				18.0f);
			foreground->AddRect(
				bannerMinimum,
				bannerMaximum,
				WithAlpha(colors[tier], alpha * 0.88f),
				18.0f,
				0,
				tier >= 3 ? 3.2f : 2.0f);
			foreground->AddLine(
				ImVec2(bannerMinimum.x + 26.0f, bannerMaximum.y + 7.0f),
				ImVec2(centerX - 28.0f, bannerMaximum.y + 7.0f),
				WithAlpha(colors[tier], alpha * 0.82f),
				2.0f);
			foreground->AddLine(
				ImVec2(centerX + 28.0f, bannerMaximum.y + 7.0f),
				ImVec2(bannerMaximum.x - 26.0f, bannerMaximum.y + 7.0f),
				WithAlpha(colors[tier], alpha * 0.82f),
				2.0f);
			const float ornament = tier >= 3 ? 14.0f : 9.0f;
			foreground->AddTriangleFilled(
				ImVec2(bannerMinimum.x - ornament, centerY),
				ImVec2(bannerMinimum.x + 5.0f, centerY - ornament),
				ImVec2(bannerMinimum.x + 5.0f, centerY + ornament),
				WithAlpha(colors[tier], alpha));
			foreground->AddTriangleFilled(
				ImVec2(bannerMaximum.x + ornament, centerY),
				ImVec2(bannerMaximum.x - 5.0f, centerY - ornament),
				ImVec2(bannerMaximum.x - 5.0f, centerY + ornament),
				WithAlpha(colors[tier], alpha));
		}

		const ImVec2 at(centerX - size.x * 0.5f, centerY - size.y * 0.58f);
		const ImU32 outline = IM_COL32(0, 0, 0, static_cast<int>(178.0f * alpha));
		for (const ImVec2 offset : {
			ImVec2(-3.0f, 0.0f), ImVec2(3.0f, 0.0f),
				ImVec2(0.0f, -3.0f), ImVec2(0.0f, 3.0f) })
		{
			foreground->AddText(
				font,
				fontSize,
				ImVec2(at.x + offset.x, at.y + offset.y),
				outline,
				chainText.c_str());
		}
		foreground->AddText(
			font,
			fontSize,
			at,
			WithAlpha(colors[tier], alpha * 0.80f),
			chainText.c_str());
		foreground->AddText(
			font,
			fontSize,
			ImVec2(at.x, at.y - 1.5f),
			IM_COL32(255, 250, 220, static_cast<int>(31.0f * alpha)),
			chainText.c_str());
		const char* meaning = PresentationUtf8(
			u8"採用された敵同士の接触回数");
		const ImVec2 meaningSize = ImGui::CalcTextSize(meaning);
		foreground->AddText(
			ImVec2(
				centerX - meaningSize.x * 0.5f,
				centerY + bannerHeight * 0.5f + 9.0f),
			IM_COL32(0, 0, 0, static_cast<int>(220.0f * alpha)),
			meaning);
		foreground->AddText(
			ImVec2(
				centerX - meaningSize.x * 0.5f,
				centerY + bannerHeight * 0.5f + 7.0f),
			IM_COL32(238, 242, 248, static_cast<int>(190.0f * alpha)),
			meaning);
	}
}

/// <summary>
/// 一度のショットで複数の敵を撃破したときのバナーを描画する。
/// </summary>
void GamePresentation::DrawMultiDefeatBanner(Game& game)
{
	ImDrawList* foreground = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	if (m_MultiDefeatDisplayLifetime > 0.0f &&
		m_CurrentShotDefeatCount >= 2 &&
		game.IsMultiDefeatEffectsEnabled())
	{
		const bool avoidDebugWindow = game.IsDebugMode() || GameUi::showDebugger;
		const float centerX = avoidDebugWindow
			? viewport->Pos.x + viewport->Size.x * 0.70f
			: viewport->Pos.x + viewport->Size.x * 0.5f;
		const float centerY = viewport->Pos.y +
			(avoidDebugWindow ? 250.0f :
				(m_HeavyChainDisplayLifetime > 0.0f ? 154.0f : 94.0f));
		const float elapsed = (std::max)(
			0.0f,
			m_MultiDefeatDisplayTotalLifetime - m_MultiDefeatDisplayLifetime);
		constexpr float popDuration = 0.12f;
		constexpr float settleDuration = 0.25f;
		constexpr float holdDuration = 0.80f;
		constexpr float fadeDuration = 1.00f;
		float popScale = 1.0f;
		if (elapsed < popDuration)
		{
			popScale = 1.0f + 0.35f * (elapsed / popDuration);
		}
		else if (elapsed < popDuration + settleDuration)
		{
			const float settle =
				(elapsed - popDuration) / settleDuration;
			popScale = 1.35f - 0.35f * settle;
		}
		const float fadeStart = popDuration + settleDuration + holdDuration;
		const float alpha = elapsed <= fadeStart
			? 1.0f
			: std::clamp(1.0f - (elapsed - fadeStart) / fadeDuration, 0.0f, 1.0f);
		const int tier = CombatPresentationRules::MultiDefeatTier(
			m_CurrentShotDefeatCount);
		const ImU32 accent = tier >= 2
			? IM_COL32(255, 132, 58, static_cast<int>(255.0f * alpha))
			: IM_COL32(255, 220, 92, static_cast<int>(255.0f * alpha));
		const std::string text = m_CurrentShotDefeatCount == 2
			? "DOUBLE BREAK"
			: "MULTI BREAK  x" + std::to_string(m_CurrentShotDefeatCount);
		ImFont* multiFont = ImGui::GetFont();
		const float multiFontSize = ImGui::GetFontSize() *
			(tier >= 2 ? 1.30f : 1.18f) * popScale;
		const ImVec2 textSize = multiFont->CalcTextSizeA(
			multiFontSize,
			FLT_MAX,
			0.0f,
			text.c_str());
		const float width = (std::max)(252.0f, textSize.x + 82.0f);
		const float halfHeight = 23.0f * popScale;
		const ImVec2 minimum(centerX - width * 0.5f, centerY - halfHeight);
		const ImVec2 maximum(centerX + width * 0.5f, centerY + halfHeight);
		foreground->AddRectFilled(
			minimum,
			maximum,
			IM_COL32(12, 6, 5, static_cast<int>(
				CombatPresentationRules::MultiBreakBackgroundAlpha * alpha)),
			13.0f);
		foreground->AddRect(minimum, maximum, accent, 13.0f, 0, 2.5f);
		foreground->AddLine(
			ImVec2(minimum.x - 34.0f, centerY),
			ImVec2(minimum.x - 4.0f, centerY),
			accent,
			3.0f);
		foreground->AddLine(
			ImVec2(maximum.x + 4.0f, centerY),
			ImVec2(maximum.x + 34.0f, centerY),
			accent,
			3.0f);
		const ImVec2 textAt(
			centerX - textSize.x * 0.5f,
			centerY - textSize.y * 0.5f);
		for (const ImVec2 offset : {
			ImVec2(-2.0f, 0.0f), ImVec2(2.0f, 0.0f),
				ImVec2(0.0f, -2.0f), ImVec2(0.0f, 2.0f) })
		{
			foreground->AddText(
				multiFont,
				multiFontSize,
				ImVec2(textAt.x + offset.x, textAt.y + offset.y),
				IM_COL32(0, 0, 0, static_cast<int>(190.0f * alpha)),
				text.c_str());
		}
		foreground->AddText(
			multiFont,
			multiFontSize,
			textAt,
			WithAlpha(
				IM_COL32(255, 238, 188, 255),
				alpha * 0.95f),
			text.c_str());
	}
}

/// <summary>
/// ダメージや能力発動などの一時表示を、盤面上またはHUDに描画する。
/// </summary>
void GamePresentation::DrawFloatingFeedback(Game& game)
{
	ImDrawList* foreground = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	std::vector<std::size_t> feedbackOrder;
	feedbackOrder.reserve(m_Feedback.size());
	for (std::size_t index = 0; index < m_Feedback.size(); ++index)
		feedbackOrder.push_back(index);
	std::stable_sort(
		feedbackOrder.begin(),
		feedbackOrder.end(),
		[&](std::size_t first, std::size_t second)
		{
			const auto priority = [](const FloatingFeedback& feedback)
				{
					if (feedback.text.find("撃破") != std::string::npos) return 10000;
					int damage = 0;
					return sscanf_s(feedback.text.c_str(), "-%d HP", &damage) == 1
						? 100 + damage
						: 0;
				};
			return priority(m_Feedback[first]) > priority(m_Feedback[second]);
		});
	std::vector<ImVec4> placedFeedbackRectangles;
	placedFeedbackRectangles.reserve(m_Feedback.size() + 16);
	if (m_HeavyChainDisplayLifetime > 0.0f &&
		CollisionPresentationRules::ChainTier(m_HeavyPhysicsChainCount) >= 2)
	{
		const bool avoidDebugWindow = game.IsDebugMode() || GameUi::showDebugger;
		const float protectedCenterX = avoidDebugWindow
			? viewport->Pos.x + viewport->Size.x * 0.405f
			: viewport->Pos.x + viewport->Size.x * 0.5f;
		const float protectedCenterY = viewport->Pos.y +
			(avoidDebugWindow ? 108.0f : 88.0f);
		placedFeedbackRectangles.emplace_back(
			protectedCenterX - 205.0f,
			protectedCenterY - 40.0f,
			protectedCenterX + 205.0f,
			protectedCenterY + 52.0f);
	}
	if (m_MultiDefeatDisplayLifetime > 0.0f &&
		m_CurrentShotDefeatCount >= 2)
	{
		const bool avoidDebugWindow = game.IsDebugMode() || GameUi::showDebugger;
		const float centerX = avoidDebugWindow
			? viewport->Pos.x + viewport->Size.x * 0.70f
			: viewport->Pos.x + viewport->Size.x * 0.5f;
		const float centerY = viewport->Pos.y +
			(avoidDebugWindow ? 250.0f :
				(m_HeavyChainDisplayLifetime > 0.0f ? 154.0f : 94.0f));
		placedFeedbackRectangles.emplace_back(
			centerX - 178.0f,
			centerY - 30.0f,
			centerX + 178.0f,
			centerY + 30.0f);
	}
	if (m_HitSummaryLifetime > 0.0f)
	{
		placedFeedbackRectangles.emplace_back(
			viewport->Pos.x + viewport->Size.x - 400.0f,
			viewport->Pos.y + 264.0f,
			viewport->Pos.x + viewport->Size.x - 18.0f,
			viewport->Pos.y + 360.0f);
	}
	for (const EnemyBall* enemy :
		GameRuntime::World().GetComponents<EnemyBall>())
	{
		if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed()) continue;
		ImVec2 center;
		ImVec2 top;
		if (!TryProjectToScreen(enemy->GetPosition(), center) ||
			!TryProjectToScreen(
				enemy->GetPosition() + Vector3(0.0f, enemy->GetVisualRadius(), 0.0f),
				top))
		{
			continue;
		}
		const float radius = (std::max)(14.0f, std::abs(center.y - top.y));
		placedFeedbackRectangles.emplace_back(
			center.x - 108.0f,
			center.y - radius - 76.0f,
			center.x + 108.0f,
			center.y + radius + 108.0f);
	}
	const std::array<ImVec2, 24> feedbackOffsets = {
		ImVec2(0.0f, 0.0f),
		ImVec2(-34.0f, -26.0f), ImVec2(34.0f, -26.0f),
		ImVec2(-62.0f, -10.0f), ImVec2(62.0f, -10.0f),
		ImVec2(-42.0f, 28.0f), ImVec2(42.0f, 28.0f),
		ImVec2(0.0f, 46.0f),
		ImVec2(-78.0f, 34.0f), ImVec2(78.0f, 34.0f),
		ImVec2(-96.0f, 0.0f), ImVec2(96.0f, 0.0f),
		ImVec2(-72.0f, 68.0f), ImVec2(72.0f, 68.0f),
		ImVec2(0.0f, 76.0f),
		ImVec2(-108.0f, 58.0f), ImVec2(108.0f, 58.0f),
		ImVec2(-120.0f, 18.0f), ImVec2(120.0f, 18.0f),
		ImVec2(-45.0f, 98.0f), ImVec2(45.0f, 98.0f),
		ImVec2(0.0f, 112.0f),
		ImVec2(-105.0f, 96.0f), ImVec2(105.0f, 96.0f),
	};
	const std::array<ImVec2, 7> targetFeedbackOffsets = {
		ImVec2(0.0f, 0.0f),
		ImVec2(-18.0f, -12.0f), ImVec2(18.0f, -12.0f),
		ImVec2(-28.0f, 8.0f), ImVec2(28.0f, 8.0f),
		ImVec2(0.0f, -22.0f), ImVec2(0.0f, 20.0f),
	};
	for (const std::size_t feedbackIndex : feedbackOrder)
	{
		const FloatingFeedback& feedback = m_Feedback[feedbackIndex];
		ImVec2 position;
		ImVec2 worldAnchor;
		const bool playerHudMessage = feedback.playerHudMessage;
		if (playerHudMessage)
		{
			position = ImVec2(
				viewport->Pos.x + viewport->Size.x * 0.5f,
				viewport->Pos.y +
				(m_HeavyChainDisplayLifetime > 0.0f ? 278.0f : 105.0f));
			worldAnchor = position;
		}
		else if (!TryProjectToScreen(feedback.worldPosition, worldAnchor))
		{
			continue;
		}
		else
		{
			position = worldAnchor;
		}

		const float progress = 1.0f -
			feedback.lifetime / feedback.totalLifetime;
		if (feedback.followsDamageTarget)
		{
			const float age = (std::max)(
				0.0f,
				feedback.totalLifetime - feedback.lifetime);
			const float postFollowAge = (std::max)(
				0.0f,
				age - CombatPresentationRules::DamageTargetFollowSeconds);
			const float postFollowDuration = (std::max)(
				0.001f,
				feedback.totalLifetime -
					CombatPresentationRules::DamageTargetFollowSeconds);
			const float postFollowProgress = std::clamp(
				postFollowAge / postFollowDuration,
				0.0f,
				1.0f);
			// Keep the label diagonally above the ball while following, then rise
			// from the exact release position without a transition jump.
			position.x += 18.0f;
			position.y -= 24.0f + postFollowProgress * 34.0f;
		}
		else
		{
			position.y -= 20.0f + progress * 58.0f;
		}
		const float fadeIn = std::clamp(progress * 8.0f, 0.0f, 1.0f);
		const float fadeOut = std::clamp(
			feedback.lifetime / 0.28f,
			0.0f,
			1.0f);
		const float alpha = fadeIn * fadeOut;
		ImFont* font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize() * feedback.fontScale;
		const ImVec2 textSize = font->CalcTextSizeA(
			fontSize,
			FLT_MAX,
			0.0f,
			feedback.text.c_str());
		ImVec2 textPosition(
			position.x - textSize.x * 0.5f,
			position.y - textSize.y * 0.5f);
		if (!playerHudMessage)
		{
			float bestScore = FLT_MAX;
			ImVec2 bestPosition = textPosition;
			ImVec4 bestRectangle{};
			const ImVec2* candidateOffsets = feedback.followsDamageTarget
				? targetFeedbackOffsets.data()
				: feedbackOffsets.data();
			const std::size_t candidateCount = feedback.followsDamageTarget
				? targetFeedbackOffsets.size()
				: feedbackOffsets.size();
			for (std::size_t candidateIndex = 0;
				candidateIndex < candidateCount;
				++candidateIndex)
			{
				const ImVec2& offset = candidateOffsets[candidateIndex];
				ImVec2 candidate(
					textPosition.x + offset.x,
					textPosition.y + offset.y);
				candidate.x = std::clamp(
					candidate.x,
					viewport->Pos.x + 12.0f,
					viewport->Pos.x + viewport->Size.x - textSize.x - 12.0f);
				candidate.y = std::clamp(
					candidate.y,
					viewport->Pos.y + 28.0f,
					viewport->Pos.y + viewport->Size.y - textSize.y - 18.0f);
				const ImVec4 candidateRectangle(
					candidate.x - 5.0f,
					candidate.y - 3.0f,
					candidate.x + textSize.x + 5.0f,
					candidate.y + textSize.y + 3.0f);
				float overlapArea = 0.0f;
				for (const ImVec4& placed : placedFeedbackRectangles)
				{
					const float width = (std::max)(
						0.0f,
						(std::min)(candidateRectangle.z, placed.z) -
						(std::max)(candidateRectangle.x, placed.x));
					const float height = (std::max)(
						0.0f,
						(std::min)(candidateRectangle.w, placed.w) -
						(std::max)(candidateRectangle.y, placed.y));
					overlapArea += width * height;
				}
				const float distancePenalty =
					(offset.x * offset.x + offset.y * offset.y) *
					(feedback.followsDamageTarget ? 0.08f : 0.012f);
				const float score = overlapArea * 12.0f + distancePenalty;
				if (score < bestScore)
				{
					bestScore = score;
					bestPosition = candidate;
					bestRectangle = candidateRectangle;
					if (overlapArea <= 0.0f && distancePenalty <= 0.0f) break;
				}
			}
			textPosition = bestPosition;
			placedFeedbackRectangles.push_back(bestRectangle);
		}
		const bool defeated =
			feedback.text.find("撃破") != std::string::npos;
		int damageValue = 0;
		const bool normalDamage =
			sscanf_s(feedback.text.c_str(), "-%d HP", &damageValue) == 1;
		if (defeated)
		{
			foreground->AddRectFilled(
				ImVec2(textPosition.x - 7.0f, textPosition.y - 4.0f),
				ImVec2(textPosition.x + textSize.x + 7.0f,
					textPosition.y + textSize.y + 4.0f),
				IM_COL32(38, 18, 2, static_cast<int>(
					CombatPresentationRules::DefeatBackgroundAlpha * alpha)),
				7.0f);
		}
		else if (!playerHudMessage)
		{
			foreground->AddRectFilled(
				ImVec2(textPosition.x - 5.0f, textPosition.y - 2.0f),
				ImVec2(textPosition.x + textSize.x + 5.0f,
					textPosition.y + textSize.y + 2.0f),
				IM_COL32(6, 12, 18, static_cast<int>(
					(normalDamage
						? CombatPresentationRules::NormalDamageBackgroundAlpha
						: CombatPresentationRules::AbilityBackgroundAlpha) * alpha)),
				5.0f);
		}
		const float textAlpha = alpha * (defeated ? 0.95f : 0.85f);
		const float outlineAlpha = alpha * (defeated ? 0.78f : 0.65f);
		for (const ImVec2 outlineOffset : {
			ImVec2(-1.2f, 0.0f), ImVec2(1.2f, 0.0f),
				ImVec2(0.0f, -1.2f), ImVec2(0.0f, 1.2f) })
		{
			foreground->AddText(
				font,
				fontSize,
				ImVec2(
					textPosition.x + outlineOffset.x,
					textPosition.y + outlineOffset.y),
				IM_COL32(0, 0, 0, static_cast<int>(255.0f * outlineAlpha)),
				feedback.text.c_str());
		}
		foreground->AddText(
			font,
			fontSize,
			textPosition,
			WithAlpha(feedback.color, textAlpha),
			feedback.text.c_str());
		if (feedback.drawRing)
		{
			const float radius = 16.0f + progress * 48.0f;
			foreground->AddCircle(
				worldAnchor,
				radius,
				WithAlpha(feedback.color, alpha * 0.7f),
				32,
				3.0f);
		}
	}
}

/// <summary>
/// 衝突や反応演出の診断情報をデバッグ表示に描画する。
/// </summary>
void GamePresentation::DrawFeedbackDiagnostics(Game& game)
{
	ImDrawList* foreground = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	if (GameUi::showDebugger && IsBattleScene(game))
	{
		const CollisionVfxDiagnostics& vfx =
			m_CollisionVfx.GetDiagnostics();
		char diagnostics[256]{};
		sprintf_s(
			diagnostics,
			"VFX impact=%d chain=%d sparks=%d/%d waves=%d/%d burst=%d/%d trail=%d/%d hitflash=%d/%d defeat=%d dup=%d gen=%d skip=%d merge=%d prim=%d/%d",
			vfx.impactEvents,
			m_HeavyPhysicsChainCount,
			vfx.activeSparks,
			vfx.maximumActiveSparks,
			vfx.activeWaves,
			vfx.maximumActiveWaves,
			vfx.activeBursts,
			vfx.maximumActiveBursts,
			vfx.activeTrails,
			vfx.maximumActiveTrails,
			m_CombatReactionDiagnostics.activeHitFlashes,
			m_CombatReactionDiagnostics.maximumActiveHitFlashes,
			m_CombatReactionDiagnostics.uniqueDefeats,
			m_CombatReactionDiagnostics.duplicateDefeatsSuppressed,
			vfx.sparksGenerated + vfx.burstsGenerated + vfx.trailsGenerated,
			vfx.sparksSkipped + vfx.wavesSkipped + vfx.burstsSkipped +
			vfx.trailsSkipped,
			vfx.wavesMerged,
			vfx.primitiveSubmissionsLastFrame,
			vfx.maximumPrimitiveSubmissions);
		foreground->AddText(
			ImVec2(viewport->Pos.x + 18.0f, viewport->Pos.y + viewport->Size.y - 28.0f),
			IM_COL32(210, 235, 255, 230),
			diagnostics);
		const FrameTimingSnapshot& timing = game.GetFrameTimingSnapshot();
		char timingText[256]{};
		sprintf_s(
			timingText,
			"FRAME %s n=%d avg=%.2f p95=%.2f p99=%.2f max=%.2f ms  update=%.2f draw=%.2f present=%.2f",
			timing.active ? "MEASURING" : timing.completed ? "SHOT" : "IDLE",
			timing.sampleCount,
			timing.averageFrameMilliseconds,
			timing.p95FrameMilliseconds,
			timing.p99FrameMilliseconds,
			timing.maximumFrameMilliseconds,
			timing.averageUpdateMilliseconds,
			timing.averageDrawMilliseconds,
			timing.averagePresentMilliseconds);
		foreground->AddText(
			ImVec2(viewport->Pos.x + 18.0f, viewport->Pos.y + viewport->Size.y - 50.0f),
			IM_COL32(255, 232, 150, 235),
			timingText);
	}
}

/// <summary>
/// 命中、ダメージ、撃破や能力軸の変化をまとめて短時間表示する。
/// </summary>
void GamePresentation::DrawShotSummary()
{
	ImDrawList* foreground = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	if (m_HitSummaryLifetime > 0.0f)
	{
		std::vector<std::string> lines;
		std::ostringstream primary;
		primary << "SHOT RESULT   DMG " << m_CurrentShotDamage
			<< "   HIT " << m_CurrentShotHitCount;
		lines.push_back(primary.str());
		if (m_CurrentShotDefeatCount > 0 ||
			m_CurrentShotChargeInterruptions > 0)
		{
			std::ostringstream result;
			if (m_CurrentShotDefeatCount > 0)
				result << "BREAK " << m_CurrentShotDefeatCount;
			if (m_CurrentShotChargeInterruptions > 0)
			{
				if (m_CurrentShotDefeatCount > 0) result << "   ";
				result << "CHARGE BREAK " << m_CurrentShotChargeInterruptions;
			}
			lines.push_back(result.str());
		}
		std::ostringstream resources;
		int resourceItems = 0;
		for (std::size_t index = 0;
			index < m_CurrentShotResourceGained.size();
			++index)
		{
			const int gained = m_CurrentShotResourceGained[index];
			const int consumed = m_CurrentShotResourceConsumed[index];
			if (gained <= 0 && consumed <= 0) continue;
			if (resourceItems > 0) resources << "   ";
			resources << AxisResourceLabel(
				static_cast<AxisResourceKind>(index));
			if (gained > 0) resources << " +" << gained;
			if (consumed > 0) resources << " -" << consumed;
			++resourceItems;
			if (resourceItems >= 2) break;
		}
		if (m_CurrentShotFinisherMask != 0)
		{
			if (resourceItems > 0) resources << "   ";
			resources << "FINISHER";
		}
		if (resourceItems > 0 || m_CurrentShotFinisherMask != 0)
			lines.push_back(resources.str());
		if (lines.size() == 1 && m_CurrentShotHitCount == 0)
			lines.push_back("NO HIT");

		float width = 0.0f;
		for (const std::string& line : lines)
			width = (std::max)(width, ImGui::CalcTextSize(line.c_str()).x);
		width += 34.0f;
		const float height = 18.0f +
			static_cast<float>(lines.size()) * 22.0f;
		// Keep the result below the built-in top-right HUD and the debug-battle
		// controls.  Dense battles also tend to occupy the centre-left, so this
		// preserves a stable, readable result area without covering enemy state.
		constexpr float summaryTop = 270.0f;
		const ImVec2 maximum(
			viewport->Pos.x + viewport->Size.x - 26.0f,
			viewport->Pos.y + summaryTop + height);
		const ImVec2 minimum(
			maximum.x - width,
			viewport->Pos.y + summaryTop);
		const float alpha = std::clamp(
			m_HitSummaryLifetime / 0.28f,
			0.0f,
			1.0f);
		foreground->AddRectFilled(
			minimum,
			maximum,
			IM_COL32(8, 13, 22, static_cast<int>(
				CombatPresentationRules::ShotResultBackgroundAlpha * alpha)),
			9.0f);
		foreground->AddRect(
			minimum,
			maximum,
			IM_COL32(255, 207, 72, static_cast<int>(235.0f * alpha)),
			9.0f,
			0,
			1.6f);
		for (std::size_t index = 0; index < lines.size(); ++index)
		{
			foreground->AddText(
				ImVec2(minimum.x + 17.0f, minimum.y + 10.0f + index * 22.0f),
				index == 0
				? IM_COL32(255, 224, 100, static_cast<int>(242.0f * alpha))
				: IM_COL32(224, 235, 246, static_cast<int>(217.0f * alpha)),
				lines[index].c_str());
		}
	}
}
