#include "GamePresentation.h"
#include "GamePresentationPrimitives.h"
#include "Game.h"
#include "BattleController.h"
#include "BattleViewportLayout.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "PlayerBall.h"
#include "PlayerBallData.h"
#include "PlayerBallText.h"
#include "GameUi.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>

using namespace GamePresentationPrimitives;

namespace
{
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
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoBackground;
	const float hudHeight = BattleViewportLayout::Calculate(
		workSize.x, workSize.y).hudHeight;
	const ImVec2 hudPosition(workPosition.x, workPosition.y);
	const ImU32 panelBackground = IM_COL32(8, 16, 25, 210);
	const ImU32 panelBorder = IM_COL32(83, 110, 138, 220);

	const auto players = GameRuntime::World().GetComponents<PlayerBall>();
	const int currentHp = players.empty() || players[0] == nullptr
		? game.GetPlayerCurrentHp()
		: players[0]->GetHP();
	const int maxHp = (std::max)(1, game.GetPlayerMaxHp());
	const float hpRatio = std::clamp(
		static_cast<float>(currentHp) / static_cast<float>(maxHp),
		0.0f,
		1.0f);

	const bool compact = workSize.x < 1280.0f;
	const int areaGoal = (std::max)(1, game.GetNormalRouteAreaGoal());
	const int area = std::clamp(game.GetAreaProgress(), 0, areaGoal);
	const BattleController* controller = game.GetBattleControllerDiagnostics();
	char progressText[192]{};
	if (controller != nullptr && controller->IsPhasePrototype())
	{
		sprintf_s(progressText,
			compact
			? PresentationUtf8(u8"進行 %d/%d  F%d/%d  次%dT")
			: PresentationUtf8(u8"進行 %d / %d    PHASE %d / %d    次まで %dターン"),
			area, areaGoal, controller->GetCurrentPhase(),
			controller->GetTotalPhases(), controller->GetTurnsUntilNextPhase());
	}
	else
	{
		sprintf_s(progressText,
			compact ? "PROGRESS %d/%d  LEGACY" : "PROGRESS %d / %d    LEGACY BATTLE",
			area, areaGoal);
	}

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(hudPosition);
	ImGui::SetNextWindowSize(ImVec2(workSize.x, hudHeight));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 7.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(compact ? 4.0f : 7.0f, 4.0f));
	if (ImGui::Begin("##battle_top_bar", nullptr, fixedFlags))
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(hudPosition,
			ImVec2(hudPosition.x + workSize.x, hudPosition.y + hudHeight),
			panelBackground);
		draw->AddLine(
			ImVec2(hudPosition.x, hudPosition.y + hudHeight - 1.0f),
			ImVec2(hudPosition.x + workSize.x, hudPosition.y + hudHeight - 1.0f),
			panelBorder, 1.0f);

		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(ImVec4(1.0f, 0.46f, 0.42f, 1.0f),
			compact ? "HP %d/%d" : "HP  %d / %d", currentHp, maxHp);
		ImGui::SameLine();
		const float hpBarHeight = 9.0f;
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
			(std::max)(0.0f,
				(ImGui::GetTextLineHeight() - hpBarHeight) * 0.5f));
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
			ImVec4(0.92f, 0.25f, 0.22f, 1.0f));
		ImGui::ProgressBar(hpRatio,
			ImVec2(compact ? 68.0f : 104.0f, hpBarHeight), "");
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.45f, 0.82f, 1.0f, 1.0f),
			compact ? "S %d" : PresentationUtf8(u8"シールド %d"),
			game.GetPlayerShield());

		const ImVec2 progressSize = ImGui::CalcTextSize(progressText);
		ImGui::SameLine((std::max)(
			ImGui::GetCursorPosX() + 8.0f,
			(workSize.x - progressSize.x) * 0.5f));
		ImGui::TextUnformatted(progressText);

		const float relicWidth = compact ? 58.0f : 92.0f;
		const float deckWidth = compact ? 54.0f : 76.0f;
		const float settingsWidth = compact ? 50.0f : 64.0f;
		const char* debugLabel = compact
			? "DBG##battle_debug"
			: PresentationUtf8(u8"デバッグ##battle_debug");
		const char* debugVisibleLabel = compact
			? "DBG"
			: PresentationUtf8(u8"デバッグ");
		const float debugWidth = ImGui::GetFrameHeight() +
			ImGui::GetStyle().ItemInnerSpacing.x +
			ImGui::CalcTextSize(debugVisibleLabel).x;
		const char* moneyText = compact ? "$%d" : PresentationUtf8(u8"所持金 %d");
		char moneyLabel[64]{};
		sprintf_s(moneyLabel, moneyText, game.GetPlayerMoney());
		const float rightWidth = ImGui::CalcTextSize(moneyLabel).x +
			relicWidth + deckWidth + settingsWidth + debugWidth +
			ImGui::GetStyle().ItemSpacing.x * 4.0f;
		ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 8.0f,
			workSize.x - rightWidth - 10.0f));
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.28f, 1.0f), "%s", moneyLabel);
		ImGui::SameLine();
		char relicButton[64]{};
		sprintf_s(relicButton,
			compact ? "R %d##relic_list" : PresentationUtf8(u8"レリック %d##relic_list"),
			game.GetOwnedRelicCount());
		if (ImGui::Button(relicButton, ImVec2(relicWidth, 28.0f)))
		{
			m_RelicListOpen = !m_RelicListOpen;
		}
		ImGui::SameLine();
		if (ImGui::Button(compact ? "DECK##deck_list" : PresentationUtf8(u8"デッキ##deck_list"),
			ImVec2(deckWidth, 28.0f)))
		{
			m_DeckListView = DeckListView::All;
			m_DeckListOpen = !m_DeckListOpen;
		}
		ImGui::SameLine();
		if (ImGui::Button(PresentationUtf8(u8"設定##settings"),
			ImVec2(settingsWidth, 28.0f)))
		{
			game.RequestPauseToggle();
		}
		ImGui::SameLine();
		ImGui::Checkbox(debugLabel, &GameUi::showDebugger);
	}
	ImGui::End();
	ImGui::PopStyleVar(2);

	const float pileWidth = std::clamp(workSize.x * 0.085f, 124.0f, 140.0f);
	const float pileHeight = std::clamp(workSize.y * 0.092f, 78.0f, 88.0f);
	const ImVec2 pileSize(pileWidth, pileHeight);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		workPosition.x + 16.0f,
		workPosition.y + workSize.y - pileSize.y - 16.0f));
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
		workPosition.y + workSize.y - pileSize.y - 16.0f));
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

	if (game.IsShotPreparationInputEnabled())
	{
		const PlayerBallData* selected = game.GetBallOffer(
			game.GetSelectedBallOfferIndex());
		if (selected != nullptr)
		{
			const float readyWidth = std::clamp(workSize.x * 0.30f, 280.0f, 460.0f);
			const ImVec2 readyPosition(
				workPosition.x + (workSize.x - readyWidth) * 0.5f,
				workPosition.y + workSize.y - 62.0f);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::SetNextWindowPos(readyPosition);
			ImGui::SetNextWindowSize(ImVec2(readyWidth, 46.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
			if (ImGui::Begin("##selected_ball_compact", nullptr,
				fixedFlags | ImGuiWindowFlags_NoInputs))
			{
				ImDrawList* draw = ImGui::GetWindowDrawList();
				draw->AddRectFilled(readyPosition,
					ImVec2(readyPosition.x + readyWidth, readyPosition.y + 46.0f),
					IM_COL32(7, 17, 27, 220), 8.0f);
				ImGui::Text("%s  Lv.%d", PlayerBallText::GetName(selected->definitionId),
					selected->upgradeLevel);
				ImGui::SameLine();
				ImGui::TextDisabled(PresentationUtf8(u8"右クリック: カード選択に戻る"));
			}
			ImGui::End();
			ImGui::PopStyleVar();
		}
	}

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
	const float overlayWidth = std::clamp(
		viewport->WorkSize.x - 32.0f, 280.0f, 372.0f);
	const float overlayHeight = std::clamp(
		viewport->WorkSize.y - 128.0f, 220.0f, 440.0f);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		viewport->WorkPos.x + viewport->WorkSize.x - overlayWidth - 16.0f,
		viewport->WorkPos.y + 96.0f));
	ImGui::SetNextWindowSize(ImVec2(overlayWidth, overlayHeight));
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

		int count = 0;
		auto ballAt = [&](int index) -> const PlayerBallData*
			{
				switch (m_DeckListView)
				{
				case DeckListView::DrawPile:
					return game.GetDrawPileBall(index);
				case DeckListView::DiscardPile:
					return game.GetDiscardPileBall(index);
				default:
					return game.GetDeckBall(index);
				}
			};
		switch (m_DeckListView)
		{
		case DeckListView::DrawPile: count = game.GetPlayerDeckCount(); break;
		case DeckListView::DiscardPile: count = game.GetPlayerDiscardCount(); break;
		default: count = game.GetDeckBallCount(); break;
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
	const float overlayWidth = std::clamp(
		viewport->WorkSize.x - 32.0f, 280.0f, 362.0f);
	const float overlayHeight = std::clamp(
		viewport->WorkSize.y - 164.0f, 200.0f, 360.0f);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		viewport->WorkPos.x + 16.0f,
		viewport->WorkPos.y + 138.0f));
	ImGui::SetNextWindowSize(ImVec2(overlayWidth, overlayHeight));
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
