#include "GamePersistenceRuntime.h"

#include <utility>

void GamePersistenceRuntime::AdvanceFrame(bool runActive)
{
	if (runActive)
	{
		m_Statistics.AdvanceFrame();
	}
	if (m_SaveLoadMessageFrames > 0)
	{
		--m_SaveLoadMessageFrames;
		if (m_SaveLoadMessageFrames == 0)
		{
			m_SaveLoadMessage.clear();
		}
	}
}

RunStatisticsTracker& GamePersistenceRuntime::Statistics()
{
	return m_Statistics;
}

const RunStatisticsTracker& GamePersistenceRuntime::Statistics() const
{
	return m_Statistics;
}

ProgressionProfile& GamePersistenceRuntime::Profile()
{
	return m_Profile;
}

const ProgressionProfile& GamePersistenceRuntime::Profile() const
{
	return m_Profile;
}

int& GamePersistenceRuntime::ActiveAscension()
{
	return m_ActiveAscension;
}

int GamePersistenceRuntime::ActiveAscension() const
{
	return m_ActiveAscension;
}

bool& GamePersistenceRuntime::PersistentProgressEligible()
{
	return m_PersistentProgressEligible;
}

bool GamePersistenceRuntime::PersistentProgressEligible() const
{
	return m_PersistentProgressEligible;
}

float& GamePersistenceRuntime::DefaultRestHealRatio()
{
	return m_DefaultRestHealRatio;
}

float GamePersistenceRuntime::DefaultRestHealRatio() const
{
	return m_DefaultRestHealRatio;
}

std::vector<std::string>& GamePersistenceRuntime::LastProgressionUnlocks()
{
	return m_LastProgressionUnlocks;
}

const std::vector<std::string>&
GamePersistenceRuntime::LastProgressionUnlocks() const
{
	return m_LastProgressionUnlocks;
}

bool GamePersistenceRuntime::IsRestoringRunSave() const
{
	return m_IsRestoringRunSave;
}

void GamePersistenceRuntime::SetRestoringRunSave(bool restoring)
{
	m_IsRestoringRunSave = restoring;
}

void GamePersistenceRuntime::SetSaveLoadMessage(std::string message)
{
	m_SaveLoadMessage = std::move(message);
	m_SaveLoadMessageFrames = m_SaveLoadMessage.empty() ? 0 : 240;
}

const std::string& GamePersistenceRuntime::SaveLoadMessage() const
{
	return m_SaveLoadMessage;
}
