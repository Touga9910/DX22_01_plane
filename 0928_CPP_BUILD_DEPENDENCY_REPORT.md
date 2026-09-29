# C++ 依存関係とビルド時間の改善レポート

作成日: 2026-09-28  
対象: `develop` (`a5a50ad` からの作業ツリー変更)

追加の `Game.h` 段階分割と再計測は `0928_GAME_HEADER_SPLIT_REPORT.md`、UI シーン境界まで進めた第2段階は `0928_GAME_SCENE_BOUNDARY_REPORT.md` を参照。

## 結論

`GameObject.h` 変更時の影響を自作 43 `.cpp` から 27 `.cpp` へ減らし、増分ビルド中央値を 66.507 秒から 43.838 秒へ 34.09% 短縮した。`Camera.h` の推移的影響も 35 `.cpp` から 10 `.cpp` へ減少した。`Game.h` の直接利用者 34 件は公開 API の互換性のため維持したが、重い実装型の解析を外し、同ヘッダー更新の中央値を 2.46% 短縮した。

## 計測対象の分類

Visual Studio の現行プロジェク `DX22_01_plane/DX22_01_plane.vcxproj` に `ClCompile` 登録された 81 `.cpp` だけをビルド対象とした。内訳は自作 73 件、プロジェク内組み込み第三者ソース 8 件 (`imgui/*.cpp` 7 件、`stb_image.cpp` 1 件) である。

次は依存解析と再コンパイル数から除外した。

- `third_party/`: インクルードまたはリンク入力。ソリューションのコンパイル対象ではない。
- `x64/`, `.vs/`: 生成物と IDE 状態。
- `提出用_20260912/`, `提出用_20260919/`: 提出用コピー。
- `packaging_work/`: 過去の梱包作業。作業開始前から未追跡だった4ディレクトリは一切変更していない。
- `tools/runtime_tests/**/before`, `baseline`: 比較用の過去スナップショット。

## 計測条件

- Visual Studio 2022 Enterprise 17.14.51
- MSBuild 17.14.51.32402
- `Debug|x64`, toolset `v143`
- MSBuild: `/m` (最大ノード数自動)
- compiler: `/MP` (`MultiProcessorCompilation=true`)
- 論理プロセッサ: 20
- PCH: プロジェクに設定なし
- 各条件 3 回、中央値を比較。各増分計測の前に変更なしビルドを実行し、その後に対象ファイル1件の更新時刻だけを更新。
- 影響の大きいヘッダー A/B: `GameObject.h`, `TableConfig.h`
- 代表 `.cpp`: `GameSaveManager.cpp`

再現コマンド:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools\measure_cpp_build.ps1 `
  -Phase before -Runs 3 `
  -Headers Game.h,GameObject.h,TableConfig.h `
  -RepresentativeSource GameSaveManager.cpp
```

修正後は `-Phase after` に変更する。スクリプト内の実コマンドは `MSBuild.exe DX22_01_plane.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /v:normal /nologo` である。クリーンは同条件で `/t:Clean` の後に `/t:Build` を実行した。

## 計測結果

| 計測対象 | 修正前 | 修正後 | 差分（秒） | 短縮率 | 再コンパイル数（前→後） |
| --- | ---: | ---: | ---: | ---: | ---: |
| クリーンビルド | 87.485 | 84.881 | 2.604 | 2.98% | 73→73 |
| 変更なしビルド | 0.706 | 0.694 | 0.012 | 1.70% | 0→0 |
| `Game.h` 更新 | 60.272 | 58.788 | 1.484 | 2.46% | 34→34 |
| 影響の大きいヘッダー A (`GameObject.h`) 更新 | 66.507 | 43.838 | 22.669 | 34.09% | 43→27 |
| 影響の大きいヘッダー B (`TableConfig.h`) 更新 | 68.950 | 66.375 | 2.575 | 3.73% | 43→43 |
| 代表的な `.cpp` (`GameSaveManager.cpp`) 更新 | 10.930 | 10.227 | 0.703 | 6.43% | 1→1 |

いずれも悪化はなかった。計測幅 (最小～最大) は次の通り。

- クリーン: 86.832～88.232 秒 → 84.562～85.189 秒
- 変更なし: 0.696～0.710 秒 → 0.682～0.709 秒
- `Game.h`: 59.890～60.672 秒 → 58.579～59.429 秒
- `GameObject.h`: 66.087～66.808 秒 → 43.312～43.852 秒
- `TableConfig.h`: 68.471～70.400 秒 → 66.025～66.801 秒
- 代表 `.cpp`: 10.711～11.172 秒 → 10.224～10.372 秒

生ログは `build_measurements/before/logs/` と `build_measurements/after/logs/` に各22ファイル、集計は各 `summary.json`、依存解析は各 `dependency_graph.json` にある。

## 主要な依存の変化

| ヘッダー | 修正前: 直接 / 推移 `.cpp` | 修正後: 直接 / 推移 `.cpp` | 根拠 |
| --- | ---: | ---: | --- |
| `GameObject.h` | 25 / 43 | 31 / 27 | 暗黙の `Game.h` 経由を廃止し、実際にメンバーを使う `.cpp` だけが明示的に include。 |
| `Camera.h` | 14 / 35 | 14 / 10 | `Game.h` から除外。`Game` は引き続き singleton への参照を保持し、ライフタイムは不変。 |
| `Game.h` | 34 / 34 | 34 / 34 | 互換性を優先し、直接 API 利用者は未変更。各利用者の解析量だけを削減。 |
| `TableConfig.h` | 21 / 43 | 20 / 43 | スカラ寸法と `SimpleMath` を使うポケット幾何を分離。寸法変更の意味上の影響範囲は正直に維持。 |

依存サイクルは修正前後とも 0 件だった。

変更後の方向は次の通り。

```text
Game.h
  -> GameWorld.h -> Component.h + GameObjectTag.h
                   (GameObject.h / TagComponent.h は GameWorld.cpp だけ)
  -> Camera* 型の前方宣言
     (Camera.h は Game.cpp と実際の表示利用側だけ)

TableConfig.h (スカラ寸法)
  <- 広いゲームルール利用側
TableGeometry.h -> TableConfig.h + SimpleMath.h + array
  <- ポケット中心座標を必要とする側だけ
```

## 実装変更

- `Game.h`, `Game.cpp`: `Camera.h` と `GameObject.h` を公開ヘッダーから除外。カメラは従来どおり参照メンバーで、コンストラクタで初期化する。
- `GameWorld.h`, `GameWorld.cpp`: `GameObject` の保有形式は `unique_ptr` のまま。デストラクタと列挙実装を `.cpp` に移し、不自然な PImpl や追加ヒープ確保なしで依存を切った。
- `GameObjectTag.h`, `TagComponent.h`: 軽量な enum をコンポーネント実装から分離。
- `Game.cpp`, `GameBossAI.cpp`, `GameMcpBridge.cpp`, `GamePresentation.cpp`, `GameProgression.cpp`, `ResultScene.cpp`, `StageBase.cpp`: `GameObject` を実際に使う実装で直接 include し、暗黙の推移依存を廃止。
- `TableConfig.h`, `TableGeometry.h`: 寸法とベクトル幾何を分離。数値、ポケット配置、2:1 コンパイル時検査は不変。
- `PocketReturnRules.h`, `GameStageEditor.cpp`, `StageLayoutEditor.cpp`, `TableFrameRenderComponent.cpp`: ポケット幾何が必要な利用側だけ `TableGeometry.h` を include。
- `BallPhysicsRules.h`: `TableConfig.h` から偶然供給されていた `<array>` を直接 include し、ヘッダーを自己完結化。
- `DX22_01_plane.vcxproj`, `.filters`: 新規ヘッダー2件を Visual Studio プロジェクに登録。
- `tools/analyze_cpp_dependencies.py`: プロジェク対象の直接／推移依存、影響 `.cpp`、SCC 循環を JSON 出力。
- `tools/measure_cpp_build.ps1`: 同等のビルド済み状態を作り、各条件の時間と実コンパイル数を記録。

## `Game` の責務とアクセス境界

作業開始時点で、`GamePresentation`、`GameSaveManager`、`GameMcpBridge`、`BalanceAutoPlayer`、`BossShotPlanner`、`RunController`、`SceneManager` への分離はすでに導入済みだった。現状は以下の所有関係である。

- `Game` はフレーム更新順、シーン遷移、各サブシステムの接続を担当する。
- ラン状態は `RunController`、シーンは `SceneManager`、表示状態は `GamePresentation`、MCP 変換は `GameMcpBridge`、自動操作は `BalanceAutoPlayer`、ボス射出計画は `BossShotPlanner` が所有する。
- セーブは `GameRunSaveSnapshot` / `GameRunRestoreRequest` の型付き境界を使う。形式と schema version は変更していない。
- 表示と外部操作は `Game` の公開 query / request を通す。今回、`Game` の内部状態への新たな直接アクセスは追加していない。

`Game` には `GameMcpBridge` と `GameDebugController` の `friend` が残る。これらは既存の明示的な特権境界で、今回のヘッダー分離だけで消すと操作 API の大幅拡大につながるため維持した。`GameWorld` は自身が所有する `GameObject` のコンポーネント格納を追加配列なしで列挙するため、`GameObject` の狭い `friend` とした。これは状態変更を外部へ開放するものではない。

## 検証

合格:

- VS2022 `Debug|x64` ビルド。既存の DirectXTK PDB `LNK4099` 警告のみで exe 生成。
- 責務分割契約 10件、セーブ契約 5件、C++ 自動プレイ回復 5件、クリア報酬 4件。
- MCP schema 31件、boss MCP 7件、shop 複数購入 2件、run-map policy 3件。MCP コマンドと状態形式は未変更。
- ランマップ 200 seed: 全ノード到達性、接続ルート、回復アクセス、セーブ往復、旧形式、破損パスが合格。
- 進行プロファイル: achievement、unlock gate、ascension、永続化、破損時 fallback が合格。
- 共有弾道予測、同期物理 substep、固定時間制御 (30/60/144 Hz と不規則フレーム) が合格。

未合格／未検証:

- `test_result_pause_contract.py`: 今回未変更の `GameEvent.h` を UTF-8 として読む既存の文字コードエラー1件と、既存の `run_endpoint_save_migrated` 期待値差1件。
- `test_debug_combat_forecast_contract.py`: 今回未変更の旧 API 期待値2件。
- `test_ball_upgrade_runtime.exe`: 現行データに対し、旧初期 HP 50 を期待する既存アサートで停止。
- 手動 GUI の戦闘→報酬→次ルートは未実施。代わりに進行、報酬、ランマップとセーブ往復の自動検証を実施。
- 計測後に `PocketReturnRules.h` のコメント文字コードだけを元の Shift-JIS へ完全復元した。トークン列と include は計測時と同一だが、復元後の再ビルドは Codex の外部実行承認上限により実行できていない。

物理、弾道予測、戦闘ルール、報酬値、セーブ schema、MCP schema、自動プレイ意思決定、乱数シード処理の実装は変更していない。そのため変更前後の100ラン比較は行っていない。

## 4 / 5 評価の根拠と残課題

4 / 5 に届く根拠は、依存の効果を「include 行数」ではなく実再コンパイル 43→27 と同条件の 34.09% 短縮で示したこと、所有権を変える PImpl を導入せず `GameWorld` の実装詳細だけを閉じたこと、型付きセーブ境界や共通操作経路を維持したことである。新たな循環はなく、影響を小さい変更単位で確認した。

5 / 5 に向けた残課題は次の通り。

- `Game.h` の直接利用 34 件は依然広い。次は表示用 read model、操作 command facade、起動／更新調停の3境界に段階分割し、シーンが `Game.h` 全体を読まないようにする。
- `GameMcpBridge` / `GameDebugController` の `Game friend` を、必要な query/command のみを持つ小さな境界へ置き換える。
- `Component.h`, `BallStatus.h`, `GameTypes.h` は依然 43件以上の推移影響を持つ。値型の安定部分と変更頻度の高いルールを分ける余地がある。
- PCH は未導入。ただし、まず自作依存を減らした上で、Windows / DirectX など低頻度変更の外部ヘッダーだけを PCH 候補として別計測すべきである。
- 失敗している3系統の旧テストを現行仕様と文字コードに合わせ、GUI 通しプレイを CI から実行できるスモークにする。
