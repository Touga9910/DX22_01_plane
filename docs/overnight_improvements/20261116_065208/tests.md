# テスト記録

作成日: 2026年11月16日

| 検証 | 結果 | 内容 |
|---|---|---|
| test_battle_flow_rules | 成功 | 10/11/12停止tick、Legacy/単一/複数フェーズ開始、生成条件、勝利、クリア回復 |
| test_battle_controller_runtime | 成功 | 実ステージ `phase_run_p1_mixed6` / `phase_prototype_004` / `phase_prototype_009`、6×1、6→4、6→6、最終勝利、途中敗北、結果消費とUpdateの二重防止 |
| test_ball_upgrade_runtime | 成功 | フェーズ構成、遭遇順、固定seedのステージ選択 |
| test_enemy_action_runtime | 成功 | 行動予算、Charge予約、公平性、妨害 |
| test_fixed_timestep_runtime | 成功 | 固定更新と停止 |
| test_ccd_toi_runtime | 成功 | 高速衝突、連鎖衝突、反復上限 |
| test_ball_synergy_rules | 成功 | 連続跳弾の衝突段階、速度維持、重量/貫通/錨ルール |
| test_shared_prediction_optimized | 成功 | 共通予測、貫通ミニオン、ポケット、撃破済み球を含む軽量プレビュー |
| test_fixed_timestep_runtime_performance | 成功 | 30/60/144Hz、不規則フレーム、ストール、停止 |
| test_shot_physics_telemetry | 成功 | CCD/固定更新テレメトリ |
| Python unittest | 成功 | `discover -s tools -p test_*.py`、159件 |
| Release x64 | 失敗（環境） | コンパイル開始後、全翻訳単位で `C1902: プログラム データベース マネージャーが一致していません`。MSVC 14.38とMSPDBのインストール整合問題 |

## 実機計測

- DX22 Game MCPの `get_game_state` は2回とも接続プローブがHTTP 429を返したため、実機の平均/P95/P99フレーム、WorldKey、キャッシュ率、CCD反復は未計測。
- 同一マイクロベンチの予測線平均は敵6体 `0.048051 ms`、10体 `0.0553303 ms`、12体 `0.0651643 ms`。これは実フレーム計測の代用ではない。
- 新規ランや長時間自動ランは開始していない。

## ビルド環境注記

- 一部の独立Debugテスト再リンクは `LNK1101: MSPDB140.DLL バージョンが正しくありません` で失敗。同ソースから前回作成済みの `test_shared_prediction_optimized.exe` は成功した。
- 通常Releaseの最終再ビルドも同じMSPDB不整合の `C1902` で失敗。ソース診断ではなく、Visual Studio C++ツールセットの修復が必要。
