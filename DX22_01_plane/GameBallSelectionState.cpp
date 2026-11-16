#include "GameBallSelectionState.h"

#include <algorithm>

namespace
{
	constexpr float kHandTransitionSeconds = 0.24f;
}

int GameBallSelectionState::SelectedOfferIndex() const
{
	return m_SelectedOfferIndex;
}

int GameBallSelectionState::HeldOfferIndex() const
{
	return -1; // Legacy read API; holding is removed.
}

void GameBallSelectionState::Begin(int /*offerCount*/, int /*legacyHeldOfferIndex*/)
{
	m_FlowState = FlowState::Selecting;
	m_HandHiddenRatio = 0.0f;
	m_SelectedOfferIndex = 0;
}

void GameBallSelectionState::Update(float deltaSeconds)
{
	const float step = (std::max)(0.0f, deltaSeconds) /
		kHandTransitionSeconds;
	if (m_FlowState == FlowState::Hiding)
	{
		m_HandHiddenRatio = (std::min)(1.0f, m_HandHiddenRatio + step);
		if (m_HandHiddenRatio >= 1.0f)
		{
			m_FlowState = FlowState::ShotReady;
		}
	}
	else if (m_FlowState == FlowState::Showing)
	{
		m_HandHiddenRatio = (std::max)(0.0f, m_HandHiddenRatio - step);
		if (m_HandHiddenRatio <= 0.0f)
		{
			m_FlowState = FlowState::Selecting;
		}
	}
}

bool GameBallSelectionState::SelectOffer(int offerIndex, int offerCount)
{
	if (offerIndex < 0 || offerIndex >= offerCount)
	{
		return false;
	}
	m_SelectedOfferIndex = offerIndex;
	return true;
}

bool GameBallSelectionState::ConfirmSelection(int offerCount)
{
	if (!IsSelectionInteractive() ||
		m_SelectedOfferIndex < 0 || m_SelectedOfferIndex >= offerCount)
	{
		return false;
	}
	m_FlowState = FlowState::Hiding;
	m_HandHiddenRatio = 0.0f;
	return true;
}

bool GameBallSelectionState::CancelShotPreparation()
{
	if (!IsShotReady())
	{
		return false;
	}
	m_FlowState = FlowState::Showing;
	m_HandHiddenRatio = 1.0f;
	return true;
}

bool GameBallSelectionState::ApplyPresentationSelection(
	int selectedOfferIndex,
	int /*legacyHeldOfferIndex*/,
	int offerCount)
{
	if (!IsSelectionInteractive())
	{
		return false;
	}
	if (!SelectOffer(selectedOfferIndex, offerCount))
	{
		return false;
	}
	return true;
}
