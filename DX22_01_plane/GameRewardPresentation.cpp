#include "GamePresentation.h"
#include "GameUi.h"
#include "GameTypes.h"
#include "PlayerBallData.h"
#include "PlayerBallText.h"
#include "PlayerBallUI.h"
#include "UiText.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

namespace
{
	const char* const kClearRewardNames[] =
	{
		UiText::RewardNewBall,
		UiText::RewardUpgrade,
		UiText::RewardExtraMoney,
	};
	constexpr int kClearRewardCount =
		static_cast<int>(sizeof(kClearRewardNames) / sizeof(kClearRewardNames[0]));
}

ClearRewardPresentationIntent GamePresentation::DrawClearReward(
	const ClearRewardPresentationModel& model)
{
	ClearRewardPresentationIntent intent{
		model.selectedRelicOfferIndex,
		model.selectedRewardIndex,
		model.selectedRewardBallIndex,
		false,
	};
	GameUi::PrepareWindow("clear_reward", ImVec2(290, 90), ImVec2(700, 550));
	ImGui::Begin(UiText::ClearWindow, nullptr, ImGuiWindowFlags_NoCollapse);
	ImGui::TextUnformatted(UiText::StageClear);
	ImGui::Text(
		UiText::RewardMoneyFormat,
		model.stageRewardMoney);
	ImGui::Text(UiText::MoneyFormat, model.playerMoney);
	ImGui::Text("HP %d / %d", model.playerCurrentHp, model.playerMaxHp);
	ImGui::Separator();
	if (model.midBossRelicSelectionActive)
	{
		ImGui::TextUnformatted("中ボス撃破報酬：レリックを1つ選択");
		for (int index = 0; index < static_cast<int>(model.relicOffers.size()); ++index)
		{
			const auto* relic = model.relicOffers[static_cast<std::size_t>(index)];
			if (relic == nullptr) continue;
			ImGui::PushID(index);
			if (ImGui::Selectable(
				relic->name,
				index == intent.selectedRelicOfferIndex))
			{
				intent.selectedRelicOfferIndex = index;
			}
			ImGui::TextWrapped("%s", relic->description);
			ImGui::PopID();
		}
		if (ImGui::Button("選択したレリックを獲得", ImVec2(-1, 40)))
			intent.confirm = true;
	}
	else if (model.clearRewardChosen)
	{
		ImGui::TextWrapped("%s", model.rewardMessage.c_str());
		if (ImGui::Button("次のルートへ", ImVec2(-1, 44)))
			intent.confirm = true;
	}
	else
	{
		ImGui::TextUnformatted(UiText::ChooseReward);
		for (int index = 0; index < kClearRewardCount; ++index)
		{
			if (ImGui::RadioButton(
				kClearRewardNames[index],
				index == intent.selectedRewardIndex))
			{
				intent.selectedRewardIndex = index;
				intent.selectedRewardBallIndex = 0;
			}
		}
		ImGui::BeginChild("reward_targets", ImVec2(0, -96), ImGuiChildFlags_Borders);
		const bool upgrading = intent.selectedRewardIndex == 1;
		const int count = intent.selectedRewardIndex == 0
			? static_cast<int>(model.newBallOffers.size())
			: (upgrading ? static_cast<int>(model.upgradeTargets.size()) : 0);
		for (int index = 0; index < count; ++index)
		{
			const auto* ball = upgrading
				? model.upgradeTargets[static_cast<std::size_t>(index)].ball
				: model.newBallOffers[static_cast<std::size_t>(index)];
			if (ball == nullptr) continue;
			ImGui::PushID(index);
			if (PlayerBallUI::Select(
				*ball,
				index == intent.selectedRewardBallIndex))
			{
				intent.selectedRewardBallIndex = index;
			}
			if (index == intent.selectedRewardBallIndex)
			{
				ImGui::TextWrapped("%s", PlayerBallText::GetDescription(ball->definitionId));
				if (upgrading)
				{
					ImGui::TextWrapped("%s", PlayerBallText::GetUpgradePreview(*ball).c_str());
					const int upgradeCost =
						model.upgradeTargets[static_cast<std::size_t>(index)].upgradeCost;
					if (upgradeCost >= 0) ImGui::Text(UiText::UpgradeCostFormat, upgradeCost);
					if (upgradeCost > model.playerMoney) ImGui::TextUnformatted(UiText::UpgradeMoneyShortage);
				}
				else ImGui::TextWrapped("%s", PlayerBallText::GetStats(*ball, ball->status).c_str());
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (intent.selectedRewardIndex == 2)
			ImGui::Text("追加で %d Money を受け取ります。", model.extraMoneyAmount);
		ImGui::EndChild();
		const int cost = upgrading &&
			intent.selectedRewardBallIndex >= 0 &&
			intent.selectedRewardBallIndex < static_cast<int>(model.upgradeTargets.size())
			? model.upgradeTargets[static_cast<std::size_t>(intent.selectedRewardBallIndex)].upgradeCost
			: 0;
		const bool unavailable = upgrading
			? cost < 0 || cost > model.playerMoney
			: (intent.selectedRewardIndex == 0 && count == 0);
		ImGui::BeginDisabled(unavailable);
		if (ImGui::Button(
			upgrading ? "選択した個体を強化" : "選択した報酬を獲得",
			ImVec2(-1, 40)))
		{
			intent.confirm = true;
		}
		ImGui::EndDisabled();
		if (!model.rewardMessage.empty())
			ImGui::TextWrapped("%s", model.rewardMessage.c_str());
	}
	ImGui::End();
	return intent;
}
