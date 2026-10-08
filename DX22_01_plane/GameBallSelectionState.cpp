#include "GameBallSelectionState.h"

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
	if (offerIndex < 0 || offerIndex >= offerCount ||
		offerIndex == m_SelectedOfferIndex)
	{
		return false;
	}
	m_HeldOfferIndex = m_HeldOfferIndex == offerIndex ? -1 : offerIndex;
	return true;
}

bool GameBallSelectionState::ApplyPresentationSelection(
	int selectedOfferIndex,
	int heldOfferIndex,
	int offerCount)
{
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
