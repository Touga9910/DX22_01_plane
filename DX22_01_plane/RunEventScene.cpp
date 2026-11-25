#pragma execution_character_set("utf-8")
#include "RunEventScene.h"

#include "GameSceneCommands.h"
#include "GameView.h"
#include "GoldUi.h"
#include "IllustratedUi.h"
#include "RunEventView.h"
#include "input.h"

#include <algorithm>
#include <cfloat>
#include <string>

namespace
{
    using IllustratedUi::Canvas;
    using IllustratedUi::Cream;
    using IllustratedUi::Gold;

    constexpr float ListX = 304.0f;
    constexpr float ListY = 270.0f;
    constexpr float ListWidth = 1064.0f;
    constexpr float ListHeight = 466.0f;
    constexpr float RowInset = 10.0f;
    constexpr float DetailSize = 22.0f;

    const RunEventChoice* ActiveChoice(const RunEventViewSnapshot& view, int index)
    {
        if (index < 0 || index >= static_cast<int>(view.choices.size())) return nullptr;
        const auto& choice = view.choices[index];
        return choice.enabled && !choice.targets.empty() ? &choice : nullptr;
    }

    bool ConfirmHeld()
    {
        return Input::GetKeyPress(VK_RETURN) || Input::GetKeyPress(VK_SPACE) ||
            Input::GetKeyPress(VK_LBUTTON) || Input::GetButtonPress(XINPUT_A);
    }

    bool BackTriggered()
    {
        return Input::GetKeyTrigger(VK_BACK) || Input::GetButtonTrigger(XINPUT_B);
    }

    float DetailHeight(const Canvas& ui, const std::string& detail, float width)
    {
        if (detail.empty()) return 0.0f;
        const auto bounds = ImGui::GetFont()->CalcTextSizeA(
            DetailSize * ui.scale, FLT_MAX, width * ui.scale, detail.c_str());
        return bounds.y / ui.scale;
    }

    bool DrawChoice(const Canvas& ui, int index, float& offset,
        const std::string& label, const std::string& detail,
        bool enabled, bool focused, bool scrollToFocus)
    {
        const float x = ListX + RowInset;
        const float y = ListY + RowInset + offset - ImGui::GetScrollY() / ui.scale;
        const float width = ListWidth - 3.0f * RowInset;
        const float detailHeight = DetailHeight(ui, detail, width - 48.0f);
        const float height = (std::max)(76.0f, 62.0f + detailHeight);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ui.At(x, y), ui.At(x + width, y + height),
            IM_COL32(54, 39, 33, 255), 10 * ui.scale);
        draw->AddRect(ui.At(x, y), ui.At(x + width, y + height),
            IM_COL32(158, 121, 71, 255), 10 * ui.scale, 0, 1.5f * ui.scale);

        ImGui::PushID(index);
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ui.Hit("choice", x, y, width, height);
        const bool hovered = ImGui::IsItemHovered();
        if (focused && scrollToFocus) ImGui::SetScrollHereY(0.5f);
        ImGui::EndDisabled();
        ImGui::PopID();

        ui.Highlight(x, y, width, height, focused, hovered, enabled);
        const ImU32 labelColor = enabled ? Cream : IM_COL32(185, 178, 160, 255);
        ui.Text(label.c_str(), x + 24, y + 12, width - 48, 34, 27, labelColor, false);
        if (!detail.empty())
        {
            ui.Text(detail.c_str(), x + 24, y + 51, width - 48, detailHeight + 2,
                DetailSize, enabled ? Gold : IM_COL32(175, 166, 150, 255), false);
        }
        offset += height + 12.0f;
        return clicked;
    }

    bool DrawButton(const Canvas& ui, const char* id, const char* label,
        float x, float width, bool focused, bool enabled = true)
    {
        constexpr float y = 770.0f;
        constexpr float height = 58.0f;
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ui.At(x, y), ui.At(x + width, y + height),
            IM_COL32(69, 48, 34, 255), 10 * ui.scale);
        draw->AddRect(ui.At(x, y), ui.At(x + width, y + height), Gold, 10 * ui.scale);
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ui.Hit(id, x, y, width, height);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::EndDisabled();
        ui.Highlight(x, y, width, height, focused, hovered, enabled);
        ui.Text(label, x + 12, y + 8, width - 24, height - 16, 26, Cream);
        return clicked;
    }

    void DrawTextPanel(const Canvas& ui, const char* id, const std::string& text,
        float y, float height)
    {
        ImGui::SetCursorScreenPos(ui.At(ListX + 10, y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.Size(18, 16));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(42, 32, 29, 245));
        if (ImGui::BeginChild(id, ui.Size(ListWidth - 20, height), ImGuiChildFlags_Borders))
        {
            ImGui::PushFont(nullptr, 25 * ui.scale);
            ImGui::PushStyleColor(ImGuiCol_Text, Cream);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
}

void RunEventScene::SelectChoice(int choiceIndex, int targetIndex, bool requiresConfirmation)
{
    m_SelectionRejected = false;
    if (requiresConfirmation)
    {
        m_ConfirmChoice = choiceIndex;
        m_ConfirmTarget = targetIndex;
        m_Confirmation = {};
        m_Confirmation.SetIndex(1, 2);
        m_ConfirmationInputReady = false;
        return;
    }
    m_ActiveChoice = -1;
    if (!GameSceneCommands::ChooseRunEvent(choiceIndex, targetIndex)) m_SelectionRejected = true;
}

void RunEventScene::Update()
{
    const auto view = GameView::CaptureRunEvent();
    m_ScrollToFocus = false;

    if (view.result.pending)
    {
        if (!m_ResultVisible)
        {
            m_ResultVisible = true;
            m_ResultInputReady = false;
            m_Result = {};
            m_ActiveChoice = -1;
            m_ConfirmChoice = -1;
            m_ShowDeck = false;
            return;
        }
        // The click or Enter that paid for an action cannot leave its result.
        if (!m_ResultInputReady)
        {
            m_ResultInputReady = !ConfirmHeld();
            return;
        }
        if (m_Result.UpdateVertical(view.result.repeatable ? 2 : 1))
        {
            GameSceneCommands::ContinueRunEvent(m_Result.GetIndex() == 1);
            m_Result = {};
            m_ResultInputReady = false;
        }
        return;
    }
    if (m_ResultVisible)
    {
        m_ResultVisible = false;
        m_ResultInputReady = false;
        m_Choices = {};
        m_Targets = {};
        return;
    }

    if (m_ShowDeck)
    {
        if (!m_DeckInputReady)
        {
            m_DeckInputReady = !ConfirmHeld();
            return;
        }
        const int count = static_cast<int>(view.deck.size());
        const bool confirmed = m_Deck.UpdateVertical(count + 1);
        m_ScrollToFocus = m_Deck.WasMoved();
        if (BackTriggered() || (confirmed && m_Deck.GetIndex() == count)) m_ShowDeck = false;
        return;
    }

    if (m_ConfirmChoice >= 0)
    {
        if (m_ConfirmChoice >= static_cast<int>(view.choices.size()))
        {
            m_ConfirmChoice = -1;
            return;
        }
        if (!m_ConfirmationInputReady)
        {
            m_ConfirmationInputReady = !ConfirmHeld();
            return;
        }
        if (BackTriggered())
        {
            m_ConfirmChoice = -1;
            return;
        }
        if (!m_Confirmation.UpdateVertical(2)) return;
        const int choiceIndex = m_ConfirmChoice;
        m_ConfirmChoice = -1;
        if (m_Confirmation.GetIndex() == 0)
        {
            m_ActiveChoice = -1;
            m_SelectionRejected = !GameSceneCommands::ChooseRunEvent(choiceIndex, m_ConfirmTarget);
        }
        return;
    }

    if (const auto* choice = ActiveChoice(view, m_ActiveChoice))
    {
        const int targetCount = static_cast<int>(choice->targets.size());
        const bool confirmed = m_Targets.UpdateVertical(targetCount + 2);
        m_ScrollToFocus = m_Targets.WasMoved();
        if (BackTriggered())
        {
            m_ActiveChoice = -1;
            return;
        }
        if (!confirmed) return;
        const int target = m_Targets.GetIndex();
        if (target == targetCount)
        {
            m_ActiveChoice = -1;
        }
        else if (target == targetCount + 1)
        {
            m_ShowDeck = true;
            m_Deck = {};
            m_DeckInputReady = false;
            m_ScrollToFocus = true;
        }
        else
        {
            SelectChoice(m_ActiveChoice, choice->targets[target].index, choice->requiresConfirmation);
        }
        return;
    }

    m_ActiveChoice = -1;
    const int count = static_cast<int>(view.choices.size());
    if (count == 0) return;
    m_Choices.SetIndex(m_Choices.GetIndex(), count + 1);
    const bool confirmed = m_Choices.UpdateVertical(count + 1);
    m_ScrollToFocus = m_Choices.WasMoved();
    if (!confirmed) return;
    const int index = m_Choices.GetIndex();
    if (index == count)
    {
        m_ShowDeck = true;
        m_Deck = {};
        m_DeckInputReady = false;
        m_ScrollToFocus = true;
        return;
    }
    const auto& choice = view.choices[index];
    m_SelectionRejected = false;
    if (!choice.enabled) return;
    if (!choice.targets.empty())
    {
        m_ActiveChoice = index;
        m_Targets = {};
        m_ScrollToFocus = true;
        return;
    }
    SelectChoice(index, -1, choice.requiresConfirmation);
}

void RunEventScene::DrawUI()
{
    const auto view = GameView::CaptureRunEvent();
    const Canvas ui("run_event", 1672, 941, IM_COL32(20, 24, 22, 255));
    if (ui.scale <= 0.0f) return;

    ui.Art("assets/UI/Lounge/services_background.png", 0, 0, 1672, 941, IM_COL32_WHITE, false);
    ui.Art("assets/UI/Lounge/node_event.png", 310, 127, 74, 74);
    ui.Text(view.title.c_str(), 407, 141, 620, 66, 48, Cream, false);
    const auto hp = "HP " + std::to_string(view.hp) + " / " + std::to_string(view.maxHp);
    ui.Text(hp.c_str(), 1060, 151, 160, 32, 26, Cream, false);
    GoldUi::Draw(ImGui::GetWindowDrawList(), ui.At(1235, 147), view.money,
        37 * ui.scale, 26 * ui.scale);

    if (view.result.pending)
    {
        ui.Text(view.result.title.c_str(), 320, 222, 1024, 48, 34, Gold, false);
        const auto changes = "HP  " + std::to_string(view.result.hpBefore) + " → " +
            std::to_string(view.result.hpAfter) + "    /    Money  " +
            std::to_string(view.result.moneyBefore) + " → " + std::to_string(view.result.moneyAfter);
        ui.Text(changes.c_str(), 336, 294, 990, 50, 30, Cream, false);
        DrawTextPanel(ui, "event_result_detail", view.result.detail, 364, 372);
        const int resultCount = view.result.repeatable ? 2 : 1;
        if (DrawButton(ui, "event_result_continue", "次へ進む", 1064, 280,
            m_Result.GetIndex() == 0, m_ResultVisible && m_ResultInputReady))
            m_Result.Confirm(0, resultCount);
        if (view.result.repeatable && DrawButton(ui, "event_result_polish_again", "もう一度磨く",
            320, 300, m_Result.GetIndex() == 1, m_ResultVisible && m_ResultInputReady))
            m_Result.Confirm(1, resultCount);
        ui.Text(view.result.repeatable ?
            "W/S・上下キー：選択    ENTER・SPACE・クリック：決定" :
            "結果を確認してから、ボタンまたは ENTER・SPACE で進めます。",
            320, 850, 1024, 32, 21, Cream, false);
        return;
    }

    if (m_ConfirmChoice >= 0 && m_ConfirmChoice < static_cast<int>(view.choices.size()))
    {
        const auto& choice = view.choices[m_ConfirmChoice];
        ui.Text("この内容で実行しますか？", 320, 222, 1024, 44, 32, Gold, false);
        std::string detail = choice.label + "\n" + choice.detail;
        for (const auto& target : choice.targets)
        {
            if (target.index == m_ConfirmTarget)
            {
                detail += "\n\n対象：" + target.label + "\n" + target.detail;
                break;
            }
        }
        DrawTextPanel(ui, "event_confirmation_detail", detail, 282, 454);
        if (DrawButton(ui, "event_confirm_execute", "実行する", 1004, 340,
            m_Confirmation.GetIndex() == 0, m_ConfirmationInputReady)) m_Confirmation.Confirm(0, 2);
        if (DrawButton(ui, "event_confirm_back", "戻る", 320, 220,
            m_Confirmation.GetIndex() == 1, m_ConfirmationInputReady)) m_Confirmation.Confirm(1, 2);
        ui.Text("W/S・上下キー：選択    ENTER・SPACE：決定    BACKSPACE：戻る",
            320, 850, 1024, 32, 21, Cream, false);
        return;
    }

    if (m_ShowDeck)
    {
        const auto heading = "現在のデッキ（" + std::to_string(view.deck.size()) + "球）";
        ui.Text(heading.c_str(), 320, 222, 1024, 40, 30, Gold, false);
        ImGui::SetCursorScreenPos(ui.At(ListX, ListY));
        if (ImGui::BeginChild("event_deck", ui.Size(ListWidth, ListHeight)))
        {
            float offset = 0.0f;
            for (int index = 0; index < static_cast<int>(view.deck.size()); ++index)
            {
                const auto& ball = view.deck[index];
                if (DrawChoice(ui, index, offset, ball.label, ball.detail, true,
                    index == m_Deck.GetIndex(), m_ScrollToFocus))
                    m_Deck.SetIndex(index, static_cast<int>(view.deck.size()) + 1);
            }
        }
        ImGui::EndChild();
        if (DrawButton(ui, "event_deck_back", "戻る", 320, 220,
            m_Deck.GetIndex() == static_cast<int>(view.deck.size()), m_DeckInputReady))
            m_Deck.Confirm(static_cast<int>(view.deck.size()), static_cast<int>(view.deck.size()) + 1);
        ui.Text("W/S・上下キー：球を確認    BACKSPACE：戻る    マウスホイール：スクロール",
            320, 850, 1024, 32, 21, Cream, false);
        return;
    }

    const auto* active = ActiveChoice(view, m_ActiveChoice);
    ui.Text(active != nullptr ? "対象のボールを選択" : "行動を選択",
        320, 222, 1024, 38, 28, Gold, false);
    ImGui::SetCursorScreenPos(ui.At(ListX, ListY));
    if (ImGui::BeginChild(active == nullptr ? "event_choices" : "event_targets",
        ui.Size(ListWidth, ListHeight)))
    {
        float offset = 0.0f;
        if (active == nullptr)
        {
            const int count = static_cast<int>(view.choices.size());
            for (int index = 0; index < count; ++index)
            {
                const auto& choice = view.choices[index];
                if (DrawChoice(ui, index, offset, choice.label, choice.detail,
                    choice.enabled, index == m_Choices.GetIndex(), m_ScrollToFocus))
                    m_Choices.Confirm(index, count + 1);
            }
        }
        else
        {
            const int count = static_cast<int>(active->targets.size());
            for (int index = 0; index < count; ++index)
            {
                const auto& target = active->targets[index];
                if (DrawChoice(ui, index, offset, target.label, target.detail,
                    true, index == m_Targets.GetIndex(), m_ScrollToFocus))
                    m_Targets.Confirm(index, count + 2);
            }
        }
    }
    ImGui::EndChild();
    m_ScrollToFocus = false;

    if (active != nullptr)
    {
        const int count = static_cast<int>(active->targets.size());
        if (DrawButton(ui, "event_target_back", "戻る", 320, 220,
            count == m_Targets.GetIndex())) m_Targets.Confirm(count, count + 2);
    }
    const auto deckLabel = "デッキを見る（" + std::to_string(view.deck.size()) + "球）";
    const int deckIndex = active != nullptr ? static_cast<int>(active->targets.size()) + 1
        : static_cast<int>(view.choices.size());
    const bool deckFocused = active != nullptr ? m_Targets.GetIndex() == deckIndex
        : m_Choices.GetIndex() == deckIndex;
    if (DrawButton(ui, "event_deck_open", deckLabel.c_str(), 1004, 340, deckFocused))
    {
        if (active != nullptr) m_Targets.Confirm(deckIndex, deckIndex + 1);
        else m_Choices.Confirm(deckIndex, deckIndex + 1);
    }
    ui.Text(m_SelectionRejected ? "その選択肢は現在利用できません。" :
        "W/S・上下キー：選択    ENTER・SPACE・クリック：決定    マウスホイール：スクロール",
        320, 850, 1024, 32, 21, m_SelectionRejected ? Gold : Cream, false);
}
