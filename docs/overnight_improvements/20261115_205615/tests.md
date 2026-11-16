# テスト記録

## 開始時確認

- Release x64本体ビルドは直前作業で成功済み。
- C++テストプロジェクトはDebug x64構成のみ。
- 環境のPDB DLL不整合を避ける場合、`_CL_=/Z7` と `_LINK_=/DEBUG:NONE` をテスト時だけ指定する。
- Python実行環境はWindowsAppsの無効なエイリアスのみで、Pythonテストは現時点では実行不能。

以降の実行条件・結果を項目ごとに追記する。

## P0-1 追加回帰条件

- 貫通スタック15・貫通痕0本: 有効消費0、残量15。
- 貫通スタック15・貫通痕1本・フィニッシャー: 有効消費11、残量4。
- 既存の重量・反射・アンカー・貫通ルールテストと併せて実行予定。

## Debug x64 C++ 回帰結果

実行条件: MSBuild v143、Debug x64、`_CL_=/Z7`、`_LINK_=/DEBUG:NONE`。各プロジェクトを並列化せずビルドし、各exeをソースルートから実行。

| テスト | 結果 | 主な検証 |
|---|---|---|
| test_ball_synergy_rules | 成功 | 4軸スタック、シールド、ピンボール、追加貫通消費 |
| test_ball_upgrade_runtime | 成功 | 17球、フェーズ構成、決定的ステージ選択、敵能力 |
| test_shared_prediction_runtime | 成功 | 軽量予測と共通物理ルール |
| test_fixed_timestep_runtime | 成功 | 30/60/144Hz、ストール、NaN、停止 |
| test_ccd_toi_runtime | 成功 | 高速衝突、壁、接線、連鎖、上限 |
| test_synchronized_physics_runtime | 成功 | 同期substep、加速、リタイア、中断 |
| test_enemy_action_runtime | 成功 | 行動予算、Charge予約、公平性、妨害 |
| test_enemy_validation_runtime | 成功 | 6/8/10体、Dense/Line/Random、固定seed |
| test_run_map_runtime | 成功 | 200 seed、到達性、保存復元、破損経路 |
| test_progression_profile | 成功 | 解放、永続化、破損フォールバック |
| test_debug_battle_setup | 成功 | プリセット往復、不正値、重なり |
| test_shot_physics_telemetry | 成功 | 移動・衝突・物理step計測 |
| test_stage_layout_editor | 初回失敗→修正後成功 | 現行ボスデータを旧形式と決め打ちしたテスト不具合 |
| test_boss_armor_runtime | 初回ビルド失敗→修正後成功 | `TableGeometry.h` include漏れ |
| test_combat_audio_feedback | 成功 | 既存回帰の完全性確認のため実行。SE変更なし |

## 予測マイクロベンチ

300回平均、同一実行内の計測。

| 敵数 | 平均軽量予測時間 |
|---:|---:|
| 6 | 0.0352 ms |
| 10 | 0.0560 ms |
| 12 | 0.0656 ms |

- 詳細予測用ball構造 584 bytes、軽量表示用 376 bytes。
- 作業開始前と同一バイナリでの比較はないため、改善率は断定しない。

## Release x64

- `DX22_01_plane.sln` Release x64: 成功。
- 出力: `x64/Release/DX22_01_plane.exe`
- 警告: テスト用の`/Z7`がプロジェクトの`/Zi`を上書きするD9025と、一部DirectXTKのLNK4075。エラーなし。

## 未実行

- Python: 実行環境がWindowsAppsエイリアスのみで起動不能のため未検証。
- MCP長時間自動ラン: 明示的な禁止条件により未実施。
- 実画面・実操作: 未検証。
