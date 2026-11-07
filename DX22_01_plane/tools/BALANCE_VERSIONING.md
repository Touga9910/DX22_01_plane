# Balance version operation

`assets/data/balance_version.json` is the source of truth for the current
balance version. `B0001` registers the 2026-11-07 balance without changing any
gameplay value. Historical, unversioned logs remain `Legacy/Unknown`.

## Normal workflow

1. Confirm the current registration:
   `python tools/balance_version.py status`
2. Change the intended balance data or dedicated rule file.
3. Bump once, with an explicit human-authored reason:
   `python tools/balance_version.py bump --target enemy_hp --before "100" --after "110" --reason "Survival time was below target" --expected-impact "Average battle turns increase"`
4. Run `python tools/balance_version.py status` again. A changed managed hash
   without a bump exits with code 1 and must fail balance validation/CI.
5. Build the game, collect fixed-condition runs, then run
   `python tools/analyze_balance_logs.py`.

Do not bump on game start, log write, UI/VFX/audio-only work, or a refactor
that cannot affect rules. Do bump for a balance-affecting bug fix.

## Managed scope

The registry lists the JSON balance data and dedicated `*Rules` files included
in the SHA-256. Dashboard, telemetry, UI, VFX, and audio files are excluded.
Inline gameplay constants outside the listed files cannot be distinguished
automatically from unrelated edits; balance changes in those files require a
manual review and an explicit bump. Moving such constants into a dedicated
rule/config file improves future detection.

## Dashboard selection

The analyzer reads all `run_*.json`, skips broken JSON, duplicate `run_id`
values, and unsupported future schemas, then builds `version_catalog`. The default detailed cohort is the
highest measured `B` version; if no versioned data exists it is
`Legacy/Unknown`. Within one version, only the newest configuration fingerprint
is used for decision metrics. This selection uses the recorded run identity,
not filesystem modification time.

The current version is still listed when it has no logs. In that case metrics
are `null` and the game UI shows `未計測`; it never substitutes legacy or
placeholder values. Two-version comparisons warn when controller, build,
validation variant, seed coverage, or stage composition differ, and when fewer
than 30 runs are available.
