#pragma once

#include <string>

struct EnemyData;

// DDAの設定、戦闘計測、次戦補正を所有する。
class DynamicBalanceController final
{
public:
	void LoadConfig(
		const std::string& filePath =
			"assets/data/dynamic_balance.json");
	void OnRunStarted();
	void OnBattleStarted(int enemyCount);
	void OnShotStarted();
	void OnHit();
	void OnShotFinished();
	void OnBattleFinished(bool cleared, int currentHp, int maxHp);

	void ApplyToEnemyData(
		EnemyData& enemyData,
		float baselineHpMultiplier,
		int baselineAttackDelta,
		int progress,
		int ascension,
		bool debugMode) const;

	void Set(
		bool enabled,
		bool resetLevel,
		int requestedLevel,
		bool hasRequestedLevel,
		bool lockedOffByValidation);
	void ForceDisabled();
	void RestoreRunState(
		bool enabled,
		int level,
		bool appliedEnabled,
		int appliedLevel,
		const std::string& lastResult,
		const std::string& lastReason);

	bool IsConfiguredEnabled() const { return m_ConfiguredEnabled; }
	bool IsEnabled() const { return m_Enabled; }
	bool IsAppliedEnabled() const { return m_AppliedEnabled; }
	bool IsStageActive() const { return m_StageActive; }
	int GetLevel() const { return m_Level; }
	int GetAppliedLevel() const { return m_AppliedLevel; }
	int GetMinimumLevel() const { return m_MinimumLevel; }
	int GetMaximumLevel() const { return m_MaximumLevel; }
	int GetHpStep() const { return m_HpStep; }
	int GetStageShots() const { return m_StageShots; }
	int GetStageNoHitShots() const { return m_StageNoHitShots; }
	int GetStageEnemyCount() const { return m_StageEnemyCount; }
	int GetLastLevelChange() const { return m_LastLevelChange; }
	float GetLastHpRatio() const { return m_LastHpRatio; }
	float GetLastNoHitRate() const { return m_LastNoHitRate; }
	float GetLastShotsPerEnemy() const { return m_LastShotsPerEnemy; }
	const std::string& GetLastResult() const { return m_LastResult; }
	const std::string& GetLastReason() const { return m_LastReason; }

	int CalculateAttackModifier(int level) const;
	int CalculateProgressionHpModifier(int progress) const;
	int CalculateProgressionAttackModifier(int progress) const;
	bool IsProgressionScalingEnabled() const { return m_ProgressionScalingEnabled; }
	int GetProgressionHpStart() const { return m_ProgressionHpStart; }
	int GetProgressionHpInterval() const { return m_ProgressionHpInterval; }
	int GetProgressionHpStep() const { return m_ProgressionHpStep; }
	int GetProgressionHpMaximumDelta() const { return m_ProgressionHpMaximumDelta; }
	int GetProgressionAttackStart() const { return m_ProgressionAttackStart; }
	int GetProgressionAttackInterval() const { return m_ProgressionAttackInterval; }
	int GetProgressionAttackStep() const { return m_ProgressionAttackStep; }
	int GetProgressionAttackMaximumDelta() const { return m_ProgressionAttackMaximumDelta; }

private:
	bool m_ConfiguredEnabled = true;
	bool m_Enabled = true;
	bool m_AppliedEnabled = true;
	int m_InitialLevel = 0;
	int m_Level = 0;
	int m_AppliedLevel = 0;
	int m_MinimumLevel = -3;
	int m_MaximumLevel = 3;
	int m_HpStep = 1;
	int m_AttackStep = 1;
	int m_LevelsPerAttackStep = 2;
	bool m_PositiveAttackScalingEnabled = false;
	int m_MinimumEnemyHp = 1;
	int m_MaximumEnemyHp = 100;
	int m_MinimumEnemyAttack = 0;
	int m_MaximumEnemyAttack = 50;
	float m_StrongHpRatio = 0.70f;
	float m_WeakHpRatio = 0.30f;
	float m_StrongNoHitRate = 0.20f;
	float m_WeakNoHitRate = 0.50f;
	float m_TargetShotsPerEnemy = 3.0f;
	float m_WeakShotMultiplier = 1.5f;
	bool m_StageActive = false;
	bool m_ShotActive = false;
	bool m_CurrentShotHit = false;
	int m_StageShots = 0;
	int m_StageNoHitShots = 0;
	int m_StageEnemyCount = 0;
	int m_LastLevelChange = 0;
	float m_LastHpRatio = 1.0f;
	float m_LastNoHitRate = 0.0f;
	float m_LastShotsPerEnemy = 0.0f;
	std::string m_LastResult = "not_evaluated";
	std::string m_LastReason = "No battle has been evaluated.";

	bool m_ProgressionScalingEnabled = true;
	int m_ProgressionHpStart = 10;
	int m_ProgressionHpInterval = 5;
	int m_ProgressionHpStep = 1;
	int m_ProgressionHpMaximumDelta = 4;
	int m_ProgressionAttackStart = 20;
	int m_ProgressionAttackInterval = 5;
	int m_ProgressionAttackStep = 1;
	int m_ProgressionAttackMaximumDelta = 8;
};
