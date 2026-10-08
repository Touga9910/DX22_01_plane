#pragma once

// Owns the transient offer/hold selection shared by human input and automated
// controllers. It validates indices without knowing deck or rendering details.
class GameBallSelectionState final
{
public:
	int SelectedOfferIndex() const;
	int HeldOfferIndex() const;

	void Begin(int offerCount, int heldOfferIndex);
	bool SelectOffer(int offerIndex, int offerCount);
	bool ToggleHold(int offerIndex, int offerCount);
	bool ApplyPresentationSelection(
		int selectedOfferIndex,
		int heldOfferIndex,
		int offerCount);

private:
	int m_SelectedOfferIndex = 0;
	int m_HeldOfferIndex = -1;
};
