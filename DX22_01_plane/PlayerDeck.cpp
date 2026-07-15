#include "PlayerDeck.h"

#include <algorithm>
#include <random>

void PlayerDeck::SetDefaultDeck(const std::vector<PlayerBallData>& defaultDeck)
{
	m_DefaultDeck = defaultDeck;
}

void PlayerDeck::Reset()
{
	m_DrawPile = m_DefaultDeck;
	m_DiscardPile.clear();
	m_CurrentBall.reset();
	m_IsCurrentBallUsed = false;
	ShuffleDrawPile();
	DrawNext();
}

bool PlayerDeck::DrawNext()
{
	if (m_DrawPile.empty())
	{
		if (!m_DiscardPile.empty())
		{
			m_DrawPile = m_DiscardPile;
			m_DiscardPile.clear();
			ShuffleDrawPile();
		}
		else if (!m_DefaultDeck.empty())
		{
			m_DrawPile = m_DefaultDeck;
			ShuffleDrawPile();
		}
	}

	if (m_DrawPile.empty())
	{
		m_CurrentBall.reset();
		m_IsCurrentBallUsed = false;
		return false;
	}

	m_CurrentBall = std::move(m_DrawPile.back());
	m_DrawPile.pop_back();
	m_CurrentBall->status = NormalizeStatus(m_CurrentBall->status);
	m_IsCurrentBallUsed = false;
	return true;
}

bool PlayerDeck::DiscardCurrentIfUsed()
{
	if (!m_CurrentBall.has_value())
	{
		m_IsCurrentBallUsed = false;
		return false;
	}

	if (!m_IsCurrentBallUsed)
	{
		return false;
	}

	m_DiscardPile.push_back(std::move(*m_CurrentBall));
	m_CurrentBall.reset();
	m_IsCurrentBallUsed = false;
	return true;
}

void PlayerDeck::MarkCurrentUsed()
{
	if (m_CurrentBall.has_value())
	{
		m_IsCurrentBallUsed = true;
	}
}

void PlayerDeck::ClearCurrentUsed()
{
	m_IsCurrentBallUsed = false;
}

const PlayerBallData* PlayerDeck::GetCurrent() const
{
	if (!m_CurrentBall.has_value())
	{
		return nullptr;
	}

	return &(*m_CurrentBall);
}

PlayerBallData* PlayerDeck::GetCurrent()
{
	if (!m_CurrentBall.has_value())
	{
		return nullptr;
	}

	return &(*m_CurrentBall);
}

int PlayerDeck::GetRewardTargetCount() const
{
	int count = static_cast<int>(m_DrawPile.size() + m_DiscardPile.size());
	if (m_CurrentBall.has_value())
	{
		count++;
	}

	return count;
}

const PlayerBallData* PlayerDeck::GetRewardTarget(int index) const
{
	if (index < 0)
	{
		return nullptr;
	}

	if (m_CurrentBall.has_value())
	{
		if (index == 0)
		{
			return &(*m_CurrentBall);
		}

		index--;
	}

	if (index < static_cast<int>(m_DrawPile.size()))
	{
		return &m_DrawPile[index];
	}

	index -= static_cast<int>(m_DrawPile.size());
	if (index < static_cast<int>(m_DiscardPile.size()))
	{
		return &m_DiscardPile[index];
	}

	return nullptr;
}

PlayerBallData* PlayerDeck::GetRewardTarget(int index)
{
	if (index < 0)
	{
		return nullptr;
	}

	if (m_CurrentBall.has_value())
	{
		if (index == 0)
		{
			return &(*m_CurrentBall);
		}

		index--;
	}

	if (index < static_cast<int>(m_DrawPile.size()))
	{
		return &m_DrawPile[index];
	}

	index -= static_cast<int>(m_DrawPile.size());
	if (index < static_cast<int>(m_DiscardPile.size()))
	{
		return &m_DiscardPile[index];
	}

	return nullptr;
}

void PlayerDeck::ShuffleDrawPile()
{
	static std::mt19937 rng(std::random_device{}());
	std::shuffle(m_DrawPile.begin(), m_DrawPile.end(), rng);
}

BallStatus PlayerDeck::NormalizeStatus(BallStatus status)
{
	status.maxHp = (std::max)(1, status.maxHp);
	status.mass = (std::max)(0.0001f, status.mass);
	status.radius = (std::max)(0.0f, status.radius);
	status.restitution = std::clamp(status.restitution, 0.0f, 1.0f);
	status.friction = (std::max)(0.0f, status.friction);

	return status;
}
