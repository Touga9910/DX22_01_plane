# BattleScene 境界分割とビルド時間比較

作成日: 2026-10-02  
対象: `develop` (`1543d0e` に対する作業ツリー変更)

## 結論

`BattleScene` が直接行っていたステージ読込・配置検証、オブジェクト生成、デバッグ JSON hot reload を3境界へ分割した。`BattleScene` とその3境界から `Game::GetInstance()` を除去し、`Game` が生成する型付き `BattleSceneServices` をコンストラクタで注入する方向へ変更した。

同時刻帯の対照測定で、`Game.h` 更新時の再コンパイル数は 21 から 20 `.cpp` へ減少し、中央値は 58.536 秒から 58.202 秒へ 0.57% 短縮した。一方、3つの専用 `.cpp` を追加したためクリーンビルドは 1.94% 悪化し、`TableConfig.h` の影響数は 34 から 37 `.cpp` へ増えた。したがって、今回の主成果は責務と依存方向の改善であり、ビルド時間の大幅短縮ではない。

## プレイヤー体験への影響

- プレイヤーに見える変化: なし。
- 「選択・狙い・連鎖」のルール、ステージ配置計算、敵難易度補正、BreakBall 生成、乱数は変更していない。
- 実ダメージとデバッグ予測で重複していた最低1ダメージ計算は `PlayerBall::CalculateIncomingDamage` へ共通化した。数式と結果は不変である。

## 依存と所有の変化

### 変更前

```text
SceneManager
  -> BattleScene を直接生成

BattleScene
  -> Game::GetInstance()
  -> StageDataLoader
  -> BallFactory / GameRuntime
  -> Input / filesystem
  -> 配置検証・生成・hot reload 状態をすべて所有
```

### 変更後

```text
Game
  -> GameBattleSceneServices (Game と戦闘シーンのアダプタ)
  -> SceneManager へ BattleSceneFactory を登録

SceneManager
  -> 登録済み factory だけを呼ぶ
  -X-> BattleScene.h

BattleScene
  -> BattleSceneServices (コンストラクタ注入)
  -> BattleSceneSetup
  -> BattleSceneObjectFactory
  -> BattleDebugHotReload

3境界
  -> BattleSceneServices の型付き操作
  -X-> Game.h
  -X-> Game::GetInstance()
```

`BattleSceneServices` の実装は既存の `GameBattleBridge.cpp` に置き、`Game.h` を取り込む翻訳単位を新設していない。`Game` の `friend` も追加していない。

## 依存解析

| ヘッダー | 分割前の推移影響 | 分割後の推移影響 | 備考 |
| --- | ---: | ---: | --- |
| `Game.h` | 21 | 20 | 旧 `BattleScene.cpp` が影響範囲から外れた。 |
| `BattleScene.h` | 6 | 6 | 完全型は `.cpp` に閉じ、利用側の範囲は維持。 |
| `StageData.h` | 27 | 30 | セットアップ・生成・hot reload の専用実装に分割したため。 |
| `TableConfig.h` | 34 | 37 | 新しい戦闘実装の意味上の依存。 |
| `BattleSceneServices.h` | - | 5 | `BattleScene` と3境界、アダプタに限定。 |

自作 `.cpp` は 75 から 78 件、循環依存は前後とも 0 件。変更後の解析結果は `build_measurements/battle_scene_split_after/dependency_graph.json` に保存した。

## 計測条件

- Visual Studio 2022 Enterprise 17.14.51 / MSBuild 17.14.51.32402
- `Debug|x64`
- MSBuild `/m` 、compiler `/MP`
- 論理プロセッサ 20
- PCH 未設定
- 各条件3回、中央値を採用
- 各増分ビルド前に変更なしビルドを実行し、対象ファイル1件の更新時刻だけを変更
- 分割前 `develop` (`1543d0e`) は別 worktree で、分割後の直後に連続計測

過去の `game_route_split_after` との比較ではマシン全体が約2倍遅い状態だったため、秒数の最終比較には同時刻帯の対照 worktree を使用した。

## 計測結果

| 計測対象 | 修正前 | 修正後 | 差分（秒） | 短縮率 | 再コンパイル数（前→後） |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーンビルド | 83.540 | 85.160 | -1.620 | -1.94% | 75→78 |
| 変更なしビルド | 1.494 | 1.539 | -0.045 | -3.01% | 0→0 |
| `Game.h` 更新 | 58.536 | 58.202 | 0.334 | 0.57% | 21→20 |
| `GameRuntime.h` 更新 | 20.849 | 21.463 | -0.614 | -2.94% | 15→16 |
| `GameObject.h` 更新 | 57.232 | 57.769 | -0.537 | -0.94% | 27→27 |
| `TableConfig.h` 更新 | 66.785 | 68.524 | -1.739 | -2.60% | 34→37 |
| 代表 `.cpp` (`GameSaveManager.cpp`) 更新 | 15.596 | 15.787 | -0.191 | -1.22% | 1→1 |

短縮率は `(修正前 - 修正後) / 修正前 * 100` で算出した。負値は悪化を示す。

計測幅（最小～最大）:

- クリーン: 83.194～84.182 秒 → 84.364～86.856 秒
- 変更なし: 1.487～1.561 秒 → 1.514～1.578 秒
- `Game.h`: 58.210～59.559 秒 → 57.703～58.227 秒
- `GameRuntime.h`: 20.798～21.247 秒 → 21.217～21.591 秒
- `GameObject.h`: 56.860～57.580 秒 → 57.382～58.260 秒
- `TableConfig.h`: 66.359～67.417 秒 → 68.500～69.094 秒
- 代表 `.cpp`: 15.450～15.718 秒 → 15.745～15.856 秒

前後の生ログと集計:

- 修正前: `build_measurements/battle_scene_split_before_same_session/`
- 修正後: `build_measurements/battle_scene_split_after/`

## 実装ファイル

- `BattleScene.h/.cpp`: 起動順序と更新順序の調停だけを残し、3境界を所有。
- `BattleSceneServices.h`: 戦闘シーンが必要とする `Game` 操作だけの型付き port。
- `BattleSceneSetup.h/.cpp`: ステージ読込、フォールバック、難易度補正、密集配置、初期位置検証。
- `BattleSceneObjectFactory.h/.cpp`: プレイヤー、床、テーブル、ポケット、敵、BreakBall の生成。
- `BattleDebugHotReload.h/.cpp`: JSON 更新時刻、60フレーム監視、30フレーム debounce、F5 手動再読込。
- `GameBattleBridge.cpp`: `GameBattleSceneServices` アダプタと戦闘シーン factory の登録。
- `SceneManager.h/.cpp`: 具体的な `BattleScene` への依存をやめ、登録済み factory を実行。
- `PlayerBall.h/.cpp`, `Game.cpp`: 予測と実ダメージの最低ダメージ式を共通化。
- `DX22_01_plane.vcxproj`, `.filters`: 新規ファイルを Visual Studio プロジェクトに登録。
- `tools/measure_cpp_build.ps1`: 今回の計測 phase を追加。
- `tools/test_game_responsibility_split_contract.py`, `tools/test_debug_combat_forecast_contract.py`: 依存注入と共通ダメージ式を契約化。

## 検証

合格:

- VS2022 `Debug|x64` でコンパイル・リンク成功。
- 責務分割、セーブ、C++ 自動プレイ回復、クリア報酬、デバッグ戦闘契約: 32件成功。
- MCP schema、boss MCP、shop 複数購入、run-map policy: 43件成功。
- 依存グラフ: 循環 0件。
- `git diff --check`: 改行コード警告に加え、作業期間中に現れた `Game.cpp:145` の末尾空白1件を検出。今回の責務分割と無関係な既存変更のため、保護して未修正。

未検証:

- GUI での戦闘開始→報酬→次ルートの手動通しプレイ。
- F5 と実ファイル更新による JSON hot reload の画面上確認。
- ゲーム内ルールや乱数を変更していないため、100ラン比較は未実施。

## 再レビュー

4 / 5 に近づいた根拠は、`BattleScene` の所有状態を責務別に分けたこと、シングルトン探索を明示的な型付き注入に置き換えたこと、`SceneManager` から具体戦闘シーンを切り離したこと、実測の悪化も隠さず記録したことである。

残る課題:

- `TableConfig.h` と `StageData.h` が新しい3翻訳単位へ広がった。次は座標補正・検証の軽量値境界を検討する。
- `BattleSceneServices` は `Game&` をオブジェクト生成のために公開している。`BallFactory` / `GameRuntime` がより狭い `GameWorld` 生成 port を受けられる構造になれば、この公開も削除できる。
- `IndexBuffer.h` / `VertexBuffer.h` は依然 47 `.cpp`、`Component.h` は46 `.cpp` に推移しており、全体ビルド時間の次の大きな対象である。
- 手動GUIスモークを自動化するまでは、初期化順序と hot reload の最終確認は人間プレイに残る。
