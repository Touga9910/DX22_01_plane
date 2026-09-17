#pragma once

#include <string>

struct EnemyData;

// 基準難易度、固定の進行度スケーリング、アセンション補正を敵に適用する。
// クラス名は旧DDA実装由来だが、プレイ結果に応じた難易度変更は保持しない。
class DynamicBalanceController final
{
public:
	void LoadConfig(
		const std::string& filePath =
			"assets/data/dynamic_balance.json");

	void ApplyToEnemyData(
		EnemyData& enemyData,
		float baselineHpMultiplier,
		int baselineAttackDelta,
		int progress,
		int ascension,
		bool debugMode) const;

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
	int m_MinimumEnemyHp = 1;
	int m_MaximumEnemyHp = 100;
	int m_MinimumEnemyAttack = 0;
	int m_MaximumEnemyAttack = 50;

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
