#include "GameMcpStateSerialization.h"
#include "GameMcpJson.h"
#include "Game.h"
#include "GameMcpState.h"
#include "json/json.hpp"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "EnemyBall.h"
#include "BallPhysicsComponent.h"
#include "BallPhysicsWorld.h"
#include "BattleController.h"
#include "GameRuntimeState.h"
#include "CollisionVfxSystem.h"
#include "GamePresentation.h"
#include "AudioFeedback.h"
#include <algorithm>

using namespace GameMcpJson;

namespace GameMcpStateSerialization
{
void AppendBattleDiagnostics(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state)
{
	state["physics_clock"] = {
		{ "model", BallPhysicsWorld::ModelName },
        { "trajectory_prediction", "shared_ccd_toi_v1" },
		{ "substeps_last_tick", snapshot.physics.substepsLastTick },
        { "max_toi_iterations_per_tick", ContinuousBallStepper::MaxIterations },
		{ "substep_limit_count", snapshot.physics.substepLimitCount },
		{ "frequency_hz", 60 },
		{ "tick", snapshot.physics.tick },
		{ "steps_last_frame", snapshot.physics.stepsLastFrame },
		{ "max_steps_per_frame", FixedStepClock::MaxStepsPerFrame },
		{ "dropped_seconds", snapshot.physics.droppedSeconds },
	};
	if (const BattleController* controller =
		game.GetBattleControllerDiagnostics())
	{
		nlohmann::json stateHistory = nlohmann::json::array();
		for (BattleState entry : controller->GetStateHistory())
		{
			stateHistory.push_back(ToString(entry));
		}

		nlohmann::json nonStopped = nlohmann::json::array();
		int activePhysicsCount = 0;
		const auto physicsObjects =
			GameRuntime::World().GetObjectsWith<BallPhysicsComponent>();
		for (GameObject* object : physicsObjects)
		{
			if (object == nullptr)
			{
				continue;
			}
			if (object->IsActive())
			{
				++activePhysicsCount;
			}
			const auto* physics = object->GetComponent<BallPhysicsComponent>();
			if (physics == nullptr || physics->IsStopped())
			{
				continue;
			}

			nlohmann::json ball = {
				{ "object_id", reinterpret_cast<std::uintptr_t>(object) },
				{ "name", object->GetName() },
				{ "active", object->IsActive() },
				{ "velocity", VectorToJson(physics->GetVelocity()) },
			};
			if (const auto* enemy = object->GetComponent<EnemyBall>())
			{
				ball["enemy_id"] = enemy->GetEnemyId();
				ball["defeated"] = enemy->IsDefeated();
			}
			nonStopped.push_back(std::move(ball));
		}

		const auto enemies = GameRuntime::World().GetComponents<EnemyBall>();
		const int livingEnemyCount = static_cast<int>(std::count_if(
			enemies.begin(), enemies.end(), [](const EnemyBall* enemy)
			{
				return enemy != nullptr && enemy->CanParticipateInCombat();
			}));
		state["battle_progress_diagnostics"] = {
			{ "combat_mode", ToString(controller->GetCombatMode()) },
			{ "current_floor", snapshot.progressionScaling.currentProgress },
			{ "turn", controller->GetTurnNumber() },
			{ "current_phase", controller->GetCurrentPhase() },
			{ "total_phases", controller->GetTotalPhases() },
			{ "turns_until_next_phase",
				controller->GetTurnsUntilNextPhase() },
			{ "next_phase_enemy_count",
				controller->GetNextPhaseEnemyCount() },
			{ "living_enemies", livingEnemyCount },
			{ "defeated_enemies",
				controller->GetDefeatedEnemyCount() },
			{ "pocket_defeated_enemies",
				controller->GetPocketDefeatedEnemyCount() },
			{ "physics_component_count", physicsObjects.size() },
			{ "active_physics_component_count", activePhysicsCount },
			{ "non_stopped_balls", std::move(nonStopped) },
			{ "continuous_stopped_tick_count",
				controller->GetStoppedTickCount() },
			{ "fixed_step_count", controller->GetFixedStepCount() },
			{ "shot_fixed_step_count", controller->GetShotFixedStepCount() },
			{ "state_history", std::move(stateHistory) },
		};
		state["phase_battle"] = {
			{ "combat_mode", ToString(controller->GetCombatMode()) },
			{ "current_floor", snapshot.progressionScaling.currentProgress },
			{ "current_phase", controller->GetCurrentPhase() },
			{ "total_phases", controller->GetTotalPhases() },
			{ "turns_until_next_phase",
				controller->GetTurnsUntilNextPhase() },
			{ "living_enemy_count", livingEnemyCount },
			{ "next_phase_enemy_count",
				controller->GetNextPhaseEnemyCount() },
			{ "cumulative_defeated_enemy_count",
				controller->GetDefeatedEnemyCount() },
			{ "pocket_defeated_enemy_count",
				controller->GetPocketDefeatedEnemyCount() },
		};
		state["battle_combat_mode"] =
			ToString(controller->GetCombatMode());
	}
}

void AppendPerformanceDiagnostics(Game& game, nlohmann::json& state)
{
	const FrameTimingSnapshot& frameTiming = game.GetFrameTimingSnapshot();
	const auto timingAggregateJson = [](const TimingAggregate& aggregate)
	{
		return nlohmann::json{
			{ "total_ms", aggregate.totalMilliseconds },
			{ "average_ms_per_call", aggregate.callCount > 0
				? aggregate.totalMilliseconds /
					static_cast<double>(aggregate.callCount)
				: 0.0 },
			{ "maximum_ms", aggregate.maximumMilliseconds },
			{ "call_count", aggregate.callCount },
		};
	};
	nlohmann::json physicsStepHistogram = nlohmann::json::object();
	for (std::size_t step = 0;
		step < frameTiming.physicsStepsPerFrameHistogram.size();
		++step)
	{
		physicsStepHistogram[std::to_string(step)] =
			frameTiming.physicsStepsPerFrameHistogram[step];
	}
	state["frame_timing"] = {
		{ "enabled", game.IsFrameTimeMeasurementEnabled() },
		{ "active", frameTiming.active },
		{ "completed", frameTiming.completed },
		{ "sequence", frameTiming.sequence },
		{ "sample_count", frameTiming.sampleCount },
		{ "physics_step_count", frameTiming.physicsStepCount },
		{ "average_frame_ms", frameTiming.averageFrameMilliseconds },
		{ "maximum_frame_ms", frameTiming.maximumFrameMilliseconds },
		{ "p95_frame_ms", frameTiming.p95FrameMilliseconds },
		{ "p99_frame_ms", frameTiming.p99FrameMilliseconds },
		{ "average_fps", frameTiming.averageFps },
		{ "one_percent_low_fps", frameTiming.onePercentLowFps },
		{ "shot_duration_seconds", frameTiming.shotDurationSeconds },
		{ "average_update_ms", frameTiming.averageUpdateMilliseconds },
		{ "maximum_update_ms", frameTiming.maximumUpdateMilliseconds },
		{ "average_draw_ms", frameTiming.averageDrawMilliseconds },
		{ "maximum_draw_ms", frameTiming.maximumDrawMilliseconds },
		{ "average_present_ms", frameTiming.averagePresentMilliseconds },
		{ "maximum_present_ms", frameTiming.maximumPresentMilliseconds },
		{ "average_physics_steps_per_frame", frameTiming.sampleCount > 0
			? static_cast<double>(frameTiming.physicsStepCount) /
				static_cast<double>(frameTiming.sampleCount)
			: 0.0 },
		{ "maximum_physics_steps_per_frame",
			frameTiming.maximumPhysicsStepsPerFrame },
		{ "frames_at_observed_max_physics_steps",
			frameTiming.framesAtMaximumPhysicsSteps },
		{ "frames_at_physics_step_cap",
			frameTiming.framesAtPhysicsStepCap },
		{ "zero_physics_step_frames", frameTiming.zeroPhysicsStepFrames },
		{ "physics_steps_per_frame_histogram", physicsStepHistogram },
		{ "simulated_physics_seconds",
			frameTiming.physicsStepCount * FixedStepClock::StepSeconds },
		{ "dropped_simulation_seconds",
			frameTiming.droppedSimulationSeconds },
		{ "maximum_fixed_step_remainder_seconds",
			frameTiming.maximumRemainderSeconds },
		{ "maximum_consecutive_frames_over_16_667_ms",
			frameTiming.maximumConsecutiveDelayedFrames },
		{ "update_breakdown_scope", "mutually_exclusive_top_level" },
		{ "update_breakdown", {
			{ "audio_feedback", timingAggregateJson(frameTiming.audioFeedbackUpdate) },
			{ "presentation", timingAggregateJson(frameTiming.presentationUpdate) },
			{ "game_mcp", timingAggregateJson(frameTiming.gameMcpUpdate) },
			{ "scene", timingAggregateJson(frameTiming.sceneUpdate) },
			{ "camera", timingAggregateJson(frameTiming.cameraUpdate) },
			{ "world_update", timingAggregateJson(frameTiming.worldUpdate) },
			{ "fixed_physics", timingAggregateJson(frameTiming.fixedPhysicsUpdate) },
			{ "world_late_update", timingAggregateJson(frameTiming.worldLateUpdate) },
			{ "battle_controller", timingAggregateJson(frameTiming.battleControllerUpdate) },
			{ "other", timingAggregateJson(frameTiming.otherUpdate) },
		} },
		{ "contained_detail_scope", "contained_in_parent_not_additive" },
		{ "contained_detail", {
			{ "world_fixed_update", timingAggregateJson(frameTiming.worldFixedUpdate) },
			{ "ball_physics_world_step", timingAggregateJson(frameTiming.ballPhysicsWorldStep) },
			{ "fixed_battle_controller", timingAggregateJson(frameTiming.fixedBattleController) },
			{ "shot_prediction", timingAggregateJson(frameTiming.shotPrediction) },
		} },
		{ "clock", "cpu_steady_clock" },
		{ "gpu_time_measured", false },
		{ "includes_vsync_wait", true },
	};
	const AimingPerformanceSnapshot aimingTiming =
		game.GetAimingPerformanceSnapshot();
	state["aiming_performance"] = {
		{ "active", aimingTiming.active },
		{ "sample_count", aimingTiming.sampleCount },
		{ "duration_seconds", aimingTiming.durationSeconds },
		{ "average_fps", aimingTiming.averageFps },
		{ "average_frame_ms", aimingTiming.averageFrameMilliseconds },
		{ "maximum_frame_ms", aimingTiming.maximumFrameMilliseconds },
		{ "p95_frame_ms", aimingTiming.p95FrameMilliseconds },
		{ "average_update_ms", aimingTiming.averageUpdateMilliseconds },
		{ "preview_prediction", timingAggregateJson(
			aimingTiming.previewPrediction) },
		{ "preview_world_key", timingAggregateJson(
			aimingTiming.previewWorldKey) },
		{ "prediction_calls_per_second", aimingTiming.durationSeconds > 0.0
			? static_cast<double>(aimingTiming.previewPrediction.callCount) /
				aimingTiming.durationSeconds
			: 0.0 },
		{ "cache_reuse_frames", aimingTiming.previewCacheReuseFrames },
		{ "preview_refreshes", aimingTiming.previewRefreshes },
		{ "cache_hit_rate", aimingTiming.previewCacheReuseFrames +
			aimingTiming.previewRefreshes > 0
			? static_cast<double>(aimingTiming.previewCacheReuseFrames) /
				static_cast<double>(aimingTiming.previewCacheReuseFrames +
					aimingTiming.previewRefreshes)
			: 0.0 },
		{ "maximum_power_preview_cache_hits",
			aimingTiming.maximumPowerPreviewCacheHits },
		{ "ball_state_copies", aimingTiming.previewBallCopies },
		{ "average_ball_state_copies", aimingTiming.previewPrediction.callCount > 0
			? static_cast<double>(aimingTiming.previewBallCopies) /
				static_cast<double>(aimingTiming.previewPrediction.callCount)
			: 0.0 },
	};
	const CollisionVfxDiagnostics& collisionVfx =
		game.GetCollisionVfxDiagnostics();
	const CombatReactionDiagnostics& combatReaction =
		game.GetCombatReactionDiagnostics();
	state["collision_vfx"] = {
		{ "heavy_chain_count", game.GetHeavyPhysicsChainPresentationCount() },
		{ "impact_events", collisionVfx.impactEvents },
		{ "sparks_generated", collisionVfx.sparksGenerated },
		{ "sparks_skipped", collisionVfx.sparksSkipped },
		{ "waves_generated", collisionVfx.wavesGenerated },
		{ "waves_merged", collisionVfx.wavesMerged },
		{ "waves_skipped", collisionVfx.wavesSkipped },
		{ "bursts_generated", collisionVfx.burstsGenerated },
		{ "bursts_skipped", collisionVfx.burstsSkipped },
		{ "defeat_requests", collisionVfx.defeatRequests },
		{ "defeat_vfx_generated", collisionVfx.defeatVfxGenerated },
		{ "defeat_vfx_skipped", collisionVfx.defeatVfxSkipped },
		{ "active_sparks", collisionVfx.activeSparks },
		{ "active_waves", collisionVfx.activeWaves },
		{ "active_bursts", collisionVfx.activeBursts },
		{ "maximum_active_sparks", collisionVfx.maximumActiveSparks },
		{ "maximum_active_waves", collisionVfx.maximumActiveWaves },
		{ "maximum_active_bursts", collisionVfx.maximumActiveBursts },
		{ "hit_flash_requests", combatReaction.hitFlashRequests },
		{ "hit_flashes_triggered", combatReaction.hitFlashesTriggered },
		{ "zero_damage_flashes_suppressed",
			combatReaction.zeroDamageFlashesSuppressed },
		{ "hit_flashes_skipped", combatReaction.hitFlashesSkipped },
		{ "active_hit_flashes", combatReaction.activeHitFlashes },
		{ "maximum_active_hit_flashes",
			combatReaction.maximumActiveHitFlashes },
		{ "unique_defeats", combatReaction.uniqueDefeats },
		{ "duplicate_defeats_suppressed",
			combatReaction.duplicateDefeatsSuppressed },
		{ "multi_defeat_displays", combatReaction.multiDefeatDisplays },
		{ "primitive_submissions_last_frame",
			collisionVfx.primitiveSubmissionsLastFrame },
		{ "maximum_primitive_submissions",
			collisionVfx.maximumPrimitiveSubmissions },
	};
	const AudioFeedbackDiagnostics& audio =
		game.GetAudioFeedbackDiagnostics();
	const auto audioCategory = [&](CombatSeCategory category)
	{
		const std::size_t index = CombatAudioRules::Index(category);
		return nlohmann::json{
			{ "requests", audio.categoryRequests[index] },
			{ "played", audio.categoryPlayed[index] },
		};
	};
	state["audio_feedback"] = {
		{ "device_initialized", audio.deviceInitialized },
		{ "enabled", audio.enabled },
		{ "se_volume", game.GetSeVolume() },
		{ "test_tone_mode", audio.testToneMode },
		{ "active_voices", audio.activeVoices },
		{ "maximum_active_voices", audio.maximumActiveVoices },
		{ "voice_limit", audio.voiceLimit },
		{ "loaded_audio_bytes", audio.loadedAudioBytes },
		{ "loaded_asset_files", audio.loadedAssetFiles },
		{ "missing_asset_files", audio.missingAssetFiles },
		{ "requests", audio.requests },
		{ "played", audio.played },
		{ "skipped", {
			{ "cooldown", audio.cooldownSkipped },
			{ "category_limit", audio.categoryLimitSkipped },
			{ "voice_limit", audio.voiceLimitSkipped },
			{ "muted", audio.mutedSkipped },
			{ "missing_asset", audio.missingAssetSkipped },
			{ "device_unavailable", audio.deviceUnavailableSkipped },
			{ "backend_error", audio.backendErrorSkipped },
		} },
		{ "duplicates_suppressed", {
			{ "defeats", audio.duplicateDefeatsSuppressed },
			{ "chain_milestones", audio.duplicateMilestonesSuppressed },
		} },
		{ "chain_stages", {
			{ "3", { { "requests", audio.chainStageRequests[0] }, { "played", audio.chainStagePlayed[0] } } },
			{ "6", { { "requests", audio.chainStageRequests[1] }, { "played", audio.chainStagePlayed[1] } } },
			{ "10", { { "requests", audio.chainStageRequests[2] }, { "played", audio.chainStagePlayed[2] } } },
			{ "15", { { "requests", audio.chainStageRequests[3] }, { "played", audio.chainStagePlayed[3] } } },
		} },
		{ "categories", {
			{ "direct_impact", audioCategory(CombatSeCategory::DirectImpact) },
			{ "enemy_impact", audioCategory(CombatSeCategory::EnemyImpact) },
			{ "enemy_defeat", audioCategory(CombatSeCategory::EnemyDefeat) },
			{ "chain_milestone", audioCategory(CombatSeCategory::ChainMilestone) },
			{ "wall_impact", audioCategory(CombatSeCategory::WallImpact) },
            { "player_hurt", audioCategory(CombatSeCategory::PlayerHurt) },
            { "shield_block", audioCategory(CombatSeCategory::ShieldBlock) },
		} },
		{ "timing", {
			{ "last_update_ms", audio.lastUpdateMilliseconds },
			{ "average_update_ms", audio.updateCount > 0
				? audio.totalUpdateMilliseconds /
					static_cast<double>(audio.updateCount)
				: 0.0 },
			{ "maximum_update_ms", audio.maximumUpdateMilliseconds },
			{ "average_request_ms", audio.requests > 0
				? audio.totalRequestMilliseconds /
					static_cast<double>(audio.requests)
				: 0.0 },
			{ "maximum_request_ms", audio.maximumRequestMilliseconds },
		} },
	};
}

}
