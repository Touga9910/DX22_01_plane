# Game 戦闘ランタイム境界とビルド時間比較

作成日: 2026-10-02  
対象: `develop` (`1543d0e` に対する作業ツリー変更)

## 結論

`Game.h` が値で所有していた戦闘進行・球種効果・ショット中状態を `GameBattleState` に集約し、`Game` は `std::unique_ptr<GameBattleState>` だけを所有する構造へ変更した。`Game.h` から `BattleController.h`、`HeavyCollisionRules.h`、`AnchorStackRules.h` を除き、列挙値だけを `BattleTypes.h` で公開した。

これにより、`BattleController.h` 変更の推移的影響は 21 から 8 自作 `.cpp`、`HeavyCollisionRules.h` と `AnchorStackRules.h` はそれぞれ 20 から 7 へ減少した。`Game.h` を取り込む `.cpp` 数は 20 のままだが、公開ヘッダーの直接 include は実装ヘッダー3件を外し、軽量な列挙値ヘッダー1件へ置き換えたため差し引き2件減った。

## プレイヤー体験への影響

- プレイヤーに見える変化: なし。
- 選択、狙い、連鎖、物理、弾道予測、報酬、セーブ形式、MCP形式、自動プレイ判断、乱数処理は変更していない。
- 実行中の値の初期値と更新順序は維持し、参照先だけを `m_BattleState->...` へ置き換えた。

## 依存と所有の変化

### 変更前

```text
Game.h
  -> BattleController.h -> ShotRelicRules / StageData / GameTypes
  -> HeavyCollisionRules.h
  -> AnchorStackRules.h
  -> CushionChargeRules.h / PierceTraceRules.h

Game
  -> 戦闘状態とショット中状態を個別メンバーとして直接所有
```

### 変更後

```text
Game.h
  -> BattleTypes.h (公開APIに必要な列挙値のみ)
  -> GameBattleState を前方宣言
  -X-> BattleController.h
  -X-> HeavyCollisionRules.h
  -X-> AnchorStackRules.h

Game
  -> unique_ptr<GameBattleState>

GameBattleState.h (戦闘実装側の境界)
  -> BattleController
  -> 各球種効果の可変状態

GameBattleBridge.cpp / GameProgression.cpp 等
  -> GameBattleState.h
  -> 検証済みの従来ルールをそのまま実行
```

`friend` は追加していない。戦闘状態の公開参照は従来の `Game` の型付き getter を維持し、その実装を `GameBattleBridge.cpp` へ移した。これにより、getter の本体変更も `Game.h` 利用側に再コンパイルを要求しない。

## 依存解析

| ヘッダー | 変更前の推移影響 | 変更後の推移影響 | 備考 |
| --- | ---: | ---: | --- |
| `Game.h` | 20 | 20 | 直接利用側は次フェーズで port/view へ置換する。 |
| `BattleController.h` | 21 | 8 | `Game.h` から外れ、戦闘実装側に限定。 |
| `HeavyCollisionRules.h` | 20 | 7 | `Game.h` から外れた。 |
| `AnchorStackRules.h` | 20 | 7 | `Game.h` から外れた。 |
| `BattleTypes.h` | - | 21 | 列挙値と `ToString` 宣言だけの安定ヘッダー。 |
| `GameBattleState.h` | - | 7 | 可変戦闘状態を必要な実装側だけで読む。 |

変更後の自作 `.cpp` は78件、インクルード循環は0件。解析結果は `build_measurements/game_battle_state_after/dependency_graph.json` に保存した。

## 計測条件

- Visual Studio 2022 Enterprise 17.14.51 / MSBuild 17.14.51.32402
- `Debug|x64`
- MSBuild `/m`、compiler `/MP`
- 論理プロセッサ 20
- PCH 未設定
- 各条件3回、中央値を採用
- 各増分ビルド前に変更なしビルドを実行し、対象ファイル1件の更新時刻だけを変更
- 同一マシン、同一構成、同一並列設定

比較元は `battle_scene_split_after` (2026-09-29 UTC)、今回は `game_battle_state_after` (2026-10-01 UTC) であり、同時刻帯の連続測定ではない。特に今回のクリーンビルドは 43.306～91.033 秒とばらつきが大きい。そのため、秒数差は参考値とし、設計効果の根拠は再コンパイル数と依存グラフを主とする。

## 計測結果

| 計測対象 | 修正前 | 修正後 | 差分（秒） | 短縮率 | 再コンパイル数（前→後） |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーンビルド | 85.160 | 46.040 | 39.120 | 45.94% | 78→78 |
| 変更なしビルド | 1.539 | 0.811 | 0.728 | 47.30% | 0→0 |
| `Game.h` 更新 | 58.202 | 48.303 | 9.899 | 17.01% | 20→20 |
| `GameObject.h` 更新 | 57.769 | 48.277 | 9.492 | 16.43% | 27→27 |
| `TableConfig.h` 更新 | 68.524 | 63.176 | 5.348 | 7.80% | 37→37 |
| 代表 `.cpp` (`GameSaveManager.cpp`) 更新 | 15.787 | 13.058 | 2.729 | 17.29% | 1→1 |

短縮率は `(修正前 - 修正後) / 修正前 * 100` で算出した。ただし上記の通り、前後の日時が異なるため、この短縮率を今回のコード変更だけの効果とは断定しない。

変更後の計測幅:

- クリーン: 43.306～91.033 秒
- 変更なし: 0.784～0.864 秒
- `Game.h`: 47.903～49.875 秒
- `GameObject.h`: 46.018～48.671 秒
- `TableConfig.h`: 62.477～64.125 秒
- 代表 `.cpp`: 12.458～13.239 秒

生ログと集計:

- 比較元: `build_measurements/battle_scene_split_after/`
- 今回: `build_measurements/game_battle_state_after/`

## 実装ファイル

- `BattleTypes.h`: `BattleState` / `BattleResult` だけを戦闘制御実装から分離。
- `BattleController.h`: 上記列挙値の定義を軽量ヘッダーへ移動。
- `GameBattleState.h`: 戦闘進行、クッション、重量、貫通、アンカー、シールドの可変状態を集約。
- `Game.h`: 個別の戦闘実装メンバーを削除し、不完全型の所有に変更。インラインの戦闘 getter も宣言のみに変更。
- `GameBattleBridge.cpp`: 戦闘 getter の本体を移動し、既存の戦闘境界と同じ翻訳単位に集約。
- `Game.cpp`、`GameProgression.cpp`、`GameAutoPlay.cpp`、`GameBalanceConfig.cpp`、`GameMcpBridge.cpp`、`GameSaveBridge.cpp`: 参照先を `GameBattleState` に変更。ルールと更新順序は不変。
- `DX22_01_plane.vcxproj` / `.filters`: 新規ヘッダーを Visual Studio プロジェクトに登録。
- `tools/test_game_responsibility_split_contract.py`: `Game.h` が戦闘実装型を再び直接所有しないことを契約化。
- `tools/measure_cpp_build.ps1`: 今回の計測 phase を追加。

## 検証

合格:

- VS2022 `Debug|x64` でコンパイル・リンク成功。DirectXTK の既存 `LNK4099` 警告2件のみ。
- 責務分割、セーブ、C++ 自動プレイ回復、クリア報酬、デバッグ戦闘契約: 33件成功。
- MCP schema、boss MCP、shop 複数購入、run-map policy: 43件成功。
- 依存グラフ: 循環0件。
- `git diff --check`: 改行コード警告と、既存の `Game.cpp:146` 末尾空白1件。今回の変更範囲ではないため保護した。

未検証:

- GUI の通常戦闘→報酬→次ルートの手動通しプレイ。
- ゲームルールや乱数を変更していないため、同一シードのラン比較は未実施。

## 再レビュー

4 / 5 に近づいた根拠は、`Game` の公開ヘッダーから可変戦闘実装を隠したこと、公開列挙値と実装コントローラーを分けたこと、getter 本体の変更を利用側へ波及させなくしたこと、そして主要な戦闘実装ヘッダーの影響数を実際に半分以下へ減らしたことである。

残る課題:

- `Game.h` 自体の直接利用側20 `.cpp` は未削減。次は `GameView` / `GameRuntime` / 戦闘操作 port への置換を、`PlayerBall`、`EnemyBall`、表示コンポーネントから順に行う。
- `CushionChargeRules.h` と `PierceTraceRules.h` は、公開 getter がその `State` を返すため `Game.h` に残る。次は状態型とインラインルールを分離し、読み取りモデルを公開する。
- `GameBattleState` は現状では所有境界であり、ドメインサービスではない。リセットとスナップショット構築をここへ集約するのが次の責務分離となる。
- 今回の時間比較は同時刻帯ではない。厳密な秒数の因果比較には、今回の差分だけを外した対照 worktree を同時刻帯で連続測定する必要がある。
