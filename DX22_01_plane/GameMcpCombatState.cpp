#include "GameMcpStateSerialization.h"
#include "GameMcpJson.h"
#include "Game.h"
#include "json/json.hpp"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "EnemyBall.h"
#include "BreakBall.h"
#include "BallComponent.h"
#include "PlayerBallData.h"
#include "BalanceLogger.h"
#include "HeavyCollisionRules.h"
#include "AnchorStackRules.h"
#include "EnemyActionPlanner.h"
#include <cmath>

using namespace GameMcpJson;

namespace GameMcpStateSerialization
{
std::vector<EnemyBall*> AppendEnemies(Game& game, nlohmann::json& state)
{
    state["boss_state"] = nullptr;
    state["break_balls"] = nlohmann::json::array();
    for (auto* neutral : GameRuntime::World().GetComponents<BreakBall>())
    {
        auto* ball = neutral->GetBall();
        state["break_balls"].push_back({
            {"target_id", "break_ball:" + std::to_string(neutral->GetIndex())},
            {"position", VectorToJson(ball->GetPosition())}, {"velocity", VectorToJson(ball->GetVelocity())},
            {"radius", ball->GetRadius()}, {"mass", ball->GetStatus().mass},
            {"active", neutral->GetGameObject()->IsActive()}, {"used_this_shot", neutral->IsUsed()},
            {"pocketed", neutral->IsPocketed()}, {"fixed_boss_damage", BossCombatRules::BreakBallDamage},
            {"armor_damage", 1}, {"max_activations_per_shot", 1}, {"pierce_passes_through", false},
            {"consumes_enemy_damage_relics", false}});
    }
	state["enemies"] = nlohmann::json::array();
	const EnemyActionConfig enemyActionConfig =
		game.GetEnemyActionConfig();
	state["enemy_action_rules"] = {
		{ "maximum_budget", enemyActionConfig.maximumBudget },
		{ "normal_attack_cost", enemyActionConfig.normalAttackCost },
		{ "charge_start_cost", enemyActionConfig.chargeStartCost },
		{ "charge_attack_cost", enemyActionConfig.chargeAttackCost },
		{ "maximum_charge_starts_per_turn",
			enemyActionConfig.maximumChargeStartsPerTurn },
		{ "charge_damage_multiplier",
			enemyActionConfig.chargeDamageMultiplier },
		{ "heavy_interrupt_diameter_multiplier",
			enemyActionConfig.heavyInterruptDiameterMultiplier },
		{ "pierce_interrupt_max_hp_ratio",
			enemyActionConfig.pierceInterruptMaxHpRatio },
	};
	const std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();
	for (std::size_t enemyIndex = 0;
		enemyIndex < enemies.size();
		enemyIndex++)
	{
		EnemyBall* enemy = enemies[enemyIndex];
		if (enemy == nullptr)
		{
			continue;
		}
		const std::string enemyTargetId =
			"enemy:" + std::to_string(enemyIndex);
		const bool combatActive = enemy->CanParticipateInCombat();
		const bool attacksThisTurn =
			combatActive &&
			(enemy->GetPlannedAction() == EnemyActionType::NormalAttack ||
			 enemy->GetPlannedAction() == EnemyActionType::ChargeAttack);
		const int plannedAttackDamage = attacksThisTurn
			? static_cast<int>(std::ceil(
				static_cast<float>(enemy->GetAttack()) *
				enemy->GetPlannedDamageMultiplier()))
			: 0;
		const int chargedAttackDamage = combatActive
			? static_cast<int>(std::ceil(
				static_cast<float>(enemy->GetAttack()) *
				enemyActionConfig.chargeDamageMultiplier))
			: 0;
		nlohmann::json affectedAllies = nlohmann::json::array();
		nlohmann::json protectionSources = nlohmann::json::array();
		for (std::size_t otherIndex = 0;
			otherIndex < enemies.size();
			++otherIndex)
		{
			EnemyBall* other = enemies[otherIndex];
			if (!combatActive || other == nullptr || other == enemy ||
				!other->CanParticipateInCombat())
			{
				continue;
			}
			DirectX::SimpleMath::Vector3 delta =
				other->GetPosition() - enemy->GetPosition();
			delta.y = 0.0f;
			const float radius = enemy->GetBoardAbilityRadius();
			if (radius > 0.0f && delta.LengthSquared() <= radius * radius)
			{
				affectedAllies.push_back(
					"enemy:" + std::to_string(otherIndex));
			}
			DirectX::SimpleMath::Vector3 sourceDelta =
				other->GetPosition() - enemy->GetPosition();
			sourceDelta.y = 0.0f;
			if (other->GetBoardAbilityType() ==
					EnemyBoardAbilityType::ProtectionResonator &&
				other->GetBoardAbilityRadius() > 0.0f &&
				sourceDelta.LengthSquared() <=
					other->GetBoardAbilityRadius() *
					other->GetBoardAbilityRadius())
			{
				protectionSources.push_back(
					"enemy:" + std::to_string(otherIndex));
			}
		}

		nlohmann::json chargeInterruptBalls = nlohmann::json::array();
		for (int offerIndex = 0;
			offerIndex < game.GetBallOfferCount();
			++offerIndex)
		{
			const PlayerBallData* offer = game.GetBallOffer(offerIndex);
			if (offer == nullptr ||
				(offer->category != BallCategory::Heavy &&
				 offer->category != BallCategory::Pierce))
			{
				continue;
			}
			chargeInterruptBalls.push_back({
				{ "offer_index", offerIndex },
				{ "definition_id", offer->definitionId },
				{ "method", offer->category == BallCategory::Heavy
					? "move_enemy_by_heavy_collision"
					: "deal_pierce_damage" },
			});
		}

		const int nearbyAfterOneRemoved =
			(std::max)(0, enemy->GetNearbyLivingAllyCount() - 1);
		const int swarmStageAfterOneRemoved =
			EnemyBoardAbilityRules::SwarmStage(nearbyAfterOneRemoved);
		const int swarmAttackAfterOneRemoved =
			EnemyBoardAbilityRules::SwarmAttack(
				nearbyAfterOneRemoved,
				enemy->GetSwarmAttackByStage(),
				enemy->GetBoardAbilityMaximumAttack());
		nlohmann::json tactics = {
			{ "next_action_prediction", {
				{ "action", ToString(enemy->GetPlannedAction()) },
				{ "budget_cost", enemy->GetPlannedActionCost() },
				{ "executes_this_turn", attacksThisTurn },
				{ "predicted_player_damage_before_defense", plannedAttackDamage },
			} },
			{ "defeat_prevention", {
				{ "immediate_attack_damage", plannedAttackDamage },
				{ "future_charged_attack_damage",
					enemy->GetPlannedAction() == EnemyActionType::ChargeStart
						? chargedAttackDamage : 0 },
				{ "counted_once_in_shot_score", true },
			} },
			{ "charge", {
				{ "state", ToString(enemy->GetChargeState()) },
				{ "interruptible_now", enemy->IsCharging() },
				{ "interruptible_offered_balls", chargeInterruptBalls },
				{ "heavy_required_enemy_movement",
					2.0f * enemy->GetRadius() *
					enemyActionConfig.heavyInterruptDiameterMultiplier },
				{ "pierce_required_damage",
					static_cast<int>(std::ceil(
						enemy->GetMaxHP() *
						enemyActionConfig.pierceInterruptMaxHpRatio)) },
				{ "attack_damage_prevented_if_interrupted",
					enemy->IsCharging() ? chargedAttackDamage : 0 },
			} },
			{ "board_ability", {
				{ "type", ToString(enemy->GetBoardAbilityType()) },
				{ "radius", enemy->GetBoardAbilityRadius() },
				{ "affected_ally_target_ids", affectedAllies },
				{ "protection_source_target_ids", protectionSources },
				{ "is_protected", enemy->IsProtectedByResonator() },
				{ "protection_damage_reduction",
					enemy->GetProtectionDamageReduction() },
				{ "resonator_damage_reduction",
					enemy->GetResonatorDamageReduction() },
				{ "resonator_protected_target_count",
					enemy->GetBoardAbilityType() ==
						EnemyBoardAbilityType::ProtectionResonator
						? affectedAllies.size() : 0 },
				{ "resonator_removal_effect",
					"defeat, pocket, or move source/ally outside radius removes protection" },
				{ "nearby_living_allies", enemy->GetNearbyLivingAllyCount() },
				{ "swarm_stage", enemy->GetSwarmStage() },
				{ "swarm_attack", enemy->GetAttack() },
				{ "swarm_stage_after_one_ally_removed",
					swarmStageAfterOneRemoved },
				{ "swarm_attack_after_one_ally_removed",
					swarmAttackAfterOneRemoved },
				{ "swarm_drop_condition",
					"defeat, pocket, or move an ally outside radius" },
				{ "brittle_active", enemy->IsBrittle() },
				{ "brittle_actions_remaining",
					enemy->GetBrittleActionsRemaining() },
				{ "brittle_wall_speed_threshold",
					enemy->GetBrittleCollisionSpeed() },
				{ "attack_before_brittle",
					enemy->GetAttackBeforeBoardAbility() },
				{ "attack_while_brittle", enemy->GetBrittleAttack() },
				{ "brittle_action_count", enemy->GetBrittleActionCount() },
			} },
		};
        if (enemy->IsArmorBoss())
        {
            const auto& boss = enemy->GetBossState();
            state["boss_state"] = {
                {"boss_id", enemy->GetEnemyId()}, {"target_id", "enemy:" + std::to_string(enemyIndex)},
                {"phase", 1}, {"hp", enemy->GetHP()}, {"max_hp", enemy->GetMaxHP()},
                {"armor", boss.armor}, {"max_armor", BossCombatRules::MaxArmor},
                {"is_broken", boss.IsBroken()}, {"break_shots_remaining", boss.shotsRemaining},
                {"break_started_this_shot", boss.startedThisShot}, {"pocket_immune", false},
                {"direct_damage_multiplier", boss.IsBroken() ? 1.0f : 0.25f},
                {"damage_order", "directional_then_defense_then_armor_ceil_min1"},
                {"break_ball_refreshes_break", false}, {"trigger_shot_consumes_break", false}};
        }
		state["enemies"].push_back({
			{ "target_id",
				enemyTargetId },
			{ "enemy_id", enemy->GetEnemyId() },
			{ "can_charge", enemy->CanUseCharge() },
			{ "charge_state", ToString(enemy->GetChargeState()) },
			{ "planned_action", ToString(enemy->GetPlannedAction()) },
			{ "planned_action_cost", enemy->GetPlannedActionCost() },
			{ "attack_interval", enemy->GetAttackInterval() },
			{ "turns_until_attack", enemy->GetTurnsUntilAttack() },
			{ "hp", enemy->GetHP() },
			{ "max_hp", enemy->GetMaxHP() },
			{ "attack", enemy->GetAttack() },
			{ "defense", enemy->GetDefense() },
			{ "mass", enemy->GetStatus().mass },
			{ "friction", enemy->GetStatus().friction },
			{ "restitution", enemy->GetStatus().restitution },
			{ "anchor_stacks", enemy->GetAnchorStacks() },
			{ "pending_lightweight_knockback_multiplier",
				enemy->GetPendingKnockbackMultiplier() },
			{ "current_lightweight_knockback_multiplier",
				enemy->GetNextKnockbackMultiplier() },
			{ "frontal_damage_multiplier", enemy->GetFrontalDamageMultiplier() },
			{ "pocket_damage_ratio", enemy->GetPocketDamageRatio() },
			{ "defeated", enemy->IsDefeated() },
			{ "pocketed", enemy->IsPocketed() },
			{ "combat_active", combatActive },
            { "pocket_immune", false },
			{ "can_attack_this_turn", attacksThisTurn },
			{ "tactics", std::move(tactics) },
			{ "pocket_queue_index", -1 },
			{ "hp_ratio",
				enemy->GetMaxHP() <= 0
					? 0.0f
					: static_cast<float>(enemy->GetHP()) /
						static_cast<float>(enemy->GetMaxHP()) },
			{ "pocket_finisher_eligible",
				game.IsEnemyPocketFinisherEligible(enemy) },
			{ "stopped", enemy->IsStopped() },
			{ "radius", enemy->GetRadius() },
			{ "position", VectorToJson(enemy->GetPosition()) },
			{ "velocity", VectorToJson(enemy->GetVelocity()) },
		});
	}
	return enemies;
}

void AppendSynergies(Game& game, const std::vector<EnemyBall*>& enemies, nlohmann::json& state)
{
	state["table"]["cushions"] = nlohmann::json::array();
	state["table"]["cushion_boost_consumed_this_shot"] =
		game.WasCushionBoostConsumedThisShot();
	const auto& heavyState = game.GetHeavyCollisionState();
	const auto& anchorState = game.GetAnchorStackState();
	const auto& cushionCharges = game.GetCushionCharges();
	state["ball_synergies"] = {
		{ "heavy_stacks", game.GetHeavyCollisionCount() },
		{ "heavy_stack_distance_per_stack",
			heavyState.distancePerStack },
		{ "heavy_stack_gain_limit_per_turn",
			heavyState.gainLimitPerTurn },
		{ "heavy_stacks_gained_this_turn",
			heavyState.gainedThisTurn },
		{ "heavy_pending_enemy_collision_multiplier",
			heavyState.pendingEnemyCollisionMultiplier },
		{ "heavy_active_enemy_collision_multiplier",
			heavyState.activeEnemyCollisionMultiplier },
		{ "pierce_stacks", game.GetPierceTraceState().stackCount },
		{ "pierce_stack_max", PierceTraceRules::MaxStackCount },
		{ "pierce_trace_count",
			static_cast<int>(game.GetPierceTraceState().traces.size()) },
		{ "pierce_trace_max",
			static_cast<int>(PierceTraceRules::MaxTraceCount) },
		{ "player_anchor_stacks", game.GetPlayerAnchorStacks() },
		{ "anchor_conversion_progress", game.GetAnchorConversionProgress() },
		{ "anchor_next_grant_bonus", anchorState.nextAnchorGrantBonus },
		{ "origin_return_pending", anchorState.returnPositionPending },
		{ "origin_return_position", VectorToJson(anchorState.returnPosition) },
		{ "cushion_strong_uses_this_shot", game.GetCushionStrongUsesThisShot() },
		{ "pierce_traces", nlohmann::json::array() },
		{ "enemy_anchor_stacks", nlohmann::json::array() },
		{ "reflection", {
			{ "shared_stack_count", cushionCharges.stackCount },
			{ "pending_setup_bonus", cushionCharges.pendingSetupBonus },
			{ "active_setup_bonus", cushionCharges.activeSetupBonus },
			{ "setup_triggered_this_shot", cushionCharges.setupTriggeredThisShot },
			{ "valid_reflections_this_shot", cushionCharges.validReflectionsThisShot },
			{ "generated_this_shot", cushionCharges.generatedThisShot },
			{ "consumed_this_shot", cushionCharges.consumedThisShot },
			{ "empowered_hits_remaining", cushionCharges.empoweredHitsRemaining },
			{ "pinball_target_id", nullptr },
			{ "pinball_collision_count", cushionCharges.pinballCollisionCount },
			{ "pinball_collisions_per_stage", cushionCharges.pinballCollisionsPerStage },
			{ "pinball_speed_retention", cushionCharges.pinballSpeedRetention },
		} },
		{ "current_shot_effects", {
			{ "heavy_enemy_collision_multiplier",
				heavyState.activeEnemyCollisionMultiplier },
			{ "reflection_setup_bonus", cushionCharges.activeSetupBonus },
			{ "lightweight_targets", nlohmann::json::array() },
		} },
		{ "next_shot_effects", {
			{ "heavy_enemy_collision_multiplier",
				heavyState.pendingEnemyCollisionMultiplier },
			{ "reflection_setup_bonus", cushionCharges.pendingSetupBonus },
			{ "anchor_grant_bonus", anchorState.nextAnchorGrantBonus },
			{ "origin_return_pending", anchorState.returnPositionPending },
			{ "lightweight_targets", nlohmann::json::array() },
		} },
	};
	state["latest_shot_physics_chain"] =
		BalanceLogger::GetInstance().GetLatestShotPhysicsTelemetry();
	state["latest_shot_pierce_attack"] =
		BalanceLogger::GetInstance().GetLatestShotPierceAttackTelemetry();
	state["latest_shot_bounce_axis"] =
		BalanceLogger::GetInstance().GetLatestShotBounceTelemetry();
	state["latest_shot_combat_result"] =
		BalanceLogger::GetInstance().GetLatestShotCombatResult();
	state["balance_metadata"] =
		BalanceLogger::GetInstance().GetBalanceMetadata();
	for (const auto& trace : game.GetPierceTraceState().traces)
	{
		state["ball_synergies"]["pierce_traces"].push_back({
			{ "id", trace.id },
			{ "start", { trace.start.x, trace.start.y, trace.start.z } },
			{ "end", { trace.end.x, trace.end.y, trace.end.z } },
			{ "direction", { trace.direction.x, trace.direction.y, trace.direction.z } },
		});
	}
	for (std::size_t enemyIndex = 0;
		enemyIndex < enemies.size();
		++enemyIndex)
	{
		EnemyBall* enemy = enemies[enemyIndex];
		if (enemy == nullptr || !enemy->CanParticipateInCombat()) continue;
		const std::string targetId =
			"enemy:" + std::to_string(enemyIndex);
		state["ball_synergies"]["enemy_anchor_stacks"].push_back({
			{ "target_id", targetId },
			{ "enemy_id", enemy->GetEnemyId() },
			{ "stacks", enemy->GetAnchorStacks() },
		});
		if (enemy->GetPendingKnockbackMultiplier() > 1.0f)
		{
			state["ball_synergies"]["next_shot_effects"]
				["lightweight_targets"].push_back({
					{ "target_id", targetId },
					{ "enemy_id", enemy->GetEnemyId() },
					{ "knockback_multiplier",
						enemy->GetPendingKnockbackMultiplier() },
				});
		}
		if (enemy->GetNextKnockbackMultiplier() > 1.0f)
		{
			state["ball_synergies"]["current_shot_effects"]
				["lightweight_targets"].push_back({
					{ "target_id", targetId },
					{ "enemy_id", enemy->GetEnemyId() },
					{ "knockback_multiplier",
						enemy->GetNextKnockbackMultiplier() },
				});
		}
		if (cushionCharges.pinballTargetId ==
			reinterpret_cast<std::uintptr_t>(enemy))
		{
			state["ball_synergies"]["reflection"]
				["pinball_target_id"] = targetId;
		}
	}
	// Preserve the original table aliases for existing MCP clients.
	state["table"]["reflection_stacks"] = cushionCharges.stackCount;
	state["table"]["reflection_setup_pending"] =
		cushionCharges.pendingSetupBonus;
}

}
