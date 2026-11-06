# Balance log schema v3

`logs/balance/run_*.json` is written incrementally during a run. Version 3
keeps the version 1/2 fields used by the existing analyzers and adds the MCP
build policy as an independent comparison condition.

## Run fields

- `controller_type`: Compatibility field (`human`, `mcp`, or `autoplay`).
- `controller`: Controller type, MCP player-skill profile, build profile, and
  build-profile settings SHA-256.
- `run_context`: Initial money/progress, the 15-area goal, run phase, DDA
  state, and known random seeds.
- `run_context.build_profile` and `build_profile_settings_hash`: Build policy
  fixed when the run starts. Skill and build are recorded independently.
- `run_context.validation`: Experiment ID and fixed-condition guarantees.
- `run_context.baseline_difficulty`: Fixed difficulty profile, separate from
  DDA assist mode.
- `configuration.files`: Path, size, and FNV-1a fingerprint of each balance
  configuration file.
- `build`: Compile date/time and compiler identity.
- `initial_player.deck[].instance_id`: Stable ball instance identifier.
- `events`: Route, reward, rest, shop, relic, stage-money, and DDA decisions.
- `events`: MCP acquisition and upgrade decisions are recorded as
  `mcp_build_decision`, including the selected build ID and decision reason.
- `events`: Pocket outcomes are recorded as `enemy_pocket_finisher`,
  `enemy_pocket_controlled`, and `enemy_pocket_returned` so defeats and
  temporary attack prevention can be analyzed separately.
- `events`: Finite-run progression uses `route_area_completed`,
  `boss_preparation_entered`, `boss_preparation_completed`, and
  `final_boss_defeated`. The final boss is not counted as area 16.

`stage_selection_seed` is `null` until stage selection uses a stored seed.
This is intentional; an unknown seed must not be reported as deterministic.

## Stage fields

- `stage_context.progress`, `area_progress`, `area_goal`, `run_phase`, `par`,
  and `money`.
- `stage_context.layout_source`: `stage_data`, `dense_auto_layout`, or
  `mcp_override`.
- `stage_context.deck`: Full deck snapshot at stage start.
- `stage_context.owned_relics`: Relics owned at stage start.
- `stage_context.dynamic_balance`: Whether DDA was applied, applied level,
  and the resulting enemy HP/attack modifiers.
- `stage_context.progression_scaling`: Fixed late-run enemy HP/attack
  modifiers that remain active in both DDA variants.
- `stage_context.encounter_threat`: Enemy-cost sum, layout multiplier, target
  budget, and deviation.
- `damage_events`: Damage taken by the player, including source and enemy ID
  when available.
- `stage_result.player_damage_taken`: Total observed player damage.
- `stage_result.*_damage`: Actual enemy HP removed by collision source.
- Pocket damage to the player is recorded in `damage_events` with source
  `pocket`; it ignores defense and uses the configured max-HP ratio.

## Shot fields

- `shot_context.selected_offer_index` and `offers`: What balls were offered
  and which instance was selected.
- `shot_context.effective_attack` / `effective_defense`: Values after relics.
- `shot_context.mcp_telemetry`: Target, shot type, player profile, build
  profile/hash, ball-selection reason, ideal and actual aim points, requested
  and actual power, and applied human error.
- `enemy_damage_events`: Actual damage dealt to each enemy, grouped by
  player-enemy or enemy-enemy collision.
- `player_enemy_damage`, `enemy_enemy_damage`, `total_enemy_damage`:
  Per-shot damage totals.
- `heavy_stack_distance_per_stack`: Effective JSON-configured distance needed
  for one distance-derived Heavy stack.
- `heavy_stacks_before`, `heavy_stacks_uncapped_gain`,
  `heavy_stacks_generated`, `heavy_stacks_consumed`, and
  `heavy_stacks_after`: Per-shot Heavy resource flow.
- `heavy_stack_gain_limit_per_turn` and
  `heavy_stacks_discarded_by_turn_limit`: Effective turn cap and overflow.
- `physics_chain`: XZ enemy movement and effective enemy-enemy collision
  telemetry; the MCP state exposes the same latest snapshot as
  `latest_shot_physics_chain`.
- `pierce_attack`: Exclusive attack-source totals for `player_direct`,
  `player_pierce`, `minion`, `trace`, and `other`. Each source records attack
  checks, live-target hits, positive-HP-damage attacks, calculated damage,
  applied HP damage, and overkill. It also records unique effective targets,
  generated/maximum-simultaneous minions, and each minion's movement,
  lifetime, destruction reason, attacks, damage, and unique targets. The MCP
  state exposes the same latest snapshot as `latest_shot_pierce_attack`.
- `pierce_stacks_before`, `pierce_stacks_uncapped_gain`,
  `pierce_stacks_generated`, `pierce_stacks_consumed`, and
  `pierce_stacks_after`: Per-shot Pierce resource flow. `pierce_stack_max`,
  `pierce_stack_max_reached`, and `pierce_minion_fizzle_count` expose the
  storage cap and a consumer activation that launched no minion.
- `bounce_axis`: Reflection-axis shot telemetry. It separates accepted
  physical wall reflections for the player and pinball target from effective
  player reflections resolved by `CushionChargeRules`, and records effective
  enemy attacks, unique targets, stack generation/consumption, pinball
  activation/fizzle, empowered-hit activations, staged bonus damage,
  duration, and physics processing.
  Individual wall events include wall index/group and velocities before
  reflection, after physical restitution, and after reflection-axis effects.
  `timeline_event_index` orders those reflections against `attack_timeline`.
  Each attack is classified exclusively as `before_first_reflection` or
  `after_first_reflection`; `reflection_derived` is an independent attribute,
  so an empowered or pinball-stage attack is not counted a second time.
  `source` distinguishes player-enemy from enemy-enemy contact, while
  `telemetry_source` retains the Pierce collector's source category.
  `attack_timing` aggregates checks, live-target hits, effective attacks,
  actual HP damage, overkill, and unique effective targets for both phases.
  The MCP state exposes the same latest snapshot as
  `latest_shot_bounce_axis`.
- `reflection_stacks_before`, `reflection_stacks_uncapped_gain`,
  `reflection_stacks_generated`, `reflection_stacks_consumed`, and
  `reflection_stacks_after`: Per-shot reflection resource flow.
  `reflection_stack_max` is `null` because the current rules have no
  gameplay storage cap; `reflection_stacks_discarded_by_max` is therefore
  zero.

Older version 1/2 logs remain valid input for `tools/analyze_balance_logs.py`
and `tools/adjust_balance_from_logs.py`; the added fields are optional to
those tools.

The analyzer derives a configuration-suite SHA-256 fingerprint from the
sorted file paths and their logged FNV-1a values. This derived value is an
analysis cohort identifier; it does not replace the original per-file values
in the run log. Use `--latest-configuration` or
`--configuration-fingerprint` when current and historical logs share a
directory.

Use `--build-profile` to keep different collection strategies out of the same
analysis cohort. Reports also list selected build IDs and settings hashes so a
profile edit cannot silently masquerade as the old policy.

Analysis report schema version 7 separates `stages` encounter evaluations
from the top-level `run_balance` milestone evaluation. This is a report-schema
version independent from balance run log schema version 3. Version 7 includes
confidence intervals and sequential-sampling recommendations, descriptive
`system_metrics`, telemetry-derived failure and difficulty diagnostics, and
optional human playtest feedback. The human feedback remains a separate design
signal and is not an automatic balance-adjustment input.
