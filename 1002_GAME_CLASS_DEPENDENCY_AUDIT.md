# Game クラス全体の依存・責務分離監査

作成日: 2026-10-02  
最終更新日: 2026-10-04  
対象: `develop` (`1543d0e` に対する作業ツリー変更)

## 結論

`Game` は以前より調停役に近づいているが、現在も739行のヘッダーに、ラン進行、報酬UI状態、永続進行、設定、セーブ、MCP用状態、物理計測値が混在している。今後は巨大な1個のPImplへ隠すのではなく、状態の意味と変更理由ごとに所有者を分けるべきである。

今回は精査だけで終わらせず、`StageBase.cpp`、`TableFrameRenderComponent.cpp`、`BallCollisionComponent.cpp`、`EnemyBall.cpp`、`PlayerBall.cpp`、`BallPhysicsWorld.cpp`、`BallShotPrediction.cpp`、`GameSaveManager.cpp` から `Game.h` を除去した。読み取りは `GameBattleView`、状態変更要求は `GameBattleCommands`、物理・球からの事実通知は `GameBattleEvents`、カメラとWorldは既存の `GameRuntime` を通す。プレイヤー状態同期は `GamePlayerState`、共有弾道予測は `GameShotPrediction` と `BallShotPrediction::Input`、実プレイ物理は `BallPhysicsWorldContext` へ分離した。セーブは `GameRunSaveSnapshot` と `GameRunRestoreRequest` を入出力とし、JSON管理から `Game` と具体シーンを切り離した。MCPは読み取りを `GameMcpReadSnapshot`、特権操作を `GameMcpCommands` に分け、JSONブリッジの `friend` を解消した。Debugは通常ラン復元状態を `DebugRunSnapshot`、特権操作を `GameDebugHost` に分け、UI/controllerの直接内部参照を解消した。さらに自動プレイ、validation、固定スケーリング、再現用seed、基準難易度を `GameBalanceRuntime` にまとめ、3つの具象controllerヘッダーを `Game.h` から除去した。`GameBalanceRuntime` は状態袋ではなく、MCP用読み取りsnapshot、Debug用automation snapshot、名前付き操作を提供するownerへ進めた。`Game.h` 更新の影響は20から12自作 `.cpp`、各controller変更の影響も12～13件から8～9件へ減少した。

## 現在の責務分類

| 責務 | 現在の主な状態 | 判定 |
| --- | --- | --- |
| 全体調停 | `SceneManager`、`Camera`、`GameWorld` | `Game` に残してよいが、完全型は runtime state 側へ隠せる。 |
| 戦闘進行 | `GameBattleState` / `BattleController` | 分離済み。`Game` は接続と更新順序を担当。 |
| ラン進行 | `RunController`、デッキ、HP、マップ | `GameRunSession` 候補。変更頻度が高く公開APIも多い。 |
| 報酬・選択UI | 選択index、オファー、メッセージ、中ボス選択 | `ClearRewardController` / `RewardFlow` 候補。ラン本体と分ける。 |
| 自動プレイ・検証 | `GameBalanceRuntime` が `BalanceAutoPlayer`、`BalanceValidationController`、seed群を所有 | `Game.h` から具象型を隠蔽し、外部境界向けsnapshot・名前付き操作もowner側へ移行済み。 |
| 固定スケーリング | `GameBalanceRuntime` が `DynamicBalanceController` と基準難易度を所有。脅威コストは `Game` に残存 | 実装状態の境界は分離済み。脅威configを計算サービスへ移す余地がある。 |
| 永続化 | `ProgressionProfile`、`RunStatisticsTracker`、save message | `GamePersistenceState` 候補。型付きsnapshotを入出力する。 |
| 設定・ポーズ | `SettingsManager`、ポーズ入力request、表示適用request | application/presentation 側の責務。 |
| MCP | `GameMcpReadView` が読み取りsnapshotを生成、`GameMcpCommands` が特権操作だけを提供 | JSONブリッジからの直接メンバー参照は解消。今後は各ownerへ境界実装を移す。 |
| Debug | `GameDebugController` がUI/workflowを所有、`GameDebugHost` が特権操作を提供 | 退避・復元は `DebugRunSnapshot` で型付き化済み。今後はrun/automation owner側へ実装を移す。 |

## Game.h が直接読む主なヘッダー

| ヘッダー | 変更後の推移影響 `.cpp` | `Game.h` から外す方法 |
| --- | ---: | --- |
| `GameWorld.h` | 21 | `GameRuntimeState` に値所有させ、検索APIは `GameRuntime::World()` に限定。 |
| `ProgressionProfile.h` | 18 | persistence state へ移し、公開は前方宣言とview snapshotにする。 |
| `PlayerRunStatus.h` | 16 | run session に所有を移し、レリック型は軽量定義へ分離。 |
| `PlayerDeck.h` | 15 | inline getterを `.cpp` へ移し、デッキ操作は run session 経由にする。 |
| `RunProgressConstants.h` | 15 | 定数は run controller 側へ封じる。 |
| `ShotRelicRules.h` | 15 | prediction input snapshot とbattle stateに限定する。 |
| `CushionChargeRules.h` | 15 | state DTOとrule関数を分離する。 |
| `PierceTraceRules.h` | 14 | state DTOとrule関数を分離する。 |
| `RunController.h` | 13 | `GameRunSession` を不完全型で所有する。 |
| `SceneManager.h` | 13 | runtime state へ移す。 |
| `RunStatisticsTracker.h` | 13 | persistence state へ移す。 |
| `SettingsManager.h` | 13 | application settings owner へ移す。 |
| `GameEvent.h` | 12 | event publisher実装の `.cpp` に限定する。 |
| `FixedStepClock.h` | 12 | runtime state へ移す。 |

`BalanceAutoPlayer.h`、`BalanceValidationController.h`、`DynamicBalanceController.h` は `Game.h` の直接依存から除去済みで、現在の推移影響はそれぞれ8、9、9 `.cpp` である。

`BallStatus.h` は全体で40 `.cpp` に影響する最重要ドメインヘッダーの1つである。`Game` のデフォルト状態所有を run session へ移せば `Game.h` から外せるが、他のボール実装も直接利用するため、それだけで全影響数が大幅に下がるわけではない。

## Game.h の残る12利用側

### Game内部と境界実装: 12件

`Game.cpp`、`GameAutoPlay.cpp`、`GameBalanceConfig.cpp`、`GameBattleBridge.cpp`、`GameBossAI.cpp`、`GameDebugMode.cpp`、`GameMcpBridge.cpp`、`GamePresentation.cpp`、`GameProgression.cpp`、`GameSaveBridge.cpp`、`GameSaveSystem.cpp`、`GameSceneAccess.cpp`。

これらは `Game` のメンバー関数実装または明示的アダプタであり、当面の `Game.h` 参照は正当である。クラス分離後は所有者のヘッダーへ順次置き換わる。

### 外部ゲームプレイ・サービス: 0件

`GameSaveManager.cpp` は `GameRunSaveSnapshot` を直接シリアライズし、読込時は `GameRunLoadRules` に従って `GameRunRestoreRequest` を返す。`GameSaveSystem.cpp` がアダプタとして、現在シーン固有状態の取得と `Game::RestoreRunSaveSnapshot` の適用を担う。これでゲームプレイ・物理・予測・永続化の外部実装から `Game.h` の直接依存がなくなった。

## friendと私有状態アクセス

| friend | 直接メンバー参照回数 | 問題 | 改善方針 |
| --- | ---: | --- | --- |
| `GameMcpCommands` | 11 | ステージ編集、override、追加報酬など、公開すべきでない操作を名前付きの関数に集約。automation/validation操作はowner APIへ移行。 | 残るrun/reward状態を各owner commandへ移す。 |
| `GameMcpReadView` | 31 | `const Game&` から型付きsnapshotを作る1か所に私有状態参照を集約。balance詳細はowner snapshotへ移行。 | 読み取り専用friendとして維持し、残るrun/reward/persistence snapshotを合成する。 |
| `GameDebugHost` | 42 | debug戦闘に必要なラン、シーンへの特権操作を名前付き関数に集約。automation状態はowner APIへ移行。 | `GameRunSession` 分離時に残るrun操作をowner commandへ移す。 |
| `GameRuntime` | 1 | `m_World` 取得だけであり、スコープは小さい。 | runtime state分離時に自然に解消する。 |

`friend` を宣言上だけ削除するのではなく、呼び出し側の直接参照を0にしてから、snapshotとcommandの専用境界に権限を限定する。

## 推奨する段階的分離

1. **戦闘・永続化境界**: `StageBase`、テーブル描画、`BallCollisionComponent`、`EnemyBall`、`PlayerBall`、`BallPhysicsWorld`、`BallShotPrediction`、`GameSaveManager` を移行済み。外部実装からの `Game.h` 直接依存は0件。
2. **戦闘効果のstate/rule分離**: `CushionChargeRules` / `PierceTraceRules` のDTOとインライン計算を分け、`Game.h` の公開getterを読み取りsnapshotに置き換える。
3. **予測と物理の入力型付き化**: `BallShotPrediction(Game&)` と `BallPhysicsWorld::Step(Game&)` を、同じルールを保持したまま専用context/portに変更する。
4. **`GameRunSession`**: `RunController`、デフォルトステータス、stage override、ランactiveを所有。報酬選択状態は別の `RewardFlow` へ置く。
5. **automation/balance分離**: `GameBalanceRuntime` への状態集約と公開ヘッダーからの具象型隠蔽は完了。次にsnapshot/commandをownerへ移し、MCP/Debugのfriend実装から内部配置知識を減らす。
6. **persistence/settings分離**: 永続進行、実績、セーブ、設定の所有者を明確にする。
7. **MCP / Debug friend解消**: MCPは読み取りsnapshotと名前付きcommand、Debugは `DebugRunSnapshot` とhost commandに分離済み。`GameMcpBridge` と `GameDebugController` 自体のfriendは解消した。
8. **runtime state**: `SceneManager`、`GameWorld`、`FixedStepClock` を一つの基盤stateにまとめ、`Game.h` から重い完全型を外す。ここでの1回のheap割り当ては、影響範囲削減の根拠がある。

`Game` の最終的な公開責務は、起動・終了、フレームと固定stepの更新順序、シーン遷移、サブシステム間の接続に限定する。

## 今回の実装

- `GameBattleView.h`: 戦闘フェーズとクッション表示状態の読み取り境界。
- `GameBattleCommands.h`: 検証済み戦闘進行要求の入口。
- `GameBattleEvents.h`: 物理解決後の壁衝突、球衝突、貫通、チェイン、フィードバックを事実として通知する境界。
- `GameBattleBridge.cpp`: view/command と `Game` の接続を実装。
- `StageBase.cpp`: `Game.h` を除去。フェーズ参照とステージ完了を専用境界へ、World操作を `GameRuntime` へ移行。
- `TableFrameRenderComponent.cpp`: `Game.h` と可変なクッションstateへの直接依存を除去。
- `BallCollisionComponent.cpp`: `Game.h` を除去。衝突式とダメージ順序は不変のまま、World検索・戦闘参照・事実通知を専用境界へ移行。
- `EnemyBall.cpp`: `Game.h` を除去。カメラ・World検索は `GameRuntime`、ポケット・撃破・戦闘フィードバックは `GameBattleEvents` を通し、従来の `Game` 処理へ転送。
- `BallFactory.h/.cpp`: 迷惑球生成だけが `EnemyBall` に `Game&` を要求していたため、共通初期化を維持したまま現在のruntimeへ生成する狭いオーバーロードを追加。CP932を維持して編集。
- `GameRuntime.h` / `Game.cpp`: 現在の `GameWorld` にオブジェクトを作る `CreateObject(name)` を追加。明示的な `Game&` を使う従来APIも維持。
- `PlayerBall.cpp`: `Game.h` と `Game::GetInstance()` を除去。入力可否・戦闘状態・照準遷移・発射通知・被ダメージ・デバフ検索を用途別境界へ移行。
- `GamePlayerState.h`: ランが所有するプレイヤー状態の適用・取得、ポケットダメージ、シールド消費を型付きの狭い入口として分離。
- `GameShotPrediction.h`: `Game&` をプレイヤー入力側へ露出せず、既存 `BallShotPrediction` を同じ引数・同じ摩擦スキップ値で呼ぶ予測境界を追加。
- `GameShotTraceView.h`: 貫通軌跡の重い状態型を一般的な `GameBattleView` に混ぜず、PlayerBall描画だけが読む専用Viewへ分離。
- `GameBattleView` / `GameBattleCommands` / `GameBattleEvents`: 人間入力可否、照準状態遷移、ショット・ダメージ・敗北通知を追加。処理本体は従来の `Game` メソッドへ転送。
- `BallPhysicsWorld.h/.cpp`: `Game&` を除去し、`GameWorld&`、戦闘継続判定、軌跡移動通知だけを持つ `BallPhysicsWorldContext` を導入。衝突式、TOI順序、環境・球ペア解決順は不変。
- `Game.cpp`: 固定step開始時の戦闘状態を保持したcontextを構築し、従来と同じ状態変化条件で物理catch-upを中断。
- `BallShotPrediction.h/.cpp`: `Game&` を除去。球・台・ポケットの対象一覧と、クッション・貫通・レリック・ポケット規則・接触カウンタを保持する `BallShotPrediction::Input` を導入。
- `GameBattleBridge.cpp`: 予測開始時に `Input` を一度だけ構築。PlayerBall、ボスAI、検証出力が同じsnapshot生成経路を使用。
- `GameBossAI.cpp` / `Game.cpp`: 直接の `BallShotPrediction(Game&)` 呼び出しを `GameShotPrediction` へ統一。
- `GameBalanceRuntime.h`: 自動プレイ、validation、固定スケーリング、seed、基準難易度を一つの非公開状態境界へ集約。内部フィールドをprivate化し、MCP用 `GameBalanceReadSnapshot`、Debug用 `GameBalanceAutomationSnapshot`、名前付き操作を公開。`Game.h` は不完全型の `unique_ptr` だけを保持。
- `Game.cpp`、`GameAutoPlay.cpp`、`GameBalanceConfig.cpp`、`GameDebugMode.cpp`、`GameMcpBridge.cpp`、`GameProgression.cpp`、`GameSaveBridge.cpp`、`GameSaveSystem.cpp`: 実装が必要な場所だけ `GameBalanceRuntime.h` を直接includeし、従来と同じcontroller・seed・倍率を参照。
- Visual Studioプロジェクと責務分割契約テストを更新。

## 依存とビルド計測

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 46.040 | 43.435 | 2.605 | 5.66% | 78→78 |
| 変更なし | 0.811 | 0.766 | 0.045 | 5.55% | 0→0 |
| `Game.h` | 48.303 | 28.006 | 20.297 | 42.02% | 20→18 |
| `GameObject.h` | 48.277 | 28.356 | 19.921 | 41.26% | 27→27 |
| `TableConfig.h` | 63.176 | 33.544 | 29.632 | 46.90% | 37→36 |
| 代表 `.cpp` | 13.058 | 7.527 | 5.531 | 42.36% | 1→1 |

条件は VS2022 17.14.51、`Debug|x64`、MSBuild `/m`、compiler `/MP`、PCHなし、各3回中央値で、修正前後を連続測定した。

ただし、全増分シナリオが一律に約42%速くなっており、代表 `.cpp` を基準にした `Game.h / cpp` の比は 3.699→3.721 でほぼ不変である。したがって、生の42.02%をコード変更の効果とは判定しない。確実な短縮効果は `Game.h` 変更の再コンパイル数20→18、10%減である。`TableConfig.h` も1件減少した。

生ログ:

- 修正前: `build_measurements/game_battle_state_after/`
- 修正後: `build_measurements/game_battle_access_after/`

### 追加フェーズ: BallCollision境界

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 43.435 | 43.775 | -0.340 | -0.78% | 78→78 |
| 変更なし | 0.766 | 0.787 | -0.021 | -2.74% | 0→0 |
| `Game.h` | 28.006 | 45.880 | -17.874 | -63.82% | 18→17 |
| `GameObject.h` | 28.356 | 46.511 | -18.155 | -64.03% | 27→27 |
| `TableConfig.h` | 33.544 | 64.258 | -30.714 | -91.56% | 36→36 |
| 代表 `.cpp` | 7.527 | 13.134 | -5.607 | -74.49% | 1→1 |

この追加計測はクリーンがほぼ同値の一方で、すべての増分ビルドが一律に大幅悪化している。差分はコード因果ではなく、ファイルキャッシュ、ウイルススキャン、ディスク状態等の外部要因を含む。実装効果として採用するのは `Game.h` の再コンパイル数18→17のみである。

- 追加フェーズの生ログ: `build_measurements/game_collision_boundary_after/`

### 追加フェーズ: EnemyBall境界

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 43.775 | 48.725 | -4.950 | -11.31% | 78→78 |
| 変更なし | 0.787 | 0.984 | -0.197 | -25.03% | 0→0 |
| `Game.h` | 45.880 | 57.786 | -11.906 | -25.95% | 17→16 |
| `GameObject.h` | 46.511 | 60.738 | -14.227 | -30.59% | 27→27 |
| `TableConfig.h` | 64.258 | 68.569 | -4.311 | -6.71% | 36→36 |
| 代表 `.cpp` | 13.134 | 15.940 | -2.806 | -21.36% | 1→1 |

修正後の min–max は、クリーン48.210–53.927秒、変更なし0.935–1.064秒、`Game.h` 31.142–58.311秒、`GameObject.h` 60.049–64.510秒、`TableConfig.h` 68.211–72.237秒、代表 `.cpp` 15.712–16.449秒だった。特に `Game.h` のばらつきが27.169秒と大きく、さらに全シナリオが一律に悪化しているため、生の秒数悪化を今回のコード変更の因果とは判定しない。

静的依存解析とMSBuildログの双方で `Game.h` の再コンパイル数は17→16となった。これが今回帰属できる短縮効果であり、このフェーズ単独で5.88%、監査開始時の20件からは20%削減である。循環依存は0件を維持した。

- 追加フェーズの生ログとsummary: `build_measurements/game_enemy_boundary_after/`
- 依存グラフ: `build_measurements/game_enemy_boundary_after/dependency_graph.json`

### 追加フェーズ: PlayerBall境界

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 48.725 | 84.860 | -36.135 | -74.16% | 78→78 |
| 変更なし | 0.984 | 1.544 | -0.560 | -56.91% | 0→0 |
| `Game.h` | 57.786 | 57.558 | 0.228 | 0.39% | 16→15 |
| `GameObject.h` | 60.738 | 56.973 | 3.765 | 6.20% | 27→27 |
| `TableConfig.h` | 68.569 | 67.248 | 1.321 | 1.93% | 36→36 |
| 代表 `.cpp` | 15.940 | 16.039 | -0.099 | -0.62% | 1→1 |

修正後の min–max は、クリーン84.482–85.774秒、変更なし1.534–1.985秒、`Game.h` 57.430–57.660秒、`GameObject.h` 56.823–58.421秒、`TableConfig.h` 67.142–68.004秒、代表 `.cpp` 15.953–16.605秒だった。クリーンと変更なしだけが大幅に悪化する一方、ヘッダー増分ケースは同等または改善し、代表 `.cpp` はほぼ同値であるため、秒数の一部にはマシン・キャッシュ・スキャン状態の差が含まれる。

静的依存解析とMSBuildログの双方で `Game.h` の再コンパイル数は16→15となった。今回帰属できる短縮効果はこの6.25%減であり、監査開始時の20件からは25%削減である。`Game.h` の中央値も0.228秒改善したが、外部要因を除去できないため補助指標として扱う。循環依存は0件を維持した。

- 追加フェーズの生ログとsummary: `build_measurements/game_player_boundary_after/`
- 依存グラフ: `build_measurements/game_player_boundary_after/dependency_graph.json`

### 追加フェーズ: BallPhysicsWorld context

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 84.860 | 84.810 | 0.050 | 0.06% | 78→78 |
| 変更なし | 1.544 | 1.580 | -0.036 | -2.33% | 0→0 |
| `Game.h` | 57.558 | 57.213 | 0.345 | 0.60% | 15→14 |
| `GameObject.h` | 56.973 | 57.057 | -0.084 | -0.15% | 27→27 |
| `TableConfig.h` | 67.248 | 32.589 | 34.659 | 51.54% | 36→36 |
| 代表 `.cpp` | 16.039 | 6.879 | 9.160 | 57.11% | 1→1 |

修正後の min–max は、クリーン84.164–84.955秒、変更なし1.523–1.602秒、`Game.h` 57.081–57.683秒、`GameObject.h` 44.622–57.486秒、`TableConfig.h` 31.016–33.307秒、代表 `.cpp` 6.864–7.002秒だった。クリーン、変更なし、`Game.h` がほぼ同値なのに後半の `TableConfig.h` と代表 `.cpp` だけが約半分になっており、ファイルキャッシュ等の外部要因が測定途中で変化したと判断する。51～57%の生短縮率は実装効果として採用しない。

静的依存解析とMSBuildログの双方で `Game.h` の再コンパイル数は15→14となった。今回帰属できる短縮効果はこの6.67%減であり、監査開始時の20件からは30%削減である。`Game.h` の中央値は0.345秒改善したが補助指標として扱う。循環依存は0件を維持した。

- 追加フェーズの生ログとsummary: `build_measurements/game_physics_context_after/`
- 依存グラフ: `build_measurements/game_physics_context_after/dependency_graph.json`

### 追加フェーズ: BallShotPrediction input snapshot

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 84.810 | 36.785 | 48.025 | 56.63% | 78→78 |
| 変更なし | 1.580 | 0.702 | 0.878 | 55.57% | 0→0 |
| `Game.h` | 57.213 | 17.033 | 40.180 | 70.23% | 14→13 |
| `GameObject.h` | 57.057 | 24.405 | 32.652 | 57.23% | 27→27 |
| `TableConfig.h` | 32.589 | 29.538 | 3.051 | 9.36% | 36→36 |
| 代表 `.cpp` | 6.879 | 6.840 | 0.039 | 0.57% | 1→1 |

修正後の min–max は、クリーン36.669–37.321秒、変更なし0.687–0.714秒、`Game.h` 16.924–17.509秒、`GameObject.h` 24.391–24.580秒、`TableConfig.h` 29.373–29.671秒、代表 `.cpp` 6.825–6.889秒だった。代表 `.cpp` がほぼ同値なのにクリーン・無変更・複数ヘッダーが一斉に大幅短縮しており、OSキャッシュ、スキャン、並列実行状態等がフェーズ間で変化したと判断する。56～70%の生短縮率は実装効果として採用しない。

静的依存解析とMSBuildログの双方で `Game.h` の再コンパイル数は14→13となった。今回帰属できる短縮効果はこの7.14%減であり、監査開始時の20件からは35%削減である。`BallShotPrediction.h` 自体の推移影響も5→4 `.cpp` となり、`PlayerBallData.h` の影響数は増えていない。循環依存は0件を維持した。

- 追加フェーズの生ログとsummary: `build_measurements/game_prediction_input_after/`
- 依存グラフ: `build_measurements/game_prediction_input_after/dependency_graph.json`

### 追加フェーズ: GameSaveManager typed snapshot

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 36.785 | 36.917 | -0.132 | -0.36% | 78→78 |
| 変更なし | 0.702 | 0.703 | -0.001 | -0.14% | 0→0 |
| `Game.h` | 17.033 | 16.851 | 0.182 | 1.07% | 13→12 |
| `GameObject.h` | 24.405 | 24.417 | -0.012 | -0.05% | 27→27 |
| `TableConfig.h` | 29.538 | 29.520 | 0.018 | 0.06% | 36→36 |
| 代表 `.cpp` | 6.840 | 6.582 | 0.258 | 3.77% | 1→1 |

修正後の min–max は、クリーン36.854–37.523秒、変更なし0.698–0.740秒、`Game.h` 16.679–17.103秒、`GameObject.h` 24.311–24.600秒、`TableConfig.h` 29.309–29.641秒、代表 `.cpp` 6.571–6.591秒だった。クリーンと無変更は事実上同等、非対象ヘッダーも±0.06%以内であり、今回は前フェーズと比較しやすい安定した計測になった。

`GameSaveManager` は `Game&` を受け取らず、保存時は `GameRunSaveSnapshot`、読込時は `GameRunLoadRules` と出力 `GameRunRestoreRequest` だけを扱う。セーブJSONのキー、schema version、checksum、移行判定は変更していない。静的依存解析とMSBuildログの両方で `Game.h` の再コンパイル数は13→12となった。このフェーズで7.69%、監査開始時の20件から40%削減である。循環依存は0件を維持した。

`GameRunSaveState.h` の推移影響は2→3 `.cpp` に1件増えた。これは `GameSaveSystem.cpp` が不完全型を `Game.h` から偶然取得せず、使う型を直接includeするためである。セーブDTOの変更が影響すべき3実装に限定されており、依存の正確さを優先した意図的なトレードオフである。

- 追加フェーズの生ログとsummary: `build_measurements/game_save_context_after/`
- 依存グラフ: `build_measurements/game_save_context_after/dependency_graph.json`
- 再現可能な解析スクリプト: `tools/analyze_cpp_dependencies.py`

### 追加フェーズ: MCP read snapshot

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 36.917 | 36.816 | 0.101 | 0.27% | 78→78 |
| 変更なし | 0.703 | 0.688 | 0.015 | 2.13% | 0→0 |
| `Game.h` | 16.851 | 16.807 | 0.044 | 0.26% | 12→12 |
| `GameObject.h` | 24.417 | 24.447 | -0.030 | -0.12% | 27→27 |
| `TableConfig.h` | 29.520 | 29.658 | -0.138 | -0.47% | 36→36 |
| 代表 `.cpp` | 6.582 | 6.620 | -0.038 | -0.58% | 1→1 |

修正後の min–max は、クリーン36.594–37.002秒、変更なし0.685–0.693秒、`Game.h` 16.795–17.028秒、`GameObject.h` 24.337–24.792秒、`TableConfig.h` 29.259–29.810秒、代表 `.cpp` 6.607–6.665秒だった。全条件が±0.6%以内であり、今回の変更によるビルド時間の有意な改善・悪化は判定しない。

`GameMcpBridge::BuildState` の `game.m_*` 直接参照は0件になった。読み取りは `GameMcpReadView::Capture(const Game&)` が `GameMcpReadSnapshot` を構築する1か所に限定し、MCPのJSON整形はスナップショットだけを参照する。ファイル全体の `game.m_*` は147→62件、そのうち31件がconst capture、31件が未移行のcommand適用である。

新規 `.cpp` は増やさず、スナップショット実装を既存 `GameMcpBridge.cpp` に配置した。そのため `Game.h` 更新の再コンパイル数は12件を維持している。`GameMcpState.h` の推移影響は `GameMcpBridge.cpp` 1件だけで、循環依存は0件である。MCPのschema、コマンド、判断ルールは変更していない。

- 追加フェーズの生ログとsummary: `build_measurements/game_mcp_read_snapshot_after/`
- 依存グラフ: `build_measurements/game_mcp_read_snapshot_after/dependency_graph.json`

### 追加フェーズ: MCP command gateway

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 36.816 | 37.095 | -0.279 | -0.76% | 78→78 |
| 変更なし | 0.688 | 0.688 | 0.000 | 0.00% | 0→0 |
| `Game.h` | 16.807 | 16.799 | 0.008 | 0.05% | 12→12 |
| `GameObject.h` | 24.447 | 24.403 | 0.044 | 0.18% | 27→27 |
| `TableConfig.h` | 29.658 | 29.429 | 0.229 | 0.77% | 36→36 |
| 代表 `.cpp` | 6.620 | 6.617 | 0.003 | 0.05% | 1→1 |

修正後の min–max は、クリーン36.983–37.170秒、変更なし0.671–0.692秒、`Game.h` 16.749–16.863秒、`GameObject.h` 24.335–24.551秒、`TableConfig.h` 29.275–29.573秒、代表 `.cpp` 6.570–6.649秒だった。クリーンの0.76%悪化を含めて通常のばらつき範囲であり、ビルド時間の有意な変化は判定しない。

`GameMcpBridge::ExecuteCommand` の `game.m_*` は31→0件となり、`friend class GameMcpBridge` を削除した。ボール選択、デッキ検索、ボール獲得通知、報酬確定は人間操作・自動プレイと共有する公開APIを通す。公開API化すべきでない特権操作のみ `GameMcpCommands` の9関数に集約した。`GameMcpBridge.cpp` 全体の私有メンバー参照は62→42件で、読み取り31件とcommand境界11件に完全に局所化された。

新規 `.cpp` は増やさず、`GameMcpCommands.h` の推移影響は `GameMcpBridge.cpp` 1件だけである。`Game.h` の再コンパイル数12件とinclude循環0件を維持し、MCP schema、既存command名、応答形式は変更していない。

- 追加フェーズの生ログとsummary: `build_measurements/game_mcp_commands_after/`
- 依存グラフ: `build_measurements/game_mcp_commands_after/dependency_graph.json`

### 追加フェーズ: Debug host boundary

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 37.095 | 83.296 | -46.201 | -124.55% | 78→78 |
| 変更なし | 0.688 | 0.821 | -0.133 | -19.33% | 0→0 |
| `Game.h` | 16.799 | 19.616 | -2.817 | -16.77% | 12→12 |
| `GameObject.h` | 24.403 | 27.094 | -2.691 | -11.03% | 27→27 |
| `TableConfig.h` | 29.429 | 32.990 | -3.561 | -12.10% | 36→36 |
| 代表 `.cpp` | 6.617 | 7.160 | -0.543 | -8.21% | 1→1 |

修正後の min–max は、クリーン51.402–93.258秒、変更なし0.807–0.831秒、`Game.h` 19.405–20.554秒、`GameObject.h` 26.859–27.676秒、`TableConfig.h` 32.482–33.412秒、代表 `.cpp` 7.115–7.174秒だった。クリーンのばらつきが41.856秒あり、変更なしから単一 `.cpp` まで対象に関係なく一律に遅くなっている。さらに、中間構成で取った同日計測でもクリーン中央値74.091秒に対して増分条件が最大2倍近く変動した。したがって、この生秒数の悪化は実装の因果とは判定せず、影響ファイル数を主指標とする。

`GameDebugController` と `Game.cpp` 内のDebug診断UIにあった `game.m_*` 参照は0件となり、`friend class GameDebugController` を削除した。通常プロフィール、自動プレイ、validationの復元値は `DebugRunSnapshot` にまとめ、開始、ラン設定適用、終了復元、ポーズ、MCP更新、診断保存は `GameDebugHost` の名前付き操作を通す。特権境界内の直接参照は42件で、自由なUI/controllerではなく1か所に局所化された。

`GameDebugHost.h` を `GameDebugController.h` から公開しない構成に修正し、推移影響は8→2自作 `.cpp` まで絞った。`Game.h` は12件、include循環は0件を維持している。

- 追加フェーズの生ログとsummary: `build_measurements/game_debug_host_after/`
- 依存グラフ: `build_measurements/game_debug_host_after/dependency_graph.json`

### 追加フェーズ: Game balance runtime

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 83.296 | 40.901 | 42.395 | 50.90% | 78→78 |
| 変更なし | 0.821 | 0.723 | 0.098 | 11.94% | 0→0 |
| `Game.h` | 19.616 | 18.441 | 1.175 | 5.99% | 12→12 |
| `GameObject.h` | 27.094 | 26.858 | 0.236 | 0.87% | 27→27 |
| `TableConfig.h` | 32.990 | 32.325 | 0.665 | 2.02% | 36→36 |
| 代表 `.cpp` | 7.160 | 7.168 | -0.008 | -0.11% | 1→1 |

修正後の min–max は、クリーン40.614–41.494秒、変更なし0.716–0.753秒、`Game.h` 18.440–18.662秒、`GameObject.h` 26.783–27.094秒、`TableConfig.h` 32.304–32.435秒、代表 `.cpp` 7.054–7.267秒だった。前フェーズのクリーン計測は51.402–93.258秒と41.856秒のばらつきがあり、今回は代表 `.cpp` が0.11%悪化している一方でクリーンだけが50.90%改善している。そのためクリーンと無変更の大幅な生短縮率は、キャッシュ、リアルタイムスキャン、マシン負荷等の外部要因を含むものとして実装効果には採用しない。

このフェーズの構造上の効果は、`BalanceAutoPlayer.h` の推移影響12→8 `.cpp`（33.33%減）、`BalanceValidationController.h` と `DynamicBalanceController.h` の13→9 `.cpp`（各30.77%減）である。`Game.h` 自体の影響数は12件を維持したが、上記3ヘッダーを変更しても `GameBattleBridge.cpp`、`GameBossAI.cpp`、`GamePresentation.cpp`、`GameSceneAccess.cpp` は再コンパイルされなくなった。新しい `GameBalanceRuntime.h` の影響は、実際に状態へ触れる8実装だけで、include循環は0件である。

ランseed、stage/route seed、route counter、基準難易度、HP倍率、攻撃加算値は値も初期値も変えずに移動した。セーブsnapshotとMCP snapshotも同じ値を読み書きし、自動プレイ・validation・固定スケーリングの判断実装には変更を加えていない。

- 追加フェーズの生ログとsummary: `build_measurements/game_balance_runtime_after/`
- 依存グラフ: `build_measurements/game_balance_runtime_after/dependency_graph.json`

### 追加フェーズ: Game balance owner boundary

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 40.901 | 38.036 | 2.865 | 7.00% | 78→78 |
| 変更なし | 0.723 | 0.694 | 0.029 | 4.01% | 0→0 |
| `Game.h` | 18.441 | 17.164 | 1.277 | 6.92% | 12→12 |
| `GameObject.h` | 26.858 | 24.982 | 1.876 | 6.98% | 27→27 |
| `TableConfig.h` | 32.325 | 29.755 | 2.570 | 7.95% | 36→36 |
| 代表 `.cpp` | 7.168 | 6.685 | 0.483 | 6.74% | 1→1 |

修正後の min–max は、クリーン37.707–38.385秒、変更なし0.678–0.698秒、`Game.h` 17.123–17.184秒、`GameObject.h` 24.976–25.032秒、`TableConfig.h` 29.674–29.942秒、代表 `.cpp` 6.653–6.728秒だった。代表 `.cpp` を含む全コンパイル条件が6.74～7.95%の近い割合で一様に短縮しているため、この生秒数はマシン・キャッシュ状態の改善を含み、owner境界追加の効果とは判定しない。

`GameBalanceRuntime` のフィールドをprivate化し、`Game` だけが内部実装を接続できるようにした。`GameDebugHost` は `CaptureAutomationSnapshot`、`SetAutoPlayEnabled`、`SetValidationEnabled`、`SetStopAfterCurrentRunRequested` を使用し、`GameMcpReadView` は `CaptureReadSnapshot`、`GameMcpCommands` は名前付き操作を使用する。これにより `GameDebugMode.cpp` と `GameMcpBridge.cpp` における `m_BalanceRuntime->autoPlayer`、`validation`、`scaling` の直接参照はすべて0件になった。

専用 `.cpp` の追加はcontrollerヘッダーの影響数を各1件増やすため採用せず、ownerメソッド実装を既に同じ依存を持つ `GameBalanceConfig.cpp` に配置した。この結果、自作 `.cpp` は78件、`GameBalanceRuntime.h` は8件、`BalanceAutoPlayer.h` は8件、validationとdynamic balanceは各9件、`Game.h` は12件、include循環は0件を維持した。

- 追加フェーズの生ログとsummary: `build_measurements/game_balance_owner_after/`
- 依存グラフ: `build_measurements/game_balance_owner_after/dependency_graph.json`

### 追加フェーズ: Game runtime state

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 38.140 | 37.717 | 0.423 | 1.11% | 78→79 |
| 変更なし | 0.680 | 0.707 | -0.027 | -3.97% | 0→0 |
| `Game.h` | 17.355 | 17.151 | 0.204 | 1.18% | 12→12 |
| `GameObject.h` | 25.021 | 25.008 | 0.013 | 0.05% | 27→27 |
| `TableConfig.h` | 30.080 | 29.919 | 0.161 | 0.54% | 36→36 |
| 代表 `.cpp` | 6.790 | 6.806 | -0.016 | -0.24% | 1→1 |

修正前の min–max は、クリーン37.788–38.361秒、変更なし0.677–0.700秒、`Game.h` 17.190–17.358秒、`GameObject.h` 24.988–25.137秒、`TableConfig.h` 29.949–30.236秒、代表 `.cpp` 6.776–6.792秒だった。修正後は、クリーン37.608–38.370秒、変更なし0.704–0.708秒、`Game.h` 17.100–17.218秒、`GameObject.h` 24.967–25.146秒、`TableConfig.h` 29.739–30.105秒、代表 `.cpp` 6.750–6.815秒だった。変化は全体に小さく、`Game.h` の0.204秒短縮だけを単独のコード効果として断定しない。

構造上の効果は明確である。`Game.h` から `SceneManager.h` と `FixedStepClock.h` を除去し、シーン寿命と固定step計測状態を `GameRuntimeState` に移した。`Game` には更新順序、固定step中断条件、シーン遷移前後の判断を残している。`SceneManager.h` の推移影響は13→5自作 `.cpp`（61.54%減）、`FixedStepClock.h` は12→4 `.cpp`（66.67%減）となった。新しい `GameRuntimeState.h` の影響は、実際に接続・読み取りを行う4実装だけである。`Game.h` の再コンパイル数は12件のままだが、同じ12件が解析する推移ヘッダーを削減した。

`GameSaveSystem.cpp` のシーン種別だけで可能な判定は `GetCurrentSceneType()` に置き換え、`StageSelectScene.h`、`ShopScene.h`、`TitleScene.h` の不要includeも削除した。RestSite固有の使用済み操作だけは具体型情報が必要なため、`RestSiteScene` 参照を維持した。物理tick、substep上限回数、drop秒、リセット条件の初期値と更新順序は変更していない。include循環は0件を維持した。

- 修正前ログとsummary: `build_measurements/game_runtime_state_before/`
- 修正後ログとsummary: `build_measurements/game_runtime_state_after/`
- 修正後依存グラフ: `build_measurements/game_runtime_state_after/dependency_graph.json`

### 追加フェーズ: Game settings runtime

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 37.717 | 37.882 | -0.165 | -0.44% | 79→80 |
| 変更なし | 0.707 | 0.701 | 0.006 | 0.85% | 0→0 |
| `Game.h` | 17.151 | 17.162 | -0.011 | -0.06% | 12→12 |
| `GameObject.h` | 25.008 | 25.140 | -0.132 | -0.53% | 27→27 |
| `TableConfig.h` | 29.919 | 30.215 | -0.296 | -0.99% | 36→36 |
| 代表 `.cpp` | 6.806 | 6.775 | 0.031 | 0.46% | 1→1 |

修正後の min–max は、クリーン37.699–38.005秒、変更なし0.689–0.710秒、`Game.h` 17.147–17.270秒、`GameObject.h` 25.054–25.162秒、`TableConfig.h` 30.136–30.243秒、代表 `.cpp` 6.746–6.793秒だった。増減はいずれも1%未満であり、新しい自作 `.cpp` が1件増えた影響を含む。秒数は同等と判定し、短縮効果を過大評価しない。

`SettingsManager` の所有、読込、保存、画面設定反映、全画面切り替え、UI設定置換を `GameSettingsRuntime` に移した。`Game` はF11やポーズUIから届いた要求の処理順だけを決める。振動停止は従来どおり設定が有効から無効へ変わったときだけ実行し、設定JSONの形式と保存タイミングは変更していない。

`Game.h` から `SettingsManager.h` と値メンバーを除去した結果、`SettingsManager.h` の推移影響は13→6自作 `.cpp`（53.85%減）になった。新しい `GameSettingsRuntime.h` の影響は、owner実装と接続・表示を担う3実装だけである。`Game.h` の再コンパイル数は12件、include循環は0件を維持した。

- 比較元ログとsummary: `build_measurements/game_runtime_state_after/`
- 修正後ログとsummary: `build_measurements/game_settings_runtime_after/`
- 修正後依存グラフ: `build_measurements/game_settings_runtime_after/dependency_graph.json`

### 追加フェーズ: Game persistence runtime

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 37.882 | 38.010 | -0.128 | -0.34% | 80→81 |
| 変更なし | 0.701 | 0.694 | 0.007 | 1.00% | 0→0 |
| `Game.h` | 17.162 | 17.087 | 0.075 | 0.44% | 12→12 |
| `GameObject.h` | 25.140 | 25.218 | -0.078 | -0.31% | 27→27 |
| `TableConfig.h` | 30.215 | 29.985 | 0.230 | 0.76% | 36→36 |
| 代表 `.cpp` | 6.775 | 6.819 | -0.044 | -0.65% | 1→1 |

修正後の min–max は、クリーン37.997–38.069秒、変更なし0.692–0.711秒、`Game.h` 16.864–17.157秒、`GameObject.h` 25.188–25.244秒、`TableConfig.h` 29.876–30.122秒、代表 `.cpp` 6.792–6.869秒だった。新しい自作 `.cpp` が1件増えてもクリーンは0.34%差、増分条件は±1%内であり、実測上は同等である。

ラン統計、永続進行、アセンション、永続報酬可否、セーブ復元中フラグ、セーブ通知寿命を `GamePersistenceRuntime` に移した。`Game` はラン中だけ統計frameを進める判断、セーブ復元の開始・終了、確定ラン結果を記録するタイミングを調停する。統計値、進行プロフィール、アセンション補正、通知240frameという既存値は変更していない。

`Game.h` から `RunStatisticsTracker.h` と `ProgressionProfile.h`、対応する値メンバーを除去した。`RunStatisticsTracker.h` の推移影響は13→11自作 `.cpp`（15.38%減）となった。一方、`ProgressionProfile.h` は18件のままである。これは10実装が利用するownerヘッダーがプロフィールを値保持し、各実装も型付きプロフィール操作を行うためで、二重PImplや汎用コマンド層を追加してまで隠蔽しなかった意図的なトレードオフである。`GamePersistenceRuntime.h` の影響は10件、`Game.h` は12件、include循環は0件を維持した。

- 比較元ログとsummary: `build_measurements/game_settings_runtime_after/`
- 修正後ログとsummary: `build_measurements/game_persistence_runtime_after/`
- 修正後依存グラフ: `build_measurements/game_persistence_runtime_after/dependency_graph.json`

### 追加フェーズ: Shot relic rule forward declaration

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 38.010 | 38.037 | -0.027 | -0.07% | 81→81 |
| 変更なし | 0.694 | 0.701 | -0.007 | -1.01% | 0→0 |
| `Game.h` | 17.087 | 17.271 | -0.184 | -1.08% | 12→12 |
| `GameObject.h` | 25.218 | 24.964 | 0.254 | 1.01% | 27→27 |
| `TableConfig.h` | 29.985 | 29.954 | 0.031 | 0.10% | 36→36 |
| 代表 `.cpp` | 6.819 | 6.822 | -0.003 | -0.04% | 1→1 |

修正後の min–max は、クリーン37.993–38.350秒、変更なし0.690–0.720秒、`Game.h` 17.119–17.351秒、`GameObject.h` 24.960–24.986秒、`TableConfig.h` 29.925–29.970秒、代表 `.cpp` 6.796–6.831秒だった。対象により増減方向が異なり、すべて約1%内なので秒数は同等と判定する。`Game.h` の0.184秒悪化も修正後のばらつき0.232秒内であり、依存除去の悪化とは判定しない。

`ShotRelicRules` は`Game.h`で値保持されず、関数の戻り値と参照引数にだけ現れる。そのため完全型includeを前方宣言へ置き換え、実装する `GameProgression.cpp` と戦闘接続を行う `GameBattleBridge.cpp` が明示的にincludeする構成へ変更した。規則、初期値、予測への引き渡し方は変更していない。

`ShotRelicRules.h` の推移影響は15→11自作 `.cpp`（26.67%減）となった。`Game.h` 自体の影響数は12件、include循環は0件を維持した。

- 比較元ログとsummary: `build_measurements/game_persistence_runtime_after/`
- 修正後ログとsummary: `build_measurements/game_shot_relic_forward_after/`
- 修正後依存グラフ: `build_measurements/game_shot_relic_forward_after/dependency_graph.json`

### 追加フェーズ: Game run defaults

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 38.037 | 37.878 | 0.159 | 0.42% | 81→82 |
| 変更なし | 0.701 | 0.707 | -0.006 | -0.86% | 0→0 |
| `Game.h` | 17.271 | 17.150 | 0.121 | 0.70% | 12→12 |
| `GameObject.h` | 24.964 | 25.023 | -0.059 | -0.24% | 27→27 |
| `TableConfig.h` | 29.954 | 29.958 | -0.004 | -0.01% | 36→36 |
| 代表 `.cpp` | 6.822 | 6.731 | 0.091 | 1.33% | 1→1 |

修正後の min–max は、クリーン37.826–38.227秒、変更なし0.704–0.710秒、`Game.h` 17.058–17.302秒、`GameObject.h` 24.986–25.073秒、`TableConfig.h` 29.757–30.047秒、代表 `.cpp` 6.699–6.760秒だった。`Game.h` の0.121秒短縮は修正後のばらつき0.244秒内であり、実測上は同等である。代表 `.cpp` は1.33%短縮したが、責務分割と直接関係する効果とは判定しない。

設定から得るプレイヤーボール初期値とラン初期値を `GameRunDefaults` に移し、可変な実行中状態を持つ `RunController` と区別した。`Game` は初期値のロード、ラン開始時の適用順、シード確定後のデッキ再構築を引き続き調停する。既定値の型、正規化、半径のMCP公開値、ラン開始処理の順序は変更していない。

`Game.h` から `BallStatus.h` と `PlayerRunStatus.h`、対応する値メンバーを除去し、`GameRunDefaults` を不完全型の `unique_ptr` として保持するようにした。ただし `GameRunDefaults.h` 自体が2型を値保持するため、今回の依存グラフでは `BallStatus.h` が40→41件、`PlayerRunStatus.h` が16→17件と、新規実装1件分だけ増えた。新しいownerヘッダーの影響は4件、`Game.h` は12件、include循環は0件である。これは責務と所有者を先に明確化した段階であり、ビルド影響の短縮は未達である。型を完全に隠すためだけの二重PImplは採らず、今後、設定ロードと初期状態適用をowner操作へ移せる時点で公開する型付き参照を減らす。

- 比較元ログとsummary: `build_measurements/game_shot_relic_forward_after/`
- 修正後ログとsummary: `build_measurements/game_run_defaults_after/`
- 修正後依存グラフ: `build_measurements/game_run_defaults_after/dependency_graph.json`

### 追加フェーズ: Game run session

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 37.878 | 38.294 | -0.416 | -1.10% | 82→83 |
| 変更なし | 0.707 | 0.708 | -0.001 | -0.14% | 0→0 |
| `Game.h` | 17.150 | 17.380 | -0.230 | -1.34% | 12→12 |
| `GameObject.h` | 25.023 | 25.020 | 0.003 | 0.01% | 27→27 |
| `TableConfig.h` | 29.958 | 30.206 | -0.248 | -0.83% | 36→36 |
| 代表 `.cpp` | 6.731 | 6.809 | -0.078 | -1.16% | 1→1 |

修正後の min–max は、クリーン37.736–38.496秒、変更なし0.702–0.730秒、`Game.h` 17.293–17.420秒、`GameObject.h` 25.003–25.202秒、`TableConfig.h` 29.972–30.659秒、代表 `.cpp` 6.783–6.810秒だった。新しい自作 `.cpp` が1件増え、中央値はクリーン0.416秒、`Game.h` 0.230秒悪化した。ただしクリーンの差は修正後のばらつき0.760秒内で、`Game.h` も前後のmin–maxが近接している。秒数は概ね同等だが短縮は達成しておらず、生の悪化値をそのまま記録する。

ランの有効状態と確定済みリザルトを `GameRunSession` に移した。新規ラン開始は結果初期化と有効化を一操作にし、通常の勝敗確定は完成した `RunResultSnapshot` の格納と無効化を一操作にした。セーブ復元、タイトル帰還、デバッグ終了は明示的な有効化・無効化を通す。`Game` はラン開始・終了のタイミング、統計からの結果生成、シーン遷移を引き続き調停するため、セーブ形式、MCP状態形式、統計値、乱数順序は変更していない。

`Game.h` から `RunResultSnapshot.h`、`m_RunActive`、`m_LastRunResult` を除去し、結果型を前方宣言、ownerを不完全型の `unique_ptr` とした。`RunResultSnapshot.h` の推移影響は23件のままである。`Game.h` 経由の依存はなくなった一方、`ProgressionProfile`、`RunStatisticsTracker`、`ResultScene` など型を実際に使う既存経路と、新owner実装があるため総数は減らなかった。`GameRunSession.h` の影響はowner実装と `Game.cpp` の2件、`Game.h` は12件、include循環は0件である。

- 比較元ログとsummary: `build_measurements/game_run_defaults_after/`
- 修正後ログとsummary: `build_measurements/game_run_session_after/`
- 修正後依存グラフ: `build_measurements/game_run_session_after/dependency_graph.json`

### 追加フェーズ: Game reward state

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 38.294 | 38.187 | 0.107 | 0.28% | 83→84 |
| 変更なし | 0.708 | 0.699 | 0.009 | 1.27% | 0→0 |
| `Game.h` | 17.380 | 17.205 | 0.175 | 1.01% | 12→12 |
| `GameObject.h` | 25.020 | 25.069 | -0.049 | -0.20% | 27→27 |
| `TableConfig.h` | 30.206 | 29.971 | 0.235 | 0.78% | 36→36 |
| 代表 `.cpp` | 6.809 | 6.805 | 0.004 | 0.06% | 1→1 |

修正後の min–max は、クリーン37.821–38.347秒、変更なし0.695–0.715秒、`Game.h` 17.079–17.269秒、`GameObject.h` 25.039–25.091秒、`TableConfig.h` 29.706–30.099秒、代表 `.cpp` 6.746–6.837秒だった。中央値は多くの条件でわずかに短縮したが、最大1.27%で前後のばらつきと重なるため、実測上は同等と判定する。新しい自作 `.cpp` が1件増えた状態でもクリーン中央値は0.28%短縮した。

クリア報酬の有効状態、報酬・対象ボール・中ボスレリックの選択位置、提示ボール番号、結果メッセージ、確定要求を `GameRewardState` に集約した。キーボード、表示層、オートプレイ、MCPは同じ状態機械を利用し、`Game` は報酬開始・付与処理・次ルート遷移の順序だけを調停する。報酬候補数、追加Money、強化費用、抽選、イベント形式は変更していない。

`Game.h` から9個の報酬状態メンバーと、それらを読むインライン実装を除去した。MCPの追加Money操作はprivateフラグの組み合わせではなく、`IsClearRewardActive()`、`IsClearRewardChosen()`、`IsMidBossRelicSelectionActive()` を通して検証する。`GameRewardState.h` の影響はowner実装と報酬を調停する5実装の計6件、`Game.h` は12件、include循環は0件である。既存の標準型includeは他の公開APIでも必要なため、この段階だけではヘッダー更新時の再コンパイル数は減っていない。

- 比較元ログとsummary: `build_measurements/game_run_session_after/`
- 修正後ログとsummary: `build_measurements/game_reward_state_after/`
- 修正後依存グラフ: `build_measurements/game_reward_state_after/dependency_graph.json`

### 追加フェーズ: Game encounter balance

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 38.187 | 38.274 | -0.087 | -0.23% | 84→85 |
| 変更なし | 0.699 | 0.712 | -0.013 | -1.86% | 0→0 |
| `Game.h` | 17.205 | 17.239 | -0.034 | -0.20% | 12→12 |
| `GameObject.h` | 25.069 | 25.085 | -0.016 | -0.06% | 27→27 |
| `TableConfig.h` | 29.971 | 29.949 | 0.022 | 0.07% | 36→36 |
| 代表 `.cpp` | 6.805 | 6.779 | 0.026 | 0.38% | 1→1 |

修正後の min–max は、クリーン38.117–38.746秒、変更なし0.703–0.716秒、`Game.h` 17.100–17.330秒、`GameObject.h` 25.014–25.142秒、`TableConfig.h` 29.765–30.274秒、代表 `.cpp` 6.774–6.811秒だった。すべて2%未満で前後のばらつきと重なるため、秒数は同等と判定する。新しい自作 `.cpp` が1件増えた影響を含み、クリーン中央値の悪化は0.23%だった。

敵別コスト、ステージ別目標値、配置種別倍率とJSON読込を `GameEncounterBalance` に移し、ステージログに必要な基礎予算、倍率、実予算、目標との差を `EncounterThreatSnapshot` として返すようにした。`Game` は起動時の読込順とログ記録のタイミングだけを調停する。既定コスト10、4体以上のdense判定、倍率下限0.1、2桁丸め、JSONキーとログ形式は変更していない。

`Game.h` から2個の `unordered_map` と3個の倍率、`<unordered_map>` includeを除去した。評価境界は重い `StageData` を受け取らず、ステージID、敵ID一覧、MCP上書き有無だけを受け取る。このため新しい自作 `.cpp` を追加しても、`IndexBuffer.h`、`BallStatus.h`、`TableConfig.h` など既存の高波及ヘッダー件数は増えていない。`GameEncounterBalance.h` の影響はowner実装、owner生成、設定接続、ログ接続の4件、`Game.h` は12件、include循環は0件である。

- 比較元ログとsummary: `build_measurements/game_reward_state_after/`
- 修正後ログとsummary: `build_measurements/game_encounter_balance_after/`
- 修正後依存グラフ: `build_measurements/game_encounter_balance_after/dependency_graph.json`

### 追加フェーズ: Game ball selection state

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 38.274 | 38.562 | -0.288 | -0.75% | 85→86 |
| 変更なし | 0.712 | 0.723 | -0.011 | -1.54% | 0→0 |
| `Game.h` | 17.239 | 17.294 | -0.055 | -0.32% | 12→12 |
| `GameObject.h` | 25.085 | 25.252 | -0.167 | -0.67% | 27→27 |
| `TableConfig.h` | 29.949 | 29.972 | -0.023 | -0.08% | 36→36 |
| 代表 `.cpp` | 6.779 | 6.847 | -0.068 | -1.00% | 1→1 |

修正後の min–max は、クリーン38.523–38.978秒、変更なし0.715–0.729秒、`Game.h` 17.285–17.461秒、`GameObject.h` 25.216–25.332秒、`TableConfig.h` 29.912–29.981秒、代表 `.cpp` 6.820–6.899秒だった。全条件で中央値はわずかに悪化したが最大1.54%であり、新しい自作 `.cpp` 1件の追加と測定ばらつきの範囲にある。短縮は未達で、再コンパイル数も維持である。

使用ボール候補の選択位置とホールド位置を `GameBallSelectionState` に移し、候補範囲の検証、選択した候補のホールド解除、ホールド切替、UI選択反映を一箇所へまとめた。人間の数字キー、表示UI、自動プレイ、ボスAI、MCP経由の自動ショットは同じ選択操作を通る。候補作成、AIの候補決定、発射可否、デッキ確定順序は変更していない。

`Game.h` から2個の選択状態メンバーを除去し、MCPの状態取得もprivate参照ではなく `GetSelectedBallOfferIndex()` と `GetHeldBallOfferIndex()` を使う。`GameBallSelectionState.h` の影響はowner実装と選択を接続する4実装の計5件、`Game.h` は12件、include循環は0件である。状態は整数だけなのでヘッダー型依存の削減効果はなく、この段階の価値は入力経路間の規則統一と責務分離にある。

- 比較元ログとsummary: `build_measurements/game_encounter_balance_after/`
- 修正後ログとsummary: `build_measurements/game_ball_selection_after/`
- 修正後依存グラフ: `build_measurements/game_ball_selection_after/dependency_graph.json`

### 追加フェーズ: Game pause state

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 38.562 | 43.470 | -4.908 | -12.73% | 86→87 |
| 変更なし | 0.723 | 0.753 | -0.030 | -4.15% | 0→0 |
| `Game.h` | 17.294 | 18.393 | -1.099 | -6.35% | 12→12 |
| `GameObject.h` | 25.252 | 26.413 | -1.161 | -4.60% | 27→27 |
| `TableConfig.h` | 29.972 | 31.918 | -1.946 | -6.49% | 36→36 |
| 代表 `.cpp` | 6.847 | 6.949 | -0.102 | -1.49% | 1→1 |

修正後の min–max は、クリーン41.881–43.748秒、変更なし0.752–0.758秒、`Game.h` 18.282–18.865秒、`GameObject.h` 26.128–26.575秒、`TableConfig.h` 31.162–32.007秒、代表 `.cpp` 6.923–6.953秒だった。全条件が同じ向きに1.49–12.73%遅くなった一方、再コンパイル数は新規ownerを含むクリーン以外すべて不変である。このため依存波及の悪化ではなく、測定セッション間のマシン負荷差が主因と判断する。ただし生値は補正せず、そのまま悪化として記録する。

ポーズ状態、タイトル復帰確認、表示適用、およびUIから更新ループへ渡す3種類のone-shot要求を `GamePauseState` に移した。`Game` は要求を所定の更新順で消費し、設定保存、全画面切替、セーブ、フレーム停止を調停する。要求消費の順序、ポーズ解除時だけ設定を保存する条件、デバッグ・自動プレイ時の制約は変更していない。公開requestと`IsPaused()`はout-of-line化し、状態配置の変更を`Game.h`利用側へ伝えない。

`Game.h` から6個のUI状態boolと、それらを操作するインライン実装を除去した。`GameDebugHost` とラン開始処理は状態メンバーを直接書かず、ownerへ委譲する `Game` の操作を使う。`GamePauseState.h` の影響はowner実装、更新調停、表示接続の3件だけで、`Game.h` は12件、include循環は0件である。この段階は`Game.h`更新時の再コンパイル数をさらに減らすものではないが、今後のポーズUI・要求キュー変更の波及を3実装へ閉じ込める。

- 比較元ログとsummary: `build_measurements/game_ball_selection_after/`
- 修正後ログとsummary: `build_measurements/game_pause_state_after/`
- 修正後依存グラフ: `build_measurements/game_pause_state_after/dependency_graph.json`

### 追加フェーズ: RunController implementation boundary

| 計測対象 | 直前の保存計測 | 今回 | 参考差分 | 参考短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 43.470 | 89.975 | -46.505 | -106.98% | 87→88 |
| 変更なし | 0.753 | 0.768 | -0.015 | -1.99% | 0→0 |
| `Game.h` | 18.393 | 29.903 | -11.510 | -62.58% | 12→13 |
| `GameObject.h` | 26.413 | 45.985 | -19.572 | -74.10% | 27→27 |
| `TableConfig.h` | 31.918 | 61.976 | -30.058 | -94.17% | 36→37 |
| 代表 `.cpp` | 6.949 | 12.451 | -5.502 | -79.18% | 1→1 |

今回の min–max は、クリーン89.110–90.604秒、変更なし0.735–0.804秒、`Game.h` 29.801–29.959秒、`GameObject.h` 45.319–47.482秒、`TableConfig.h` 60.987–62.648秒、代表 `.cpp` 11.929–12.695秒だった。ただし比較元は2026-10-05、今回は2026-10-08の計測であり、その間に本フェーズ外の自作ソース変更も入っている。さらにすべてのコンパイル条件が一様に大幅悪化しているため、この差分を本変更の速度効果として扱わない。数値は推測や補正をせず参考値として残し、構造上の効果はプロジェクト登録済みソースを走査した依存グラフの再コンパイル範囲で判定する。

`RunController` の値保持を `std::unique_ptr` に変更し、`Game.h` から `RunController.h`、`PlayerDeck.h`、`RunProgressConstants.h` を除去した。ラン状態を読む18個のインラインAPIは `GameRunAccess.cpp` に移し、公開ヘッダーからデッキ、所持金、HP、ショップ候補、レリック候補の実装詳細を外した。実行時の所有者は引き続き `Game` で、生成・破棄順、乱数エンジン、デッキ状態、セーブsnapshot、ゲームルールは変更していない。

依存グラフ上、`RunController.h` 変更時の波及は13→11 cpp、`PlayerDeck.h` は15→14 cppへ減った。`Game.h` は分割先の新規cppが加わり12→13 cppとなったため、この段階単体では `Game.h` の再コンパイル数を1件悪化させている。一方、今後変更頻度が高いラン・デッキ実装の変更は、シーンアクセス、戦闘bridge、セーブシステムなどへ推移しなくなった。include循環は0件である。

- 比較元ログとsummary: `build_measurements/game_pause_state_after/`
- 今回ログとsummary: `build_measurements/game_run_controller_pimpl_after/`
- 今回依存グラフ: `build_measurements/game_run_controller_pimpl_after/dependency_graph.json`

### 追加フェーズ: Game stage override state

| 計測対象 | 修正前 | 修正後 | 差分 | 短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 89.975 | 93.067 | -3.092 | -3.44% | 88→89 |
| 変更なし | 0.768 | 0.767 | 0.001 | 0.13% | 0→0 |
| `Game.h` | 29.903 | 29.814 | 0.089 | 0.30% | 13→13 |
| `GameObject.h` | 45.985 | 45.581 | 0.404 | 0.88% | 27→27 |
| `TableConfig.h` | 61.976 | 62.720 | -0.744 | -1.20% | 37→38 |
| 代表 `.cpp` | 12.451 | 11.044 | 1.407 | 11.30% | 1→1 |

修正後のmin–maxは、クリーン92.566–93.193秒、変更なし0.735–0.775秒、`Game.h` 29.592–30.098秒、`GameObject.h` 44.683–46.443秒、`TableConfig.h` 61.796–64.209秒、代表 `.cpp` 10.653–11.119秒だった。直前フェーズと同一日・同一スクリプト・同一設定で測定した。`Game.h` と `GameObject.h` の秒数は1%未満でばらつきと重なるため同等と判定する。代表 `.cpp` は11.30%短縮したが再コンパイル数は不変で、本分離との因果は認めない。新規owner実装が1 TU増えたため、クリーンは3.44%、`TableConfig.h` は1.20%悪化し、波及数も37→38となった。

MCPとdebug hot reloadが別々に書き換えていた「次回」と「現在」のステージ上書き状態を `GameStageOverrideState` に集約した。予約、取消し、戦闘開始時の現在値への移動、現在値のクリアを同じlifecycleにし、`Game` は戦闘開始順序と最終ボス判定だけを調停する。MCPの入出力形式、debug読込、`StageType`判定、ステージデータの移動タイミングは変更していない。

`Game.h` から `StageData.h` と2個の `std::optional<StageData>` 値メンバーを除去し、`StageData` は前方宣言、ownerは不完全型の `unique_ptr` とした。owner自身もPImplとし、その利用側へ `StageData.h` を波及させない。ただし現状の `Game` 分割cppは `RunController.h`→`StageSelector.h`経由で `StageData.h` へ依存しており、新規owner実装が追加されたため、`StageData.h` の推移影響は24→25 cppと1件増えた。この段階の価値は公開ヘッダーからの具象型排除と状態所有の一意化であり、ビルド波及短縮は未達である。次に `RunController` 内部の `StageSelector`・`PlayerDeck` を実装境界へ隠すことで、この残存経路を縮小できる。include循環は0件である。

- 比較元ログとsummary: `build_measurements/game_run_controller_pimpl_after/`
- 修正後ログとsummary: `build_measurements/game_stage_override_state_after/`
- 修正後依存グラフ: `build_measurements/game_stage_override_state_after/dependency_graph.json`

### 追加フェーズ: RunController internal implementation boundary

| 計測対象 | 修正前 | 修正後 | 差分 | 生の短縮率 | 再コンパイル数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーン | 93.067 | 39.953 | 53.114 | 57.07% | 89→89 |
| 変更なし | 0.767 | 0.719 | 0.048 | 6.26% | 0→0 |
| `Game.h` | 29.814 | 17.367 | 12.447 | 41.75% | 13→13 |
| `GameObject.h` | 45.581 | 25.192 | 20.389 | 44.73% | 27→27 |
| `TableConfig.h` | 62.720 | 30.228 | 32.492 | 51.80% | 38→37 |
| 代表 `.cpp` | 11.044 | 6.756 | 4.288 | 38.83% | 1→1 |

修正後のmin–maxは、クリーン39.044–40.612秒、変更なし0.699–0.734秒、`Game.h` 17.354–17.486秒、`GameObject.h` 25.112–25.228秒、`TableConfig.h` 29.996–30.588秒、代表 `.cpp` 6.702–6.765秒だった。比較元と同一日・同一マシン・同一スクリプト・同一並列設定で連続測定したが、再コンパイル数が不変の代表 `.cpp` まで38.83%短縮し、全条件が一様に39–57%改善した。このため生の秒数差は主にマシン負荷変化と判定し、本変更の短縮効果としては採用しない。数値は推測や補正をせず生値のまま記録し、因果のある効果は同じログから得た再コンパイル数と依存グラフで判定する。

`RunController` のラン状態、デッキ、進行、ステージ選択、レリック、オファー、報酬状態、乱数エンジンを単一の `Impl` へ移した。従来別ヒープだった `RunProgressController` は `Impl` の値メンバーにし、PImpl導入による追加のヒープ確保数を増やしていない。公開API、参照の有効期間、メンバーの構築順、seed設定、デッキ・ステージ・レリックの乱数列は変更していない。

`RunController.h` から `PlayerDeck.h`、`PlayerRunStatus.h`、`StageSelector.h`、`RunProgressConstants.h` を除去し、完全型を操作するcppだけが必要なヘッダーを明示的にincludeするようにした。依存グラフで `StageSelector.h` は14→7 cpp、`StageData.h` は25→23 cpp、`RunProgressConstants.h` は14→11 cpp、`PlayerRunStatus.h` は17→16 cpp、`TableConfig.h` は38→37 cppに減った。`PlayerDeck.h` は表示、保存、MCP、自動プレイ等が実際にデッキAPIを使うため14 cppで不変、`RunController.h` は11 cpp、`Game.h` は13 cppで不変だった。include循環は0件を維持した。

- 比較元ログとsummary: `build_measurements/game_stage_override_state_after/`
- 修正後ログとsummary: `build_measurements/run_controller_impl_after/`
- 修正後依存グラフ: `build_measurements/run_controller_impl_after/dependency_graph.json`

### 追加フェーズ: Game.h dead API cleanup

現行の `.vcxproj` に登録された `ClCompile` / `ClInclude` だけを対象に、`Game.h` の公開メソッド、メンバー変数、前方宣言の参照数と参照位置を再走査した。過去のランタイム比較用スナップショットはビルド対象と分離し、現行ビルドで参照が0件の `GetCamera`、`GetGameObjectsWithTag`、`ContainsGameObject`、`GetPierceTraceCount`、`GetTotalCushionStacks` を宣言・インライン実装・cpp定義から削除した。コメント化された旧includeの雛形も削除し、`Game.h` は647行から629行になった。宣言しかないメンバー変数と前方宣言は検出されなかった。

これらは既に狭い `GameRuntime` / `GameBattleView` 境界へ移行済みの古い便利APIであり、ゲームルール、セーブ形式、MCP形式、乱数列に変更はない。`Game.h` 自体の波及は直接13 cpp、推移13 cppで不変、include循環は0件である。この小変更は再コンパイル数を減らす分割ではないため、秒数の小差を効果として扱わない。依存グラフは `build_measurements/game_header_dead_api_cleanup_after/dependency_graph.json` に保存した。

### 追加フェーズ: GameWorld ownership boundary

プレイヤーに見える変化はなく、選択・狙い・連鎖および取得・強化前後の判断は変えない。数値だけの強化、自動勝利、外見だけのビリヤードになる危険を生まない純粋な依存整理である。成功条件は、`Game.h` から `GameWorld` 完全型とworld検索テンプレートが外れ、更新波及が減り、既存contractと `Debug|x64` ビルドが成功することとした。

`Game` が値保持していた `GameWorld` を、既にheap所有している `GameRuntimeState` の値メンバーへ移した。したがって新しいheap割り当ては増やさず、構築・破棄順と `GameWorld` の寿命は従来どおり `Game` のランタイム寿命に一致する。`Game.h` から `GameWorld.h` と公開テンプレート `GetComponents<T>()` を削除し、world検索が必要な実装だけが `GameRuntime::World()` と `GameWorld.h` を明示的に使用する構成にした。ゲームルール、更新順序、セーブ形式、MCP形式、seedおよび乱数列には変更がない。

修正前後の依存グラフでは、`Game.h` の推移影響は13 cppで維持しながら、`GameWorld.h` 更新時の推移再コンパイル範囲が22→18 cpp（4 cpp、18.18%減）、`Component.h` が46→43 cpp（3 cpp、6.52%減）になった。直接include数は `GameWorld.h` が10→18 cppへ増えたが、これは従来 `Game.h` 越しに暗黙取得していた実装依存を、実際にworldを操作するcppへ明示した結果である。include循環は0件を維持した。

同一スクリプト、VS2022 Enterprise 17.14.51、MSVC 14.38、`Debug|x64`、`/m`、PCHなし、各3回で修正後を計測した。比較元は直近の `run_controller_impl_after` であり、今回と計測時刻が異なるため、同じ再コンパイル数の項目まで一様に遅くなった秒数差は本変更の効果として採用しない。`GameWorld.h` と `Component.h` は比較元の時間計測対象ではなかったため、修正前秒数を推測せず未計測とした。

| 計測対象 | 比較元中央値（秒） | 修正後中央値（秒） | 差分（秒） | 短縮率 | 再コンパイル数（前→後） |
|---|---:|---:|---:|---:|---:|
| クリーンビルド | 39.953 | 91.286 | -51.333 | -128.48% | 89→89 |
| 変更なしビルド | 0.719 | 0.853 | -0.134 | -18.64% | 0→0 |
| `Game.h` 更新 | 17.367 | 30.617 | -13.250 | -76.29% | 13→13 |
| `GameWorld.h` 更新 | 未計測 | 33.061 | 算出不可 | 算出不可 | 22→18（依存グラフ） |
| `Component.h` 更新 | 未計測 | 60.090 | 算出不可 | 算出不可 | 46→43（依存グラフ） |
| 代表 `.cpp` 更新 | 6.756 | 11.986 | -5.230 | -77.41% | 1→1 |

修正後のmin–maxは、クリーン91.126–91.737秒、変更なし0.839–0.864秒、`Game.h` 30.496–31.012秒、`GameWorld.h` 32.757–34.087秒、`Component.h` 59.029–60.911秒、代表 `.cpp` 10.003–12.437秒だった。全計測ビルドは成功した。時間値の悪化は、再コンパイル数が不変のクリーン、変更なし、`Game.h`、代表cppでも同時に発生しており、計測時のマシン負荷差と判断する。短縮効果の根拠は、同一ソースグラフを解析した推移再コンパイル数の減少とし、秒数を補正していない。

- 比較元ログとsummary: `build_measurements/run_controller_impl_after/`
- 修正後ログとsummary: `build_measurements/game_world_runtime_state_after/`
- 修正後依存グラフ: `build_measurements/game_world_runtime_state_after/dependency_graph.json`

### 追加フェーズ: PlayerDeck read boundary

プレイヤーに見える変化はなく、選択・狙い・連鎖および自動プレイの意思決定は変えない。数値だけの強化、自動勝利、外見だけのビリヤードになる危険を生まない依存整理である。成功条件は、表示・MCP・自動プレイから具象 `PlayerDeck` と可変ownerへの到達を除去し、同一条件で `PlayerDeck.h` 更新時の再コンパイル数と時間が減り、既存contractと `Debug|x64` ビルドが成功することとした。

`Game::GetPlayerDeckView()` はconst参照であっても `PlayerDeck` の公開API全体を外部へ露出し、表示、MCP、自動プレイに `PlayerDeck.h` を要求していた。これを削除し、既存の `GetDeckBallCount()`、`GetDeckBall()`、`GetCurrentPlayerBallData()` に加えて、山札・捨て札の指定位置と現在球の使用済み状態だけを返す狭い読み取りAPIを `GameRunAccess.cpp` に置いた。表示、MCP、自動プレイは同じインデックスの同じ `PlayerBallData` をこの境界から読み、購入・削除・強化は従来どおり検証済みの `Game` 操作を通す。毎フレームの全デッキコピーは導入していない。

`GameAutoPlay.cpp`、`GameMcpBridge.cpp`、`GamePresentation.cpp` から `PlayerDeck.h` と `m_RunController->Deck()` を除去した。球のフィールドを実際に読むため `PlayerBallData.h` の直接includeは4→7実装へ増えたが、その推移影響は21 cppで不変だった。`PlayerDeck.h` の直接consumerは17→14、推移再コンパイル範囲は14→11 cpp（21.43%減）、`Game.h` と `RunController.h` は各13、11 cppで不変、include循環は0件を維持した。

VS2022 Enterprise 17.14.51、MSVC 14.38、`Debug|x64`、MSBuild `/m`、コンパイラ `/MP`、PCHなし、論理20プロセッサ、各3回で、変更前後を同じ連続セッション内に計測した。各増分条件はビルド済み状態へ戻してから一つのファイルだけを更新した。

| 計測対象 | 修正前中央値（秒） | 修正後中央値（秒） | 差分（秒） | 短縮率 | 再コンパイル数（前→後） |
|---|---:|---:|---:|---:|---:|
| クリーンビルド | 91.869 | 91.132 | 0.737 | 0.80% | 89→89 |
| 変更なしビルド | 0.787 | 0.819 | -0.032 | -4.07% | 0→0 |
| `Game.h` 更新 | 30.971 | 31.068 | -0.097 | -0.31% | 13→13 |
| `PlayerDeck.h` 更新 | 30.961 | 25.588 | 5.373 | 17.35% | 14→11 |
| 代表 `GameMcpBridge.cpp` 更新 | 15.264 | 15.458 | -0.194 | -1.27% | 1→1 |

修正前のmin–maxはクリーン90.225–92.161秒、変更なし0.766–0.799秒、`Game.h` 30.328–31.118秒、`PlayerDeck.h` 30.829–31.512秒、代表cpp 13.704–15.437秒だった。修正後はクリーン90.267–92.228秒、変更なし0.790–0.827秒、`Game.h` 30.589–31.224秒、`PlayerDeck.h` 24.806–25.821秒、代表cpp 15.333–15.758秒だった。`PlayerDeck.h` だけが3 cpp減少と測定ばらつきを超える5.373秒短縮を同時に示したため、ここを本変更の因果のある効果と判断する。その他の±1.3%以内の差は負荷ばらつきとして扱い、悪化も生値のまま記録した。

- 修正前ログ、summary、依存グラフ: `build_measurements/game_deck_read_boundary_before/`
- 修正後ログ、summary、依存グラフ: `build_measurements/game_deck_read_boundary_after/`

## 検証

- VS2022 `Debug|x64`: コンパイル・リンク成功。コンパイラ警告0、エラー0。既存DirectXTKのPDB不足によるLNK4099が2件。
- C++ 責務分割、セーブ、自動プレイ、報酬、デバッグ契約: 42件成功。
- CCD/TOI実行テスト: 高速衝突、壁、接線、チェイン、重なり、低反発、貫通・再侵入、アンカー、ポケット、上限ケースが成功。
- 共有予測ルール実行テスト: 摩擦、威力、貫通・屈折、アンカー、テーブル、ポケット、レリック規則が成功。
- MCP、boss、shop、run-map、shot planner、比較job: 145件成功。
- 今回の変更後に、責務分割・自動プレイ復帰・セーブ・バランス設定・自動比較・Debug forecastの関連contract 71件を再実行して成功。MCP一式もプロジェクトの `.venv` で145件再実行して成功。
- include循環: 0件。
- Runtime state分割後、責務分割・バランス設定・Debug forecast・セーブ・報酬・自動プレイ復帰の関連contract 57件を再実行して成功。
- Settings runtime分割後、同じ関連contract 58件を再実行して成功。
- Persistence runtime分割後、同じ関連contract 59件を再実行して成功。
- Shot relic依存除去後、同じ関連contract 60件を再実行して成功。
- Run defaults分割後、同じ関連contract 61件を再実行して成功。VS2022 `Debug|x64` もエラー0で完了し、既存DirectXTKのLNK4099が2件だけ残った。
- Run session分割後、同じ関連contract 62件を再実行して成功。VS2022 `Debug|x64` もエラー0で完了し、既存DirectXTKのLNK4099が2件だけ残った。
- Reward state分割後、同じ関連contract 63件を再実行して成功。VS2022 `Debug|x64` もエラー0で完了し、既存DirectXTKのLNK4099が2件だけ残った。
- Encounter balance分割後、同じ関連contract 64件を再実行して成功。VS2022 `Debug|x64` もエラー0で完了し、既存DirectXTKのLNK4099が2件だけ残った。
- Ball selection state分割後、同じ関連contract 65件を再実行して成功。VS2022 `Debug|x64` もエラー0で完了し、既存DirectXTKのLNK4099が2件だけ残った。
- Pause state分割後、同じ関連contract 66件を再実行して成功。VS2022 `Debug|x64` と3回ずつの全計測ビルドもエラー0で完了した。
- RunController境界化後、同じ関連contract 67件を再実行して成功。VS2022 `Debug|x64` と3回ずつの全計測ビルドもエラー0で完了し、既存DirectXTKのLNK4099が2件だけ残った。
- Stage override state分割後、同じ関連contract 68件を再実行して成功。VS2022 `Debug|x64` と3回ずつの全計測ビルドもエラー0で完了し、既存DirectXTKのLNK4099が2件だけ残った。
- RunController実装境界化後、同じ関連contract 68件を再実行して成功。推移includeの排除で顧客cppに必要な完全型が表面化したため、型を実際に操作する10実装へ直接includeを追加した。VS2022 `Debug|x64` と3回ずつの全計測ビルドはエラー0で完了し、既存DirectXTKのLNK4099が2件だけ残った。
- 上記68件にショップ候補contractとペアseed比較・統計指標テストを加えた計79件も成功。後者3モジュールはリポジトリルートからの実行では既存の相対import制約により収集に失敗したが、`DX22_01_plane/tools` を作業ディレクトリとし、一時ファイル書込みが可能な環境で再実行して8件すべて成功した。
- Game.h dead API cleanup後、関連contract 69件が成功。VS2022 `Debug|x64` は標準の14.38ツールセットでコンパイル・リンク成功、エラー0、既存DirectXTKのLNK4099が2件。サンドボックス内ではPDB管理プロセスのIPC制約によるC1902が発生したが、同一コマンドをサンドボックス外で実行すると成功した。
- GameWorld ownership boundary導入後、同じ関連contract 69件が成功。VS2022 `Debug|x64` と3回ずつの全計測ビルドも成功し、`GameWorld.h` の推移影響は22→18 cpp、`Component.h` は46→43 cpp、include循環は0件だった。
- PlayerDeck read boundary導入後、同じ関連contract 69件が成功。変更前後各3回の全計測ビルドも成功し、`PlayerDeck.h` 更新時は30.961→25.588秒、14→11 cpp、include循環は0件だった。
- 広域の `test_result_pause_contract` は別途実行したが、既存のCP932 `GameEvent.h` をUTF-8固定で読むテストエラー1件と、分離済み `StageSelectScene` の古い実装文字列を求める失敗1件が残った。今回のセーブ境界変更による失敗ではない。
- GUIの手動通しプレイは未実施。

## 辛口再レビュー

現時点は4 / 5に届く根拠が増えたが、5 / 5ではない。戦闘、ラン、報酬、設定、永続化、選択、ポーズ、ステージ上書き状態の所有者と外部読み取り境界が生まれ、`Game.h` の影響数も20→13に減った。MCPは `BuildState` と `ExecuteCommand`、Debugはcontrollerと診断UIから直接私有参照を排除し、外部オーケストレータ自体の `friend` を解消した。automation/balanceの具象型と再現用seedも `GameBalanceRuntime` に集約し、controller変更の波及を30%以上減らした。さらに `RunController`、`PlayerDeck`、`StageData`、`GameWorld` の具体型が `Game.h` から外れ、`RunController` 自身からもデッキとステージ選択の値保持依存を隠蔽した。`GameWorld` を使うcppが依存を明示する構造となり、その更新波及も22→18 cppへ減った。表示・MCP・自動プレイからは `PlayerDeck` 具象参照と直接ownerアクセスを除去し、`PlayerDeck.h` の波及も14→11 cppへ減った。一方、通常の `Game` メンバー関数には複数ownerの具体的操作を束ねる実装がまだ多く、読み取りAPIも個別getterが増えている。次段階では用途別view modelを構築する場所を `GamePresentation` / MCP read view側へ寄せ、`Game` の公開面を再び広げない設計が必要である。

4 / 5をより安定させる次の経路は、`GameMcpReadView`、`GameMcpCommands`、`GameDebugHost` に残るowner内部へのアクセスを型付きsnapshot・commandへ移し、`Game` の値保持型について依存コストと実行時複雑さを比較しながら境界を選ぶことである。これにより `Game` を更新順序、シーン遷移、機能間接続へさらに集中できる。
