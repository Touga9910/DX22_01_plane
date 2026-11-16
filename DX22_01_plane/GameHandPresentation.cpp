#include "GamePresentation.h"
#include "BallCardPresentation.h"
#include "PlayerBallData.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>

using namespace BallCardPresentation;

BallSelectionPresentationIntent GamePresentation::DrawBallSelection(
	const BallSelectionPresentationModel& model)
{
	BallSelectionPresentationIntent intent{
		model.selectedOfferIndex,
		model.selectedHoldIndex,
		false,
	};
	const int offerCount = static_cast<int>(model.offers.size());
	if (offerCount <= 0)
	{
		return intent;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float hiddenRatio = std::clamp(model.handHiddenRatio, 0.0f, 1.0f);
	if (hiddenRatio >= 1.0f)
	{
		return intent;
	}
	const float easedHiddenRatio = hiddenRatio * hiddenRatio *
		(3.0f - 2.0f * hiddenRatio);
	// 左下・右下の山札表示ぶんを空け、手札は中央にまとめる。
	const float horizontalPadding = std::clamp(
		viewport->WorkSize.x * 0.105f,
		70.0f,
		132.0f);
	const float gap = 14.0f;
	constexpr float cardAspectRatio = 3.0f / 4.0f;
	const float fixedCardHeightLimit = std::clamp(
		viewport->WorkSize.y * 0.31f,
		228.0f,
		280.0f);
	const float cardBottom =
		viewport->WorkPos.y + viewport->WorkSize.y - 12.0f +
		easedHiddenRatio * (fixedCardHeightLimit + 52.0f);
	const float availableCardWidth =
		(viewport->WorkSize.x - horizontalPadding * 2.0f -
			gap * static_cast<float>((std::max)(0, offerCount - 1))) /
		static_cast<float>(offerCount);
	const float fixedCardWidth = (std::min)(
		(std::min)(210.0f, availableCardWidth),
		fixedCardHeightLimit * cardAspectRatio);
	const ImVec2 fixedCardSize(
		fixedCardWidth,
		fixedCardWidth / cardAspectRatio);
	const float cardsWidth = fixedCardSize.x * static_cast<float>(offerCount) +
		gap * static_cast<float>((std::max)(0, offerCount - 1));
	const float cardsStartX = viewport->WorkPos.x +
		(viewport->WorkSize.x - cardsWidth) * 0.5f;

	constexpr float hoverScale = 1.07f;
	constexpr float hoverLift = 8.0f;
	// The hovered card grows upward. Reserve that overflow inside the ImGui
	// window so the illustration and frame are not clipped at its top edge.
	const float hoverTopAllowance =
		fixedCardSize.y * (hoverScale - 1.0f) + hoverLift + 4.0f;
	const float panelHeight = fixedCardSize.y + 24.0f + hoverTopAllowance;
	const ImVec2 panelPosition(
		viewport->WorkPos.x,
		cardBottom - fixedCardSize.y - 12.0f - hoverTopAllowance);

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(panelPosition, ImGuiCond_Always);
	ImGui::SetNextWindowSize(
		ImVec2(viewport->WorkSize.x, panelHeight),
		ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::Begin("##ball_card_hud", nullptr, flags);
	ImGui::BeginDisabled(!model.selectionInteractive);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilledMultiColor(
		ImVec2(panelPosition.x, cardBottom - fixedCardSize.y - 16.0f),
		ImVec2(panelPosition.x + viewport->WorkSize.x, cardBottom + 12.0f),
		IM_COL32(5, 9, 15, 0),
		IM_COL32(5, 9, 15, 0),
		IM_COL32(5, 9, 15, 218),
		IM_COL32(5, 9, 15, 218));
	int hoveredIndex = -1;
	if (model.selectionInteractive)
	{
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		for (int index = 0; index < offerCount; ++index)
		{
			const float slotLeft = cardsStartX +
				static_cast<float>(index) * (fixedCardSize.x + gap);
			if (mouse.x >= slotLeft && mouse.x <= slotLeft + fixedCardSize.x &&
				mouse.y >= cardBottom - fixedCardSize.y && mouse.y <= cardBottom)
			{
				hoveredIndex = index;
				break;
			}
		}
	}

	const auto drawCard = [&](int index)
	{
		const PlayerBallData* ball = model.offers[static_cast<std::size_t>(index)];
		if (ball == nullptr)
		{
			return;
		}

		const bool selected = intent.selectedOfferIndex == index;
		const bool held = intent.selectedHoldIndex == index;
		const float slotLeft = cardsStartX +
			static_cast<float>(index) * (fixedCardSize.x + gap);
		const bool hovered = hoveredIndex == index;
		const float cardScale = hovered ? hoverScale : 1.0f;
		const ImVec2 cardSize(
			fixedCardSize.x * cardScale,
			fixedCardSize.y * cardScale);
		const ImVec2 cardPosition(
			slotLeft - (cardSize.x - fixedCardSize.x) * 0.5f,
			cardBottom - cardSize.y - (hovered ? hoverLift : 0.0f));
		const BallCardInteraction interaction = DrawBallSelectionCard(
			*ball,
			index,
			model.effectiveAttacks[static_cast<std::size_t>(index)],
			cardPosition,
			cardSize,
			selected,
			held);

		if (interaction.openDetails)
		{
			m_BallDetailOfferIndex = index;
			ImGui::OpenPopup("ボール詳細");
		}

		if (interaction.select)
		{
			if (!selected)
			{
				intent.selectedOfferIndex = index;
				if (held)
				{
					intent.selectedHoldIndex = -1;
				}
				intent.selectionChanged = true;
			}
			intent.confirmSelection = true;
		}

	};

	for (int index = 0; index < offerCount; ++index)
	{
		if (index != hoveredIndex)
		{
			drawCard(index);
		}
	}
	if (hoveredIndex >= 0)
	{
		drawCard(hoveredIndex);
	}

	const ImGuiViewport* detailViewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize(ImVec2(
		(std::min)(780.0f, detailViewport->WorkSize.x - 32.0f),
			(std::min)(452.0f, detailViewport->WorkSize.y - 32.0f)),
		ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("ボール詳細", nullptr, ImGuiWindowFlags_NoResize))
	{
		const PlayerBallData* detailBall =
			m_BallDetailOfferIndex >= 0 && m_BallDetailOfferIndex < offerCount
			? model.offers[static_cast<std::size_t>(m_BallDetailOfferIndex)]
			: nullptr;
		if (detailBall != nullptr)
		{
			DrawBallDetailPopup(
				*detailBall,
				model.effectiveAttacks[static_cast<std::size_t>(m_BallDetailOfferIndex)]);
		}
		if (ImGui::Button("閉じる", ImVec2(-1.0f, 38.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::EndDisabled();
	ImGui::End();
	ImGui::PopStyleVar(2);
	return intent;
}
