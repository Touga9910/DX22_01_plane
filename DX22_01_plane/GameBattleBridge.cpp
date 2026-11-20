#include "Game.h"
#include "GamePresentation.h"
#include "AudioFeedback.h"

#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "GameBattleState.h"
#include "GameBattleCommands.h"
#include "GameBattleEvents.h"
#include "GameBattleView.h"
#include "GamePlayerState.h"
#include "GameShotPrediction.h"
#include "GameShotTraceView.h"
#include "ShotRelicRules.h"
#include "CushionChargeRules.h"
#include "BallShotPrediction.h"
#include "BallComponent.h"
#include "BallFactory.h"
#include "BossPocketRules.h"
#include "BalanceLogger.h"
#include "BattleScene.h"
#include "BattleSceneServices.h"
#include "EnemyBall.h"
#include "GameDebugController.h"
#include "Pocket.h"
#include "PlayerBall.h"
#include "TableFrame.h"
#include "TableConfig.h"
#include "TableGeometry.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <utility>

BattleState GameBattleView::State()
{
    return Game::GetInstance()->GetBattleState();
}

bool GameBattleView::AcceptsHumanInput()
{
	Game* game = Game::GetInstance();
	return !game->IsBalanceAutoPlayEnabled() &&
		game->IsShotPreparationInputEnabled();
}

bool GameBattleView::AcceptsDebugMovement()
{
	const Game* game = Game::GetInstance();
	return game->IsDebugMode() && !game->IsBalanceAutoPlayEnabled();
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

int GameBattleView::CurrentShotImpactAttack()
{
    const auto* ball = Game::GetInstance()->GetCurrentPlayerBallData();
    return ball != nullptr ? ball->status.impactAttack : 1;
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

bool GameBattleView::IsCurrentShotPierceCategory()
{
    const PlayerBallData* ball =
        Game::GetInstance()->GetCurrentPlayerBallData();
    return ball != nullptr && ball->category == BallCategory::Pierce;
}

bool GameBattleView::IsCurrentShotBounceCategory()
{
	const PlayerBallData* ball =
		Game::GetInstance()->GetCurrentPlayerBallData();
	return ball != nullptr && ball->category == BallCategory::Bounce;
}

bool GameBattleView::IsPinballTarget(std::uintptr_t targetId)
{
	return CushionChargeRules::IsPinballTarget(
		Game::GetInstance()->GetCushionCharges(),
		targetId);
}

EnemyActionConfig Game::GetEnemyActionConfig() const
{
    return m_BattleState != nullptr
        ? m_BattleState->controller.GetEnemyActionConfig()
        : EnemyActionConfig{};
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

void GameBattleEvents::WallImpact(
	const DirectX::SimpleMath::Vector3& worldPosition,
	float relativeApproachSpeed,
	const void* target)
{
	Game::GetInstance()->NotifyWallImpact(
		worldPosition,
		relativeApproachSpeed,
		target);
}

void GameBattleEvents::EnemyAbilityStateChanged(
	const EnemyAbilityStateChangedEvent& event)
{
	Game::GetInstance()->NotifyEnemyAbilityStateChanged(event);
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
	bool enemyEnemyCollision,
	const EnemyBall* target)
{
    Game::GetInstance()->NotifyCombatFeedback(
		worldPosition, damage, defeated, enemyEnemyCollision, target);
}

void GameBattleEvents::CollisionImpact(
    const DirectX::SimpleMath::Vector3& worldPosition,
    const DirectX::SimpleMath::Vector3& firstTargetPosition,
    const DirectX::SimpleMath::Vector3& secondTargetPosition,
    float relativeApproachSpeed,
    CombatCollisionKind collisionKind,
    int damage,
    bool defeated,
    const EnemyBall* firstTarget,
    const EnemyBall* secondTarget)
{
    Game::GetInstance()->NotifyCollisionImpact(
        worldPosition,
        firstTargetPosition,
        secondTargetPosition,
        relativeApproachSpeed,
        collisionKind,
        damage,
        defeated,
        firstTarget,
        secondTarget);
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
BallTrajectoryPreview::Input MakeTrajectoryPreviewInput(
	const PlayerBall& player)
{
	Game& game = *Game::GetInstance();
	return BallTrajectoryPreview::Capture(
		GameRuntime::World(),
		*player.GetBall(),
		game.GetPierceMaximumUses(),
		game.GetPierceSpeedRetention());
}

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

BallTrajectoryPreview::Result GameShotPrediction::Preview(
	const PlayerBall& player,
	const DirectX::SimpleMath::Vector3& velocity,
	bool skipFirstPlayerFriction)
{
	return BallTrajectoryPreview::Predict(
		MakeTrajectoryPreviewInput(player),
		velocity,
		skipFirstPlayerFriction);
}

std::uint64_t GameShotPrediction::PreviewWorldKey()
{
	Game& game = *Game::GetInstance();
	return BallTrajectoryPreview::WorldKey(
		GameRuntime::World(),
		game.GetPierceMaximumUses(),
		game.GetPierceSpeedRetention());
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

const BattleController* Game::GetBattleControllerDiagnostics() const
{
    return m_BattleState != nullptr ? &m_BattleState->controller : nullptr;
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

const AnchorStackRules::State& Game::GetAnchorStackState() const
{
	return m_BattleState->anchorStacks;
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

	std::vector<EnemyBall*> SpawnInitialBossMinions() const override
	{
		return m_Game.SpawnBossMinions(BossEncounterRules::InitialMinions, true);
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
    hooks.playerShield = [this]() { return GetPlayerShield(); };
    hooks.enemyAttackLaunched = [this](const EnemyBall& enemy, const PlayerBall& player)
    {
        if (m_GamePresentation && !IsBalanceAutoPlayEnabled())
            m_GamePresentation->OnEnemyAttackLaunched(enemy.GetPosition(), player.GetPosition());
    };
    hooks.enemyAttackImpact = [this](int hpDamage, int absorbed)
    {
        if (m_GamePresentation && !IsBalanceAutoPlayEnabled())
            m_GamePresentation->OnEnemyAttackImpact(hpDamage, absorbed);
        if (m_AudioFeedback) m_AudioFeedback->OnPlayerImpact(hpDamage, absorbed);
    };

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

    hooks.spawnBossMinions = [this](int count, bool advance)
    {
        const auto spawned = SpawnBossMinions(count,false,advance);
        if (auto* scene = dynamic_cast<BattleScene*>(GetCurrentScene()))
            for (auto* enemy : spawned) scene->TrackSpawnedObject(enemy->GetGameObject());
        RecordBalanceEvent("boss_minions_summoned", {{"requested",count},{"spawned",spawned.size()},{"advance",advance}});
    };
	hooks.spawnPhaseEnemies =
		[this](
			const std::vector<EnemySpawnData>& spawns,
			int defaultInterval,
			unsigned int spawnSeed,
			int phaseNumber)
		{
			RecordBalanceEvent("phase_ended", {
				{ "phase", phaseNumber - 1 },
				{ "total_phases",
					m_BattleState->controller.GetTotalPhases() },
				{ "reason", "reinforcement_advance" },
				{ "living_enemy_count_before_reinforcement",
					m_BattleState->controller.GetLivingEnemyCount() },
			});
			std::mt19937 random(spawnSeed +
				static_cast<unsigned int>(phaseNumber));
			using Vector3 = DirectX::SimpleMath::Vector3;
			using PendingBall = std::pair<Vector3, float>;
			auto isClear = [this](
				const EnemySpawnData& spawn,
				const Vector3& candidate,
				const std::vector<PendingBall>& pending)
			{
				const float radius = (std::max)(
					0.01f, spawn.enemyData.status.radius);
				const float halfWidth =
					TableConfig::GetFieldWidth() * 0.5f - radius - 1.0f;
				const float halfDepth =
					TableConfig::GetFieldDepth() * 0.5f - radius - 1.0f;
				if (std::abs(candidate.x) > halfWidth ||
					std::abs(candidate.z) > halfDepth)
				{
					return false;
				}
				for (BallComponent* ball :
					m_RuntimeState->World().GetComponents<BallComponent>())
				{
					if (ball == nullptr || ball->GetGameObject() == nullptr ||
						!ball->GetGameObject()->IsActive()) continue;
					Vector3 delta = candidate - ball->GetPosition();
					delta.y = 0.0f;
					const float clearance =
						radius + ball->GetRadius() + 1.0f;
					if (delta.LengthSquared() < clearance * clearance)
						return false;
				}
				for (const auto& pocket : TableConfig::GetPocketCenters())
				{
					Vector3 delta = candidate - pocket;
					delta.y = 0.0f;
					const float clearance = radius + 5.0f;
					if (delta.LengthSquared() < clearance * clearance)
						return false;
				}
				for (const PendingBall& other : pending)
				{
					Vector3 delta = candidate - other.first;
					delta.y = 0.0f;
					const float clearance = radius + other.second + 1.0f;
					if (delta.LengthSquared() < clearance * clearance)
						return false;
				}
				return true;
			};
			auto resolveIndividualPosition = [&](const EnemySpawnData& spawn)
			{
				const float radius = (std::max)(
					0.01f, spawn.enemyData.status.radius);
				const float halfWidth =
					TableConfig::GetFieldWidth() * 0.5f - radius - 1.0f;
				const float halfDepth =
					TableConfig::GetFieldDepth() * 0.5f - radius - 1.0f;
				std::uniform_real_distribution<float> xDistribution(
					-halfWidth, halfWidth);
				std::uniform_real_distribution<float> zDistribution(
					-halfDepth, halfDepth);
				Vector3 position = spawn.position;
				position.y = TableConfig::FIELD_HEIGHT;
				const std::vector<PendingBall> none;
				for (int attempt = 0;
					attempt < 64 && !isClear(spawn, position, none); ++attempt)
				{
					position = Vector3(
						xDistribution(random), TableConfig::FIELD_HEIGHT,
						zDistribution(random));
				}
				return position;
			};
			auto spawnAt = [this, defaultInterval](
				const EnemySpawnData& spawn, const Vector3& position)
			{
				EnemyData data = spawn.enemyData;
				data.initPosition = position;
				EnemyBall* enemy = BallFactory::CreateEnemy(*this, data);
				if (BattleScene* scene =
					dynamic_cast<BattleScene*>(GetCurrentScene()))
				{
					scene->TrackSpawnedObject(enemy->GetGameObject());
				}
				enemy->InitializeAttackTimer(
					spawn.attackInterval > 0
						? spawn.attackInterval
						: defaultInterval);
			};

			std::vector<bool> handled(spawns.size(), false);
			for (std::size_t spawnIndex = 0;
				spawnIndex < spawns.size(); ++spawnIndex)
			{
				if (handled[spawnIndex]) continue;
				const EnemySpawnData& first = spawns[spawnIndex];
				if (first.spawnGroup <= 0)
				{
					spawnAt(first, resolveIndividualPosition(first));
					handled[spawnIndex] = true;
					continue;
				}

				std::vector<std::size_t> groupIndices;
				for (std::size_t candidateIndex = spawnIndex;
					candidateIndex < spawns.size(); ++candidateIndex)
				{
					if (!handled[candidateIndex] &&
						spawns[candidateIndex].spawnGroup == first.spawnGroup)
					{
						groupIndices.push_back(candidateIndex);
					}
				}

				float minimumOffsetX = -TableConfig::GetFieldWidth();
				float maximumOffsetX = TableConfig::GetFieldWidth();
				float minimumOffsetZ = -TableConfig::GetFieldDepth();
				float maximumOffsetZ = TableConfig::GetFieldDepth();
				for (std::size_t index : groupIndices)
				{
					const EnemySpawnData& spawn = spawns[index];
					const float radius = (std::max)(
						0.01f, spawn.enemyData.status.radius);
					const float halfWidth = TableConfig::GetFieldWidth() * 0.5f -
						radius - 1.0f;
					const float halfDepth = TableConfig::GetFieldDepth() * 0.5f -
						radius - 1.0f;
					minimumOffsetX = (std::max)(
						minimumOffsetX, -halfWidth - spawn.position.x);
					maximumOffsetX = (std::min)(
						maximumOffsetX, halfWidth - spawn.position.x);
					minimumOffsetZ = (std::max)(
						minimumOffsetZ, -halfDepth - spawn.position.z);
					maximumOffsetZ = (std::min)(
						maximumOffsetZ, halfDepth - spawn.position.z);
				}

				std::vector<Vector3> resolvedGroup;
				auto tryGroupOffset = [&](float offsetX, float offsetZ)
				{
					std::vector<PendingBall> pending;
					std::vector<Vector3> candidates;
					for (std::size_t index : groupIndices)
					{
						const EnemySpawnData& spawn = spawns[index];
						Vector3 candidate = spawn.position +
							Vector3(offsetX, 0.0f, offsetZ);
						candidate.y = TableConfig::FIELD_HEIGHT;
						if (!isClear(spawn, candidate, pending)) return false;
						candidates.push_back(candidate);
						pending.emplace_back(candidate, (std::max)(
							0.01f, spawn.enemyData.status.radius));
					}
					resolvedGroup = std::move(candidates);
					return true;
				};

				bool groupPlaced = tryGroupOffset(0.0f, 0.0f);
				constexpr float kPi = 3.14159265358979323846f;
				for (int attempt = 1; attempt <= 48 && !groupPlaced; ++attempt)
				{
					const int ring = (attempt + 7) / 8;
					const float angle = static_cast<float>((attempt - 1) % 8) *
						(kPi * 0.25f);
					const float distance = static_cast<float>(ring) * 3.0f;
					const float offsetX = std::cos(angle) * distance;
					const float offsetZ = std::sin(angle) * distance;
					if (offsetX < minimumOffsetX || offsetX > maximumOffsetX ||
						offsetZ < minimumOffsetZ || offsetZ > maximumOffsetZ)
					{
						continue;
					}
					groupPlaced = tryGroupOffset(offsetX, offsetZ);
				}
				if (!groupPlaced && minimumOffsetX <= maximumOffsetX &&
					minimumOffsetZ <= maximumOffsetZ)
				{
					std::uniform_real_distribution<float> xOffsetDistribution(
						minimumOffsetX, maximumOffsetX);
					std::uniform_real_distribution<float> zOffsetDistribution(
						minimumOffsetZ, maximumOffsetZ);
					for (int attempt = 0; attempt < 128 && !groupPlaced; ++attempt)
					{
						groupPlaced = tryGroupOffset(
							xOffsetDistribution(random), zOffsetDistribution(random));
					}
				}

				for (std::size_t groupIndex = 0;
					groupIndex < groupIndices.size(); ++groupIndex)
				{
					const std::size_t index = groupIndices[groupIndex];
					spawnAt(spawns[index], groupPlaced
						? resolvedGroup[groupIndex]
						: resolveIndividualPosition(spawns[index]));
					handled[index] = true;
				}
			}
			for (EnemyBall* enemy :
				m_RuntimeState->World().GetComponents<EnemyBall>())
			{
				if (enemy != nullptr && enemy->CanParticipateInCombat())
				{
					enemy->RefreshBoardAbilityState(true);
				}
			}
			InvalidateDebugCombatForecast("フェーズ増援生成");
			RecordBalanceEvent("phase_transition", {
				{ "phase", phaseNumber },
				{ "total_phases", m_BattleState->controller.GetTotalPhases() },
				{ "spawned_enemy_count", static_cast<int>(spawns.size()) },
				{ "living_enemy_count",
					m_BattleState->controller.GetLivingEnemyCount() },
				{ "spawn_seed", spawnSeed },
			});
			RecordBalanceEvent("phase_started", {
				{ "phase", phaseNumber },
				{ "total_phases",
					m_BattleState->controller.GetTotalPhases() },
				{ "spawned_enemy_count", static_cast<int>(spawns.size()) },
				{ "living_enemy_count",
					m_BattleState->controller.GetLivingEnemyCount() },
			});
		};

	hooks.enemyActionPlanConfirmed =
		[this](const EnemyActionTurnPlan& plan)
		{
			nlohmann::json actions = nlohmann::json::array();
			int normalCount = 0;
			int chargeStartCount = 0;
			int chargeAttackCount = 0;
			for (const PlannedEnemyAction& assignment : plan.actions)
			{
				const EnemyBall* enemy =
					reinterpret_cast<const EnemyBall*>(assignment.id);
			const auto enemies =
				m_RuntimeState->World().GetComponents<EnemyBall>();
			const auto found = std::find(enemies.begin(), enemies.end(), enemy);
			const std::string targetId = found != enemies.end()
				? "enemy:" + std::to_string(
					static_cast<std::size_t>(
						std::distance(enemies.begin(), found)))
				: std::string();
				actions.push_back({
					{ "enemy_id", enemy != nullptr
						? enemy->GetEnemyId()
						: "unknown" },
				{ "target_id", targetId },
					{ "action", ToString(assignment.action) },
					{ "cost", assignment.cost },
				{ "predicted_damage_before_defense",
					enemy != nullptr &&
						(assignment.action == EnemyActionType::NormalAttack ||
						 assignment.action == EnemyActionType::ChargeAttack)
						? PlayerBall::CalculateIncomingDamage(
							static_cast<int>(std::lround(
								enemy->GetAttack() *
								enemy->GetPlannedDamageMultiplier())))
						: 0 },
				});
				if (assignment.action == EnemyActionType::NormalAttack)
					++normalCount;
				else if (assignment.action == EnemyActionType::ChargeStart)
					++chargeStartCount;
				else if (assignment.action == EnemyActionType::ChargeAttack)
					++chargeAttackCount;
			}
			RecordBalanceEvent("enemy_action_plan", {
				{ "budget_limit", plan.budgetLimit },
				{ "budget_spent", plan.budgetSpent },
				{ "reserved_next_turn", plan.reservedChargeAttackBudget },
				{ "normal_attack_count", normalCount },
				{ "charge_start_count", chargeStartCount },
				{ "charge_attack_count", chargeAttackCount },
				{ "actions", std::move(actions) },
			});
			InvalidateDebugCombatForecast("敵行動計画確定");
		};

	hooks.chargeInterrupted =
		[this](
			const EnemyBall& enemy,
			const ChargeInterruptionResult& result)
		{
			NotifyChargeInterrupted(enemy, result);
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

std::vector<EnemyBall*> Game::SpawnBossMinions(int count, bool initialFormation, bool advanceFormation)
{
    using DirectX::SimpleMath::Vector3;
    EnemyBall* boss = nullptr;
    int living = 0;
    for (auto* enemy : GameRuntime::World().GetComponents<EnemyBall>())
    {
        if (enemy->IsArmorBoss() && enemy->CanParticipateInCombat()) boss = enemy;
        if (enemy->IsBossMinion() && enemy->CanParticipateInCombat()) ++living;
    }
    std::vector<EnemyBall*> spawned;
    if (!boss) return spawned;
    count = (std::min)(count, BossEncounterRules::MaximumMinions-living);
    std::vector<Collision::Sphere> pockets;
    for (auto* pocket : GameRuntime::World().GetComponents<Pocket>()) pockets.push_back(pocket->GetSphere());
    const Vector3 preferred[] = {{-3,1,7},{20,1,6},{-20,1,12},{-28,1,-6},{28,1,22}};
    for (int i=0;i<count;++i)
    {
        std::vector<BossPocketRules::Obstacle> obstacles;
        for (auto* ball : GameRuntime::World().GetComponents<BallComponent>())
            if (ball->GetGameObject()->IsActive()) obstacles.push_back({ball->GetPosition(),ball->GetRadius()});
        const auto slot = BossEncounterRules::InitialMinionPositions[i % BossEncounterRules::InitialMinions];
        const auto advanceSlot = BossEncounterRules::AdvanceMinionPositions[boss->BossTurn().nextMinionSerial%2];
        const auto position = initialFormation
            ? BossPocketRules::FindSpawnPosition(Vector3(slot.x,TableConfig::FIELD_HEIGHT,slot.z),
                BossEncounterRules::MinionRadius,obstacles,pockets)
            : advanceFormation ? BossPocketRules::FindSpawnPosition(Vector3(advanceSlot.x,TableConfig::FIELD_HEIGHT,advanceSlot.z),
                BossEncounterRules::MinionRadius,obstacles,pockets)
            : BossPocketRules::FindReturnPosition(preferred[boss->BossTurn().nextMinionSerial%5],
                BossEncounterRules::MinionRadius,obstacles,pockets);
        if (!position) break;
        EnemyData data;
        data.id = BossEncounterRules::MinionId;
        data.maxHp = BossEncounterRules::MinionHp;
        data.status.attack = BossEncounterRules::MinionAttack;
        data.status.defense = 0;
        data.status.mass = BossEncounterRules::MinionMass;
        data.status.radius = BossEncounterRules::MinionRadius;
        data.status.restitution = 0.8f;
        data.status.friction = 0.02f;
        data.scale = Vector3(BossEncounterRules::MinionRadius);
        data.initPosition = *position;
        data.rewardMoney = data.rewardExp = 0;
        auto* minion = BallFactory::CreateEnemy(*this,data);
        minion->SetBossMinionSerial(boss->BossTurn().nextMinionSerial++);
        spawned.push_back(minion);
    }
    return spawned;
}
