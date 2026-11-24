#pragma once

// Owns the transient card selection shared by human input and automated
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
	/// <summary>
	/// 球選択とショット準備の現在の進行状態を返す。
	/// </summary>
	FlowState GetFlowState() const { return m_FlowState; }
	/// <summary>
	/// 手札が隠れている割合を返す。
	/// </summary>
	float HandHiddenRatio() const { return m_HandHiddenRatio; }
	/// <summary>
	/// 球候補を選択できる状態かを返す。
	/// </summary>
	bool IsSelectionInteractive() const
	{
		return m_FlowState == FlowState::Selecting;
	}
	/// <summary>
	/// ショット操作に進める状態かを返す。
	/// </summary>
	bool IsShotReady() const
	{
		return m_FlowState == FlowState::ShotReady;
	}

	void Begin(int offerCount, int heldOfferIndex);
	void Update(float deltaSeconds);
	bool SelectOffer(int offerIndex, int offerCount);
	bool ConfirmSelection(int offerCount);
	bool CancelShotPreparation();
	bool ApplyPresentationSelection(
		int selectedOfferIndex,
		int heldOfferIndex,
		int offerCount);

private:
	int m_SelectedOfferIndex = 0;
	FlowState m_FlowState = FlowState::Selecting;
	float m_HandHiddenRatio = 0.0f;
};
