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
  "endurance_mode": true,
  "fixed_stage_schedule": false,
  "baseline_profile": "normal"
}
```

Start a new run after changing the file. Validation settings are loaded when
the application starts. Runs 1-5 use `dda_off` with seed indexes 0-4. Runs
6-10 use `dda_on` with the same seed indexes 0-4. The sequence then repeats.
Each run records the variant ID/index plus the run, stage-selection,
route-selection, and autoplay seeds.

Set `endurance_mode` to `true` only when collecting the legacy 30-battle
endurance suite. Validation runs then move to the result scene after 30 cleared battles even when
the player is still alive. The run end reason is `validation_complete`. This
cap prevents a defensive build from blocking the remaining seed-suite runs;
it applies equally to the DDA-off and DDA-on variants.

The late-progression HP and attack modifiers are not DDA. They remain active
in both variants so the fixed baseline still has an end-game pressure curve.
With the default settings, enemy HP gains +1 at progress 10, another +1 every
five progress levels up to +4. Enemy attack gains +1 at progress 20, another
+1 every five progress levels, and stops at +8. Change these curves in
`assets/data/dynamic_balance.json` under `progression_scaling`.

Rest healing remains 25% of maximum HP, rounded up. Every visited rest site
allows healing whenever the player is below maximum HP.

Normal runs count every completed route area. Each route slot uses the weights
normal battle 63.33%, midboss 12.67%, shop 12%, and rest 12%. The normal-to-
midboss ratio remains 5:1. After 15 areas the
game guarantees a boss-preparation rest, exposes one final-boss route, and
ends the run when that boss is defeated. Keeping `endurance_mode` false allows
this finite endpoint even while fixed seeds and validation variants are enabled.

## Analyze and audit

```powershell
tools\game_mcp\.venv\Scripts\python.exe tools\analyze_balance_logs.py --latest-configuration
tools\game_mcp\.venv\Scripts\python.exe tools\validate_fixed_balance_runs.py
tools\game_mcp\.venv\Scripts\python.exe tools\compare_paired_balance_runs.py
```

`--latest-configuration` derives a SHA-256 fingerprint from the paths and
logged FNV-1a hashes in `configuration.files`. It selects the newest complete
configuration suite among runs matching the other requested conditions. This prevents runs
from before an HP, enemy, pocket, target, or MCP-profile change from being
silently combined with the current baseline.

Run-level conditions can be combined. Repeating one option accepts any of its
values; different options are combined with AND logic:

```powershell
tools\game_mcp\.venv\Scripts\python.exe tools\analyze_balance_logs.py `
  --latest-configuration `
  --controller-type mcp `
  --controller-profile intermediate `
  --build-profile pierce `
  --experiment-id paired_dda_5x2 `
  --validation-variant dda_off `
  --dynamic-balance disabled `
  --output logs\balance\report_intermediate_dda_off.json
```

Available cohort options are:

- `--controller-type`
- `--controller-profile`
- `--build-profile`
- `--experiment-id`
- `--validation-variant`
- `--dynamic-balance enabled|disabled`
- `--latest-configuration`
- `--configuration-fingerprint HASH_OR_PREFIX`

The report's `run_selection` object records the requested and effective
filters, observed configuration fingerprints, matched and excluded file
counts, and exclusion reasons. An unfiltered report remains supported for
auditing older schema-version logs, but emits a warning when it contains more
than one configuration cohort. Do not use a mixed-cohort report to apply
balance changes.

The analyzer reports every metric as `below_target`, `within_target`, or
`above_target`. It also reports a confidence interval and a statistical status.
When a statistical hard gate overlaps a target boundary, the result is
`inconclusive`: no adjustment is applied and another configured sample batch
is requested. A stage cannot be judged `balanced` while any configured hard
gate is outside its range or statistically inconclusive.

## Encounter targets and run targets

`assets/data/balance_targets.json` schema version 3 evaluates two different
scopes:

- `stage_type_targets` overrides encounter targets for `normal`, `midBoss`,
  and `boss`. Normal battles are expected to be shorter and more consistently
  cleared than bosses.
- `run_targets` evaluates progression milestones independently of individual
  encounter scores.

The `run_balance` report contains:

- first-boss reach rate;
- first-boss clear rate, measured against all completed runs rather than only
  runs that reached the boss;
- second-boss reach rate;
- median reached progress;
- terminal-run rate.

Only `game_over`, `validation_complete`, `clear`, and `victory` are treated as
completed runs for progression metrics. `application_exit` and missing results
remain visible in `result_counts` and affect `terminal_run_rate`, but they do
not make the game look harder by lowering boss reach or progress. The report
shows both `sample_count` and `balance_sample_count` so this denominator is
auditable.

Run milestone ranges use the selected MCP profile when exactly one profile is
present. `beginner` and `advanced` override the intermediate defaults. When a
report mixes profiles, it uses the configured default and marks
`used_default_profile=true`; profile-filtered reports should be used for
balance decisions.

The initial ranges are provisional design targets. Do not apply numerical
changes until `minimum_sample_count` is reached for the relevant scope.

## Build, economy, route, and pocket metrics

Report schema version 7 adds descriptive `system_metrics`, `diagnostics`, and
optional `human_feedback` alongside encounter
and run-balance judgements:

- `build` reports reward offer/choice rates, inferred new-ball acquisitions,
  per-ball offer exposure and acquisition timing, upgrades by ball and source
  scene, shot usage by ball, relic purchases and effect triggers, and the last
  observed deck composition.
- `economy` reports stage and extra money, relic spending, final money, shop
  visits, purchases per visit, rest healing, and wasted healing.
- `routes` reports candidate slots, decisions where each route was available,
  selections, selection rate when offered, controller counts, and invalid
  index/destination combinations.
- `pockets` separates controlled enemies, returned enemies, live finishers,
  balls pocketed after already being defeated, and player-pocket damage. It
  also reports tactical events and player pockets per 100 shots.
- `diagnostics.failure_taxonomy` classifies telemetry-supported game-over
  causes, while preserving overlapping labels.
- `diagnostics.difficulty_curve` compares adjacent progress values and flags
  failure-rate, damage, shot-count, and remaining-HP spikes.
- `human_feedback` aggregates optional 1-5 ratings recorded with
  `tools/balance_playtest_feedback.py`. These ratings are deliberately not
  fed directly into automatic parameter changes.

Each section includes data-coverage counts where relevant. Old logs remain
readable; missing event, shot-selection, money, or pocket telemetry produces
zero descriptive values rather than an exception. Compare the coverage count
with `sample_count` before interpreting a rate.

These statistics are descriptive, not causal. For example, a relic purchased
late in a successful run will naturally be associated with higher progress.
Do not call that relic overpowered from ownership win rate alone. Offer
exposure, acquisition timing, player profile, DDA variant, and configuration
cohort must be controlled before estimating item strength.

The fixed-run audit checks:

- matching experiment ID and random seed;
- DDA state matches each configured variant;
- deterministic seeds and route choices (the old fixed 5/10 boss schedule is
  disabled because midbosses are now weighted route candidates);
- minimum run counts for beginner, intermediate, and advanced MCP profiles.

`insufficient_data` means the collected logs are internally valid but the
configured sample count has not been reached.

The paired report only compares seeds present in both variants. It reports
the `dda_on - dda_off` difference for reached progress, cleared stages, total
shots, remaining HP ratio, and boss reach. Five complete seed pairs are
required by default.

`balance_validation.json` now separates named `tuning` and `holdout` seed
suites. `compare_paired_balance_runs.py --seed-suite <name>` selects one suite.
The automatic loop uses tuning seeds to prepare a proposal and disjoint holdout
seeds only for pre/post acceptance. A holdout failure rolls the change back.

## Build-profile collection

Player skill and build strategy are separate experimental axes. Keep one
`--profile` fixed while comparing `standard`, `heavy`, `pierce`, `bounce`, and
`anchor`; do not combine those five profiles into one balance judgement.

```powershell
tools\game_mcp\.venv\Scripts\python.exe tools\game_mcp\collect_fixed_balance_runs.py `
  --profile intermediate `
  --build-profile heavy `
  --runs 10

tools\game_mcp\.venv\Scripts\python.exe tools\analyze_balance_logs.py `
  --latest-configuration `
  --controller-profile intermediate `
  --build-profile heavy `
  --output logs\balance\report_intermediate_heavy.json

tools\game_mcp\.venv\Scripts\python.exe tools\compare_paired_balance_runs.py `
  --controller-profile intermediate `
  --build-profile heavy `
  --output logs\balance\paired_intermediate_heavy.json
```

With the default validation schedule, ten runs collect the same five seeds
for DDA off and on. This is suitable for paired comparison. The build profile
ID and its settings hash must match across every run in a cohort; restart the
MCP server after editing `build_profiles.json`.

Boss strength remains locked in the general balance report until at least
10 analyzed runs have reached a boss. This prevents one or two exceptional
runs from triggering another boss HP change.
