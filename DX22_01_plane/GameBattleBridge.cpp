#include "Game.h"

#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameBattleState.h"
#include "GameBattleCommands.h"
#include "GameBattleEvents.h"
#include "GameBattleView.h"
#include "GamePlayerState.h"
#include "GameShotPrediction.h"
#include "GameShotTraceView.h"
#include "ShotRelicRules.h"
#include "BallShotPrediction.h"
#include "BallComponent.h"
#include "BalanceLogger.h"
#include "BattleScene.h"
#include "BattleSceneServices.h"
#include "EnemyBall.h"
#include "GameDebugController.h"
#include "Pocket.h"
#include "PlayerBall.h"
#include "TableFrame.h"

#include <memory>
#include <utility>

BattleState GameBattleView::State()
{
    return Game::GetInstance()->GetBattleState();
}

bool GameBattleView::AcceptsHumanInput()
{
    return !Game::GetInstance()->IsBalanceAutoPlayEnabled();
}

bool GameBattleView::ClearRewardActive()
{
    return Game::GetInstance()->IsClearRewardActive();
}

int GameBattleView::CushionRegionCount()
{
	return 12;
}

bool GameBattleView::IsCushionChargeActive(int region)
{
    if (region < 0 || region >= CushionRegionCount()) return false;
    const auto& charges = Game::GetInstance()->GetCushionCharges();
	return CushionChargeRules::TotalStacks(charges) > 0;
}

int GameBattleView::CurrentShotCollisionAttackBonus()
{
    return Game::GetInstance()->GetCurrentShotCollisionAttackBonus();
}

int GameBattleView::PierceMaximumUses()
{
    return Game::GetInstance()->GetPierceMaximumUses();
}

float GameBattleView::PierceSpeedRetention()
{
    return Game::GetInstance()->GetPierceSpeedRetention();
}

int GameBattleView::ScaleEnemyCollisionDamage(int damage)
{
	return Game::GetInstance()->ScaleEnemyCollisionDamage(damage);
}

const PierceTraceRules::State& GameShotTraceView::Current()
{
    return Game::GetInstance()->GetPierceTraceState();
}

void GameBattleCommands::CompleteCurrentStage()
{
    Game::GetInstance()->CompleteCurrentStage();
}

void GameBattleCommands::BeginAimDirection()
{
    Game::GetInstance()->NotifyBattleAimDirectionStarted();
}

void GameBattleCommands::BeginPowerSelection()
{
    Game::GetInstance()->NotifyBattlePowerSelectionStarted();
}

void GameBattleCommands::CancelShot()
{
    Game::GetInstance()->NotifyBattleShotCancelled();
}

int GameBattleCommands::ConsumeBankShotDamageMultiplier()
{
    return Game::GetInstance()->ConsumeBankShotDamageMultiplier();
}

int GameBattleCommands::ConsumePlayerEnemyRelicDamageBonus()
{
    return Game::GetInstance()->ConsumePlayerEnemyRelicDamageBonus();
}

void GameBattleEvents::PlayerWallCollision(
    int cushionRegion,
    const DirectX::SimpleMath::Vector3& playerPosition,
    DirectX::SimpleMath::Vector3& reflectedVelocity)
{
    Game::GetInstance()->NotifyPlayerWallCollision(
        cushionRegion, playerPosition, reflectedVelocity);
}

int GameBattleEvents::EnemyEnemySynergyCollision(
    EnemyBall* first,
    EnemyBall* second,
    const DirectX::SimpleMath::Vector3& firstVelocityBefore,
    const DirectX::SimpleMath::Vector3& secondVelocityBefore)
{
	return Game::GetInstance()->NotifyEnemyEnemySynergyCollision(
        first, second, firstVelocityBefore, secondVelocityBefore);
}

PlayerEnemySynergyResult GameBattleEvents::PlayerEnemySynergyCollision(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& enemyVelocityBefore)
{
	return Game::GetInstance()->NotifyPlayerEnemySynergyCollision(
		enemy,
		enemyVelocityBefore);
}

void GameBattleEvents::EnemyWallCollision(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& velocityBefore,
	DirectX::SimpleMath::Vector3& velocityAfter)
{
	Game::GetInstance()->NotifyEnemyWallCollision(
		enemy,
		velocityBefore,
		velocityAfter);
}

void GameBattleEvents::PlayerPiercedEnemy(
    EnemyBall* enemy,
    const DirectX::SimpleMath::Vector3& playerPosition,
	const DirectX::SimpleMath::Vector3& direction)
{
    Game::GetInstance()->NotifyPlayerPiercedEnemy(
		enemy, playerPosition, direction);
}

void GameBattleEvents::PlayerDirectionChange(
    const DirectX::SimpleMath::Vector3& playerPosition)
{
    Game::GetInstance()->NotifyPlayerDirectionChange(playerPosition);
}

void GameBattleEvents::PlayerChainImpact(
    EnemyBall* directTarget,
    int attackDamage,
    float radius)
{
    Game::GetInstance()->NotifyPlayerChainImpact(
        directTarget, attackDamage, radius);
}

void GameBattleEvents::PlayerChainImpact(
    const DirectX::SimpleMath::Vector3& center,
    const EnemyBall* excludedTarget,
    int attackDamage,
    float radius)
{
    Game::GetInstance()->NotifyPlayerChainImpact(
        center, excludedTarget, attackDamage, radius);
}

void GameBattleEvents::AnchorStopped(EnemyBall* directTarget)
{
    Game::GetInstance()->NotifyAnchorStopped(directTarget);
}

void GameBattleEvents::DamageBallCollision(
    DamageBallCollisionType collisionType)
{
    Game::GetInstance()->NotifyDamageBallCollision(collisionType);
}

void GameBattleEvents::CombatFeedback(
    const DirectX::SimpleMath::Vector3& worldPosition,
    int damage,
    bool defeated,
    bool enemyEnemyCollision)
{
    Game::GetInstance()->NotifyCombatFeedback(
        worldPosition, damage, defeated, enemyEnemyCollision);
}

void GameBattleEvents::BalanceAutoFullHpEnemySurvived()
{
    Game::GetInstance()->NotifyBalanceAutoFullHpEnemySurvived();
}

void GameBattleEvents::EnemyPocketed(EnemyBall* enemy)
{
    Game::GetInstance()->HandleEnemyPocket(enemy);
}

void GameBattleEvents::EnemyDefeated(EnemyBall* enemy)
{
    Game::GetInstance()->NotifyEnemyDefeated(enemy);
}

void GameBattleEvents::PlayerDefeated()
{
    Game::GetInstance()->NotifyBattlePlayerDefeated();
}

void GameBattleEvents::PlayerShotFired(PlayerBall* player)
{
    Game::GetInstance()->OnPlayerShotFired(player);
}

void GameBattleEvents::PlayerDamage(
    const std::string& source,
    int damage,
    const std::string& enemyId,
    int hpBefore,
    int hpAfter)
{
    Game::GetInstance()->NotifyPlayerDamage(
        source, damage, enemyId, hpBefore, hpAfter);
}

void GameBattleEvents::PlayerPocketFeedback(
    const DirectX::SimpleMath::Vector3& worldPosition,
    int damage)
{
    Game::GetInstance()->NotifyPocketFeedback(
        worldPosition, true, false, damage);
}

void GamePlayerState::ApplyTo(PlayerBall* player)
{
    Game::GetInstance()->ApplyPlayerStatusTo(player);
}

void GamePlayerState::CaptureFrom(const PlayerBall* player)
{
    Game::GetInstance()->CapturePlayerStatusFrom(player);
}

int GamePlayerState::PocketDamageAmount()
{
    return Game::GetInstance()->GetPlayerPocketDamageAmount();
}

int GamePlayerState::AbsorbShieldDamage(int damage)
{
    return Game::GetInstance()->AbsorbPlayerShieldDamage(damage);
}

namespace
{
BallShotPrediction::Input MakeShotPredictionInput(float launchPower)
{
    Game& game = *Game::GetInstance();
    BallShotPrediction::Input input;
    input.balls = GameRuntime::World().GetComponents<BallComponent>();
    input.tableFrames = GameRuntime::World().GetComponents<TableFrame>();
    input.pockets = GameRuntime::World().GetComponents<Pocket>();
    input.cushionCharges = game.GetCushionCharges();
    input.pierceTraces = game.GetPierceTraceState();
    input.shot = game.MakePredictionShotRules(launchPower);
    if (const PlayerBallData* currentBall = game.GetCurrentPlayerBallData())
    {
        input.hasCurrentBall = true;
        input.currentBallCategory = static_cast<int>(currentBall->category);
    }
    input.pocketFinisherRatio = game.GetCurrentPocketFinisherRatio();
    input.playerPocketDamage = game.GetPlayerPocketDamageAmount();
    input.pierceMaximumUses = game.GetPierceMaximumUses();
    input.pierceSpeedRetention = game.GetPierceSpeedRetention();
    input.hasPierceBallCharger =
        game.HasRelic(RelicType::PierceBallCharger);
    input.relicAttackBonus = game.GetRelicAttackBonus();
	input.heavyCollisions = game.GetHeavyCollisionState();
    input.playerAnchorStacks = game.GetPlayerAnchorStacks();
	input.anchorConversionProgress = game.GetAnchorConversionProgress();
    input.cushionBoostConsumed = game.WasCushionBoostConsumedThisShot();
    input.playerEnemyContacts =
        game.GetCurrentShotPlayerEnemyCollisionCount();
    input.enemyEnemyContacts =
        game.GetCurrentShotEnemyEnemyCollisionCount();
    return input;
}
}

BallShotPrediction::Result GameShotPrediction::Predict(
    const PlayerBall& player,
    const DirectX::SimpleMath::Vector3& velocity,
    bool skipFirstPlayerFriction,
    const PlayerBallData* offer)
{
    const auto input = MakeShotPredictionInput(velocity.Length());
    return BallShotPrediction::Predict(
        input, player, velocity, skipFirstPlayerFriction, offer);
}

std::uint64_t GameShotPrediction::WorldKey()
{
    return BallShotPrediction::WorldKey(MakeShotPredictionInput(0.0f));
}

void GameShotPrediction::WriteVerification(
    const PlayerBall& player,
    const DirectX::SimpleMath::Vector3& velocity,
    bool skipFirstPlayerFriction)
{
    const auto input = MakeShotPredictionInput(velocity.Length());
    BallShotPrediction::WriteVerificationPrediction(
        input, player, velocity, skipFirstPlayerFriction);
}

void GameShotPrediction::WriteVerificationActual()
{
    BallShotPrediction::WriteVerificationActual(
        MakeShotPredictionInput(0.0f));
}

BattleState Game::GetBattleState() const
{
    return m_BattleState->controller.GetState();
}

bool Game::AreAllBallsStopped() const
{
    return m_BattleState->controller.AreAllBallsStopped();
}

const CushionChargeRules::State& Game::GetCushionCharges() const
{
    return m_BattleState->cushionCharges;
}

bool Game::WasCushionBoostConsumedThisShot() const
{
    return m_BattleState->cushionBoostConsumedThisShot;
}

const PierceTraceRules::State& Game::GetPierceTraceState() const
{
    return m_BattleState->pierceTraces;
}

int Game::GetHeavyCollisionCount() const
{
	return m_BattleState->heavyCollisions.stackCount;
}

const HeavyCollisionRules::State& Game::GetHeavyCollisionState() const
{
	return m_BattleState->heavyCollisions;
}

int Game::GetPlayerAnchorStacks() const
{
    return m_BattleState->anchorStacks.playerStacks;
}

int Game::GetAnchorConversionProgress() const
{
	return m_BattleState->anchorStacks.conversionProgress;
}

int Game::GetCushionStrongUsesThisShot() const
{
    return m_BattleState->cushionStrongUsesThisShot;
}

int Game::GetPlayerShield() const
{
    return m_BattleState->playerShield;
}

int Game::GetCurrentShotCollisionAttackBonus() const
{
    return m_BattleState->controller.GetShotRelicRules().collisionBonus;
}

int Game::GetCurrentShotPlayerEnemyCollisionCount() const
{
    return m_BattleState->controller.GetShotRelicRules().playerEnemyContacts;
}

int Game::GetCurrentShotEnemyEnemyCollisionCount() const
{
    return m_BattleState->controller.GetShotRelicRules().enemyEnemyContacts;
}

int Game::GetCurrentShotBallCollisionCount() const
{
    const auto& rules = m_BattleState->controller.GetShotRelicRules();
    return rules.playerEnemyContacts + rules.enemyEnemyContacts;
}

bool Game::IsCurrentShotBankShotReady() const
{
    const auto& rules = m_BattleState->controller.GetShotRelicRules();
    return rules.bankReady && !rules.bankConsumed;
}

namespace
{
class GameBattleSceneServices final : public BattleSceneServices
{
public:
	explicit GameBattleSceneServices(Game& game)
		: m_Game(game)
	{
	}

	Game& GameContext() const override
	{
		return m_Game;
	}

	const StageData* CurrentStageOverride() const override
	{
		return m_Game.GetCurrentStageOverride();
	}

	const std::string& SelectedStageId() const override
	{
		return m_Game.GetSelectedStageId();
	}

	void ApplyEnemyDifficultyScaling(EnemyData& enemyData) const override
	{
		m_Game.ApplyEnemyDifficultyScaling(enemyData);
	}

	void ApplyDebugPlayer(PlayerBall* player) const override
	{
		m_Game.ApplyDebugBattlePlayer(player);
	}

	void ApplyDebugEnemy(
		EnemyBall* enemy,
		std::size_t index) const override
	{
		m_Game.ApplyDebugBattleEnemy(enemy, index);
	}

	void NotifyStageStarted(const StageData& stage) const override
	{
		m_Game.OnBattleStageStarted(stage);
	}

	bool IsDebugMode() const override
	{
		return m_Game.IsDebugMode();
	}

	const std::vector<DirectX::SimpleMath::Vector3>&
	DebugBreakBallPositions() const override
	{
		return m_Game.GetDebugBreakBallPositions();
	}

	std::vector<EnemyBall*> Enemies() const override
	{
		return GameRuntime::World().GetComponents<EnemyBall>();
	}

	void InvalidateCombatForecast(const char* reason) const override
	{
		m_Game.InvalidateDebugCombatForecast(reason);
	}

private:
	Game& m_Game;
};
}

void Game::InitializeBattleSceneFactory()
{
	m_RuntimeState->SetBattleSceneFactory(
		[this]() -> std::unique_ptr<Scene>
		{
			auto services =
				std::make_unique<GameBattleSceneServices>(*this);
			return std::make_unique<BattleScene>(
				std::move(services));
		});
}

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

	hooks.beginEnemyAttackForecast =
		[this]() -> int
		{
			return m_DebugController->BeginEnemyAttackForecast(*this);
		};

	hooks.endEnemyAttackForecast =
		[this](int predictedDamage, int actualDamage)
		{
			m_DebugController->EndEnemyAttackForecast(
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

	m_BattleState->controller.Initialize(
		m_RuntimeState->World(),
		std::move(hooks));
}

// PlayerBall側から戦闘状態の変化を通知する。
void Game::NotifyBattleAimDirectionStarted()
{
	m_BattleState->controller.BeginAimingDirection();
}

void Game::NotifyBattlePowerSelectionStarted()
{
	m_BattleState->controller.BeginAimingPower();
}

void Game::NotifyBattleShotCancelled()
{
	m_BattleState->controller.BeginAimingDirection();
}

void Game::NotifyBattlePlayerDefeated()
{
	m_BattleState->controller.NotifyPlayerDefeated();
}
