#pragma once

// Owns the transient offer/hold selection shared by human input and automated
// controllers. It validates indices without knowing deck or rendering details.
class GameBallSelectionState final
{
public:
	enum class FlowState
	{
		Selecting,
		Hiding,
		ShotReady,
		Showing,
	};

	int SelectedOfferIndex() const;
	int HeldOfferIndex() const;
	FlowState GetFlowState() const { return m_FlowState; }
	float HandHiddenRatio() const { return m_HandHiddenRatio; }
	bool IsSelectionInteractive() const
	{
		return m_FlowState == FlowState::Selecting;
	}
	bool IsShotReady() const
	{
		return m_FlowState == FlowState::ShotReady;
	}

	void Begin(int offerCount, int heldOfferIndex);
	void Update(float deltaSeconds);
	bool SelectOffer(int offerIndex, int offerCount);
	bool ToggleHold(int offerIndex, int offerCount);
	bool ConfirmSelection(int offerCount);
	bool CancelShotPreparation();
	bool ApplyPresentationSelection(
		int selectedOfferIndex,
		int heldOfferIndex,
		int offerCount);

private:
	int m_SelectedOfferIndex = 0;
	int m_HeldOfferIndex = -1;
	FlowState m_FlowState = FlowState::Selecting;
	float m_HandHiddenRatio = 0.0f;
};
