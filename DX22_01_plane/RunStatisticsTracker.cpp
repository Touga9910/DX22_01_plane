#include "RunStatisticsTracker.h"

#include <algorithm>

/// <summary>
/// ランの集計データを初期状態に戻す。
/// </summary>
void RunStatisticsTracker::Reset()
{
	m_State = RunResultSnapshot{};
}

/// <summary>
/// ラン中の有効な経過フレームを1増やす。
/// </summary>
void RunStatisticsTracker::AdvanceFrame()
{
	++m_State.activeFrames;
}

/// <summary>
/// ラン中に到達した最高階層を更新する。
/// </summary>
void RunStatisticsTracker::ReachFloor(int floor)
{
	m_State.reachedFloor = (std::max)(m_State.reachedFloor, floor);
}

/// <summary>
/// クリア済みエリアの進行度と到達階層を集計データに記録する。
/// </summary>
void RunStatisticsTracker::CompleteArea(int areaProgress)
{
	m_State.areaProgress = (std::max)(m_State.areaProgress, areaProgress);
	m_State.reachedFloor = (std::max)(
		m_State.reachedFloor,
		(std::max)(1, m_State.areaProgress));
}

/// <summary>
/// 総戦闘数を増やし、中ボスや最終ボスへの挑戦状況を記録する。
/// </summary>
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

/// <summary>
/// 中ボスの撃破回数を1増やす。
/// </summary>
void RunStatisticsTracker::DefeatMidBoss()
{
	++m_State.midBossDefeats;
}

/// <summary>
/// 最終ボスへの到達と撃破を記録する。
/// </summary>
void RunStatisticsTracker::DefeatFinalBoss()
{
	m_State.finalBossReached = true;
	m_State.finalBossDefeated = true;
}

/// <summary>
/// ターン内の与ダメージをリセットし、ショット回数と使用球を記録する。
/// </summary>
void RunStatisticsTracker::BeginShot(const std::string& ballId)
{
	m_State.currentTurnDamage = 0;
	++m_State.totalShots;
	if (!ballId.empty())
	{
		++m_State.ballShotCounts[ballId];
	}
}

/// <summary>
/// 敵に与えたダメージを、ランと今回のショットの集計に加える。
/// </summary>
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

/// <summary>
/// プレイヤーが受けたダメージを累計へ加える。
/// </summary>
void RunStatisticsTracker::RecordPlayerDamage(int damage)
{
	m_State.damageTaken += (std::max)(0, damage);
}

/// <summary>
/// 取得した球に有効なIDがあればランの記録に追加する。
/// </summary>
void RunStatisticsTracker::RecordBallAcquired(const std::string& ballId)
{
	if (!ballId.empty())
	{
		m_State.acquiredBallIds.push_back(ballId);
	}
}

/// <summary>
/// 保存されたランの統計を復元し、進行中の状態に戻す。
/// </summary>
void RunStatisticsTracker::Restore(const RunResultSnapshot& state)
{
	m_State = state;
	m_State.completed = false;
	m_State.acquiredRelics.clear();
}

/// <summary>
/// 現在のランの統計に終了時点の状態を反映したリザルトデータを返す。
/// </summary>
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
