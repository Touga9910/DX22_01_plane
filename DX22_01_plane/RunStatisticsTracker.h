#pragma once

#include "RunResultSnapshot.h"

class RunStatisticsTracker final
{
public:
	void Reset();
	void AdvanceFrame();
	void ReachFloor(int floor);
	void CompleteArea(int areaProgress);
	void BeginBattle(bool midBoss, bool finalBoss, const std::string& stageId);
	void DefeatMidBoss();
	void DefeatFinalBoss();
	void BeginShot(const std::string& ballId);
	void RecordEnemyDamage(int damage);
	void RecordPlayerDamage(int damage);
	void RecordBallAcquired(const std::string& ballId);

	const RunResultSnapshot& GetState() const { return m_State; }
	void Restore(const RunResultSnapshot& state);
	RunResultSnapshot CreateSnapshot(
		bool completed,
		int currentHp,
		int maxHp,
		const std::vector<RelicType>& acquiredRelics) const;

private:
	RunResultSnapshot m_State{};
};
