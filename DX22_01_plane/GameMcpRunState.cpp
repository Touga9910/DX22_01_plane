#include "GameMcpStateSerialization.h"
#include "GameMcpJson.h"
#include "Game.h"
#include "GameMcpState.h"
#include "json/json.hpp"
#include "ProgressionProfile.h"
#include <algorithm>

using namespace GameMcpJson;

namespace GameMcpStateSerialization
{
void AppendRunMode(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state)
{
	const std::string stateScene = state.value("scene", std::string("unknown"));
	const RunCombatMode appliedRunMode = game.GetRunCombatMode();
	const RunCombatMode nextRunMode = game.GetNextRunDefaultCombatMode();
	state["combat_mode"] = McpRunCombatModeId(
		stateScene == "title" ? nextRunMode : appliedRunMode);
	state["combat_mode_scope"] = stateScene == "title"
		? "next_run_default"
		: ((stateScene == "result" || stateScene == "game_over")
			? "last_run" : "active_run");
	state["requested_combat_mode"] = McpRunCombatModeId(
		game.GetLastRequestedRunCombatMode());
	state["applied_combat_mode"] = McpRunCombatModeId(appliedRunMode);
	state["next_run_combat_mode"] = McpRunCombatModeId(nextRunMode);
	state["current_floor"] = snapshot.progressionScaling.currentProgress;
	state["available_run_modes"] = { "legacy", "phase" };
	state["phase_progression_mode"] = ToString(
		game.GetPhaseProgressionMode());
	state["phase_normal_battle_encounter_count"] =
		game.GetPhaseNormalBattleEncounterCount();
	state["balance_log_directory"] = "logs/balance";
}

void AppendProgression(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state)
{
	const auto isBallUnlocked = [&snapshot](const std::string& definitionId)
	{
		return std::find(
			snapshot.metaProgression.unlockedBallIds.begin(),
			snapshot.metaProgression.unlockedBallIds.end(),
			definitionId) != snapshot.metaProgression.unlockedBallIds.end();
	};
	state["rest_heal"] = {
		{ "heal_ratio", snapshot.restHealRatio },
		{ "heal_percent", game.GetRestHealPercent() },
		{ "configured_heal_amount", game.GetRestHealAmount() },
		{ "capped_at_max_hp", true },
		{ "available", game.CanRestHeal() },
	};
	state["meta_progression"] = {
		{"active_ascension", snapshot.metaProgression.activeAscension},
		{"selected_ascension", snapshot.metaProgression.selectedAscension},
		{"highest_unlocked_ascension", snapshot.metaProgression.highestUnlockedAscension},
		{"total_runs", snapshot.metaProgression.totalRuns},
		{"total_clears", snapshot.metaProgression.totalClears},
		{"persistent_rewards_eligible", snapshot.metaProgression.persistentRewardsEligible},
		{"enemy_hp_multiplier", ProgressionProfile::EnemyHpMultiplier(snapshot.metaProgression.activeAscension)},
		{"enemy_attack_bonus", ProgressionProfile::EnemyAttackBonus(snapshot.metaProgression.activeAscension)},
	};
	state["meta_progression"]["ascension_rules"] = nlohmann::json::array();
	for (int level = 0; level <= ProgressionProfile::MaximumAscension; ++level)
	{
		state["meta_progression"]["ascension_rules"].push_back({
			{ "level", level },
			{ "rule", ProgressionProfile::AscensionRule(level) },
			{ "unlocked", level <= snapshot.metaProgression.highestUnlockedAscension },
		});
	}
	state["meta_progression"]["achievements"] = nlohmann::json::array();
	for (std::size_t achievementIndex = 0;
		achievementIndex < AchievementCatalog.size(); ++achievementIndex)
	{
		const AchievementDefinition& achievement =
			AchievementCatalog[achievementIndex];
		state["meta_progression"]["achievements"].push_back({
			{ "key", achievement.key },
			{ "name", achievement.name },
			{ "condition", achievement.condition },
			{ "reward", achievement.reward },
			{ "unlocked", snapshot.metaProgression.achievementUnlocked[achievementIndex] },
		});
	}
	state["meta_progression"]["ball_unlocks"] = {
		{{ "definition_id", "player_standard" }, { "unlocked", true }},
		{{ "definition_id", "player_heavy" }, { "unlocked", true }},
		{{ "definition_id", "player_chain_impact" }, { "unlocked", true }},
		{{ "definition_id", "player_collision_boost" }, { "unlocked", true }},
		{{ "definition_id", "player_lightweight" }, { "unlocked", true }},
		{{ "definition_id", "player_minion_finisher" }, { "unlocked", true }},
		{{ "definition_id", "player_reflection_setup" }, { "unlocked", true }},
		{{ "definition_id", "player_origin_anchor" }, { "unlocked", true }},
		{{ "definition_id", "player_pierce" }, { "unlocked", isBallUnlocked("player_pierce") }},
		{{ "definition_id", "player_refractive_pierce" }, { "unlocked", isBallUnlocked("player_refractive_pierce") }},
		{{ "definition_id", "player_pierce_finisher" }, { "unlocked", isBallUnlocked("player_pierce_finisher") }},
		{{ "definition_id", "player_bounce" }, { "unlocked", isBallUnlocked("player_bounce") }},
		{{ "definition_id", "player_cushion_charge" }, { "unlocked", isBallUnlocked("player_cushion_charge") }},
		{{ "definition_id", "player_ricochet_finisher" }, { "unlocked", isBallUnlocked("player_ricochet_finisher") }},
		{{ "definition_id", "player_anchor" }, { "unlocked", isBallUnlocked("player_anchor") }},
		{{ "definition_id", "player_guard" }, { "unlocked", isBallUnlocked("player_guard") }},
		{{ "definition_id", "player_stop_shield" }, { "unlocked", isBallUnlocked("player_stop_shield") }},
		{{ "definition_id", "player_anchor_finisher" }, { "unlocked", isBallUnlocked("player_anchor_finisher") }},
	};
}

void AppendRunProgress(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state)
{
	const RunResultSnapshot& runStatistics = snapshot.runStatistics;
	state["run_progress"] = {
		{ "combat_mode", McpRunCombatModeId(game.GetRunCombatMode()) },
		{ "requested_combat_mode", McpRunCombatModeId(
			game.GetLastRequestedRunCombatMode()) },
		{ "applied_combat_mode", McpRunCombatModeId(
			game.GetRunCombatMode()) },
		{ "phase_progression_mode", ToString(
			game.GetPhaseProgressionMode()) },
		{ "phase_normal_battle_encounter_count",
			game.GetPhaseNormalBattleEncounterCount() },
		{ "phase", ToString(game.GetRunPhase()) },
		{ "area_progress", game.GetAreaProgress() },
		{ "area_goal", game.GetNormalRouteAreaGoal() },
		{ "total_battles", runStatistics.totalBattles },
		{ "midboss_challenges", runStatistics.midBossChallenges },
		{ "midboss_defeats", runStatistics.midBossDefeats },
		{ "final_boss_reached", runStatistics.finalBossReached },
		{ "final_boss_defeated", runStatistics.finalBossDefeated },
		{ "final_boss_id", runStatistics.finalBossId },
	};
}

void AppendBalance(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state)
{
	state["dynamic_balance"] = {
		{ "role", "retired_compatibility" },
		{ "retired", true },
		{ "enabled", false },
		{ "current_battle_enabled", false },
		{ "level", 0 },
		{ "current_battle_level", 0 },
		{ "minimum_level", 0 },
		{ "maximum_level", 0 },
		{ "application_timing", "next_battle_spawn" },
		{ "next_enemy_modifier", {
			{ "max_hp_delta", 0 },
			{ "attack_delta", 0 },
		} },
		{ "current_stage_metrics", {
			{ "active", false },
			{ "shots", 0 },
			{ "no_hit_shots", 0 },
			{ "enemy_count", 0 },
		} },
		{ "last_evaluation", {
			{ "result", "not_evaluated" },
			{ "reason", "Dynamic difficulty was retired; fixed difficulty is active." },
			{ "level_change", 0 },
			{ "remaining_hp_ratio", 1.0f },
			{ "no_hit_rate", 0.0f },
			{ "shots_per_enemy", 0.0f },
		} },
	};
	state["progression_scaling"] = {
		{ "enabled", snapshot.progressionScaling.enabled },
		{ "current_progress", snapshot.progressionScaling.currentProgress },
		{ "next_enemy_hp_delta",
			snapshot.progressionScaling.nextEnemyHpDelta },
		{ "hp_start_progress", snapshot.progressionScaling.hpStartProgress },
		{ "hp_interval", snapshot.progressionScaling.hpInterval },
		{ "hp_step", snapshot.progressionScaling.hpStep },
		{ "maximum_hp_delta",
			snapshot.progressionScaling.maximumHpDelta },
		{ "next_enemy_attack_delta",
			snapshot.progressionScaling.nextEnemyAttackDelta },
		{ "attack_start_progress", snapshot.progressionScaling.attackStartProgress },
		{ "attack_interval", snapshot.progressionScaling.attackInterval },
		{ "attack_step", snapshot.progressionScaling.attackStep },
		{ "maximum_attack_delta",
			snapshot.progressionScaling.maximumAttackDelta },
	};
	state["baseline_difficulty"] = {
		{ "profile", snapshot.baselineDifficultyProfile },
		{ "enemy_hp_multiplier", snapshot.baselineEnemyHpMultiplier },
		{ "enemy_attack_delta", snapshot.baselineEnemyAttackDelta },
	};
	state["balance_validation"] = {
		{ "enabled", snapshot.balanceValidation.enabled },
		{ "experiment_id", snapshot.balanceValidation.experimentId },
		{ "variant_id", snapshot.balanceValidation.variantId },
		{ "variant_index", snapshot.balanceValidation.variantIndex },
		{ "variant_count", snapshot.balanceValidation.variantCount },
		{ "run_seed", snapshot.balanceValidation.runSeed },
		{ "stage_selection_seed", snapshot.balanceValidation.stageSelectionSeed },
		{ "route_selection_seed", snapshot.balanceValidation.routeSelectionSeed },
		{ "seed_suite_index", snapshot.balanceValidation.seedSuiteIndex },
		{ "seed_suite_size", snapshot.balanceValidation.seedSuiteSize },
		{ "maximum_cleared_stages",
			snapshot.balanceValidation.maximumClearedStages },
		{ "endurance_mode", snapshot.balanceValidation.enduranceMode },
		{ "adjusted_early_stages",
			snapshot.balanceValidation.adjustedEarlyStages },
		{ "adjusted_mid_stages",
			snapshot.balanceValidation.adjustedMidStages },
		{ "normal_play_balance",
			snapshot.balanceValidation.normalPlayBalance },
		{ "balance_override_active",
			snapshot.balanceValidation.balanceOverrideActive },
		{ "tactical_ai", snapshot.balanceValidation.tacticalAi },
		{ "tactical_ai_profile",
			snapshot.balanceValidation.tacticalAiProfile },
		{ "early_attack_budget",
			snapshot.balanceValidation.earlyAttackBudget },
		{ "attack_budget",
			snapshot.balanceValidation.attackBudget },
		{ "effective_balance_source",
			snapshot.balanceValidation.effectiveBalanceSource },
		{ "effective_normal_stage_pool_id",
			snapshot.balanceValidation.effectiveNormalStagePoolId },
		{ "effective_attack_budget",
			snapshot.balanceValidation.effectiveAttackBudget },
		{ "cleared_stage_count", snapshot.balanceValidation.clearedStageCount },
		{
			"dynamic_balance_forced_off",
			true
		},
	};
}

}
