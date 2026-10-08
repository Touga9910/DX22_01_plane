#include "GameRewardState.h"

#include <utility>

bool GameRewardState::IsActive() const
{
	return m_Active;
}

bool GameRewardState::IsChosen() const
{
	return m_Chosen;
}

bool GameRewardState::IsMidBossRelicSelectionActive() const
{
	return m_MidBossRelicSelectionActive;
}

int GameRewardState::SelectedRewardIndex() const
{
	return m_SelectedRewardIndex;
}

int GameRewardState::SelectedRewardBallIndex() const
{
	return m_SelectedRewardBallIndex;
}

int GameRewardState::SelectedRelicOfferIndex() const
{
	return m_SelectedRelicOfferIndex;
}

const std::string& GameRewardState::Message() const
{
	return m_Message;
}

int GameRewardState::BallOfferCount() const
{
	return static_cast<int>(m_BallOfferCatalogIndices.size());
}

int GameRewardState::BallOfferCatalogIndex(int offerIndex) const
{
	return offerIndex >= 0 && offerIndex < BallOfferCount()
		? m_BallOfferCatalogIndices[static_cast<std::size_t>(offerIndex)]
		: -1;
}

void GameRewardState::SetBallOffers(std::vector<int> catalogIndices)
{
	m_BallOfferCatalogIndices = std::move(catalogIndices);
}

void GameRewardState::ClearBallOffers()
{
	m_BallOfferCatalogIndices.clear();
}

void GameRewardState::ResetForRun()
{
	m_MidBossRelicSelectionActive = false;
	m_BallOfferCatalogIndices.clear();
}

void GameRewardState::Prepare(const std::string& message)
{
	m_SelectedRewardIndex = 0;
	m_SelectedRewardBallIndex = 0;
	m_Chosen = false;
	m_ConfirmationRequested = false;
	m_Message = message;
	m_MidBossRelicSelectionActive = false;
}

void GameRewardState::Activate()
{
	m_Active = true;
}

void GameRewardState::Deactivate()
{
	m_Active = false;
}

void GameRewardState::Exit()
{
	m_Active = false;
	m_BallOfferCatalogIndices.clear();
}

void GameRewardState::ClearMessage()
{
	m_Message.clear();
}

void GameRewardState::SetMessage(std::string message)
{
	m_Message = std::move(message);
}

void GameRewardState::MarkChosen()
{
	m_Chosen = true;
}

void GameRewardState::MarkChosen(
	int rewardIndex,
	int rewardBallIndex,
	std::string message)
{
	m_SelectedRewardIndex = rewardIndex;
	m_SelectedRewardBallIndex = rewardBallIndex;
	m_Message = std::move(message);
	m_Chosen = true;
}

void GameRewardState::StartMidBossRelicSelection(bool hasOffers)
{
	m_SelectedRelicOfferIndex = 0;
	m_MidBossRelicSelectionActive = hasOffers;
}

void GameRewardState::FinishMidBossRelicSelection()
{
	m_MidBossRelicSelectionActive = false;
}

void GameRewardState::SelectPreviousRelic(int offerCount)
{
	if (offerCount > 0)
	{
		m_SelectedRelicOfferIndex =
			(m_SelectedRelicOfferIndex + offerCount - 1) % offerCount;
	}
}

void GameRewardState::SelectNextRelic(int offerCount)
{
	if (offerCount > 0)
	{
		m_SelectedRelicOfferIndex =
			(m_SelectedRelicOfferIndex + 1) % offerCount;
	}
}

void GameRewardState::SelectPreviousReward(int rewardCount)
{
	if (rewardCount > 0)
	{
		m_SelectedRewardIndex =
			(m_SelectedRewardIndex + rewardCount - 1) % rewardCount;
		m_SelectedRewardBallIndex = 0;
	}
}

void GameRewardState::SelectNextReward(int rewardCount)
{
	if (rewardCount > 0)
	{
		m_SelectedRewardIndex =
			(m_SelectedRewardIndex + 1) % rewardCount;
		m_SelectedRewardBallIndex = 0;
	}
}

void GameRewardState::SelectPreviousBall(int targetCount)
{
	if (targetCount > 0)
	{
		m_SelectedRewardBallIndex =
			(m_SelectedRewardBallIndex + targetCount - 1) % targetCount;
	}
}

void GameRewardState::SelectNextBall(int targetCount)
{
	if (targetCount > 0)
	{
		m_SelectedRewardBallIndex =
			(m_SelectedRewardBallIndex + 1) % targetCount;
	}
}

void GameRewardState::ApplyPresentationSelection(
	int relicOfferIndex,
	int rewardIndex,
	int rewardBallIndex)
{
	m_SelectedRelicOfferIndex = relicOfferIndex;
	m_SelectedRewardIndex = rewardIndex;
	m_SelectedRewardBallIndex = rewardBallIndex;
}

void GameRewardState::RequestConfirmation()
{
	m_ConfirmationRequested = true;
}

bool GameRewardState::ConsumeConfirmationRequest()
{
	return std::exchange(m_ConfirmationRequested, false);
}
