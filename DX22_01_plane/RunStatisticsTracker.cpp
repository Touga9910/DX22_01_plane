#include "RunStatisticsTracker.h"

#include <algorithm>

void RunStatisticsTracker::Reset()
{
	m_State = RunResultSnapshot{};
}

void RunStatisticsTracker::AdvanceFrame()
{
	++m_State.activeFrames;
}

void RunStatisticsTracker::ReachFloor(int floor)
{
	m_State.reachedFloor = (std::max)(m_State.reachedFloor, floor);
}

void RunStatisticsTracker::CompleteArea(int areaProgress)
{
	m_State.areaProgress = (std::max)(m_State.areaProgress, areaProgress);
	m_State.reachedFloor = (std::max)(
		m_State.reachedFloor,
		(std::max)(1, m_State.areaProgress));
}

void RunStatisticsTracker::BeginBattle(
	bool midBoss,
	bool finalBoss,
	const std::string& stageId)
{
	++m_State.totalBattles;
	if (midBoss)
	{
		++m_State.midBossChallenges;
	}
	if (finalBoss)
	{
		m_State.finalBossReached = true;
		m_State.finalBossId = stageId;
	}
}

void RunStatisticsTracker::DefeatMidBoss()
{
	++m_State.midBossDefeats;
}

void RunStatisticsTracker::DefeatFinalBoss()
{
	m_State.finalBossReached = true;
	m_State.finalBossDefeated = true;
}

void RunStatisticsTracker::BeginShot(const std::string& ballId)
{
	m_State.currentTurnDamage = 0;
	++m_State.totalShots;
	if (!ballId.empty())
	{
		++m_State.ballShotCounts[ballId];
	}
}

void RunStatisticsTracker::RecordEnemyDamage(int damage)
{
	if (damage <= 0)
	{
		return;
	}
	m_State.totalDamage += damage;
	m_State.currentTurnDamage += damage;
	m_State.maximumTurnDamage = (std::max)(
		m_State.maximumTurnDamage,
		m_State.currentTurnDamage);
}

void RunStatisticsTracker::RecordPlayerDamage(int damage)
{
	m_State.damageTaken += (std::max)(0, damage);
}

void RunStatisticsTracker::RecordBallAcquired(const std::string& ballId)
{
	if (!ballId.empty())
	{
		m_State.acquiredBallIds.push_back(ballId);
	}
}

void RunStatisticsTracker::Restore(const RunResultSnapshot& state)
{
	m_State = state;
	m_State.completed = false;
	m_State.acquiredRelics.clear();
}

RunResultSnapshot RunStatisticsTracker::CreateSnapshot(
	bool completed,
	int currentHp,
	int maxHp,
	const std::vector<RelicType>& acquiredRelics) const
{
	RunResultSnapshot snapshot = m_State;
	snapshot.completed = completed;
	snapshot.currentHp = currentHp;
	snapshot.maxHp = (std::max)(1, maxHp);
	snapshot.acquiredRelics = acquiredRelics;
	return snapshot;
}
