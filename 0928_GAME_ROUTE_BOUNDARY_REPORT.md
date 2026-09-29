# Game.h 段階分割 第3段階: ルート選択境界

作成日: 2026-09-28  
対象: `develop`  
比較基準: UI シーン境界完了時点 (`build_measurements/game_scene_split_after`)  
分割後: `build_measurements/game_route_split_after`

## 結論

`StageSelectScene.cpp` から `Game.h`、`Game::GetInstance()`、JSON ログ構築、進行更新、具体的なシーン遷移を除去した。表示は `RunRouteView`、選択処理は `RunRouteCommands` へ分けた。

`Game.h` 更新時の再コンパイル数は22→21 `.cpp`、初期計測からは34→21 `.cpp` になった。今回の実測中央値は47.756→25.731秒だが、クリーン、`GameObject.h`、代表 `.cpp` まで同時に35～57%高速化している。1本の依存除去では説明できないマシン状態差があるため、46.12%という時間差全体を今回のコード効果とは判定しない。確実な成果は再コンパイル対象1本の削減と、3入力経路の操作統一である。

## 操作経路の統一

変更前:

```text
human / BalanceAutoPlayer / MCP
  -> StageSelectScene::ChooseRoute
     -> Game singleton
     -> map validation
     -> JSON balance log
     -> battle / shop / rest transition
```

変更後:

```text
human / BalanceAutoPlayer / MCP
  -> StageSelectScene::ChooseRoute      route index と現在の候補を確認
     -> RunRouteCommands::Choose        node validation / log / transition
        -> existing Game operations

StageSelectScene::DrawUI
  -> RunRouteView::Map()                const RunMap
  -> RunRouteView::CaptureStatus()      表示用スカラー6値
```

人間操作は `ChooseRoute(..., "human")`、C++ 自動プレイは `"autoplay"`、MCP は `"mcp"` を渡し、全て同じ `RunRouteCommands::Choose` に到達する。`controller`、`offered_routes`、`selected_index`、`map_node_id`、`map_path`、`map_version`、`selected_route`、`area_progress`、`run_phase` のログ形式と記録順を維持した。

## 依存解析

| ヘッダー | 第2段階: 直接 / 推移 `.cpp` | 第3段階: 直接 / 推移 `.cpp` | 変化 |
| --- | ---: | ---: | ---: |
| `Game.h` | 22 / 22 | 21 / 21 | -1 |
| `GameRuntime.h` | 14 / 14 | 15 / 15 | +1 |
| `RunRouteView.h` | - | 2 / 2 | 新規 |
| `RunRouteCommands.h` | - | 2 / 2 | 新規 |
| `RunMap.h` | 2 / 13 | 3 / 13 | 明示 include、推移不変 |
| `GameTypes.h` | 19 / 36 | 19 / 35 | 推移 -1 |
| `TableConfig.h` | 20 / 35 | 20 / 34 | 推移 -1 |
| `BallStatus.h` | 13 / 39 | 13 / 38 | 推移 -1 |

include の強連結成分は0件。`StageSelectScene.h` は描画に必要な `RunMap` 型を既に公開していたため、`RunMap.h` の推移影響は増えていない。

## 計測条件

第2段階と同じ。

- Visual Studio 2022 Enterprise 17.14.51 / MSBuild 17.14.51.32402
- `Debug|x64`, toolset `v143`
- MSBuild `/m`、compiler `/MP`、論理 CPU 20
- PCH なし
- 各条件3回、中央値
- 各増分計測前に変更なしビルド後、対象1ファイルだけの更新時刻を変更
- 代表 `.cpp`: `GameSaveManager.cpp`
- 自作75 `.cpp`、組み込み第三者8 `.cpp`

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools\measure_cpp_build.ps1 `
  -Phase game_route_split_after -Runs 3 `
  -Headers Game.h,GameRuntime.h,GameObject.h,TableConfig.h `
  -RepresentativeSource GameSaveManager.cpp
```

## 計測結果

| 計測対象 | 第2段階 | 第3段階 | 差分（秒） | 短縮率 | 再コンパイル数（前→後） |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーンビルド | 87.329 | 37.367 | 49.962 | 57.21% | 75→75 |
| 変更なしビルド | 0.733 | 0.680 | 0.053 | 7.23% | 0→0 |
| `Game.h` 更新 | 47.756 | 25.731 | 22.025 | 46.12% | **22→21** |
| `GameRuntime.h` 更新 | 14.367 | 8.924 | 5.443 | 37.89% | **14→15** |
| `GameObject.h` 更新 | 43.155 | 25.077 | 18.078 | 41.89% | 27→27 |
| `TableConfig.h` 更新 | 59.957 | 30.235 | 29.722 | 49.57% | **35→34** |
| 代表 `.cpp` 更新 | 10.514 | 6.793 | 3.721 | 35.39% | 1→1 |

最小～最大:

- クリーン: 83.170～88.002秒 → 37.315～38.027秒
- 変更なし: 0.683～0.753秒 → 0.668～0.690秒
- `Game.h`: 47.552～47.993秒 → 25.414～25.786秒
- `GameRuntime.h`: 14.261～14.894秒 → 8.844～8.938秒
- `GameObject.h`: 42.714～43.566秒 → 24.953～25.251秒
- `TableConfig.h`: 59.713～60.197秒 → 29.937～30.413秒
- 代表 `.cpp`: 10.413～10.686秒 → 6.756～6.888秒

設定、マシン、並列数、PCH 条件は同じだが、対象数不変のクリーン、`GameObject.h`、代表 `.cpp` も大幅に高速化している。この全体シフトはファイルキャッシュ、CPU／電源状態、バックグラウンド負荷など計測外の環境状態による可能性が高い。従って時間値は再現ログとして掲載するが、今回の設計変更へ帰属できる短縮率としては扱わない。`Game.h` 22→21、`TableConfig.h` 35→34という MSBuild 実ログ上の対象数削減を確実な効果とする。

## 修正ファイル

- `RunRouteView.h`: `const RunMap`、選択可能 node、表示用状態、route log 名の読み取り境界。
- `RunRouteCommands.h`: route 選択を1操作として公開する変更境界。
- `GameSceneAccess.cpp`: 変更前の検証、ログ、進行、遷移を同じ順序で移植。
- `StageSelectScene.cpp`: `Game` と JSON 依存を除去し、view / command / scene object factory / runtime を使用。
- `.vcxproj`, `.filters`: 新規ヘッダー2件を登録。
- `test_game_responsibility_split_contract.py`: 5シーンの禁止依存と3入力経路の共通 command を契約化。
- `tools/measure_cpp_build.ps1`: 第3段階の独立ログ出力先を追加。

## 検証

合格:

- VS2022 `Debug|x64` ビルド。既存 DirectXTK PDB 警告2件、0 error。
- 責務分割契約を10→12件へ拡張し、全件合格。
- セーブ契約5件。
- ランマップ200 seed、全ノード到達性、接続、回復アクセス、セーブ往復、旧形式、破損パス。
- MCP run-map policy 3件、MCP schema 31件。
- include 循環0件、プロジェクト／filters XML、`git diff --check`。

第2段階で合格済みで今回ルール未変更の項目:

- C++ 自動プレイ回復、クリア報酬、boss MCP、shop複数購入。
- 進行プロファイル、共有弾道予測、同期物理、固定時間制御。

未検証:

- GUI でのルートノードクリック／キー決定から戦闘・ショップ・休憩へ遷移する目視確認。
- 実際の MCP プロセスから `choose_stage` を送る E2E。schema と方針決定は自動テスト済み。

## 評価と次の候補

依存方向と操作の一貫性は改善した。Scene がログ形式と進行手順を所有しなくなり、人間・自動プレイ・MCPのルール差を作りにくい。`Game.h` の影響は初期34から21 `.cpp` まで減少し、4 / 5 の根拠はさらに強くなった。

残る高効果箇所は `BattleScene.cpp` と `GamePresentation.cpp` である。ただし戦闘は表示、デバッグ hot reload、敵生成、難度補正、戦闘開始通知が密接である。次は一括 facade ではなく、(1) battle setup snapshot/commands、(2) presentation model、(3) debug hot reload の順に分け、各段階で同シードの戦闘結果を確認すべきである。

## 生ログ

- 第2段階: `build_measurements/game_scene_split_after/summary.json`
- 第2段階生ログ: `build_measurements/game_scene_split_after/logs/`
- 第3段階: `build_measurements/game_route_split_after/summary.json`
- 第3段階生ログ: `build_measurements/game_route_split_after/logs/`
- 第3段階依存グラフ: `build_measurements/game_route_split_after/dependency_graph.json`
