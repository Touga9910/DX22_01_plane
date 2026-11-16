#include "GamePresentation.h"
#include "GamePresentationPrimitives.h"
#include "CombatOverlayDrawing.h"
#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "PlayerBall.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <cfloat>
#include <cmath>

using DirectX::SimpleMath::Vector3;
using namespace GamePresentationPrimitives;
using namespace CombatOverlayDrawing;

void GamePresentation::DrawPlayerResourceBadges(Game& game)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoInputs;
	const auto players = GameRuntime::World().GetComponents<PlayerBall>();
	const PlayerBall* player = players.empty() ? nullptr : players.front();
	if (player == nullptr || player->IsDefeated()) return;
	ImVec2 center;
	ImVec2 top;
	if (!TryProjectToScreen(player->GetPosition(), center) ||
		!TryProjectToScreen(player->GetPosition() + Vector3(
			0.0f, player->GetBall()->GetRadius(), 0.0f), top))
		return;
	const float projectedRadius = (std::max)(14.0f, std::abs(center.y - top.y));
	if (player->IsPowerDragging())
	{
		ImDrawList* gaugeDrawList = ImGui::GetBackgroundDrawList(
			ImGui::GetMainViewport());
		const float powerRatio = player->GetShotPowerRatio();
		const float gaugeRadius = projectedRadius + 8.0f;
		constexpr float gaugeStart = -3.92699082f;
		constexpr float gaugeSweep = 4.71238898f;
		DrawArc(
			gaugeDrawList,
			center,
			gaugeRadius,
			gaugeStart,
			gaugeStart + gaugeSweep,
			IM_COL32(4, 10, 18, 205),
			3.6f);
		const ImU32 powerColor = powerRatio >= 0.72f
			? IM_COL32(255, 119, 58, 245)
			: powerRatio >= 0.38f
			? IM_COL32(255, 215, 74, 245)
			: IM_COL32(76, 218, 255, 245);
		DrawArc(
			gaugeDrawList,
			center,
			gaugeRadius,
			gaugeStart,
			gaugeStart + gaugeSweep * powerRatio,
			powerColor,
			2.4f);
	}
	std::vector<ResourceBadge> resources;
	const int heavyStacks = game.GetHeavyCollisionCount();
	if (heavyStacks > 0)
		resources.push_back({ CombatIconKind::ResourceHeavy, PresentationUtf8(u8"重量スタック"),
							 PresentationUtf8(u8"重量軸の能力発動に使う蓄積値です。"), heavyStacks,
							 IM_COL32(255, 146, 61, 255) });
	const int pierceStacks = game.GetPierceTraceState().stackCount;
	const int traceCount = static_cast<int>(game.GetPierceTraceState().traces.size());
	if (pierceStacks > 0 || traceCount > 0)
		resources.push_back({ CombatIconKind::ResourcePierce, PresentationUtf8(u8"貫通"),
							 PresentationUtf8(u8"左が貫通痕数、右が貫通スタック数です。"), pierceStacks,
							 IM_COL32(184, 92, 255, 255) });
	const int cushionStacks = CushionChargeRules::TotalStacks(game.GetCushionCharges());
	if (cushionStacks > 0)
		resources.push_back({ CombatIconKind::ResourceReflect, PresentationUtf8(u8"反射スタック"),
							 PresentationUtf8(u8"壁反射で獲得し、反射軸の能力発動に使う蓄積値です。"), cushionStacks,
							 IM_COL32(255, 221, 58, 255) });
	if (game.GetPlayerAnchorStacks() > 0)
		resources.push_back({ CombatIconKind::ResourceAnchor, PresentationUtf8(u8"プレイヤー錨スタック"),
							 PresentationUtf8(u8"錨軸の変換・防御能力に使う共有資源です。"),
							 game.GetPlayerAnchorStacks(), IM_COL32(75, 225, 183, 255) });
	if (resources.empty())
		return;

	constexpr float pierceSideWidth = 12.0f;
	constexpr float pierceGroupWidth = kCombatIconSize + pierceSideWidth * 2.0f;
	float rowWidth = (resources.size() - 1) * kCombatIconSpacing;
	for (const ResourceBadge& resource : resources)
	{
		rowWidth += resource.icon == CombatIconKind::ResourcePierce ? pierceGroupWidth : kResourceBadgeWidth;
	}
	const ImVec2 rowPosition(center.x - rowWidth * 0.5f, center.y + projectedRadius + 7.0f);
	if (rowPosition.x + rowWidth < viewport->Pos.x || rowPosition.x > viewport->Pos.x + viewport->Size.x ||
		rowPosition.y + kResourceBadgeHeight < viewport->Pos.y || rowPosition.y > viewport->Pos.y + viewport->Size.y)
		return;

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(rowPosition);
	ImGui::SetNextWindowSize(ImVec2(rowWidth, kResourceBadgeHeight));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kCombatIconSpacing, 0.0f));
	if (ImGui::Begin("##player_synergy_resources", nullptr, flags))
	{
		for (std::size_t index = 0; index < resources.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			const ResourceBadge& resource = resources[index];
			const ImVec2 iconMinimum = ImGui::GetCursorScreenPos();
			if (resource.icon == CombatIconKind::ResourcePierce)
			{
				const ImVec2 groupMaximum(iconMinimum.x + pierceGroupWidth, iconMinimum.y + kResourceBadgeHeight);
				const ImVec2 imageMinimum(iconMinimum.x + pierceSideWidth, iconMinimum.y);
				const ImVec2 imageMaximum(imageMinimum.x + kCombatIconSize, imageMinimum.y + kCombatIconSize);
				ImGui::Dummy(ImVec2(pierceGroupWidth, kResourceBadgeHeight));
				ImDrawList* resourceDrawList = ImGui::GetWindowDrawList();
				DrawCombatIcon(resourceDrawList, CombatIconKind::ResourcePierce,
					imageMinimum, imageMaximum);
				char traceText[12]{};
				char stackText[12]{};
				sprintf_s(traceText, "%d", traceCount);
				sprintf_s(stackText, "%d", pierceStacks);
				const float valueFontSize = ImGui::GetFontSize() * 0.67f;
				const ImVec2 traceSize = ImGui::GetFont()->CalcTextSizeA(valueFontSize, FLT_MAX, 0.0f, traceText);
				const ImVec2 stackSize = ImGui::GetFont()->CalcTextSizeA(valueFontSize, FLT_MAX, 0.0f, stackText);
				DrawOutlinedValue(resourceDrawList,
					ImVec2(iconMinimum.x + (pierceSideWidth - traceSize.x) * 0.5f,
						iconMinimum.y + (kCombatIconSize - traceSize.y) * 0.5f),
					traceText, IM_COL32(230, 204, 255, 255), 0.67f);
				DrawOutlinedValue(resourceDrawList,
					ImVec2(imageMaximum.x + (pierceSideWidth - stackSize.x) * 0.5f,
						iconMinimum.y + (kCombatIconSize - stackSize.y) * 0.5f),
					stackText, IM_COL32(230, 204, 255, 255), 0.67f);

				const ImVec2 mouse = ImGui::GetIO().MousePos;
				if (mouse.x >= iconMinimum.x && mouse.x < imageMinimum.x && mouse.y >= iconMinimum.y &&
					mouse.y < groupMaximum.y)
				{
					ImGui::BeginTooltip();
					ImGui::TextColored(ImVec4(0.78f, 0.46f, 1.0f, 1.0f), PresentationUtf8(u8"貫通痕: %d"), traceCount);
					ImGui::TextUnformatted(PresentationUtf8(u8"フィールド上に残っている貫通痕の数です。"));
					ImGui::EndTooltip();
				}
				else if (mouse.x >= imageMinimum.x && mouse.x < imageMaximum.x && mouse.y >= imageMinimum.y &&
					mouse.y < imageMaximum.y)
				{
					ImGui::BeginTooltip();
					ImGui::TextColored(ImVec4(0.78f, 0.46f, 1.0f, 1.0f), "%s", resource.title);
					ImGui::TextUnformatted(resource.description);
					ImGui::EndTooltip();
				}
				else if (mouse.x >= imageMaximum.x && mouse.x < groupMaximum.x && mouse.y >= iconMinimum.y &&
					mouse.y < groupMaximum.y)
				{
					ImGui::BeginTooltip();
					ImGui::TextColored(ImVec4(0.78f, 0.46f, 1.0f, 1.0f), PresentationUtf8(u8"貫通スタック: %d"),
						pierceStacks);
					ImGui::TextUnformatted(PresentationUtf8(u8"次の貫通能力に使う蓄積値です。"));
					ImGui::EndTooltip();
				}
				continue;
			}
			const ImVec2 iconMaximum(iconMinimum.x + kResourceBadgeWidth, iconMinimum.y + kResourceBadgeHeight);
			ImGui::Dummy(ImVec2(kResourceBadgeWidth, kResourceBadgeHeight));
			DrawResourceBadge(ImGui::GetWindowDrawList(), iconMinimum, resource);
			if (IsMouseInside(iconMinimum, iconMaximum))
			{
				ImGui::BeginTooltip();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(resource.color), "%s: %d", resource.title,
					resource.value);
				ImGui::TextUnformatted(resource.description);
				ImGui::EndTooltip();
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}
