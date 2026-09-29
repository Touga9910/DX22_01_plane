# Game.h 段階分割とビルド時間比較

作成日: 2026-09-28  
対象: `develop`  
比較基準: 前回依存整理完了時点 (`build_measurements/after`)  
分割後: `build_measurements/game_header_split_after`

UI シーンを read model / command 境界へ分けた第2段階は `0928_GAME_SCENE_BOUNDARY_REPORT.md` を参照。

## 結論

`Game.h` 更新時の自作 `.cpp` 再コンパイル数を 34 から 25 へ減らし、3回中央値を 58.788 秒から 53.340 秒へ 9.27% 短縮した。新しい安定境界 `GameRuntime.h` の更新は 9 `.cpp`、12.825 秒に限定される。

この段階ではゲームルールや状態を移さず、起動・フレーム更新・メインカメラ・オブジェクト生成・世界参照だけを `GameRuntime` に切り出した。実体は既存の `Game.cpp` / `GameWorld` / `Camera` へ委譲し、新しい所有状態は追加していない。

## 分割前の利用分類

`Game.h` を include する 34 `.cpp` を分類すると、次の3種類が混在していた。

1. `Game.cpp`、`GameProgression.cpp`、`GameSaveSystem.cpp` など、`Game` 自体の実装分割。
2. `GamePresentation`、`GameMcpBridge`、各 Scene など、ゲーム状態の query / command が必要な利用側。
3. `Application`、描画部品、Factory、単純な世界検索など、個別ルールを必要としない基盤利用側。

今回は3だけを分離した。1は `Game` の実装なので維持し、2は次段階で read model / command 単位に分ける。

## 依存の向き

分割前:

```text
Application / Render / Factory / World query
    -> Game.h
       -> GameWorld, BattleController, RunController, rules, save/view state ...
```

分割後:

```text
Application / Render / Factory / World query
    -> GameRuntime.h       (forward declaration + stable operations)
       -> Game.cpp         (existing translation unit delegates to Game)
          -> Game / GameWorld / Camera

Rule, save, MCP, presentation and scene coordination
    -> Game.h              (unchanged public behavior)
```

`GameRuntime` は以下だけを公開する。

- 起動、更新、フレーム時計リセット、描画、終了
- メインカメラ
- 現在の `GameWorld`
- `Game&` に対する `GameObject` 生成

プレイヤー HP、報酬、レリック、セーブ、MCP、自動プレイ、戦闘状態は公開していない。

## 依存解析結果

| ヘッダー | 分割前: 直接 / 推移 `.cpp` | 分割後: 直接 / 推移 `.cpp` | 変化 |
| --- | ---: | ---: | ---: |
| `Game.h` | 34 / 34 | 25 / 25 | -9 |
| `GameRuntime.h` | - | 9 / 9 | 新規の安定境界 |
| `GameWorld.h` | 3 / 36 | 5 / 29 | 利用者を明示化、推移 -7 |
| `BallStatus.h` | 13 / 44 | 13 / 39 | 推移 -5 |
| `TableConfig.h` | 20 / 43 | 20 / 38 | 推移 -5 |
| `GameObject.h` | 31 / 27 | 31 / 27 | 変化なし |
| `Camera.h` | 14 / 10 | 14 / 10 | 変化なし |

強連結成分による include 循環は分割前後とも 0 件。

`Game.h` を外した9 `.cpp`:

- `Application.cpp`
- `BallFactory.cpp`
- `BreakBall.cpp`
- `GameStageEditor.cpp`
- `GroundRenderComponent.cpp`
- `NuisanceBall.cpp`
- `PocketFactory.cpp`
- `Texture2D.cpp`
- `Texture2DFactory.cpp`

## 計測条件

前回レポートと同一。

- Visual Studio 2022 Enterprise 17.14.51 / MSBuild 17.14.51.32402
- `Debug|x64`, toolset `v143`
- MSBuild `/m`、compiler `/MP`、論理 CPU 20
- PCH なし
- 各条件 3 回、中央値
- 各増分計測前に変更なしビルドを実行し、対象ファイル1件のみ更新
- 代表 `.cpp`: `GameSaveManager.cpp`

実行コマンド:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools\measure_cpp_build.ps1 `
  -Phase game_header_split_after -Runs 3 `
  -Headers Game.h,GameRuntime.h,GameObject.h,TableConfig.h `
  -RepresentativeSource GameSaveManager.cpp
```

## 計測結果

| 計測対象 | 分割前 | 分割後 | 差分（秒） | 短縮率 | 再コンパイル数（前→後） |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーンビルド | 84.881 | 89.749 | -4.868 | -5.74% | 73→73 |
| 変更なしビルド | 0.694 | 0.743 | -0.049 | -7.06% | 0→0 |
| `Game.h` 更新 | 58.788 | 53.340 | 5.448 | **9.27%** | **34→25** |
| `GameObject.h` 更新 | 43.838 | 44.222 | -0.384 | -0.88% | 27→27 |
| `TableConfig.h` 更新 | 66.375 | 60.599 | 5.776 | **8.70%** | **43→38** |
| 代表 `.cpp` 更新 | 10.227 | 10.554 | -0.327 | -3.20% | 1→1 |

追加計測:

| 計測対象 | 分割後中央値 | 幅 | 再コンパイル数 |
| --- | ---: | ---: | ---: |
| `GameRuntime.h` 更新 | 12.825 | 12.284～13.115 | 9 |

各条件の幅:

- クリーン: 84.562～85.189 秒 → 85.322～95.434 秒
- 変更なし: 0.682～0.709 秒 → 0.716～0.746 秒
- `Game.h`: 58.579～59.429 秒 → 51.984～57.382 秒
- `GameObject.h`: 43.312～43.852 秒 → 43.892～44.813 秒
- `TableConfig.h`: 66.025～66.801 秒 → 60.248～61.037 秒
- 代表 `.cpp`: 10.224～10.372 秒 → 9.788～11.307 秒

悪化した項目も隠さない。クリーンは +4.868 秒だが、分割後の幅が 10.112 秒と差分より大きく、追加実装は既存 `Game.cpp` 内の短い委譲のみで翻訳単位数も不変なため、環境負荷の揺らぎが大い。変更なしの +0.049 秒は絶対値が小さい。`GameObject.h` と代表 `.cpp` は再コンパイル数が不変で、分割後のばらつきが差分を上回るため、性能退行の根拠とは判定しない。

一方、`Game.h` と `TableConfig.h` は再コンパイル数も減少し、分割後の最悪値も分割前の最良値より短い。これらを今回の有効な短縮効果とする。

## 修正ファイルと理由

- `GameRuntime.h`: 起動シェルとオブジェク基盤向けの小さな安定 API。
- `Game.h`: `GameRuntime` を前方宣言し、現在の `GameWorld` への委譲だけを許す `friend` を追加。
- `Game.cpp`: `GameRuntime` の各処理を既存 `Game`、`Camera`、`GameWorld` へ委譲。状態保持なし。
- `Application.cpp`: ゲーム内部 API ではなくライフサイクル境界を使用。
- `GroundRenderComponent.cpp`, `Texture2D.cpp`: メインカメラ以外の `Game` 依存を除去。
- `BallFactory.cpp`, `PocketFactory.cpp`, `Texture2DFactory.cpp`: `Game` の全 API ではなくオブジェク生成操作を使用。Factory の公開シグネチャは互換性のため維持。
- `BreakBall.cpp`, `NuisanceBall.cpp`: カメラと `GameWorld` のコンポーネント検索だけに依存。意思決定ルールは変更なし。
- `GameStageEditor.cpp`: `GameDebugController` の実装に不要だった `Game.h` を除去。
- `.vcxproj`, `.filters`: `GameRuntime.h` を Visual Studio プロジェクに登録。
- `tools/measure_cpp_build.ps1`: 段階分割後の独立ログ出力先を追加。

## `friend` と内部アクセス

`GameRuntime` に `Game` の `friend` を追加した。目的は `m_World` を一般の `Game` 公開 API に露出さず、専用境界だけが取得できるようにするためである。`GameRuntime` は `Game` の報酬、セーブ、MCP、ラン状態を直接読み書きしない。

ただし `World()` が mutable `GameWorld&` を返す点は 5 / 5 に向けた残課題である。次段階では、読み取りの `ComponentQuery` と生成／破棄の `GameObjectCommands` を分け、mutable world そのものを返さない境界に狭められる。

## 検証

合格:

- Visual Studio 2022 `Debug|x64` ビルド。既存 DirectXTK PDB 警告2件、0 error。
- 最終編集後の増分ビルド: `GameStageEditor.cpp` 1件のみ再コンパイルして成功。
- 責務分割 10件、セーブ 5件、C++ 自動プレイ回復 5件、クリア報酬 4件。
- MCP schema 31件、boss MCP 7件、shop 複数購入 2件、run-map policy 3件。
- ランマップ 200 seed、セーブ往復、旧形式、破損パス。
- 進行プロファイル、共有弾道予測、同期物理、30/60/144 Hz 固定時間制御。

未実施:

- GUI による手動通しプレイ。プレイヤー体験やルールの変更はないが、リリース前に起動→戦闘→報酬→次ルートの目視確認が必要。
- 前回報告済みの古い期待値／文字コードによる `result_pause`、`debug_combat_forecast`、`ball_upgrade_runtime` の既存失敗は今回再実行していない。

物理、弾道予測、球／レリック効果、戦闘と報酬、セーブ schema、MCP 形式、自動プレイ意思決定、乱数シードは変更していない。

## 生ログ

- 分割前集計: `build_measurements/after/summary.json`
- 分割前生ログ: `build_measurements/after/logs/`
- 分割前依存グラフ: `build_measurements/after/dependency_graph.json`
- 分割後集計: `build_measurements/game_header_split_after/summary.json`
- 分割後生ログ: `build_measurements/game_header_split_after/logs/`
- 分割後依存グラフ: `build_measurements/game_header_split_after/dependency_graph.json`

## 次の段階

1. `GamePresentation` と Scene が使う読み取り値を `GameViewSnapshot` へ集約し、描画フレーム中の多数 query を安定化する。
2. シーン操作を `RunCommands` / `SceneCommands` へ分け、Scene から `Game.h` を外す。
3. `GameMcpBridge` と `GameDebugController` の既存 `friend` を、同じ query / command 境界へ置き換える。
4. `GameRuntime::World()` を読み取り query と明示 command へ分割する。

今回は段階分割として、公開 API の一括置換や Scene 全体の大規模書き換えを避けた。`Game.h` の影響はまだ25 `.cpp` あるため完了ではないが、数と実測の両方で次段階の基準点を作れた。
