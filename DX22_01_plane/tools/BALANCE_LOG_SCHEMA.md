# Balance log schema v2

`logs/balance/run_*.json` is written incrementally during a run. Version 2
keeps the version 1 fields used by the existing analyzers and adds causal
context for comparing balance changes.

## Run fields

- `controller_type`: Compatibility field (`human`, `mcp`, or `autoplay`).
- `controller`: Controller type and MCP player profile.
- `run_context`: Initial money/progress, DDA state, and known random seeds.
- `run_context.validation`: Experiment ID and fixed-condition guarantees.
- `run_context.baseline_difficulty`: Fixed difficulty profile, separate from
  DDA assist mode.
- `configuration.files`: Path, size, and FNV-1a fingerprint of each balance
  configuration file.
- `build`: Compile date/time and compiler identity.
- `initial_player.deck[].instance_id`: Stable ball instance identifier.
- `events`: Route, reward, rest, shop, relic, stage-money, and DDA decisions.
- `events`: Pocket outcomes are recorded as `enemy_pocket_finisher`,
  `enemy_pocket_controlled`, and `enemy_pocket_returned` so defeats and
  temporary attack prevention can be analyzed separately.

`stage_selection_seed` is `null` until stage selection uses a stored seed.
This is intentional; an unknown seed must not be reported as deterministic.

## Stage fields

- `stage_context.progress`, `par`, `money`.
- `stage_context.layout_source`: `stage_data`, `dense_auto_layout`, or
  `mcp_override`.
- `stage_context.deck`: Full deck snapshot at stage start.
- `stage_context.owned_relics`: Relics owned at stage start.
- `stage_context.dynamic_balance`: Whether DDA was applied, applied level,
  and the resulting enemy HP/attack modifiers.
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
- `shot_context.mcp_telemetry`: Target, shot type, player profile, ideal and
  actual aim points, requested and actual power, and applied human error.
- `enemy_damage_events`: Actual damage dealt to each enemy, grouped by
  player-enemy or enemy-enemy collision.
- `player_enemy_damage`, `enemy_enemy_damage`, `total_enemy_damage`:
  Per-shot damage totals.

Older version 1 logs remain valid input for `tools/analyze_balance_logs.py`
and `tools/adjust_balance_from_logs.py`; the added fields are optional to
those tools.
