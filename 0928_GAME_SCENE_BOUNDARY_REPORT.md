# Game.h 段階分割 第2段階: UI シーン境界

作成日: 2026-09-28  
対象: `develop`  
比較基準: 第1段階完了時点 (`build_measurements/game_header_split_after`)  
分割後: `build_measurements/game_scene_split_after`

StageSelect の route query / command を分離した第3段階は `0928_GAME_ROUTE_BOUNDARY_REPORT.md` を参照。

## 結論

`RestSiteScene`、`ShopScene`、`TitleScene`、`ResultScene` から `Game.h` と `Game::GetInstance()` を除去した。読み取りは画面別の不変スナップショットを持つ `GameView`、状態変更は `GameSceneCommands`、UI オブジェクト生成は `SceneObjectFactory`、破棄は `GameRuntime` を通す。

`Game.h` 更新時の自作 `.cpp` 再コンパイル数は 25 から 22 へ減り、3回中央値は 53.340 秒から 47.756 秒へ 10.47% 短縮した。第1段階前からの累積では 34 から 22 `.cpp`、58.788 秒から 47.756 秒へ 18.77% 短縮した。初期計測からの累積では 60.272 秒から 47.756 秒へ 20.77% 短縮している。

新しい `GameView.h` の単独更新は 5 `.cpp`、中央値 5.086 秒であり、表示用境界の変更が `Game.h` の22 `.cpp`へ波及しないことも実測した。

## プレイヤー体験への影響

- 見える変化: なし。
- 選択・狙い・連鎖: ルール、数値、入力、乱数、表示内容を変更していない。
- 狙い方の変化: なし。
- 危険と対策: UI 境界の移行で操作経路を取り違える危険に対し、従来の `Game` 公開操作へ1対1で委譲し、契約テストと実行テストを通した。
- 成功条件: 4シーンの `Game.h`／直接 singleton 利用を0にし、同条件計測で `Game.h` の再コンパイル数と時間を減らすこと。

## 依存の向き

変更前:

```text
Rest / Shop / Title / Result
  -> Game.h
     -> RunController, BattleController, GameWorld, rules, save state ...
  -> Texture2DFactory(Game&)
```

変更後:

```text
Rest / Shop / Title / Result
  -> GameView.h                 読み取り専用、画面別 snapshot / const query
  -> GameSceneCommands.h        明示的な購入・休憩・ロード・遷移要求
  -> SceneObjectFactory.h       現在シーン所有の UI オブジェクト生成
  -> GameRuntime.h              オブジェクト破棄

GameSceneAccess.cpp
  -> Game.h                     query / command を既存ルールへ委譲する唯一の境界

SceneObjectFactory.cpp
  -> Texture2DFactory.h
  -> GameRuntime の private CurrentGame()
```

`GameView` は一つの巨大 snapshot にしていない。`RestSiteViewSnapshot`、`ShopViewSnapshot`、`TitleViewSnapshot`、`ResultViewSnapshot` を分け、例えばセーブファイル検査はタイトル以外の毎フレーム処理では実行されない。

## 依存解析

| ヘッダー | 第1段階: 直接 / 推移 `.cpp` | 第2段階: 直接 / 推移 `.cpp` | 変化 |
| --- | ---: | ---: | ---: |
| `Game.h` | 25 / 25 | 22 / 22 | -3 |
| `GameRuntime.h` | 9 / 9 | 14 / 14 | +5 |
| `GameView.h` | - | 5 / 5 | 新規の表示境界 |
| `GameSceneCommands.h` | - | 5 / 5 | 新規の操作境界 |
| `SceneObjectFactory.h` | - | 5 / 5 | 新規の生成境界 |
| `GameObject.h` | 31 / 27 | 31 / 27 | 変化なし |
| `TableConfig.h` | 20 / 38 | 20 / 35 | 推移 -3 |
| `BallStatus.h` | 13 / 39 | 13 / 39 | 変化なし |
| `GameTypes.h` | 16 / 35 | 19 / 36 | 直接 +3、推移 +1 |

強連結成分による include 循環は0件。`GameTypes.h` の増加は `GameSceneCommands.h` が `SceneType` を型付きで公開し、4シーンから利用されるためである。`GameRuntime.h` の増加は生成／破棄という安定した基盤操作へ依存を移した結果であり、計測上の悪化も後述する。

`Game.h` を外した4 `.cpp`:

- `RestSiteScene.cpp`
- `ShopScene.cpp`
- `TitleScene.cpp`
- `ResultScene.cpp`

4本を除去して `GameSceneAccess.cpp` 1本を追加したため、差し引きは25→22本となる。

## 計測条件

第1段階と同一条件。

- Visual Studio 2022 Enterprise 17.14.51 / MSBuild 17.14.51.32402
- `Debug|x64`, toolset `v143`
- MSBuild `/m`、compiler `/MP`、論理 CPU 20
- PCH なし
- 各条件3回、中央値
- 各増分計測前に変更なしビルドを実行し、対象ファイル1件だけの更新時刻を変更
- 代表 `.cpp`: `GameSaveManager.cpp`
- 第2段階で自作 `.cpp` は73→75本 (`GameSceneAccess.cpp`, `SceneObjectFactory.cpp` を追加)

実行コマンド:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools\measure_cpp_build.ps1 `
  -Phase game_scene_split_after -Runs 3 `
  -Headers Game.h,GameRuntime.h,GameObject.h,TableConfig.h `
  -RepresentativeSource GameSaveManager.cpp
```

## 計測結果

| 計測対象 | 第1段階 | 第2段階 | 差分（秒） | 短縮率 | 再コンパイル数（前→後） |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーンビルド | 89.749 | 87.329 | 2.420 | 2.70% | 73→75 |
| 変更なしビルド | 0.743 | 0.733 | 0.010 | 1.35% | 0→0 |
| `Game.h` 更新 | 53.340 | 47.756 | 5.584 | **10.47%** | **25→22** |
| `GameRuntime.h` 更新 | 12.825 | 14.367 | -1.542 | **-12.02%** | **9→14** |
| `GameObject.h` 更新 | 44.222 | 43.155 | 1.067 | 2.41% | 27→27 |
| `TableConfig.h` 更新 | 60.599 | 59.957 | 0.642 | 1.06% | **38→35** |
| 代表 `.cpp` 更新 | 10.554 | 10.514 | 0.040 | 0.38% | 1→1 |

各条件の最小～最大:

- クリーン: 85.322～95.434秒 → 83.170～88.002秒
- 変更なし: 0.716～0.746秒 → 0.683～0.753秒
- `Game.h`: 51.984～57.382秒 → 47.552～47.993秒
- `GameRuntime.h`: 12.284～13.115秒 → 14.261～14.894秒
- `GameObject.h`: 43.892～44.813秒 → 42.714～43.566秒
- `TableConfig.h`: 60.248～61.037秒 → 59.713～60.197秒
- 代表 `.cpp`: 9.788～11.307秒 → 10.413～10.686秒

`Game.h` は分割後の最悪値が分割前の最良値より4.009秒短く、再コンパイル数も減っているため有効な短縮と判定する。`GameObject.h` と `TableConfig.h` は件数または値が改善したが差が小さく、時間差だけなら環境揺らぎを含む。クリーン、変更なし、代表 `.cpp` も同様である。

`GameRuntime.h` は14.367秒へ明確に悪化した。利用者が9→14本に増えたことが原因である。ただし同ヘッダーは forward declaration と低頻度のライフサイクル／オブジェクト基盤操作だけを持つ安定境界であり、変更頻度の高い `Game.h` から UI シーンを外すための意図した交換条件である。次段階では生成／破棄を `SceneObjectFactory` 側へ完全移管し、`GameRuntime.h` の影響を戻す余地がある。

追加計測:

| 計測対象 | 中央値 | 最小～最大 | 再コンパイル数 |
| --- | ---: | ---: | ---: |
| `GameView.h` 更新 | 5.086秒 | 4.974～5.160秒 | 5 |

## 修正ファイルと理由

- `GameView.h`: UI 向けの画面別 immutable snapshot と `const` query を定義。`Game.h` を include しない。
- `GameSceneCommands.h`: シーン遷移、開始／ロード、ショップ、休憩の変更要求を列挙。状態を公開しない。
- `GameSceneAccess.cpp`: 新しい query / command を既存 `Game` API に1対1で委譲。ルールや所有状態は追加しない。
- `SceneObjectFactory.h/.cpp`: UI オブジェクト生成時の `Game&` 受け渡しをシーンから隠す。
- `GameRuntime.h`, `Game.cpp`: オブジェクト破棄を追加し、`CurrentGame()` は `SceneObjectFactory` だけが使える private API に制限。
- `RestSiteScene.cpp`: HP、回復、デッキ表示を `RestSiteViewSnapshot`、操作を commands へ移行。
- `ShopScene.cpp`: 所持金、商品、デッキ、レリック表示を `ShopViewSnapshot`、購入／削除を commands へ移行。
- `TitleScene.cpp`: セーブ検査、進行表示、開始／ロード／デバッグ操作を view / commands へ移行。
- `ResultScene.cpp`: ラン結果と解放表示、再開／タイトル遷移を view / commands へ移行。
- `.vcxproj`, `.filters`: 新規ヘッダー3件、実装2件を Visual Studio に登録。
- `tools/measure_cpp_build.ps1`: 第2段階と境界単独のログ出力先を追加。

## `friend`、内部アクセス、残る依存

今回 `Game` への新しい `friend` は追加していない。4シーンの `Game::GetInstance()` と `Game` 内部 API への直接アクセスは0件になった。`GameRuntime` の private `CurrentGame()` にだけ `SceneObjectFactory` を friend としたが、これは `GameRuntime` 内の型受け渡しを隠すためであり、`Game` の private 状態を開放しない。

`GameView` が返す球、レリック、進行、結果は `const` 参照または `const` ポインタであり、表示側から変更できない。スカラー状態は画面別 snapshot へコピーする。今後は参照のライフタイムをさらに明確にするため、必要箇所から小さな表示 DTO へ移す余地がある。

`Game.h` が残る22 `.cpp` は、`Game` 自体の分割実装、戦闘／物理接続、MCP、保存、表示本体、StageSelect などである。特に `StageSelectScene` はルート選択、ラン進行、ログ、乱数シードが結合しており、この段階で機械的に facade 化すると操作順を変える危険があるため残した。

## 検証

合格:

- VS2022 `Debug|x64` ビルド。最終の境界修正増分ビルドは0 warning / 0 error。クリーン計測も3回成功。
- 責務分割10件、セーブ5件、C++ 自動プレイ回復5件、クリア報酬4件。
- MCP schema 31件、boss MCP 7件、shop複数購入2件、run-map policy 3件。
- ランマップ200 seed、全ノード到達性、接続、回復アクセス、セーブ往復、旧形式、破損パス。
- 進行プロファイル、共有弾道予測、同期物理、30/60/144 Hz 固定時間制御。
- プロジェクト／filters XML、依存解析、include 循環0件。

未検証:

- GUI による手動のタイトル→戦闘→報酬→次ルート、休憩、ショップ、リザルトの目視操作。
- 前段で既知の古い期待値／文字コードに起因する `result_pause`、`debug_combat_forecast`、`ball_upgrade_runtime` は今回の変更範囲外のため再実行していない。

物理、弾道予測、球／レリック効果、戦闘／報酬、セーブ schema、MCP schema、自動プレイ判断、乱数シード処理は変更していない。したがって100ラン比較は行っていない。

## 生ログ

- 第1段階集計: `build_measurements/game_header_split_after/summary.json`
- 第1段階生ログ: `build_measurements/game_header_split_after/logs/`
- 第1段階依存グラフ: `build_measurements/game_header_split_after/dependency_graph.json`
- 第2段階集計: `build_measurements/game_scene_split_after/summary.json`
- 第2段階生ログ: `build_measurements/game_scene_split_after/logs/`
- 第2段階依存グラフ: `build_measurements/game_scene_split_after/dependency_graph.json`
- `GameView.h` 参考計測: `build_measurements/game_scene_boundary_headers/summary.json`
- `GameView.h` 参考生ログ: `build_measurements/game_scene_boundary_headers/logs/`

## 4 / 5 に届く根拠と残課題

4 / 5 に届く根拠は、UI が巨大 singleton へ直接問い合わせ／変更する構造から、画面別 read model と明示 command へ移り、依存方向をコードで強制したこと、`Game.h` の実再コンパイル数25→22と時間10.47%短縮を同条件で示したこと、セーブ検査を無関係な毎フレーム経路へ混ぜないところまで責務を分けたことである。新しい状態所有者や汎用巨大インターフェース、不要なヒープ確保は追加していない。

5 / 5 に近づける残課題:

1. `StageSelectScene` の route query / select command を、乱数・ログ・進行順を保持した専用境界へ切り出す。
2. `BattleScene` と `GamePresentation` を battle read model / validated battle commands へ移し、表示本体から `Game.h` を外す。
3. `GameMcpBridge` と `GameDebugController` の既存 `Game friend` を、同じ query / command の小さな権限へ置き換える。
4. `GameRuntime::World()` の mutable 参照を、component query と object commands に分ける。
5. `GameRuntime.h` の14利用者を、ライフサイクルとオブジェクト操作の別ヘッダーへ分ける価値を実測する。
6. UI 通し操作を自動化し、現在手動確認に残る画面遷移を CI のスモークへ追加する。
