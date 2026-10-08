#pragma once

#include <string>
#include <vector>

// Owns the transient state of the clear-reward flow. Game remains responsible
// for deciding when the flow starts and which gameplay operation is executed.
class GameRewardState final
{
public:
	bool IsActive() const;
	bool IsChosen() const;
	bool IsMidBossRelicSelectionActive() const;
	int SelectedRewardIndex() const;
	int SelectedRewardBallIndex() const;
	int SelectedRelicOfferIndex() const;
	const std::string& Message() const;

	int BallOfferCount() const;
	int BallOfferCatalogIndex(int offerIndex) const;
	void SetBallOffers(std::vector<int> catalogIndices);
	void ClearBallOffers();

	void ResetForRun();
	void Prepare(const std::string& message);
	void Activate();
	void Deactivate();
	void Exit();
	void ClearMessage();
	void SetMessage(std::string message);
	void MarkChosen();
	void MarkChosen(
		int rewardIndex,
		int rewardBallIndex,
		std::string message);

	void StartMidBossRelicSelection(bool hasOffers);
	void FinishMidBossRelicSelection();
	void SelectPreviousRelic(int offerCount);
	void SelectNextRelic(int offerCount);
	void SelectPreviousReward(int rewardCount);
	void SelectNextReward(int rewardCount);
	void SelectPreviousBall(int targetCount);
	void SelectNextBall(int targetCount);
	void ApplyPresentationSelection(
		int relicOfferIndex,
		int rewardIndex,
		int rewardBallIndex);

	void RequestConfirmation();
	bool ConsumeConfirmationRequest();

private:
	bool m_Active = false;
	int m_SelectedRewardIndex = 0;
	int m_SelectedRewardBallIndex = 0;
	std::vector<int> m_BallOfferCatalogIndices;
	std::string m_Message;
	bool m_Chosen = false;
	bool m_ConfirmationRequested = false;
	bool m_MidBossRelicSelectionActive = false;
	int m_SelectedRelicOfferIndex = 0;
};
