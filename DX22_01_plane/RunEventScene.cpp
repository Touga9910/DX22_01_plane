#pragma execution_character_set("utf-8")
#include "RunEventScene.h"

#include "GameSceneCommands.h"
#include "GameView.h"
#include "GoldUi.h"
#include "IllustratedUi.h"
#include "RunEventView.h"
#include "UiText.h"

#include <string>

namespace
{
    using IllustratedUi::Canvas;
    using IllustratedUi::Cream;
    using IllustratedUi::Gold;

    constexpr float ListX = 304.0f;
    constexpr float ListY = 260.0f;
    constexpr float ListWidth = 1064.0f;
    constexpr float ListHeight = 494.0f;
    constexpr float RowInset = 8.0f;
    constexpr float RowHeight = 72.0f;
    constexpr float RowSpacing = 82.0f;

    const RunEventChoice* ActiveChoice(const RunEventViewSnapshot& view, int index)
    {
        if (index < 0 || index >= static_cast<int>(view.choices.size())) return nullptr;
        const auto& choice = view.choices[index];
        return choice.enabled && !choice.targets.empty() ? &choice : nullptr;
    }

    bool DrawChoice(const Canvas& ui, int index, const char* label, const char* detail,
        bool enabled, bool focused, bool scrollToFocus)
    {
        const float x = ListX + RowInset;
        const float y = ListY + RowInset + RowSpacing * index - ImGui::GetScrollY() / ui.scale;
        const float width = ListWidth - 3.0f * RowInset;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ui.At(x, y), ui.At(x + width, y + RowHeight),
            IM_COL32(54, 39, 33, 255), 10 * ui.scale);
        draw->AddRect(ui.At(x, y), ui.At(x + width, y + RowHeight),
            IM_COL32(158, 121, 71, 255), 10 * ui.scale, 0, 1.5f * ui.scale);

        ImGui::PushID(index);
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ui.Hit("choice", x, y, width, RowHeight);
        const bool hovered = ImGui::IsItemHovered();
        if (focused && scrollToFocus) ImGui::SetScrollHereY(0.5f);
        ImGui::EndDisabled();
        ImGui::PopID();

        ui.Highlight(x, y, width, RowHeight, focused, hovered, enabled);
        const ImU32 labelColor = enabled ? Cream : IM_COL32(185, 178, 160, 255);
        if (detail[0] == '\0')
        {
            ui.Text(label, x + 24, y + 24, width - 48, 34, 28, labelColor, false);
        }
        else
        {
            ui.Text(label, x + 24, y + 9, width - 48, 31, 27, labelColor, false);
            ui.Text(detail, x + 24, y + 40, width - 48, 26, 21,
                enabled ? Gold : IM_COL32(175, 166, 150, 255), false);
        }
        return clicked;
    }
}

void RunEventScene::Update()
{
    const auto view = GameView::CaptureRunEvent();
    m_ScrollToFocus = false;

    if (const auto* choice = ActiveChoice(view, m_ActiveChoice))
    {
        const int targetCount = static_cast<int>(choice->targets.size());
        const bool confirmed = m_Targets.UpdateVertical(targetCount + 1);
        m_ScrollToFocus = m_Targets.WasMoved();
        if (!confirmed) return;

        const int choiceIndex = m_ActiveChoice;
        const int target = m_Targets.GetIndex();
        m_ActiveChoice = -1;
        m_ScrollToFocus = true;
        m_SelectionRejected = false;
        if (target < targetCount &&
            !GameSceneCommands::ChooseRunEvent(choiceIndex, choice->targets[target].index))
        {
            m_SelectionRejected = true;
        }
        return;
    }

    m_ActiveChoice = -1;
    const int count = static_cast<int>(view.choices.size());
    if (count == 0) return;
    m_Choices.SetIndex(m_Choices.GetIndex(), count);
    const bool confirmed = m_Choices.UpdateVertical(count);
    m_ScrollToFocus = m_Choices.WasMoved();
    if (!confirmed) return;

    const int index = m_Choices.GetIndex();
    const auto& choice = view.choices[index];
    m_SelectionRejected = false;
    if (!choice.enabled) return;
    if (!choice.targets.empty())
    {
        m_ActiveChoice = index;
        m_Targets.SetIndex(0, static_cast<int>(choice.targets.size()) + 1);
        m_ScrollToFocus = true;
        return;
    }
    if (!GameSceneCommands::ChooseRunEvent(index, -1)) m_SelectionRejected = true;
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
    ui.Text(hp.c_str(), 1060, 151, 145, 32, 26, Cream, false);
    GoldUi::Draw(ImGui::GetWindowDrawList(), ui.At(1235, 147), view.money,
        37 * ui.scale, 26 * ui.scale);

    const auto* active = ActiveChoice(view, m_ActiveChoice);
    if (active != nullptr)
    {
        const auto prompt = active->label + "　対象のボールを選択";
        ui.Text(prompt.c_str(), 320, 214, 495, 38, 23, Cream, false);
        ui.Text(active->detail.c_str(), 835, 214, 509, 38, 21, Gold, false);
    }

    ImGui::SetCursorScreenPos(ui.At(ListX, ListY));
    if (ImGui::BeginChild(active == nullptr ? "event_choices" : "event_targets",
        ui.Size(ListWidth, ListHeight)))
    {
        if (active == nullptr)
        {
            const int count = static_cast<int>(view.choices.size());
            for (int index = 0; index < count; ++index)
            {
                const auto& choice = view.choices[index];
                if (DrawChoice(ui, index, choice.label.c_str(), choice.detail.c_str(),
                    choice.enabled, index == m_Choices.GetIndex(), m_ScrollToFocus))
                {
                    m_Choices.Confirm(index, count);
                }
            }
        }
        else
        {
            const int count = static_cast<int>(active->targets.size());
            for (int index = 0; index < count; ++index)
            {
                const auto& target = active->targets[index];
                if (DrawChoice(ui, index, target.label.c_str(), target.detail.c_str(),
                    true, index == m_Targets.GetIndex(), m_ScrollToFocus))
                {
                    m_Targets.Confirm(index, count + 1);
                }
            }
        }
    }
    ImGui::EndChild();
    m_ScrollToFocus = false;

    if (active != nullptr)
    {
        const int count = static_cast<int>(active->targets.size());
        const bool clicked = ui.Hit("event_target_back", 312, 777, 144, 46);
        ui.Highlight(312, 777, 144, 46, count == m_Targets.GetIndex(), ImGui::IsItemHovered());
        ui.Text("戻る", 312, 777, 144, 46, 23);
        if (clicked) m_Targets.Confirm(count, count + 1);
    }

    ui.Text(m_SelectionRejected ? "その選択肢は現在利用できません。" : UiText::RestControls,
        ListX, 780, ListWidth, 40, 20, m_SelectionRejected ? Gold : Cream);
}
