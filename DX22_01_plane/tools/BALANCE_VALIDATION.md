# Fixed-condition and paired DDA balance validation

## Enable the mode

Edit `assets/data/balance_validation.json`:

```json
{
  "enabled": true,
  "experiment_id": "paired_dda_5x2",
  "random_seeds": [20260807, 20260817, 20260827, 20260906, 20260916],
  "disable_dynamic_balance": true,
  "variants": [
    { "id": "dda_off", "disable_dynamic_balance": true },
    { "id": "dda_on", "disable_dynamic_balance": false }
  ],
  "minimum_runs_per_variant": 5,
  "minimum_paired_seeds": 5,
  "maximum_cleared_stages_per_run": 30,
  "fixed_stage_schedule": true,
  "baseline_profile": "normal"
}
```

Start a new run after changing the file. Validation settings are loaded when
the application starts. Runs 1-5 use `dda_off` with seed indexes 0-4. Runs
6-10 use `dda_on` with the same seed indexes 0-4. The sequence then repeats.
Each run records the variant ID/index plus the run, stage-selection,
route-selection, and autoplay seeds.

Validation runs move to the result scene after 30 cleared battles even when
the player is still alive. The run end reason is `validation_complete`. This
cap prevents a defensive build from blocking the remaining seed-suite runs;
it applies equally to the DDA-off and DDA-on variants.

The late-progression attack modifier is not DDA. It remains active in both
variants so the fixed baseline still has an end-game pressure curve. With the
default settings it adds +1 enemy attack at progress 20, another +1 every five
progress levels, and stops at +8. Change this curve in
`assets/data/dynamic_balance.json` under `progression_scaling`.

Rest healing remains 25% of maximum HP, rounded up, but cannot be used again
until two battles have been cleared. Change this in
`assets/data/player_status.json` with `restHealCooldownBattles`.

Normal progression schedules a midboss on floors 5, 15, 25, ... and a boss
on floors 10, 20, 30, .... MCP progression follows the same schedule unless
`override_stage_schedule=true` is explicitly passed for a layout test.

## Analyze and audit

```powershell
tools\game_mcp\.venv\Scripts\python.exe tools\analyze_balance_logs.py
tools\game_mcp\.venv\Scripts\python.exe tools\validate_fixed_balance_runs.py
tools\game_mcp\.venv\Scripts\python.exe tools\compare_paired_balance_runs.py
```

The analyzer reports every metric as `below_target`, `within_target`, or
`above_target`. A stage cannot be judged `balanced` while any configured hard
gate is outside its range.

The fixed-run audit checks:

- matching experiment ID and random seed;
- DDA state matches each configured variant;
- midboss/boss stage schedule;
- minimum run counts for beginner, intermediate, and advanced MCP profiles.

`insufficient_data` means the collected logs are internally valid but the
configured sample count has not been reached.

The paired report only compares seeds present in both variants. It reports
the `dda_on - dda_off` difference for reached progress, cleared stages, total
shots, remaining HP ratio, and boss reach. Five complete seed pairs are
required by default.

Boss strength remains locked in the general balance report until at least
10 analyzed runs have reached a boss. This prevents one or two exceptional
runs from triggering another boss HP change.
