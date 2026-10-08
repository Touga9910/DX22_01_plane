#pragma once

#include "ProgressionProfile.h"
#include "RunStatisticsTracker.h"

#include <string>
#include <vector>

// Owns run statistics, persistent progression, and save/load notification
// state. Game decides when these systems advance and when snapshots are used.
class GamePersistenceRuntime final
{
public:
	void AdvanceFrame(bool runActive);

	RunStatisticsTracker& Statistics();
	const RunStatisticsTracker& Statistics() const;
	ProgressionProfile& Profile();
	const ProgressionProfile& Profile() const;

	int& ActiveAscension();
	int ActiveAscension() const;
	bool& PersistentProgressEligible();
	bool PersistentProgressEligible() const;
	float& DefaultRestHealRatio();
	float DefaultRestHealRatio() const;
	std::vector<std::string>& LastProgressionUnlocks();
	const std::vector<std::string>& LastProgressionUnlocks() const;

	bool IsRestoringRunSave() const;
	void SetRestoringRunSave(bool restoring);
	void SetSaveLoadMessage(std::string message);
	const std::string& SaveLoadMessage() const;

private:
	RunStatisticsTracker m_Statistics{};
	ProgressionProfile m_Profile{};
	int m_ActiveAscension = 0;
	bool m_PersistentProgressEligible = false;
	float m_DefaultRestHealRatio = 0.25f;
	std::vector<std::string> m_LastProgressionUnlocks;
	bool m_IsRestoringRunSave = false;
	std::string m_SaveLoadMessage;
	int m_SaveLoadMessageFrames = 0;
};
