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
	return m_HeldOfferIndex;
}

void GameBallSelectionState::Begin(int offerCount, int heldOfferIndex)
{
	m_FlowState = FlowState::Selecting;
	m_HandHiddenRatio = 0.0f;
	m_SelectedOfferIndex = 0;
	m_HeldOfferIndex =
		heldOfferIndex >= 0 && heldOfferIndex < offerCount
		? heldOfferIndex
		: -1;

	if (m_HeldOfferIndex >= 0 && offerCount > 1)
	{
		m_SelectedOfferIndex = 1;
	}
	else if (m_HeldOfferIndex == m_SelectedOfferIndex)
	{
		m_HeldOfferIndex = -1;
	}
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
	if (m_HeldOfferIndex == offerIndex)
	{
		m_HeldOfferIndex = -1;
	}
	return true;
}

bool GameBallSelectionState::ToggleHold(int offerIndex, int offerCount)
{
	if (!IsSelectionInteractive() ||
		offerIndex < 0 || offerIndex >= offerCount ||
		offerIndex == m_SelectedOfferIndex)
	{
		return false;
	}
	m_HeldOfferIndex = m_HeldOfferIndex == offerIndex ? -1 : offerIndex;
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
	int heldOfferIndex,
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
	m_HeldOfferIndex =
		heldOfferIndex >= 0 && heldOfferIndex < offerCount &&
		heldOfferIndex != m_SelectedOfferIndex
		? heldOfferIndex
		: -1;
	return true;
}
