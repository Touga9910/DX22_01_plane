#include "../BalanceLogger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
    json artifacts = { { "fixture_schema_version", 1 },
        { "cases", json::array() }, { "checks", json::array() } };

    void Check(bool condition, const std::string& description)
    {
        if (!condition) throw std::runtime_error(description);
        artifacts["checks"].push_back(description);
    }

    void WriteText(const fs::path& path, const std::string& value)
    {
        if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << value;
        if (!file) throw std::runtime_error("Cannot write fixture file: " + path.string());
    }

    void WriteJson(const fs::path& path, const json& value)
    {
        WriteText(path, value.dump(2) + '\n');
    }

    std::string ReadText(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot read fixture file: " + path.string());
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }

    json ReadLog(const fs::path& path) { return json::parse(ReadText(path)); }

    json Latest(const BalanceLogger& logger)
    {
        return {
            { "physics_chain", logger.GetLatestShotPhysicsTelemetry() },
            { "pierce_attack", logger.GetLatestShotPierceAttackTelemetry() },
            { "bounce_axis", logger.GetLatestShotBounceTelemetry() },
            { "combat_result", logger.GetLatestShotCombatResult() },
        };
    }

    void CheckLatestCleared(const BalanceLogger& logger)
    {
        const json latest = Latest(logger);
        for (const auto& entry : latest.items())
            Check(entry.value().is_null(), "Latest reset: " + entry.key());
    }

    json CheckLatestMatchesShot(const BalanceLogger& logger, const json& shot)
    {
        const json latest = Latest(logger);
        for (const char* key : { "physics_chain", "pierce_attack", "bounce_axis" })
            Check(latest.at(key) == shot.at(key), std::string("Latest equals saved shot: ") + key);
        const json& combat = latest.at("combat_result");
        Check(combat.at("measurement_scope") == "all_recorded_enemy_hp_damage", "Combat measurement scope");
        for (const char* key : { "shot_no", "ball_id", "upgrade_level", "total_enemy_damage",
                 "player_enemy_damage", "enemy_enemy_damage", "player_enemy_hit_count",
                 "enemy_enemy_hit_count", "enemies_defeated", "enemies_alive_after",
                 "player_hp_after", "decision_log" })
            Check(combat.at(key) == shot.at(key), std::string("Combat summary equals saved shot: ") + key);
        return latest;
    }

    void SaveCase(const std::string& name, const fs::path& path,
        const json& latestShots = json::array(), const json& checkpoints = json::object())
    {
        artifacts["cases"].push_back({ { "name", name }, { "run", ReadLog(path) },
            { "latest_shots", latestShots }, { "checkpoints", checkpoints } });
    }

    void WriteRegistry(const std::string& version = "fixture-1")
    {
        WriteJson("assets/data/balance_version.json", {
            { "current_version", version }, { "current_config_hash", "fixture-config" },
            { "registered_git_commit", "fixture-commit" },
            { "managed_files", { "assets/data/fixture.bin", "assets/data/missing.bin", 17 } },
        });
    }

    fs::path BeginRun(BalanceLogger& logger, const json& additions = json::object())
    {
        // Make millisecond-based run IDs distinct without changing logger clocks.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        json context = { { "controller_profile", "fixture-controller" },
            { "build_profile", "standard" }, { "build_profile_settings_hash", "fixture-build" },
            { "randomness", { { "run_seed", 1234 } } },
            { "validation", { { "enabled", false } } } };
        context.update(additions);
        BalanceBallSnapshot ball;
        ball.id = "player_standard";
        ball.instanceId = 7;
        ball.attack = 3;
        logger.BeginRun(50, 48, { ball }, "fixture", context);
        Check(logger.IsRunActive() && !logger.IsStageActive() && !logger.IsShotActive(), "BeginRun flags");
        CheckLatestCleared(logger);
        return logger.GetLogPath();
    }

    void BeginStage(BalanceLogger& logger, const std::string& id = "fixture-stage")
    {
        BalanceEnemySnapshot enemy;
        enemy.id = "enemy_fixture";
        enemy.maxHp = 10;
        enemy.attack = 2;
        logger.BeginStage(id, "normal", 1, 48, 50, { enemy, enemy });
        Check(logger.IsStageActive() && !logger.IsShotActive(), "BeginStage flags");
        CheckLatestCleared(logger);
    }

    void BeginShot(BalanceLogger& logger, const json& context = json::object())
    {
        const std::vector<BalanceShotEnemySnapshot> enemies = {
            { 101, 2, "enemy_a", 10, 10, -2.0f, 4.0f },
            { 202, 7, "enemy_b", 10, 10, 2.0f, 4.0f },
        };
        logger.BeginShot("player_reflection_setup", 1, 4.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f, 48, 2, 0, enemies, context);
        Check(logger.IsShotActive(), "BeginShot flags");
    }

    void CheckManagedConfiguration(const json& run)
    {
        Check(run.at("schema_version") == 4 && run.at("data_schema_version") == 4, "Run schema remains version 4");
        Check(run.at("balance_version") == "fixture-1", "Registry balance version");
        Check(run.at("balance_config_hash") == "fixture-config", "Registry configuration hash");
        Check(run.at("build").at("git_commit") == "fixture-commit", "Registry commit");
        Check(run.at("controller").at("build_profile_settings_hash") == "fixture-build", "Build condition preserved");
        const json& files = run.at("configuration").at("files");
        Check(files.size() == 2, "Managed files ignore non-string entries");
        Check(files[0].at("path") == "assets/data/fixture.bin", "Managed file order");
        Check(files[0].at("exists") == true && files[0].at("size_bytes") == 5, "Managed binary size");
        Check(files[0].at("fnv1a64") == "a430d84680aabd0b", "Known hello FNV-1a64");
        Check(files[1] == json({ { "path", "assets/data/missing.bin" }, { "exists", false } }), "Missing file has no invented fingerprint");
    }

    void TestSchemaAndBatchedWrites(BalanceLogger& logger)
    {
        const fs::path path = BeginRun(logger);
        CheckManagedConfiguration(ReadLog(path));
        BeginStage(logger);
        const std::string beforeEvents = ReadText(path);
        logger.RecordEvent("queued_progression", { { "value", 7 } });
        logger.RecordPlayerDamage("fixture-trap", 2);
        Check(ReadText(path) == beforeEvents, "RecordEvent and player damage are dirty-only");
        BeginShot(logger);
        const json checkpoint = ReadLog(path);
        Check(checkpoint.at("events").size() == 1, "BeginShot flushes pending root event");
        Check(checkpoint.at("stages")[0].at("damage_events").size() == 1, "BeginShot flushes pending player damage");
        Check(checkpoint.at("stages")[0].at("shots").empty(), "Pending shot is absent until EndShot");
        logger.EndShot(46, 2, 0);
        const json shot = ReadLog(path).at("stages")[0].at("shots")[0];
        Check(shot.at("player_enemy_damage") == 0 && shot.at("enemy_enemy_damage") == 0, "No contacts mean known zero damage");
        const json latest = CheckLatestMatchesShot(logger, shot);
        const std::string afterShot = ReadText(path);
        logger.EndShot(1, 0, 99);
        Check(ReadText(path) == afterShot, "Duplicate EndShot does not write or append");
        logger.EndStage("clear", 46, 50, 0);
        const json result = ReadLog(path).at("stages")[0].at("stage_result");
        Check(result.at("turn_count") == 1 && result.at("player_damage_taken") == 2, "Stage summarizes flushed damage and one shot");
        const std::string afterStage = ReadText(path);
        logger.EndStage("ignored", 0, 50, 99);
        Check(ReadText(path) == afterStage, "Duplicate EndStage is ignored");
        logger.EndRun("completed", 46, 50, 1);
        const std::string afterRun = ReadText(path);
        logger.EndRun("ignored", 0, 50, 0);
        logger.RecordEvent("ignored_after_run");
        Check(ReadText(path) == afterRun, "Duplicate EndRun and post-run event are ignored");
        SaveCase("schema_and_batching", path, json::array({ latest }), checkpoint);
    }

    void TestPocketDamageAttribution(BalanceLogger& logger)
    {
        const fs::path path = BeginRun(logger);
        BeginStage(logger);
        BeginShot(logger);
        logger.RecordDamageCollision(BalanceCollisionType::PlayerEnemy, 1, 0);
        logger.RecordNonCollisionEnemyDamage("boss_pocket", "enemy_boss_core", 15);
        logger.EndShot(50, 1, 0);
        const json shot = ReadLog(path).at("stages")[0].at("shots")[0];
        Check(shot.at("player_enemy_damage") == 1, "Pocket damage is not direct contact damage");
        Check(shot.at("non_collision_enemy_damage") == 15 && shot.at("total_enemy_damage") == 16,
            "Shot total includes pocket damage once");
        Check(shot.at("enemy_damage_events").back().at("source") == "boss_pocket", "Pocket source is recorded");
        logger.EndStage("clear", 50, 50, 0);
        const json result = ReadLog(path).at("stages")[0].at("stage_result");
        Check(result.at("total_enemy_damage") == 16 && result.at("non_collision_enemy_damage") == 15,
            "Stage total includes pocket damage once");
        logger.EndRun("completed", 50, 50, 1);
        SaveCase("pocket_damage_attribution", path);
    }

    void TestUnknownAndZeroDamage(BalanceLogger& logger)
    {
        const fs::path path = BeginRun(logger);
        BeginStage(logger);
        json latest = json::array();
        BeginShot(logger);
        logger.RecordDamageCollision(BalanceCollisionType::PlayerEnemy);
        logger.EndShot(48, 2, 0);
        json shot = ReadLog(path).at("stages")[0].at("shots")[0];
        Check(shot.at("player_enemy_damage").is_null() && shot.at("total_enemy_damage").is_null(), "Unobserved contact damage remains null");
        Check(shot.at("enemy_enemy_damage") == 0, "Untouched collision channel remains zero");
        Check(shot.at("collision_effect") == 1 && shot.at("shot_score") == 39.35, "One player contact has the established score");
        latest.push_back(CheckLatestMatchesShot(logger, shot));
        BeginShot(logger);
        logger.RecordDamageCollision(BalanceCollisionType::PlayerEnemy, 0, 0);
        logger.EndShot(48, 2, 0);
        shot = ReadLog(path).at("stages")[0].at("shots")[1];
        Check(shot.at("player_enemy_damage").is_number_integer() && shot.at("total_enemy_damage") == 0, "Observed zero remains numeric zero");
        latest.push_back(CheckLatestMatchesShot(logger, shot));
        logger.EndStage("game_over", 0, 50, 0);
        const json result = ReadLog(path).at("stages")[0].at("stage_result");
        Check(result.at("player_enemy_damage").is_null() && result.at("enemy_enemy_damage").is_null() && result.at("total_enemy_damage").is_null(), "An unknown shot keeps the stage damage aggregate unknown");
        logger.EndRun("game_over", 0, 50, 0);
        SaveCase("unknown_and_zero_damage", path, latest);
    }

    void TestTimelineAndTacticalAggregation(BalanceLogger& logger)
    {
        const fs::path path = BeginRun(logger);
        BeginStage(logger);
        BeginShot(logger, {
            { "heavy_stacks_before", 2 }, { "heavy_stacks_consumed_on_start", 1 },
            { "heavy_stack_gain_limit_per_turn", 4 }, { "heavy_stack_distance_per_stack", 40.0 },
            { "pierce_stacks_before", 14 }, { "pierce_stacks_consumed_on_start", 2 },
            { "reflection_stacks_before", 3 },
            { "mcp_telemetry", { { "build_evaluation", {
                { "target_id", "enemy:7" }, { "metrics", { { "damage", 9.0 } } },
                { "tactical_outcomes", { { "charge_interruptions", json::array({ "enemy:7" }) } } },
            } } } },
        });
        logger.RecordDamageCollision(BalanceCollisionType::PlayerEnemy, 3);
        logger.RecordPierceAttack(PierceAttackSource::PlayerDirect, 0, 101, 4, 3, true);
        logger.RecordWallReflection(0, "table", "player", 4, 8, 5, 0, -4, 0, -4.4f, 0);
        logger.RecordEvent("cushion_empowered_hit", { { "target_id", std::uint64_t(202) }, { "damage_bonus", 2 } });
        logger.RecordDamageCollision(BalanceCollisionType::PlayerEnemy, 5);
        logger.RecordPierceAttack(PierceAttackSource::PlayerPierce, 0, 202, 7, 5, true);
        logger.RecordDamageCollision(BalanceCollisionType::PlayerEnemy, 1);
        logger.RecordPierceAttack(PierceAttackSource::PlayerDirect, 0, 202, 2, 1, true);
        logger.RecordBounceSpecialDamage(2, 1, 1);
        logger.RecordDamageCollision(BalanceCollisionType::PlayerEnemy, 0);
        logger.RecordPierceAttack(PierceAttackSource::PlayerDirect, 0, 202, 1, 0, false);
        logger.RecordEnemyEnemyCollision(101, "enemy_a", 202, "enemy_b", 2, 0, 4);
        logger.RecordEnemyDamage("enemy_a", 1);
        logger.RecordEnemyMovement(101, "enemy_a", 2.5f);
        logger.RecordPhysicsStep(10);
        logger.RecordPhysicsStep(-5);
        logger.RecordEvent("heavy_distance_stacks_generated", { { "uncapped_amount", 5 }, { "granted_amount", 3 }, { "limited_amount", 2 } });
        logger.RecordEvent("heavy_micro_stack_generated");
        logger.RecordEvent("heavy_collision_boost_armed", { { "consumed", 1 } });
        logger.RecordEvent("pierce_trace_generated", { { "requested_amount", 5 }, { "granted_amount", 5 } });
        logger.RecordEvent("cushion_reflection_resolved", { { "valid_reflections_this_shot", 1 }, { "uncapped_generated", 2 }, { "generated", 2 }, { "consumed", 1 } });
        logger.RecordEvent("pinball_activation_attempted", { { "activated", false } });
        logger.RecordEvent("pinball_activation_attempted", { { "activated", true }, { "consumed", 2 } });
        logger.RecordEvent("enemy_charge_interrupted", { { "target_id", "a" }, { "planned_action", "charge_attack" }, { "prevented_attack_damage", 3 } });
        logger.RecordEvent("enemy_charge_interrupted", { { "target_id", "a" }, { "planned_action", "charge_attack" }, { "prevented_attack_damage", 5 } });
        logger.RecordEvent("enemy_defeated", { { "target_id", "a" }, { "prevented_next_turn_damage_before_defense", 4 }, { "prevented_future_charge_damage_before_defense", 2 } });
        logger.RecordEvent("enemy_charge_interrupted", { { "target_id", "b" }, { "planned_action", "charge" }, { "prevented_attack_damage", 6 } });
        logger.RecordEvent("enemy_board_ability_state_changed", { { "target_id", "a" }, { "ability_id", "swarm_amplifier" }, { "previous_attack", 7 }, { "current_attack", 1 }, { "affects_next_turn", true } });
        logger.RecordEvent("enemy_board_ability_state_changed", { { "target_id", "b" }, { "ability_id", "impact_brittle" }, { "previous_attack", 4 }, { "current_attack", 2 }, { "affects_next_turn", true } });
        logger.RecordEvent("enemy_board_ability_state_changed", { { "ability_id", "protection_resonance" }, { "reason", "protection_removed" } });
        logger.EndShot(48, 1, 1);
        const json shot = ReadLog(path).at("stages")[0].at("shots")[0];
        const json& bounce = shot.at("bounce_axis");
        const json& timeline = bounce.at("attack_timeline");
        Check(timeline.size() == 4, "One timeline entry per tracked contact attack");
        Check(timeline[0].at("timeline_event_index") == 0 && timeline[1].at("timeline_event_index") == 2 && timeline[3].at("timeline_event_index") == 4, "Wall and attack share chronological indices");
        Check(timeline[0].at("target_index") == 0 && timeline[1].at("target_index") == 1 && timeline[3].at("target_index") == 1, "Timeline target indices remain stable");
        Check(timeline[0].at("timing_phase") == "before_first_reflection" && timeline[1].at("timing_phase") == "after_first_reflection", "First accepted reflection separates timing phases");
        Check(timeline[1].at("reflection_derivation") == "cushion_empowered_hit" && timeline[1].at("reflection_bonus_calculated_damage") == 2, "Empowered bonus attaches to its next target attack");
        Check(timeline[2].at("reflection_derivation") == "pinball_damage_stage" && timeline[2].at("reflection_bonus_applied_hp_damage") == 1, "Special damage decorates the immediately preceding attack");
        Check(timeline[3].at("reflection_derived") == false, "Empowered bonus is consumed once");
        Check(bounce.at("attack_timing").at("before_first_reflection").at("applied_hp_damage") == 3 && bounce.at("attack_timing").at("after_first_reflection").at("applied_hp_damage") == 6, "Timing damage totals are exclusive");
        Check(bounce.at("attack_timing").at("after_first_reflection").at("effective_attack_count") == 2, "Zero HP damage is not an effective attack");
        Check(shot.at("total_enemy_damage") == 10 && shot.at("pierce_attack").at("total").at("applied_hp_damage") == 9, "Generic combat and tracked attack scopes remain distinct");
        Check(shot.at("physics_chain").at("enemy_distances")[0].at("enemy_index") == 2 && shot.at("physics_chain").at("enemy_distances")[1].at("enemy_index") == 7, "Physics indices preserve caller descriptors");
        Check(shot.at("physics_chain").at("physics_processing_microseconds") == 10 && shot.at("physics_chain").at("physics_step_count") == 2, "Negative processing time clamps while steps remain counted");
        Check(shot.at("heavy_stacks_generated") == 4 && shot.at("heavy_stacks_uncapped_gain") == 6 && shot.at("heavy_stacks_after") == 4, "Heavy current and legacy event forms aggregate together");
        Check(shot.at("pierce_stacks_after") == 15 && shot.at("pierce_stack_max_reached") == true, "Pierce stack storage is clamped");
        Check(shot.at("reflection_stacks_after") == 2 && shot.at("reflection_stack_max").is_null(), "Reflection consumption has no invented maximum");
        const json& decision = shot.at("decision_log");
        Check(decision.at("actual_next_turn_prevention_before_defense") == 8 && decision.at("actual_future_charge_prevention") == 8 && decision.at("actual_attack_prevention") == 16, "Prevention credits the maximum per target and separate horizon");
        Check(decision.at("actual_next_turn_prevention_by_target").at("a").at("reason") == "swarm_stage_reduction", "Stronger prevention replaces weaker credit");
        Check(decision.at("charge_interruption_candidates_generated") == 0 && decision.at("charge_interruption_candidate_adopted") == true, "Decision fields retain supplied and default values");
        const json latest = CheckLatestMatchesShot(logger, shot);
        logger.EndStage("clear", 48, 50, 1);
        logger.EndRun("completed", 48, 50, 1);
        SaveCase("timeline_and_tactical", path, json::array({ latest }));
    }

    void TestAutomaticShotEndAndEmptyStage(BalanceLogger& logger)
    {
        const fs::path path = BeginRun(logger, { { "debug_sandbox", true } });
        Check(path.parent_path() == fs::path("logs/debug_battle"), "Debug logs are isolated from balance logs");
        BeginStage(logger, "automatic-shot-end");
        BeginShot(logger);
        logger.RecordDamageCollision(BalanceCollisionType::PlayerEnemy, 0);
        logger.EndStage("clear", 48, 50, 0);
        Check(!logger.IsStageActive() && !logger.IsShotActive(), "EndStage ends an active shot first");
        json run = ReadLog(path);
        Check(run.at("stages")[0].at("shots").size() == 1, "Automatic EndShot appends exactly once");
        const json latest = CheckLatestMatchesShot(logger, run.at("stages")[0].at("shots")[0]);
        BeginStage(logger, "empty-stage");
        logger.EndStage("clear", 48, 50, 0);
        run = ReadLog(path);
        const json& result = run.at("stages")[1].at("stage_result");
        Check(result.at("total_shots") == 0 && result.at("no_hit_shot_rate") == 0.0 && result.at("average_shot_score") == 0.0, "An empty stage has finite zero summaries");
        Check(run.at("stages")[0].at("stage_index") == 1 && run.at("stages")[1].at("stage_index") == 2, "Stage indices are one based");
        logger.EndRun("application_exit", 48, 50, 2);
        SaveCase("automatic_shot_end_and_empty_stage", path, json::array({ latest }));
    }

    void TestRestartAndAbandon(BalanceLogger& logger)
    {
        const fs::path previousPath = BeginRun(logger);
        BeginStage(logger, "abandoned-stage");
        BeginShot(logger);
        BeginStage(logger, "replacement-stage");
        json previous = ReadLog(previousPath);
        Check(previous.at("stages")[0].at("stage_result").at("result") == "abandoned", "BeginStage closes the previous stage as abandoned");
        Check(previous.at("stages")[0].at("shots").size() == 1, "Stage replacement finalizes the pending shot");
        BeginShot(logger);
        const fs::path currentPath = BeginRun(logger);
        Check(currentPath != previousPath, "Restart writes a distinct run file");
        previous = ReadLog(previousPath);
        Check(previous.at("run_result").at("result") == "restarted", "BeginRun finalizes an active run as restarted");
        Check(previous.at("stages")[1].at("stage_result").at("result") == "abandoned", "Restart abandons its active stage");
        Check(previous.at("stages")[1].at("shots").size() == 1, "Restart finalizes its pending shot");
        SaveCase("restart_previous", previousPath);
        logger.EndRun("completed", 48, 50, 0);
        Check(ReadLog(currentPath).at("stages").empty(), "Restart begins with an empty run");
        SaveCase("restart_current", currentPath);
    }

    void CheckFallbackConfiguration(const json& run)
    {
        const json expected = {
            "assets/data/stage_01.json", "assets/data/enemy_data.json",
            "assets/data/player_status.json", "assets/data/player_deck.json",
            "assets/data/dynamic_balance.json", "assets/data/difficulty_profiles.json",
            "assets/data/encounter_balance.json", "assets/data/pocket_rules.json",
        };
        const json& files = run.at("configuration").at("files");
        Check(files.size() == expected.size(), "Fallback configuration contains eight files");
        for (std::size_t index = 0; index < expected.size(); ++index)
            Check(files[index] == json({ { "path", expected[index] }, { "exists", false } }), "Fallback file order and missing status " + std::to_string(index));
        Check(run.at("balance_version") == "Legacy/Unknown" && run.at("balance_config_hash") == "", "Invalid registry retains legacy metadata defaults");
    }

    void TestConfigurationFallbackAndReload(BalanceLogger& logger)
    {
        WriteText("assets/data/balance_version.json", "{");
        fs::path path = BeginRun(logger);
        CheckFallbackConfiguration(ReadLog(path));
        logger.EndRun("completed", 48, 50, 0);
        SaveCase("malformed_configuration_fallback", path);
        fs::remove("assets/data/balance_version.json");
        path = BeginRun(logger);
        CheckFallbackConfiguration(ReadLog(path));
        logger.EndRun("completed", 48, 50, 0);
        SaveCase("missing_configuration_fallback", path);
        WriteRegistry("fixture-2");
        path = BeginRun(logger);
        Check(ReadLog(path).at("balance_version") == "fixture-2", "BeginRun reloads metadata rather than using the singleton constructor cache");
        logger.EndRun("completed", 48, 50, 0);
        SaveCase("configuration_reloaded", path);
    }
}

int main()
{
    bool fixturePrepared = false;
    try
    {
        // The caller chooses an isolated CWD. Never replace project assets.
        if (fs::exists("assets") || fs::exists("logs") || fs::exists("fixture_cases.json"))
            throw std::runtime_error("Use a fresh isolated working directory for each fixture run.");
        fixturePrepared = true;
        WriteText("assets/data/fixture.bin", "hello");
        WriteRegistry();
        BalanceLogger& logger = BalanceLogger::GetInstance();
        Check(!logger.IsRunActive() && !logger.IsStageActive() && !logger.IsShotActive(), "Singleton starts inactive");
        logger.RecordEvent("ignored_before_run");
        TestSchemaAndBatchedWrites(logger);
        TestPocketDamageAttribution(logger);
        TestUnknownAndZeroDamage(logger);
        TestTimelineAndTacticalAggregation(logger);
        TestAutomaticShotEndAndEmptyStage(logger);
        TestRestartAndAbandon(logger);
        TestConfigurationFallbackAndReload(logger);
        artifacts["all_passed"] = true;
        WriteJson("fixture_cases.json", artifacts);
        std::cout << "BalanceLogger fixture passed: " << artifacts["checks"].size()
                  << " checks, " << artifacts["cases"].size() << " run artifacts\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        artifacts["all_passed"] = false;
        artifacts["failure"] = error.what();
        // Avoid writing into an existing project tree when the isolation guard failed.
        if (fixturePrepared)
        {
            try { WriteJson("fixture_failure.json", artifacts); } catch (...) {}
        }
        std::cerr << "BalanceLogger fixture failed: " << error.what() << '\n';
        return 1;
    }
}
