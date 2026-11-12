# GameProgression.cpp 責任分割レポート

実施日: 2026-11-11  
対象: `DX22_01_plane/GameProgression.cpp`  
構成: Visual Studio 2022 / x64

## 1. 結論

`GameProgression.cpp` にあった75個の `Game` メンバー関数を、機能別の13個の新規実装ファイルと2個の既存実装ファイルへ移動した。複数の翻訳単位から必要になるシーン名変換だけを、前方宣言のみの小さな共有ヘッダーと実装へ分離した。元ファイルには責任が残らないため、プロジェクト登録を外して削除した。

関数本体は機械的に移動し、移動前後の全75関数について本体ブロックのSHA-256を比較した。欠落0、ハッシュ差分0である。大関数4個は処理順序、乱数順序、イベント順序を変えないことを優先し、内部の細分化を行っていない。

`Game.h`、公開API、セーブ形式、バランス値には変更を加えていない。

## 2. 分割前の関数・責任

分割前の `GameProgression.cpp` は2,885行、物理 `#include` 57個、`Game` メンバー関数75個で、次の責任を同居させていた。

| 責任 | 主な関数 |
|---|---|
| ラン進行状態 | `GetAreaProgress`, `GetRunMap`, `ChooseMapNode`, `GetRunPhase`, `IsBossPreparation`, `IsFinalBossRoute` |
| 休憩 | `RestHeal`, `GetRestHealAmount`, `GetRestHealPercent`, `RestUpgradeBall` |
| 報酬 | `GetClearRewardUpgradeCost`, `ApplyClearRewardUpgrade`, `CalculateStageRewardMoney`, `CollectStageRewardMoney` |
| ショップ | `BuyShopBall`, `RollShopBallOffers`, `BuyShopBallOffer`, `RemoveShopBall` |
| レリック | 購入、付与、抽選、補正、`ShotRelicRules` の取得・確定 |
| プレイヤーボール寿命 | 状態適用・保存、次ボール準備、破棄 |
| ショット開始・終了 | `ResetShotRelicState`, `OnPlayerShotFired`, `ApplyEndOfShotRelicEffects` |
| 反射・貫通・接触シナジー | 壁反射、反射スタック、ピンボール、貫通痕、4軸接触計算 |
| ダメージ・撃破 | シールド、連鎖衝撃、アンカー停止、撃破、被ダメージ |
| イベント橋渡し | 戦闘フィードバック、`PublishGameEvent`、表示・音響通知 |
| バランス | ログ記録、敵難易度補正、自動調整通知 |
| デバッグ | `SaveDebugSnapshot` とシーン名変換 |

特に `OnPlayerShotFired`、`ApplyEndOfShotRelicEffects`、`NotifyPlayerEnemySynergyCollision`、`PublishGameEvent` は、異なるルールの確定順序が関数内に埋め込まれている。安全な内部抽出には順序を表す追加テストが必要なため、今回は責任別ファイルへの移動だけとした。

## 3. 分割後のファイルと関数移動先

| 移動先 | 区分 | 移動した関数 |
|---|---|---|
| `GameRunAccess.cpp` | 既存 | `GetAreaProgress`, `GetRunMap`, `ChooseMapNode`, `GetRunPhase`, `IsBossPreparation`, `IsFinalBossRoute` |
| `GameBalanceConfig.cpp` | 既存 | `RecordBalanceEvent`, `ApplyEnemyDifficultyScaling`, `NotifyBalanceAutoFullHpEnemySurvived` |
| `GameRestActions.cpp` | 新規 | `RestHeal`, `GetRestHealAmount`, `GetRestHealPercent`, `RestUpgradeBall` |
| `GameRewardOperations.cpp` | 新規 | `GetClearRewardUpgradeCost`, `ApplyClearRewardUpgrade`, `CalculateStageRewardMoney`, `CollectStageRewardMoney` |
| `GameShopOperations.cpp` | 新規 | `BuyShopBall`, `RollShopBallOffers`, `BuyShopBallOffer`, `RemoveShopBall` |
| `GameRelicOperations.cpp` | 新規 | `BuyRelic`, `GrantRelic`, `RollRelicOffers`, `RollShopRelicOffers`, `IsShopRelicOffered`, `BuyShopRelicOffer`, `BuyShopRelic`, `RollMidBossRelicOffers`, `AcquireMidBossRelicOffer`, `AcquireMidBossRelic`, `GetOwnedRelicCount`, `GetRelicAttackBonus`, `GetEffectivePlayerBallAttack`, `ApplyRelicModifiersTo`, `MakePredictionShotRules`, `CaptureShotRelicRules`, `CommitShotRelicRules` |
| `GamePlayerBallLifecycle.cpp` | 新規 | `ApplyPlayerStatusTo`, `ApplyPlayerRunStatusTo`, `CapturePlayerStatusFrom`, `CaptureCurrentPlayerStatus`, `PrepareNextPlayerBall`, `DiscardCurrentPlayerBall` |
| `GameShotStart.cpp` | 新規 | `ResetShotRelicState`, `OnPlayerShotFired` |
| `GameShotEnd.cpp` | 新規 | `ApplyEndOfShotRelicEffects` |
| `GameReflectionSynergy.cpp` | 新規 | `NotifyPlayerWallCollision`, `NotifyEnemyEnemySynergyCollision`, `NotifyEnemyWallCollision`, `ConsumeBankShotDamageMultiplier` |
| `GamePierceSynergy.cpp` | 新規 | `NotifyPlayerPiercedEnemy`, `NotifyPlayerDirectionChange`, `NotifyTraceMovement`, `GetPierceMaximumUses`, `GetPierceSpeedRetention` |
| `GameCollisionSynergy.cpp` | 新規 | `NotifyPlayerEnemySynergyCollision` |
| `GameDamageAndDefeat.cpp` | 新規 | `AbsorbPlayerShieldDamage`, `ScaleEnemyCollisionDamage`, `NotifyPlayerChainImpact` 2オーバーロード, `ConsumePlayerEnemyRelicDamageBonus`, `NotifyAnchorStopped`, `NotifyEnemyDefeated`, `NotifyDamageBallCollision`, `NotifyPlayerDamage` |
| `GameCombatEventBridge.cpp` | 新規 | `NotifyCombatFeedback`, `NotifyCollisionImpact`, `NotifyWallImpact`, `NotifyPocketFeedback`, `NotifyChargeInterrupted`, `NotifyAxisResourceChanged`, `PublishGameEvent`, `NotifyEnemyAbilityStateChanged` |
| `GameDebugSnapshot.cpp` | 新規 | `SaveDebugSnapshot` |
| `GameSceneDebugName.cpp/.h` | 新規 | 複数ファイルで使う `GetSceneDebugName(Scene*)`。ヘッダーは `Scene` の前方宣言だけを持つ |

### ファイル操作の区分

- 新規: 上表の `GameRestActions.cpp` から `GameDebugSnapshot.cpp` までの13実装、`GameSceneDebugName.cpp/.h`
- 既存変更: `GameRunAccess.cpp`, `GameBalanceConfig.cpp`, `.vcxproj`, `.vcxproj.filters`, 分割先を検査するPython契約テスト、ビルド計測スクリプト
- 削除: `GameProgression.cpp`
- 未変更: `Game.h`

新規 `.cpp` と共有ヘッダーは `.vcxproj` と `.filters` に登録済みで、両XMLをパースして妥当性を確認した。

## 4. Game.h の変更

変更なし。新しい型、実装詳細、インライン巨大関数を追加していない。既存公開APIの宣言をそのまま利用しているため、このリファクタリングを理由に `Game.h` 依存の翻訳単位が増えることはない。

## 5. 行数・インクルード数

`GameRunAccess.cpp` と `GameBalanceConfig.cpp` の値は、今回移動した関数だけでなく既存内容も含む。

| 段階 | ファイル | 行数 | include数 | Game関数数 |
|---|---|---:|---:|---:|
| 分割前 | `GameProgression.cpp` | 2,885 | 57 | 75 |
| 分割後 | `GameRunAccess.cpp` | 184 | 6 | 33 |
| 分割後 | `GameBalanceConfig.cpp` | 548 | 41 | 10 |
| 分割後 | `GameRestActions.cpp` | 109 | 9 | 4 |
| 分割後 | `GameRewardOperations.cpp` | 135 | 12 | 4 |
| 分割後 | `GameShopOperations.cpp` | 90 | 6 | 4 |
| 分割後 | `GameRelicOperations.cpp` | 236 | 13 | 17 |
| 分割後 | `GamePlayerBallLifecycle.cpp` | 163 | 15 | 6 |
| 分割後 | `GameShotStart.cpp` | 338 | 15 | 2 |
| 分割後 | `GameShotEnd.cpp` | 184 | 16 | 1 |
| 分割後 | `GameReflectionSynergy.cpp` | 221 | 9 | 4 |
| 分割後 | `GamePierceSynergy.cpp` | 167 | 13 | 5 |
| 分割後 | `GameCollisionSynergy.cpp` | 436 | 13 | 1 |
| 分割後 | `GameDamageAndDefeat.cpp` | 361 | 15 | 9 |
| 分割後 | `GameCombatEventBridge.cpp` | 387 | 17 | 8 |
| 分割後 | `GameDebugSnapshot.cpp` | 143 | 12 | 1 |
| 分割後 | `GameSceneDebugName.cpp` | 19 | 7 | 0 |

詳細CSV: `build_measurements/game_progression_split_file_metrics.csv`

### 削減・隔離した依存

- 新規の機能別13実装から `Renderer.h` とImGuiヘッダーをすべて除外した。
- `BattleScene.h`, `ShopScene.h`, `StageSelectScene.h`, `TitleScene.h`, `ResultScene.h` は新規機能別実装へ持ち込まず、`GameSceneDebugName.cpp` に隔離した。
- 休憩画面の状態を直接確認する `GameRestActions.cpp` だけが `RestSiteScene.h` を持つ。
- `json/json.hpp` はJSONログ・イベント詳細を実際に組み立てる実装だけに残した。
- `GameCombatEventBridge.cpp`, `GamePlayerBallLifecycle.cpp`, `GameDebugSnapshot.cpp`, `GameRunAccess.cpp` はJSONを直接操作しない。
- `GameBalanceConfig.cpp` のRenderer/ImGui依存は、同ファイルに既存のバランス設定UI責任があるため残る。今回移動した3関数が追加した依存ではない。

## 6. ビルド計測

### 計測上の制約

ローカルの標準Debug/Releaseビルドは、変更前から全翻訳単位で `C1902`、リンクで `LNK1101` を発生させる。原因はVisual Studio環境の `MSPDB140.DLL` バージョン不一致であり、ソースエラーではない。変更前の失敗は `build_measurements/game_progression_split_before/logs/clean-build-1.log` に保存した。

コンパイル検証と参考計測のため、プロセス内だけ `_CL_=/Z7`、`_LINK_=/DEBUG:NONE` を設定した。この回避条件ではMSBuildが毎回すべての作成元 `.cpp` を再コンパイルするため、差分ビルド時間と再コンパイル数を正しく測れない。したがって改善率は **未計測（N/A）** とし、下表を実測の参考値としてのみ残す。

| シナリオ | 分割前 | 回数 | 分割前再コンパイル数 | 分割後 | 回数 | 分割後再コンパイル数 | 改善率 |
|---|---:|---:|---:|---:|---:|---:|---|
| 全体クリーンビルド | 中央値39.862秒 | 3 | 94 | 44.806秒 | 1 | 107 | N/A |
| 無変更ビルド | 中央値39.263秒 | 3 | 94 | 43.938秒 | 1 | 107 | N/A |
| `GameProgression.cpp` / `GameCollisionSynergy.cpp` 単体変更 | 中央値39.147秒 | 5 | 94 | 43.588秒 | 1 | 107 | N/A |
| `Game.h` 単体変更 | 中央値39.143秒 | 5 | 94 | 43.589秒 | 1 | 107 | N/A |

分割前CSV: `build_measurements/game_progression_split_before/measurements.csv`  
分割後CSV: `build_measurements/game_progression_split_after/measurements.csv`

分割後の計測が1回なのは、最初の結果で全ケース107翻訳単位の再コンパイルが確定し、同じ壊れた差分条件を反復しても要求された中央値比較にならないためである。正常なPDB環境で再計測する必要がある。

構造上は、例えばショップだけの変更は `GameShopOperations.cpp`、接触計算だけの変更は `GameCollisionSynergy.cpp` に閉じる。これは依存分離の結果であり、今回の環境では時間短縮量を実測できていない。

## 7. ビルド・検証結果

### ビルド

| 項目 | 結果 |
|---|---|
| Debug/x64 標準設定 | 環境要因で失敗。`MSPDB140.DLL` 不一致 |
| Release/x64 標準設定 | 環境要因で失敗。`MSPDB140.DLL` 不一致 |
| Debug/x64 `/Z7`, `/DEBUG:NONE` | 成功。`x64/Debug/DX22_01_plane.exe` を生成 |
| Release/x64 `/Z7`, `/DEBUG:NONE` | 成功。`x64/Release/DX22_01_plane.exe` を生成 |

ログ: `logs/game_progression_refactor_debug_build.log`, `logs/game_progression_refactor_release_build.log`

### ネイティブ回帰テスト

次の7テストをビルド・実行し、すべて成功した。

- `test_ball_synergy_rules`
- `test_ball_upgrade_runtime`
- `test_progression_profile`
- `test_run_map_runtime`
- `test_fixed_timestep_runtime`
- `test_shot_physics_telemetry`
- `test_synchronized_physics_runtime`

`test_ball_upgrade_runtime` はHP独立性、4軸シナジー資源、セーブ往復、衝突順序、貫通・屈折、ステージ読込を含む。結果CSVは `logs/game_progression_refactor_cpp_tests.csv` に保存した。

### Python契約テスト

- 分割先を直接検査する対象15テスト: **15/15成功**
- 全体: 144件中135成功、8失敗、1エラー

全体の残り9件は今回の分割以前から作業ツリーにある別変更・データとの不一致である。主な内容は旧 `BallPhysicsWorld::Step(Game&)` を期待する契約、既存ステージ脅威予算、旧予測API境界、日本語表示データ、有限ラン終端契約であり、移動した75関数の本体差分ではない。便乗修正は行っていない。

ログ: `logs/game_progression_refactor_targeted_python_tests.log`, `logs/game_progression_refactor_python_tests.log`

### MCP・実機

DX22 Game MCPの外部コネクタは3回ともSSEゲートウェイのHTTP 429で利用不能だった。ゲーム自身のローカルJSONブリッジへ切り替え、次を確認した。

- Debug実行ファイル起動、タイトル状態公開
- 新規ラン開始
- ランマップのルート選択
- 戦闘開始
- ショット開始、物理処理、ショット終了、次の照準状態への復帰
- 1ショットで敵2体撃破、敵同士3接触、壁反射2回、120物理ステップのテレメトリ公開

実機ログ: `logs/game_progression_refactor_runtime_mcp.log`

MCP Pythonモジュールは14件を個別実行し、8成功、1失敗、5タイムアウトだった。失敗1件は既存の追加ボール5種に照準ガイダンス定義がないこと、タイムアウト5件はテスト終了後のプロセス/スレッド待機である。結果は `logs/game_progression_refactor_mcp_tests.csv` に保存した。

### 要求項目ごとの状態

| 項目 | 状態 |
|---|---|
| ゲーム起動 | 確認済み |
| ショット開始・終了 | 実機ローカルブリッジで確認済み |
| 重量・反射・貫通・アンカー | ネイティブ4軸回帰テスト成功。実機では反射・接触を確認 |
| 敵撃破・連鎖攻撃 | ネイティブテスト成功、実機撃破と敵同士連鎖を確認 |
| ショップ購入・削除 | 契約テストとネイティブデッキテスト成功。実機UI未到達 |
| 休憩回復・強化 | 契約テストとネイティブ強化テスト成功。実機UI未到達 |
| 報酬取得 | 報酬契約テスト成功。実機UI未到達 |
| レリック効果 | ネイティブシナジーテスト成功。実機レリック所持ラン未到達 |
| セーブ・ロード | ネイティブのセーブ往復、ショップオファー往復テスト成功 |
| 固定シード決定性 | 固定タイムステップ・同期物理テスト成功。MCP全ラン比較は未実施 |

実機未到達項目を成功扱いにはしていない。

## 8. 仕様維持の根拠

- 75関数の本体ブロックSHA-256が全件一致した。ログは `logs/game_progression_refactor_body_hashes.log`。
- `NotifyPlayerEnemySynergyCollision` の重量、反射、貫通、アンカー処理を関数内で並べ替えていない。
- ショット開始・終了、撃破、連鎖、レリック消費、イベント発行の各大関数を内部抽出していない。
- 定数、条件式、乱数呼び出し、ログ内容、イベント内容を変更していない。
- `Game.h` とセーブデータ型を変更していない。
- コンパイルとリンクにより重複定義がないことを確認した。

## 9. 残存依存と今後の改善候補

1. `GameBalanceConfig.cpp` は設定UIとバランス実行処理を同居させており、Renderer/ImGui依存が残る。別作業でUI表示だけをPresentation側へ寄せれば、さらに軽量化できる。
2. `GameCollisionSynergy.cpp` は436行だが、順序依存が非常に強い。重量・反射・貫通・アンカー各段階の中間状態とイベント列を固定するテストを先に追加してから分割すべきである。
3. `GameCombatEventBridge.cpp` はイベント型とPresentation/Audioの双方へ依存する。イベント列のスナップショットテストを追加すれば、生成と配信を将来分離できる。
4. 正常なVisual Studio PDB環境で、クリーン3回・各差分5回を再計測する必要がある。今回のCSVは回避構成の参考値であり、改善率の根拠には使えない。
5. 外部MCPゲートウェイ429が解消した環境で、ショップ、休憩、報酬、レリックを含む固定シード全ランを再実行する必要がある。

今回見つかった既存テスト・データ不一致やツールチェーン不整合は、ゲーム仕様を変えないため本リファクタリングには含めていない。
