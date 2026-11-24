#include "GamePersistenceRuntime.h"

#include <utility>

/// <summary>
/// ラン中の経過フレームと、セーブやロードに関する通知の表示期限を進める。
/// </summary>
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

/// <summary>
/// 現在のランの統計を管理するオブジェクトを更新可能な参照で返す。
/// </summary>
RunStatisticsTracker& GamePersistenceRuntime::Statistics()
{
	return m_Statistics;
}

/// <summary>
/// 現在のランの統計を管理するオブジェクトを読み取り専用の参照で返す。
/// </summary>
const RunStatisticsTracker& GamePersistenceRuntime::Statistics() const
{
	return m_Statistics;
}

/// <summary>
/// 恒久進行のプロフィールを更新可能な参照で返す。
/// </summary>
ProgressionProfile& GamePersistenceRuntime::Profile()
{
	return m_Profile;
}

/// <summary>
/// 恒久進行のプロフィールを読み取り専用の参照で返す。
/// </summary>
const ProgressionProfile& GamePersistenceRuntime::Profile() const
{
	return m_Profile;
}

/// <summary>
/// 現在の昇格レベルを更新可能な参照で返す。
/// </summary>
int& GamePersistenceRuntime::ActiveAscension()
{
	return m_ActiveAscension;
}

/// <summary>
/// 現在の昇格レベルを返す。
/// </summary>
int GamePersistenceRuntime::ActiveAscension() const
{
	return m_ActiveAscension;
}

/// <summary>
/// 現在のランを恒久進行に反映できるかを示すフラグを更新可能な参照で返す。
/// </summary>
bool& GamePersistenceRuntime::PersistentProgressEligible()
{
	return m_PersistentProgressEligible;
}

/// <summary>
/// 現在のランを恒久進行に反映できるかを返す。
/// </summary>
bool GamePersistenceRuntime::PersistentProgressEligible() const
{
	return m_PersistentProgressEligible;
}

/// <summary>
/// 休憩時の既定の回復割合を更新可能な参照で返す。
/// </summary>
float& GamePersistenceRuntime::DefaultRestHealRatio()
{
	return m_DefaultRestHealRatio;
}

/// <summary>
/// 休憩時の既定の回復割合を返す。
/// </summary>
float GamePersistenceRuntime::DefaultRestHealRatio() const
{
	return m_DefaultRestHealRatio;
}

/// <summary>
/// 直近の恒久進行で解放された項目一覧を更新可能な参照で返す。
/// </summary>
std::vector<std::string>& GamePersistenceRuntime::LastProgressionUnlocks()
{
	return m_LastProgressionUnlocks;
}

/// <summary>
/// 直近の恒久進行で解放された項目一覧を読み取り専用の参照で返す。
/// </summary>
const std::vector<std::string>&
GamePersistenceRuntime::LastProgressionUnlocks() const
{
	return m_LastProgressionUnlocks;
}

/// <summary>
/// セーブからランを復元中かを返す。
/// </summary>
bool GamePersistenceRuntime::IsRestoringRunSave() const
{
	return m_IsRestoringRunSave;
}

/// <summary>
/// セーブからランを復元中かを示すフラグを設定する。
/// </summary>
void GamePersistenceRuntime::SetRestoringRunSave(bool restoring)
{
	m_IsRestoringRunSave = restoring;
}

/// <summary>
/// セーブやロードに関する通知と、残りの表示フレーム数を設定する。
/// </summary>
void GamePersistenceRuntime::SetSaveLoadMessage(std::string message)
{
	m_SaveLoadMessage = std::move(message);
	m_SaveLoadMessageFrames = m_SaveLoadMessage.empty() ? 0 : 240;
}

/// <summary>
/// 現在表示するセーブやロードの通知を返す。
/// </summary>
const std::string& GamePersistenceRuntime::SaveLoadMessage() const
{
	return m_SaveLoadMessage;
}
