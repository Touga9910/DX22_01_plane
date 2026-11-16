#include "GamePresentation.h"
#include "BallCardPresentation.h"
#include "GameUi.h"
#include "GameTypes.h"
#include "IllustratedUi.h"
#include "PlayerBallData.h"
#include "PlayerBallText.h"
#include "PlayerBallUI.h"
#include "UiText.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <string>

ClearRewardPresentationIntent GamePresentation::DrawClearReward(
    const ClearRewardPresentationModel& model)
{
    using namespace IllustratedUi;
    ClearRewardPresentationIntent intent{
        model.selectedRelicOfferIndex, model.selectedRewardIndex,
        model.selectedRewardBallIndex, false,
    };
    // The battle scene is already rendered underneath. Keep it visible through
    // the artwork's alpha, with only a light scrim behind the reward choices.
    Canvas ui("clear_reward_art", 1448, 1086, IM_COL32(0, 0, 0, 72));
    // Preserve the full exported canvas so card text and hit areas stay aligned.
    ui.Art("assets/UI/Run/reward_background.png", 0, 0, 1448, 1086, IM_COL32_WHITE, false);
    const bool relics = model.midBossRelicSelectionActive;
    const bool chosen = model.clearRewardChosen;
    std::string status = chosen ? model.rewardMessage : relics
        ? "中ボスに勝利しました！ レリックを1つ選んでください。"
        : model.rewardBallAcquired ? "ボール獲得済み。強化か追加ゴールドを1つ選んでください。"
        : "ボールは任意で1つ獲得。その後、強化か追加ゴールドを1つ選べます。";
    ui.Text(status.c_str(), 230, 164, 988, 35, 23);
    const std::string money = "戦闘報酬 +" + std::to_string(model.stageRewardMoney) +
        " Money  /  所持金 " + std::to_string(model.playerMoney);
    ui.Text(money.c_str(), 250, 200, 948, 26, 19, Gold);

    for (int index = 0; index < 3; ++index)
    {
        const float x = 177 + index * 380.0f;
        const bool selected = relics ? index == intent.selectedRelicOfferIndex :
            intent.selectedRewardIndex == 0 && index == intent.selectedRewardBallIndex;
        const auto* ball = index < static_cast<int>(model.newBallOffers.size()) ? model.newBallOffers[index] : nullptr;
        const auto* relic = index < static_cast<int>(model.relicOffers.size()) ? model.relicOffers[index] : nullptr;
        const bool exists = relics ? relic != nullptr : ball != nullptr;
        ImGui::PushID(index);
        if (ui.Hit("reward_card", x, 235, 330, 287) && !chosen && exists && (relics || !model.rewardBallAcquired))
        {
            if (relics) intent.selectedRelicOfferIndex = index;
            else { intent.selectedRewardIndex = 0; intent.selectedRewardBallIndex = index; }
        }
        const bool hovered = ImGui::IsItemHovered();
        ui.Highlight(x, 235, 330, 287, selected, hovered && !chosen, exists && (relics || !model.rewardBallAcquired));
        if (exists && hovered)
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24);
            ImGui::TextUnformatted(relics ? relic->description : PlayerBallText::GetDescription(ball->definitionId));
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        if (relics && relic)
        {
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddCircleFilled(ui.At(x + 165, 298), 45 * ui.scale, IM_COL32(79, 39, 107, 255));
            draw->AddCircle(ui.At(x + 165, 298), 45 * ui.scale, Gold, 0, 3 * ui.scale);
            ui.Text("R", x + 120, 251, 90, 94, 48, Gold);
            ui.Text(relic->name, x + 18, 354, 294, 48, 26, Ink);
            ui.Text(relic->description, x + 26, 411, 278, 96, 21, Ink, false);
        }
        else if (ball)
        {
            BallCardPresentation::DrawBallIcon(*ball, ui.At(x + 111, 245), ui.Size(108, 108));
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(ui.At(x + 16, 356), ui.At(x + 314, 395), IM_COL32(89, 48, 99, 255), 10 * ui.scale);
            const std::string name = std::string(PlayerBallText::GetName(ball->definitionId)) + " +" + std::to_string(ball->upgradeLevel);
            ui.Text(name.c_str(), x + 22, 359, 286, 32, 25);
            const auto description = PlayerBallText::GetShortDescription(*ball);
            ui.Text(description.c_str(), x + 27, 407, 276, 72, 21, Ink, false);
            const std::string attack = "攻撃力 " + std::to_string(ball->status.attack) + "  /  " + PlayerBallText::GetTrait(*ball);
            ui.Text(attack.c_str(), x + 28, 477, 274, 22, 18, Ink);
            const std::string price = model.rewardBallAcquired ? "ボール獲得済み" :
                std::to_string(model.rewardBallCost) + " ゴールド";
            ui.Text(price.c_str(), x + 28, 500, 274, 24, 23, Ink);
        }
        else ui.Text("候補がありません", x + 24, 360, 282, 48, 25, Ink);
        ImGui::PopID();
    }

    const bool upgrading = intent.selectedRewardIndex == 1;
    const int targetIndex = intent.selectedRewardBallIndex;
    const auto* target = targetIndex >= 0 && targetIndex < static_cast<int>(model.upgradeTargets.size())
        ? &model.upgradeTargets[targetIndex] : nullptr;
    const bool upgradeAvailable = std::any_of(model.upgradeTargets.begin(), model.upgradeTargets.end(),
        [&](const auto& item) { return item.ball && item.upgradeCost >= 0 && item.upgradeCost <= model.playerMoney; });
    ui.Text(relics ? "レリックを1つ選択" : "追加報酬（強化・ゴールドから1つ）", 458, 599, 532, 43, 25);
    ui.Art("assets/UI/Run/reward_upgrade.png", 190, 672, 470, 127);
    if (ui.Hit("upgrade_reward", 190, 672, 470, 127) && !chosen && !relics && upgradeAvailable)
    {
        intent.selectedRewardIndex = 1;
        if (!upgrading || !target || !target->ball || target->upgradeCost < 0 || target->upgradeCost > model.playerMoney)
        {
            for (int i = 0; i < static_cast<int>(model.upgradeTargets.size()); ++i)
                if (model.upgradeTargets[i].ball && model.upgradeTargets[i].upgradeCost >= 0 &&
                    model.upgradeTargets[i].upgradeCost <= model.playerMoney)
                { intent.selectedRewardBallIndex = i; break; }
        }
        ImGui::OpenPopup("強化するボールを選択###reward_upgrade_targets");
    }
    ui.Highlight(190, 672, 470, 127, !relics && intent.selectedRewardIndex == 1,
        ImGui::IsItemHovered() && !chosen, upgradeAvailable && !relics);
    if (ImGui::IsItemHovered() && !upgradeAvailable && !relics)
        ImGui::SetTooltip("強化できる球、または強化に必要なMoneyがありません。");

    ui.Art("assets/UI/Run/reward_money.png", 788, 672, 470, 127);
    if (ui.Hit("money_reward", 788, 672, 470, 127) && !chosen && !relics)
        intent.selectedRewardIndex = 2;
    ui.Highlight(788, 672, 470, 127, !relics && intent.selectedRewardIndex == 2,
        ImGui::IsItemHovered() && !chosen, !relics);
    // The supplied plate says +10; show the live amount too so data changes
    // never silently change the actual payout represented by the artwork.
    if (model.extraMoneyAmount != 10)
        ui.Text(("獲得：" + std::to_string(model.extraMoneyAmount) + " Money").c_str(), 825, 804, 400, 29, 22, Gold);
    if (!relics && intent.selectedRewardIndex != 0)
        ImGui::GetWindowDrawList()->AddCircleFilled(ui.At(intent.selectedRewardIndex == 1 ? 245.0f : 843.0f, 735),
            10 * ui.scale, Gold);

    bool unavailable = false;
    std::string selection;
    if (relics)
    {
        unavailable = intent.selectedRelicOfferIndex < 0 || intent.selectedRelicOfferIndex >= static_cast<int>(model.relicOffers.size()) ||
            !model.relicOffers[intent.selectedRelicOfferIndex];
        selection = "選んだレリックを受け取ります。";
    }
    else if (intent.selectedRewardIndex == 1)
    {
        const int i = intent.selectedRewardBallIndex;
        const auto* selected = i >= 0 && i < static_cast<int>(model.upgradeTargets.size()) ? &model.upgradeTargets[i] : nullptr;
        unavailable = !selected || !selected->ball || selected->upgradeCost < 0 || selected->upgradeCost > model.playerMoney;
        if (selected && selected->ball)
            selection = std::string("強化：") + PlayerBallText::GetName(selected->ball->definitionId) + " +" +
                std::to_string(selected->ball->upgradeLevel) + " → +" + std::to_string(selected->ball->upgradeLevel + 1) +
                "  /  " + std::to_string(selected->upgradeCost) + " Money";
        else selection = "強化できるボールがありません。";
    }
    else if (intent.selectedRewardIndex == 2)
        selection = "追加マネー +" + std::to_string(model.extraMoneyAmount) + " を受け取ります。";
    else
    {
        const int i = intent.selectedRewardBallIndex;
        unavailable = model.rewardBallAcquired || model.playerMoney < model.rewardBallCost ||
            i < 0 || i >= static_cast<int>(model.newBallOffers.size()) || !model.newBallOffers[i];
        if (model.playerMoney < model.rewardBallCost) selection = "ボールの獲得は所持金不足です。強化か追加ゴールドを選べます。";
        else if (!unavailable) selection = std::string("獲得：") + PlayerBallText::GetName(model.newBallOffers[i]->definitionId) +
            " / " + std::to_string(model.rewardBallCost) + " ゴールド（追加報酬は獲得後に選択）";
    }
    if (chosen) { selection = "報酬を受け取りました。「つづける」で次のルートへ。"; unavailable = false; }
    ui.Text(selection.c_str(), 310, 891, 838, 39, 23);
    ui.Art("assets/UI/Run/button_continue.png", 526, 963, 396, 91, unavailable ? IM_COL32(140, 140, 150, 255) : IM_COL32_WHITE);
    if (ui.Hit("confirm_reward", 526, 963, 396, 91) && !unavailable) intent.confirm = true;
    ui.Highlight(526, 963, 396, 91, false, ImGui::IsItemHovered(), !unavailable);

    // Upgrade targets are individual deck instances; keep the complete list
    // scrollable so large decks and repeated definitions remain selectable.
    ImGui::SetNextWindowSize(ImVec2(650 * ui.scale, 640 * ui.scale), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ui.At(724, 530), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("強化するボールを選択###reward_upgrade_targets", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted("強化するボールを選択");
        ImGui::Text("所持金：%d Money", model.playerMoney);
        ImGui::BeginChild("upgrade_list", ImVec2(0, -52 * ui.scale));
        for (int i = 0; i < static_cast<int>(model.upgradeTargets.size()); ++i)
        {
            const auto& item = model.upgradeTargets[i];
            if (!item.ball) continue;
            ImGui::PushID(i);
            const bool disabled = item.upgradeCost < 0 || item.upgradeCost > model.playerMoney;
            ImGui::BeginDisabled(disabled);
            if (PlayerBallUI::Select(*item.ball, i == intent.selectedRewardBallIndex))
            {
                intent.selectedRewardBallIndex = i;
                intent.selectedRewardIndex = 1;
            }
            ImGui::EndDisabled();
            if (item.upgradeCost < 0) ImGui::TextUnformatted("最大強化済み");
            else ImGui::Text("強化費用：%d Money%s", item.upgradeCost, disabled ? "（所持金不足）" : "");
            if (i == intent.selectedRewardBallIndex)
                ImGui::TextWrapped("%s", PlayerBallText::GetUpgradePreview(*item.ball).c_str());
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (ImGui::Button("選択を閉じる", ImVec2(-1, 40 * ui.scale))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return intent;
}
