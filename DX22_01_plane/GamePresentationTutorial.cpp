#include "GamePresentation.h"
#include "Game.h"
#include "BattleViewportLayout.h"
#include "GamePresentationPrimitives.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "PlayerBall.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <cmath>

using DirectX::SimpleMath::Vector3;
using GamePresentationPrimitives::PresentationUtf8;
using GamePresentationPrimitives::TryProjectToScreen;

namespace
{
constexpr float kHintMargin = 12.0f;
constexpr float kHintGap = 6.0f;

enum class MouseGesture { LeftClick, RightClick, Move, PullBack };

constexpr float kMouseHintIconWidth = 65.0f;
constexpr float kMouseHintHeight = 46.0f;
constexpr float kMouseHintRightPadding = 13.0f;

ImVec2 MouseHintSize(const char* label)
{
	const ImVec2 textSize = ImGui::CalcTextSize(label);
	return ImVec2(kMouseHintIconWidth + textSize.x + kMouseHintRightPadding,
		(std::max)(kMouseHintHeight, textSize.y + 16.0f));
}

void DrawGestureArrow(ImDrawList& draw, ImVec2 start, ImVec2 end, ImU32 color)
{
	const float dx = end.x - start.x;
	const float dy = end.y - start.y;
	const float length = std::sqrt(dx * dx + dy * dy);
	const float forwardX = dx / length;
	const float forwardY = dy / length;
	const ImVec2 base(end.x - 4.5f * forwardX, end.y - 4.5f * forwardY);
	draw.AddLine(start, base, color, 1.6f);
	draw.AddTriangleFilled(end, ImVec2(base.x - 3.0f * forwardY, base.y + 3.0f * forwardX),
		ImVec2(base.x + 3.0f * forwardY, base.y - 3.0f * forwardX), color);
}

void DrawMouseHint(ImDrawList& draw, ImVec2 minimum, MouseGesture gesture,
	const char* label, float seconds)
{
	constexpr ImU32 background = IM_COL32(10, 21, 35, 246);
	constexpr ImU32 cyan = IM_COL32(102, 222, 253, 255);
	constexpr ImU32 pale = IM_COL32(232, 247, 254, 255);
	const ImVec2 size = MouseHintSize(label);
	const ImVec2 maximum(minimum.x + size.x, minimum.y + size.y);
	draw.AddRectFilled(ImVec2(minimum.x, minimum.y + 2.0f),
		ImVec2(maximum.x, maximum.y + 2.0f), IM_COL32(2, 5, 12, 140), 9.0f);
	draw.AddRectFilled(minimum, maximum, background, 9.0f);
	draw.AddRect(minimum, maximum, IM_COL32(89, 187, 222, 180), 9.0f, 1.2f);

	const float phase = std::fmod(seconds, 1.65f) / 1.65f;
	const float centerY = minimum.y + size.y * 0.5f;
	const auto iconPoint = [&](float x, float y) { return ImVec2(minimum.x + x, centerY + y); };
	ImVec2 mouseCenter = iconPoint(31.0f, 0.0f);
	bool held = false;
	float pulse = 1.0f;
	if (gesture == MouseGesture::Move)
	{
		mouseCenter.x += 8.0f * std::sin(phase * 6.2831853f);
		DrawGestureArrow(draw, iconPoint(18.0f, 0.0f), iconPoint(6.0f, 0.0f), cyan);
		DrawGestureArrow(draw, iconPoint(44.0f, 0.0f), iconPoint(56.0f, 0.0f), cyan);
	}
	else if (gesture == MouseGesture::PullBack)
	{
		const float pull = std::clamp((phase - 0.18f) / 0.42f, 0.0f, 1.0f);
		const float reset = std::clamp((phase - 0.80f) / 0.20f, 0.0f, 1.0f);
		const float amount = pull * pull * (3.0f - 2.0f * pull) * (1.0f - reset);
		mouseCenter = iconPoint(20.0f + 13.0f * amount, -6.0f + 12.0f * amount);
		DrawGestureArrow(draw, iconPoint(38.0f, -7.0f), iconPoint(54.0f, 10.0f), cyan);
		held = phase >= 0.12f && phase < 0.62f;
		pulse = (phase - 0.62f) / 0.17f;
	}
	else
	{
		held = phase < 0.26f;
		pulse = phase / 0.34f;
	}

	const auto mousePoint = [&](float x, float y) { return ImVec2(mouseCenter.x + x, mouseCenter.y + y); };
	const ImVec2 bodyMinimum = mousePoint(-9.0f, -13.0f);
	const ImVec2 bodyMaximum = mousePoint(9.0f, 13.0f);
	draw.AddRectFilled(bodyMinimum, bodyMaximum, IM_COL32(26, 45, 63, 255), 7.0f);
	const bool right = gesture == MouseGesture::RightClick;
	if (gesture != MouseGesture::Move)
	{
		const ImU32 buttonColor = held ? cyan : IM_COL32(59, 113, 140, 255);
		draw.AddRectFilled(mousePoint(right ? 0.0f : -8.0f, -12.0f),
			mousePoint(right ? 8.0f : 0.0f, -2.0f), buttonColor, 5.0f,
			right ? ImDrawFlags_RoundCornersTopRight : ImDrawFlags_RoundCornersTopLeft);
	}
	draw.AddRect(bodyMinimum, bodyMaximum, pale, 7.0f, 1.5f);
	draw.AddLine(mousePoint(0.0f, -12.0f), mousePoint(0.0f, -2.0f), pale, 1.1f);
	draw.AddLine(mousePoint(-8.0f, -2.0f), mousePoint(8.0f, -2.0f), pale, 1.1f);
	if (pulse >= 0.0f && pulse < 1.0f)
	{
		const auto alpha = static_cast<int>(145.0f * (1.0f - pulse));
		draw.AddCircle(mousePoint(right ? 4.0f : -4.0f, -7.0f),
			5.0f + pulse * 8.0f, IM_COL32(106, 225, 255, alpha), 20, 1.4f);
	}
	draw.AddText(ImVec2(minimum.x + kMouseHintIconWidth,
		centerY - ImGui::CalcTextSize(label).y * 0.5f), pale, label);
}

float TutorialTop(const ImGuiViewport& viewport)
{
	const auto layout = BattleViewportLayout::Calculate(viewport.WorkSize.x, viewport.WorkSize.y);
	return viewport.WorkPos.y + layout.hudHeight + kHintMargin;
}

ImVec2 KeepMouseHintsOnScreen(const ImGuiViewport& viewport, ImVec2 position, ImVec2 size)
{
	const ImVec2 minimum(viewport.WorkPos.x + kHintMargin, TutorialTop(viewport));
	const ImVec2 maximum(
		viewport.WorkPos.x + viewport.WorkSize.x - kHintMargin - size.x,
		viewport.WorkPos.y + viewport.WorkSize.y - kHintMargin - size.y);
	return ImVec2(
		std::clamp(position.x, minimum.x, (std::max)(minimum.x, maximum.x)),
		std::clamp(position.y, minimum.y, (std::max)(minimum.y, maximum.y)));
}
}

/// <summary>
/// 自動表示か手動表示かを記録して、チュートリアルを最初の手順から始める。
/// </summary>
void GamePresentation::StartTutorial(bool openedManually)
{
	m_TutorialOpenedManually = openedManually;
	m_TutorialActive = true;
	m_TutorialStep = TutorialStep::PracticeShot;
}

/// <summary>
/// 操作対象のカードに、選択と詳細を開くクリック操作を表示する。
/// </summary>
void GamePresentation::DrawTutorialBallChoice(ImVec2 cardTopCenter)
{
	if (!m_TutorialActive || m_TutorialStep != TutorialStep::PracticeShot ||
		IsBattleInteractionBlocked())
		return;

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const char* selectLabel = PresentationUtf8(u8"球を選ぶ");
	const char* detailLabel = PresentationUtf8(u8"詳細を見る");
	const ImVec2 selectSize = MouseHintSize(selectLabel);
	const ImVec2 detailSize = MouseHintSize(detailLabel);
	const float rowWidth = selectSize.x + kHintGap + detailSize.x;
	const bool stacked = rowWidth > viewport->WorkSize.x - 2.0f * kHintMargin;
	const ImVec2 size(
		stacked ? (std::max)(selectSize.x, detailSize.x) : rowWidth,
		stacked ? selectSize.y + kHintGap + detailSize.y : (std::max)(selectSize.y, detailSize.y));
	const ImVec2 position = KeepMouseHintsOnScreen(*viewport,
		ImVec2(cardTopCenter.x - size.x * 0.5f, cardTopCenter.y - size.y - 15.0f), size);

	ImDrawList& draw = *ImGui::GetForegroundDrawList(viewport);
	const float pointerY = position.y + size.y;
	if (pointerY + 10.0f <= cardTopCenter.y)
	{
		const float pointerX = std::clamp(cardTopCenter.x,
			position.x + 10.0f, position.x + size.x - 10.0f);
		draw.AddTriangleFilled(ImVec2(pointerX - 5.0f, pointerY - 1.0f),
			ImVec2(pointerX + 5.0f, pointerY - 1.0f),
			ImVec2(cardTopCenter.x, cardTopCenter.y - 4.0f), IM_COL32(102, 222, 253, 215));
	}
	DrawMouseHint(draw, position, MouseGesture::LeftClick, selectLabel, m_PresentationTimeSeconds);
	const ImVec2 detailPosition = stacked
		? ImVec2(position.x, position.y + selectSize.y + kHintGap)
		: ImVec2(position.x + selectSize.x + kHintGap, position.y);
	DrawMouseHint(draw, detailPosition, MouseGesture::RightClick, detailLabel, m_PresentationTimeSeconds);
}

/// <summary>
/// 自球の近くに、現在の照準またはパワー調整の操作を表示する。
/// </summary>
void GamePresentation::DrawTutorialShotHint(BattleState state)
{
	const auto players = GameRuntime::World().GetComponents<PlayerBall>();
	const PlayerBall* player = players.empty() ? nullptr : players.front();
	if (player == nullptr || !player->IsIdle())
		return;

	const Vector3 ballPosition = player->GetPosition();
	ImVec2 center;
	ImVec2 edge;
	if (!TryProjectToScreen(ballPosition, center) ||
		!TryProjectToScreen(ballPosition + Vector3(player->GetBall()->GetRadius(), 0.0f, 0.0f), edge))
		return;

	const bool pulling = state == BattleState::AimingPower;
	const char* label = pulling
		? PresentationUtf8(u8"左ボタンを押したまま手前に引く\n離すとショット")
		: PresentationUtf8(u8"マウスを動かして狙う\n左ボタンで照準を固定");
	const char* reselectLabel = PresentationUtf8(u8"球を選び直す");
	const ImVec2 primarySize = MouseHintSize(label);
	const ImVec2 reselectSize = MouseHintSize(reselectLabel);
	const ImVec2 size = pulling ? primarySize : ImVec2(
		(std::max)(primarySize.x, reselectSize.x), primarySize.y + kHintGap + reselectSize.y);

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float clearance = std::hypot(edge.x - center.x, edge.y - center.y) + 24.0f;
	const float rightX = center.x + clearance;
	const float leftX = center.x - clearance - size.x;
	const float viewportRight = viewport->WorkPos.x + viewport->WorkSize.x;
	const bool fitsRight = rightX + size.x <= viewportRight - kHintMargin;
	const bool fitsLeft = leftX >= viewport->WorkPos.x + kHintMargin;
	const bool preferLeft = center.x > viewport->WorkPos.x + viewport->WorkSize.x * 0.5f;
	ImVec2 position(center.x - size.x * 0.5f, center.y - clearance - size.y);
	if (fitsRight || fitsLeft)
	{
		position = ImVec2(
			fitsLeft && (preferLeft || !fitsRight) ? leftX : rightX,
			center.y - size.y * 0.5f);
	}
	position = KeepMouseHintsOnScreen(*viewport, position, size);
	ImDrawList& draw = *ImGui::GetForegroundDrawList(viewport);
	DrawMouseHint(draw, position, pulling ? MouseGesture::PullBack : MouseGesture::Move,
		label, m_PresentationTimeSeconds);
	if (!pulling)
	{
		position.y += primarySize.y + kHintGap;
		DrawMouseHint(draw, position, MouseGesture::RightClick, reselectLabel, m_PresentationTimeSeconds);
	}
}

/// <summary>
/// プレイ中の操作ガイドと、スキップや完了に使うボタンを描画する。
/// </summary>
void GamePresentation::DrawTutorial(Game& game)
{
	if (game.IsPaused() || game.IsDebugEditorOpen() || IsBattleInteractionBlocked() ||
		ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
		return;

	const bool battleScene = IsBattleScene(game);
	if (!battleScene && !m_TutorialOpenedManually)
		return;

	const BattleState state = game.GetBattleState();
	if (battleScene && m_TutorialStep == TutorialStep::PracticeShot &&
		game.IsShotPreparationInputEnabled() &&
		(state == BattleState::AimingDirection || state == BattleState::AimingPower))
	{
		DrawTutorialShotHint(state);
		// A window under the mouse would cancel the drag via WantCaptureMouse.
		if (state == BattleState::AimingPower)
			return;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kHintMargin, TutorialTop(*viewport)));
	ImGui::SetNextWindowBgAlpha(0.90f);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus;
	if (ImGui::Begin("##play_tutorial", nullptr, flags))
	{
		if (m_TutorialStep == TutorialStep::ReadResult)
		{
			ImGui::TextUnformatted(PresentationUtf8(u8"基本操作は完了です"));
			ImGui::TextDisabled("%s", PresentationUtf8(u8"球が止まると敵が反撃し、次の球を選びます。"));
			if (ImGui::TreeNode(PresentationUtf8(u8"戦闘のヒント")))
			{
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 320.0f);
				ImGui::TextUnformatted(PresentationUtf8(u8"生成球で資源を作り、利用球・決着球へつないで敵を倒しましょう。"));
				ImGui::TextUnformatted(PresentationUtf8(u8"カテゴリ固有の資源は、対応するカテゴリの生成球で蓄積できます。"));
				ImGui::TextUnformatted(PresentationUtf8(u8"貫通痕とクッションはどのカテゴリでも利用でき、同じカテゴリでは効果が強くなります。"));
				ImGui::TextUnformatted(PresentationUtf8(u8"敵を全滅させると報酬を選び、次のルートへ進めます。"));
				ImGui::PopTextWrapPos();
				ImGui::TreePop();
			}
			if (ImGui::SmallButton(PresentationUtf8(u8"ガイドを閉じる")))
				m_TutorialActive = false;
		}
		else
		{
			if (!battleScene)
				ImGui::TextUnformatted(PresentationUtf8(u8"戦闘が始まると操作ガイドを表示します"));
			else if (m_TutorialStep == TutorialStep::WatchShot)
				ImGui::TextUnformatted(PresentationUtf8(u8"赤い数字が与えたダメージです"));
			if (ImGui::SmallButton(PresentationUtf8(u8"ガイドをスキップ")))
				m_TutorialActive = false;
		}
	}
	ImGui::End();
}
