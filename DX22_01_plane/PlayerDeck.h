#pragma once

#include <optional>
#include <vector>

#include "PlayerBallData.h"

class PlayerDeck
{
public:
	void SetDefaultDeck(const std::vector<PlayerBallData>& defaultDeck);
	void Reset();

	bool DrawNext();
	bool DiscardCurrentIfUsed();
	void MarkCurrentUsed();
	void ClearCurrentUsed();

	bool HasCurrent() const { return m_CurrentBall.has_value(); }
	bool IsCurrentUsed() const { return m_IsCurrentBallUsed; }

	const PlayerBallData* GetCurrent() const;
	PlayerBallData* GetCurrent();

	int GetDrawPileCount() const { return static_cast<int>(m_DrawPile.size()); }
	int GetDiscardPileCount() const { return static_cast<int>(m_DiscardPile.size()); }

	const std::vector<PlayerBallData>& GetDrawPile() const { return m_DrawPile; }
	const std::vector<PlayerBallData>& GetDiscardPile() const { return m_DiscardPile; }

	int GetRewardTargetCount() const;
	const PlayerBallData* GetRewardTarget(int index) const;
	PlayerBallData* GetRewardTarget(int index);

private:
	void ShuffleDrawPile();
	static BallStatus NormalizeStatus(BallStatus status);

private:
	std::vector<PlayerBallData> m_DefaultDeck;
	std::vector<PlayerBallData> m_DrawPile;
	std::vector<PlayerBallData> m_DiscardPile;
	std::optional<PlayerBallData> m_CurrentBall;
	bool m_IsCurrentBallUsed = false;
};
