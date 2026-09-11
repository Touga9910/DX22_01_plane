#include "Game.h"

#include "BalanceLogger.h"
#include "PlayerBall.h"

#include <utility>

// BattleControllerがGame全体へ依存しないよう、
// 必要な処理だけをHookとして接続する。
void Game::InitializeBattleController()
{
	BattleControllerHooks hooks{};

	hooks.restorePocketedPlayer =
		[this]()
		{
			RestorePocketedPlayer();
		};

	hooks.applyEndOfShotEffects =
		[this](PlayerBall* player)
		{
			ApplyEndOfShotRelicEffects(player);
		};

	hooks.endShotLog =
		[](int playerHp, int aliveEnemies, int defeatedEnemies)
		{
			BalanceLogger::GetInstance().EndShot(
				playerHp,
				aliveEnemies,
				defeatedEnemies);
		};

	hooks.finishDynamicBalanceShot =
		[this]()
		{
			m_DynamicBalanceController.OnShotFinished();
		};

	hooks.beginEnemyAttackForecast =
		[this]() -> int
		{
			return m_DebugController.BeginEnemyAttackForecast(*this);
		};

	hooks.endEnemyAttackForecast =
		[this](int predictedDamage, int actualDamage)
		{
			m_DebugController.EndEnemyAttackForecast(
				predictedDamage,
				actualDamage);
		};

	hooks.notifyPlayerDamage =
		[this](
			const std::string& source,
			int damage,
			const std::string& sourceId,
			int hpBefore,
			int hpAfter)
		{
			NotifyPlayerDamage(
				source,
				damage,
				sourceId,
				hpBefore,
				hpAfter);
		};

	hooks.capturePlayerStatus =
		[this](PlayerBall* player)
		{
			CapturePlayerStatusFrom(player);
		};

	hooks.restoreNextPocketedEnemy =
		[this]()
		{
			RestoreNextPocketedEnemy();
		};

	hooks.prepareNextTurn =
		[this]() -> bool
		{
			return PrepareNextPlayerBall();
		};

	hooks.onClearStateRecovered =
		[this](
			const char* source,
			BattleState previousState)
		{
			RecordBalanceEvent(
				"battle_clear_state_recovered",
				{
					{
						"source",
						source != nullptr
							? source
							: "unknown"
					},
					{
						"previous_battle_state",
						ToString(previousState)
					},
				});
		};

	m_BattleController.Initialize(
		m_World,
		std::move(hooks));
}

// PlayerBall側から戦闘状態の変化を通知する。
void Game::NotifyBattleAimDirectionStarted()
{
	m_BattleController.BeginAimingDirection();
}

void Game::NotifyBattlePowerSelectionStarted()
{
	m_BattleController.BeginAimingPower();
}

void Game::NotifyBattleShotConfirmed()
{
	m_BattleController.BeginConfirmShot();
}

void Game::NotifyBattleShotCancelled()
{
	m_BattleController.BeginAimingDirection();
}

void Game::NotifyBattlePlayerDefeated()
{
	m_BattleController.NotifyPlayerDefeated();
}
