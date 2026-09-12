#pragma once

#include "DebugBattleSetup.h"
#include "StageLayoutEditor.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class EnemyBall;
class Game;
class PlayerBall;

// Owns debug battles, stage editing, combat forecasts, and their UI state.
class GameDebugController
{
public:
	bool IsActive() const { return m_DebugMode; }
	bool IsEditorOpen() const { return m_DebugEditorOpen; }
	bool IsBattleFinished() const { return m_DebugBattleFinished; }

	void Open(Game& game);
	bool Update(Game& game);
	void Draw(Game& game);
	void DrawDiagnostics(Game& game);
	void End(Game& game);
	void FinishBattle(Game& game, bool victory);
	void ApplyRunSettings(Game& game);
	void ApplyBattlePlayer(PlayerBall* player);
	void ApplyBattleEnemy(EnemyBall* enemy, std::size_t index);
	const std::vector<DirectX::SimpleMath::Vector3>& GetActiveBreakBallPositions() const
	{
		return m_DebugActiveSetup.breakBallPositions;
	}

	void RequestStart() { m_DebugRequest = 1; }
	void RequestExit() { m_DebugRequest = 2; }
	bool LoadPreset();
	void SavePreset();

	void InvalidateCombatForecast(const char* reason);
	void RefreshCombatForecast(Game& game);
	int BeginEnemyAttackForecast(Game& game);
	void EndEnemyAttackForecast(int predictedDamage, int actualDamage);
	void RecordPlayerDamage(
		const std::string& source,
		const std::string& sourceId,
		int damage,
		int hpBefore,
		int hpAfter);
	void ResetDiagnostics(const char* reason);

	float StageEditorPlayerRadius() const;
	const StageLayoutEditor& GetStageEditor() const { return m_StageEditor; }
	StageLayoutEditor& GetStageEditor() { return m_StageEditor; }
	const std::vector<EnemyData>& GetEnemyCatalog() const { return m_DebugEnemyCatalog; }

private:
	struct EnemyCombatSnapshot
	{
		std::string id;
		int currentHp = 0;
		int maxHp = 0;
		int attack = 0;
		int damageBeforeMinimum = 0;
		int expectedDamage = 0;
		bool minimumDamageApplied = false;
		bool defeated = false;
		bool pocketed = false;
		bool canAttack = false;
		std::string state;
	};

	struct CombatForecastSnapshot
	{
		bool hasPlayer = false;
		int playerCurrentHp = 0;
		int playerMaxHp = 0;
		int playerDefense = 0;
		int playerShield = 0;
		int theoreticalDamage = 0;
		int expectedDamage = 0;
		int overkillDamage = 0;
		int attackerCount = 0;
		int hpAfterAttack = 0;
		bool lethal = false;
		std::vector<EnemyCombatSnapshot> enemies;
		std::uint64_t updateRevision = 0;
		std::string updateReason = "initial";
	};

	struct PlayerDamageRecord
	{
		std::uint64_t sequence = 0;
		std::string source;
		std::string sourceId;
		int damage = 0;
		int hpBefore = -1;
		int hpAfter = -1;
	};

	bool StartBattle(Game& game);
	void DrawStageEditor(Game& game);
	void TestStageEditorLayout();

	bool m_DebugMode = false;
	bool m_DebugEditorOpen = false;
	bool m_DebugBattleFinished = false;
	bool m_DebugPreviousAutoPlay = false;
	bool m_DebugPreviousValidation = false;
	bool m_DebugProgressionSnapshotValid = false;
	ProgressionProfile m_DebugPreviousProgressionProfile{};
	int m_DebugRequest = 0;
	DebugBattleSetup m_DebugSetup;
	DebugBattleSetup m_DebugActiveSetup;
	std::vector<PlayerBallData> m_DebugBallCatalog;
	std::vector<EnemyData> m_DebugEnemyCatalog;
	std::vector<StageData> m_DebugStages;
	std::string m_DebugMessage;
	StageLayoutEditor m_StageEditor;

	CombatForecastSnapshot m_DebugCombatForecast{};
	bool m_DebugCombatForecastDirty = true;
	std::string m_DebugCombatForecastPendingReason = "initial";
	std::deque<PlayerDamageRecord> m_DebugPlayerDamageHistory;
	std::uint64_t m_DebugDamageSequence = 0;
	bool m_DebugLastEnemyAttackComparisonValid = false;
	int m_DebugLastEnemyAttackPredictedDamage = 0;
	int m_DebugLastEnemyAttackActualDamage = 0;
	bool m_DebugShowDefeatedEnemies = true;
	bool m_DebugShowPocketedEnemies = true;
	bool m_DebugOnlyAttackers = false;
	int m_DebugEnemySortMode = 0;

	friend class Game;
};
