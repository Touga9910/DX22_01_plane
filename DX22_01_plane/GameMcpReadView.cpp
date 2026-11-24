#include "GameMcpState.h"
#include "Game.h"
#include "GameRuntimeState.h"
#include "GamePersistenceRuntime.h"
#include "GameBattleState.h"
#include "GameBalanceRuntime.h"
#include "GameStageOverrideState.h"
#include "GameDebugController.h"
#include "RunController.h"
#include "RunProgressController.h"

/// <summary>
/// ゲームからラン、バランス検証、物理、デバッグなどの公開用状態を取得してスナップショットを作る。
/// </summary>
GameMcpReadSnapshot GameMcpReadView::Capture(const Game& game)
{
	GameMcpReadSnapshot snapshot;
	const GameBalanceReadSnapshot balance =
		game.m_BalanceRuntime->CaptureReadSnapshot(
			game.m_RunController->Status().progress,
			game.m_RunController->Progress().GetClearedBattleCount());
	snapshot.autoplayEnabled = balance.automation.enabled;
	snapshot.autoplayStopAfterCurrentRun =
		balance.automation.stopAfterCurrentRunRequested;
	snapshot.autoplayStopAfterCurrentRunDefault =
		balance.automation.stopAfterCurrentRunDefault;

	const GameRuntimeState::PhysicsTiming physics =
		game.m_RuntimeState->CapturePhysicsTiming();
	snapshot.physics.substepsLastTick = physics.substepsLastTick;
	snapshot.physics.substepLimitCount = physics.substepLimitCount;
	snapshot.physics.tick = physics.tick;
	snapshot.physics.stepsLastFrame = physics.stepsLastFrame;
	snapshot.physics.droppedSeconds = physics.droppedSeconds;
	snapshot.restHealRatio = game.m_RunController->RestHealRatio();

	const ProgressionProfile& progression =
		game.m_PersistenceRuntime->Profile();
	snapshot.metaProgression.activeAscension =
		game.m_PersistenceRuntime->ActiveAscension();
	snapshot.metaProgression.selectedAscension = progression.selectedAscension;
	snapshot.metaProgression.highestUnlockedAscension =
		progression.highestUnlockedAscension;
	snapshot.metaProgression.totalRuns = progression.totalRuns;
	snapshot.metaProgression.totalClears = progression.totalClears;
	snapshot.metaProgression.persistentRewardsEligible =
		game.m_PersistenceRuntime->PersistentProgressEligible();
	snapshot.metaProgression.achievementUnlocked.reserve(
		AchievementCatalog.size());
	for (const AchievementDefinition& achievement : AchievementCatalog)
	{
		snapshot.metaProgression.achievementUnlocked.push_back(
			progression.IsAchievementUnlocked(achievement.id));
	}
	constexpr const char* unlockableBallIds[]{
		"player_pierce",
		"player_refractive_pierce",
		"player_pierce_finisher",
		"player_bounce",
		"player_cushion_charge",
		"player_ricochet_finisher",
		"player_anchor",
		"player_stop_shield",
		"player_anchor_finisher",
		"player_collision_boost",
		"player_lightweight",
		"player_minion_finisher",
		"player_reflection_setup",
		"player_origin_anchor",
	};
	for (const char* ballId : unlockableBallIds)
	{
		if (progression.IsBallUnlocked(ballId))
		{
			snapshot.metaProgression.unlockedBallIds.emplace_back(ballId);
		}
	}
	snapshot.metaProgression.relicUnlocked.reserve(game.GetRelicCount());
	for (int index = 0; index < game.GetRelicCount(); ++index)
	{
		const RelicDefinition* relic = game.GetRelic(index);
		snapshot.metaProgression.relicUnlocked.push_back(
			relic != nullptr && progression.IsRelicUnlocked(relic->type));
	}

	if (game.m_BattleState != nullptr)
	{
		const BattleController& controller = game.m_BattleState->controller;
		const BattlePocketRules& rules = controller.PocketRules();
		snapshot.pocketRules.playerDamageRatio = rules.playerDamageRatio;
		snapshot.pocketRules.enemyInstantDefeat = rules.enemyInstantDefeat;
		snapshot.pocketRules.playerReturnHalfWidth = rules.playerReturnHalfWidth;
		snapshot.pocketRules.playerReturnHalfDepth = rules.playerReturnHalfDepth;
		snapshot.pocketRules.stageType = controller.GetStageType();
	}

	snapshot.queuedStageOverride.reset();
	if (const StageData* queued = game.m_StageOverrideState->Queued())
	{
		snapshot.queuedStageOverride = *queued;
	}
	snapshot.currentStageOverride.reset();
	if (const StageData* current = game.m_StageOverrideState->Current())
	{
		snapshot.currentStageOverride = *current;
	}
	snapshot.runStatistics =
		game.m_PersistenceRuntime->Statistics().GetState();
	snapshot.selectedOfferIndex = game.GetSelectedBallOfferIndex();
	snapshot.selectedHoldIndex = game.GetHeldBallOfferIndex();
	snapshot.pendingBallAdjustments = balance.pendingBallAdjustments;

	snapshot.progressionScaling.enabled =
		balance.progressionScalingEnabled;
	snapshot.progressionScaling.currentProgress = balance.currentProgress;
	snapshot.progressionScaling.nextEnemyHpDelta =
		balance.nextEnemyHpDelta;
	snapshot.progressionScaling.hpStartProgress = balance.progressionHpStart;
	snapshot.progressionScaling.hpInterval = balance.progressionHpInterval;
	snapshot.progressionScaling.hpStep = balance.progressionHpStep;
	snapshot.progressionScaling.maximumHpDelta =
		balance.progressionHpMaximumDelta;
	snapshot.progressionScaling.nextEnemyAttackDelta =
		balance.nextEnemyAttackDelta;
	snapshot.progressionScaling.attackStartProgress =
		balance.progressionAttackStart;
	snapshot.progressionScaling.attackInterval =
		balance.progressionAttackInterval;
	snapshot.progressionScaling.attackStep = balance.progressionAttackStep;
	snapshot.progressionScaling.maximumAttackDelta =
		balance.progressionAttackMaximumDelta;
	snapshot.baselineDifficultyProfile = balance.baselineDifficultyProfile;
	snapshot.baselineEnemyHpMultiplier = balance.baselineEnemyHpMultiplier;
	snapshot.baselineEnemyAttackDelta = balance.baselineEnemyAttackDelta;

	snapshot.balanceValidation.enabled = balance.validationEnabled;
	snapshot.balanceValidation.experimentId = balance.validationExperimentId;
	snapshot.balanceValidation.variantId = balance.validationVariantId;
	snapshot.balanceValidation.variantIndex = balance.validationVariantIndex;
	snapshot.balanceValidation.variantCount = balance.validationVariantCount;
	snapshot.balanceValidation.runSeed = balance.runRandomSeed;
	snapshot.balanceValidation.stageSelectionSeed = balance.stageSelectionSeed;
	snapshot.balanceValidation.routeSelectionSeed = balance.routeSelectionSeed;
	snapshot.balanceValidation.seedSuiteIndex = balance.validationSeedIndex;
	snapshot.balanceValidation.seedSuiteSize = balance.validationSeedCount;
	snapshot.balanceValidation.maximumClearedStages =
		balance.validationMaximumClearedStages;
	snapshot.balanceValidation.enduranceMode =
		balance.validationEnduranceMode;
	snapshot.balanceValidation.adjustedEarlyStages =
		balance.validationAdjustedEarlyStages;
	snapshot.balanceValidation.adjustedMidStages =
		balance.validationAdjustedMidStages;
	snapshot.balanceValidation.normalPlayBalance =
		balance.validationNormalPlayBalance;
	snapshot.balanceValidation.balanceOverrideActive =
		balance.validationBalanceOverrideActive;
	snapshot.balanceValidation.tacticalAi =
		balance.validationTacticalAi;
	snapshot.balanceValidation.tacticalAiProfile =
		balance.validationTacticalAiProfile;
	snapshot.balanceValidation.earlyAttackBudget =
		balance.validationEarlyAttackBudget;
	snapshot.balanceValidation.attackBudget =
		balance.validationAttackBudget;
	snapshot.balanceValidation.effectiveBalanceSource =
		balance.effectiveBalanceSource;
	snapshot.balanceValidation.effectiveNormalStagePoolId =
		balance.effectiveNormalStagePoolId;
	snapshot.balanceValidation.effectiveAttackBudget =
		balance.effectiveAttackBudget;
	snapshot.balanceValidation.clearedStageCount =
		balance.clearedStageCount;

	const GameDebugController& debug = *game.m_DebugController;
	snapshot.debug.active = debug.IsActive();
	snapshot.debug.editorOpen = debug.IsEditorOpen();
	snapshot.debug.finished = debug.IsBattleFinished();
	if (!debug.GetStageEditor().draft.is_null())
	{
		snapshot.debug.stageEditor = debug.GetStageEditor().Snapshot(
			debug.GetEnemyCatalog(), debug.StageEditorPlayerRadius());
		snapshot.debug.stageEditor["open"] = debug.IsEditorOpen();
	}
	return snapshot;
}
