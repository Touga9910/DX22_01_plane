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
	/// <summary>
	/// 進行度による敵性能の補正を有効にしているかを返す。
	/// </summary>
	bool IsProgressionScalingEnabled() const { return m_ProgressionScalingEnabled; }
	/// <summary>
	/// 敵のHPに進行補正を加え始める進行度を返す。
	/// </summary>
	int GetProgressionHpStart() const { return m_ProgressionHpStart; }
	/// <summary>
	/// 敵のHP補正が次の段階に上がる進行間隔を返す。
	/// </summary>
	int GetProgressionHpInterval() const { return m_ProgressionHpInterval; }
	/// <summary>
	/// 進行段階が一つ上がるごとの敵HP加算値を返す。
	/// </summary>
	int GetProgressionHpStep() const { return m_ProgressionHpStep; }
	/// <summary>
	/// 進行補正による敵HPの加算上限を返す。
	/// </summary>
	int GetProgressionHpMaximumDelta() const { return m_ProgressionHpMaximumDelta; }
	/// <summary>
	/// 敵の攻撃力に進行補正を加え始める進行度を返す。
	/// </summary>
	int GetProgressionAttackStart() const { return m_ProgressionAttackStart; }
	/// <summary>
	/// 敵の攻撃力補正が次の段階に上がる進行間隔を返す。
	/// </summary>
	int GetProgressionAttackInterval() const { return m_ProgressionAttackInterval; }
	/// <summary>
	/// 進行段階が一つ上がるごとの敵攻撃力加算値を返す。
	/// </summary>
	int GetProgressionAttackStep() const { return m_ProgressionAttackStep; }
	/// <summary>
	/// 進行補正による敵攻撃力の加算上限を返す。
	/// </summary>
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
