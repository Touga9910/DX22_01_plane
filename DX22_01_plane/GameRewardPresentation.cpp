#include "ClearRewardPresentation.h"
#include "GameTypes.h"
#include "BallCardPresentation.h"
#include "GoldUi.h"
#include "IllustratedUi.h"
#include "PlayerBallData.h"
#include "PlayerBallText.h"
#include "PlayerBallUI.h"
#include "UiText.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <string>

namespace
{
    constexpr const char* PurchasePopup = "ボールの購入###reward_purchase_confirm";
    constexpr const char* DetailPopup = "ボール詳細###reward_ball_details";
    constexpr const char* UpgradePopup = "強化するボールを選択###reward_upgrade_targets";
}

ClearRewardPresentationIntent ClearRewardUi::Draw(
    const ClearRewardPresentationModel& model, int& pendingPurchaseOfferIndex)
{
    using namespace IllustratedUi;
    ClearRewardPresentationIntent intent{
        model.selectedRelicOfferIndex, model.selectedRewardIndex,
        model.selectedRewardBallIndex, false,
    };
    Canvas ui("clear_reward_art", 1448, 1086, IM_COL32(0, 0, 0, 72));
    if (model.fieldBounds.valid)
    {
        ui.origin = ImVec2(model.fieldBounds.left, model.fieldBounds.top);
        ui.scale = (std::min)(model.fieldBounds.width / 1448.0f, model.fieldBounds.height / 1086.0f);
    }
    ui.Art("assets/UI/Lounge/reward_background.png", 0, 0, 1448, 1086, IM_COL32_WHITE, false);
    ui.Text("バトル報酬", 190, 91, 1050, 58, 36);
    const bool relics = model.midBossRelicSelectionActive;
    const bool chosen = model.clearRewardChosen;
    const bool purchased = model.rewardBallAcquired && !relics;
    const bool keyboardConfirm = !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false));
    const std::string status = relics ? "レリックを1つ選んでください。" : chosen && !purchased
        ? "追加報酬は受取済み。ボールを購入するか、スキップで見送れます。" : purchased
        ? "ボール購入済み。強化かゴールドを選ぶとマップへ戻ります。"
        : "ボール購入と追加報酬は、どちらを先に選んでも受け取れます。";
    ui.Text(status.c_str(), 220, 169, 1008, 30, 22);
    ui.Text("所持金", 555, 201, 100, 30, 21, Gold);
    GoldUi::Draw(ImGui::GetWindowDrawList(), ui.At(660, 199), model.playerMoney,
        32 * ui.scale, 23 * ui.scale);

    for (int index = 0; index < 3; ++index)
    {
        const float x = 177 + index * 380.0f;
        const auto* ball = index < static_cast<int>(model.newBallOffers.size()) ? model.newBallOffers[index] : nullptr;
        const auto* relic = index < static_cast<int>(model.relicOffers.size()) ? model.relicOffers[index] : nullptr;
        const bool exists = relics ? relic != nullptr : ball != nullptr;
        const bool selected = relics ? index == intent.selectedRelicOfferIndex :
            intent.selectedRewardIndex == 0 && index == intent.selectedRewardBallIndex;
        ImGui::PushID(index);
        const bool clicked = ui.Hit("reward_card", x, 235, 330, 287);
        const bool hovered = ImGui::IsItemHovered();
        const bool showDetails = !relics && ball && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if ((!relics || !chosen) && exists && !purchased &&
            (clicked || (!relics && keyboardConfirm && selected)))
        {
            if (relics) intent.selectedRelicOfferIndex = index;
            else
            {
                intent.selectedRewardIndex = 0;
                intent.selectedRewardBallIndex = index;
                pendingPurchaseOfferIndex = index;
                // Open outside the per-card ID scope below.
            }
        }
        ImGui::PopID();
        if (showDetails)
        {
            pendingPurchaseOfferIndex = index;
            ImGui::OpenPopup(DetailPopup);
        }
        if (!relics && !purchased && exists &&
            (clicked || (keyboardConfirm && selected))) ImGui::OpenPopup(PurchasePopup);
        if (purchased)
            ImageRegion(ImGui::GetWindowDrawList(), "assets/UI/Lounge/reward_background.png",
                ui.At(x - 16, 217), ui.At(x + 346, 537),
                ImVec2((x - 16) / 1448, 217.0f / 1086), ImVec2((x + 346) / 1448, 537.0f / 1086), true);
        else ui.Highlight(x, 235, 330, 287, selected, hovered, exists);
        if (relics && relic)
        {
            ui.Text("R", x + 120, 251, 90, 94, 48, Gold);
            ui.Text(relic->name, x + 18, 354, 294, 48, 26, Ink);
            ui.Text(relic->description, x + 26, 411, 278, 96, 21, Ink, false);
        }
        else if (ball)
        {
            BallCardPresentation::DrawBallIcon(*ball, ui.At(x + 111, 250), ui.Size(108, 108), purchased);
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(ui.At(x + 16, 368), ui.At(x + 314, 414),
                purchased ? IM_COL32(92, 92, 92, 255) : IM_COL32(68, 40, 31, 255), 10 * ui.scale);
            const std::string name = std::string(PlayerBallText::GetName(ball->definitionId)) + " +" + std::to_string(ball->upgradeLevel);
            ui.Text(name.c_str(), x + 22, 371, 286, 40, 26,
                purchased ? IM_COL32_WHITE : Cream);
            const float width = GoldUi::Width(model.rewardBallCost, 38 * ui.scale, 30 * ui.scale);
            GoldUi::Draw(draw, ui.At(x + 247 - width / (2 * ui.scale), 451), model.rewardBallCost,
                38 * ui.scale, 30 * ui.scale, purchased ? IM_COL32(55, 55, 55, 255) : Ink, purchased);
        }
        else ui.Text("候補がありません", x + 24, 360, 282, 48, 25, Ink);
    }

    const bool upgradeAvailable = std::any_of(model.upgradeTargets.begin(), model.upgradeTargets.end(),
        [&](const auto& item) { return item.ball && item.upgradeCost >= 0 && item.upgradeCost <= model.playerMoney; });
    ui.Text(relics ? "レリックを1つ選択" : "追加報酬（強化・ゴールドから1つ）", 458, 599, 532, 43, 25);
    ui.Art("assets/UI/Lounge/reward_upgrade.png", 190, 672, 470, 127);
    const bool upgradeClicked = ui.Hit("upgrade_reward", 190, 672, 470, 127);
    const bool upgradeHovered = ImGui::IsItemHovered();
    if (!chosen && !relics && upgradeAvailable &&
        (upgradeClicked || (keyboardConfirm && intent.selectedRewardIndex == 1)))
    {
        intent.selectedRewardIndex = 1;
        const int selected = intent.selectedRewardBallIndex;
        if (selected < 0 || selected >= static_cast<int>(model.upgradeTargets.size()) ||
            !model.upgradeTargets[selected].ball || model.upgradeTargets[selected].upgradeCost < 0 ||
            model.upgradeTargets[selected].upgradeCost > model.playerMoney)
            for (int i = 0; i < static_cast<int>(model.upgradeTargets.size()); ++i)
                if (model.upgradeTargets[i].ball && model.upgradeTargets[i].upgradeCost >= 0 &&
                    model.upgradeTargets[i].upgradeCost <= model.playerMoney)
                { intent.selectedRewardBallIndex = i; break; }
        ImGui::OpenPopup(UpgradePopup);
    }
    ui.Highlight(190, 672, 470, 127, false, upgradeHovered && !chosen, upgradeAvailable && !relics && !chosen);
    if (upgradeHovered && !upgradeAvailable && !relics)
        ImGui::SetTooltip("強化できるボール、または強化に必要なゴールドがありません。");
    ui.Art("assets/UI/Lounge/reward_money.png", 788, 672, 470, 127);
    if (ui.Hit("money_reward", 788, 672, 470, 127) && !chosen && !relics)
    { intent.selectedRewardIndex = 2; intent.confirm = true; }
    if (keyboardConfirm && intent.selectedRewardIndex == 2 && !chosen && !relics)
        intent.confirm = true;
    ui.Highlight(788, 672, 470, 127, false, ImGui::IsItemHovered() && !chosen, !relics && !chosen);
    ui.Text("ボールを強化", 390, 689, 260, 35, 24);
    ui.Text("15 / 30 ゴールド", 390, 736, 260, 30, 19, Gold);
    ui.Text(("ゴールド +"+std::to_string(model.extraMoneyAmount)).c_str(), 968, 710, 270, 40, 25);
    ui.Text(relics ? "レリックを選択して受け取ってください。" :
        "詳細：右クリック ／ 左クリックで購入確認 ／ 追加報酬はどちらか1つ", 310, 891, 838, 39, 22);

    // Replace the baked green button with a red, gold-framed skip plate.
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 outer[] = {ui.At(522, 950), ui.At(926, 950), ui.At(948, 972), ui.At(948, 1040),
        ui.At(926, 1062), ui.At(522, 1062), ui.At(500, 1040), ui.At(500, 972)};
    draw->AddConvexPolyFilled(outer, 8, Gold);
    const ImVec2 inner[] = {ui.At(525, 955), ui.At(923, 955), ui.At(942, 975), ui.At(942, 1037),
        ui.At(923, 1057), ui.At(525, 1057), ui.At(506, 1037), ui.At(506, 975)};
    draw->AddConvexPolyFilled(inner, 8, IM_COL32(139, 29, 48, 255));
    ui.Text(relics ? "受け取る" : "スキップ", 526, 963, 396, 81, 36);
    if (ui.Hit("skip_reward", 500, 950, 448, 112))
    {
        if (relics) intent.confirm = true;
        else intent.skip = true;
    }
    ui.Highlight(500, 950, 448, 112, false, ImGui::IsItemHovered());

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.065f, 0.17f, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(Gold));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.34f, 0.16f, 0.43f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.48f, 0.26f, 0.57f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.57f, 0.34f, 0.65f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2);
    // Same fixed, interactive detail panel as the battle hand. The close
    // button consumes the final cursor position left by the shared renderer.
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2((std::min)(780.0f, viewport->WorkSize.x - 32.0f),
        (std::min)(452.0f, viewport->WorkSize.y - 32.0f)), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ui.At(724, 530), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(DetailPopup, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        const int index = pendingPurchaseOfferIndex;
        const auto* ball = index >= 0 && index < static_cast<int>(model.newBallOffers.size()) ? model.newBallOffers[index] : nullptr;
        if (ball) BallCardPresentation::DrawBallDetailPopup(*ball, ball->status.attack);
        if (ImGui::Button("閉じる", ImVec2(-1.0f, 38.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(ui.At(724, 530), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2((std::min)(520.0f, ImGui::GetMainViewport()->WorkSize.x - 24), 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(PurchasePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        const int index = pendingPurchaseOfferIndex;
        const auto* ball = index >= 0 && index < static_cast<int>(model.newBallOffers.size()) ? model.newBallOffers[index] : nullptr;
        const bool canBuy = ball && !purchased && model.playerMoney >= model.rewardBallCost;
        ImGui::TextUnformatted(ball ? PlayerBallText::GetName(ball->definitionId) : "候補がありません");
        ImGui::TextUnformatted("このボールを購入しますか？");
        GoldUi::Draw(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), model.rewardBallCost, 32, 24);
        ImGui::Dummy(ImVec2(150, 42));
        ImGui::Text("所持金：%d", model.playerMoney);
        if (!canBuy) ImGui::TextUnformatted("購入済み、または所持金が不足しています。");
        ImGui::BeginDisabled(!canBuy);
        if (ImGui::Button("Yes（購入）", ImVec2(200, 42)))
        {
            intent.selectedRewardIndex = 0;
            intent.selectedRewardBallIndex = index;
            intent.confirm = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("No（戻る）", ImVec2(200, 42))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2((std::min)(650.0f, ImGui::GetMainViewport()->WorkSize.x - 24),
        (std::min)(640.0f, ImGui::GetMainViewport()->WorkSize.y - 24)), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ui.At(724, 530), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(UpgradePopup, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted("強化するボールを選択");
        GoldUi::Draw(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), model.playerMoney, 28, 22);
        ImGui::Dummy(ImVec2(180, 34));
        ImGui::BeginChild("upgrade_list", ImVec2(0, -58));
        for (int i = 0; i < static_cast<int>(model.upgradeTargets.size()); ++i)
        {
            const auto& item = model.upgradeTargets[i];
            if (!item.ball) continue;
            ImGui::PushID(i);
            const bool disabled = item.upgradeCost < 0 || item.upgradeCost > model.playerMoney;
            ImGui::BeginDisabled(disabled);
            if (PlayerBallUI::Select(*item.ball, i == intent.selectedRewardBallIndex)) intent.selectedRewardBallIndex = i;
            ImGui::EndDisabled();
            if (item.upgradeCost < 0) ImGui::TextUnformatted(UiText::BallAlreadyMax);
            else
            {
                GoldUi::Draw(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), item.upgradeCost, 25, 20);
                ImGui::Dummy(ImVec2(150, 30));
            }
            if (i == intent.selectedRewardBallIndex) ImGui::TextWrapped("%s", PlayerBallText::GetUpgradePreview(*item.ball).c_str());
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
        const int selected = intent.selectedRewardBallIndex;
        const bool canUpgrade = !chosen && selected >= 0 && selected < static_cast<int>(model.upgradeTargets.size()) &&
            model.upgradeTargets[selected].ball && model.upgradeTargets[selected].upgradeCost >= 0 &&
            model.upgradeTargets[selected].upgradeCost <= model.playerMoney;
        ImGui::BeginDisabled(!canUpgrade);
        if (ImGui::Button("強化する", ImVec2(220, 42)))
        { intent.selectedRewardIndex = 1; intent.confirm = true; ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("戻る", ImVec2(180, 42))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    intent.modalOpen = ImGui::IsPopupOpen(PurchasePopup) || ImGui::IsPopupOpen(UpgradePopup) || ImGui::IsPopupOpen(DetailPopup);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    return intent;
}
