# Releaseビルド環境診断とローカル照準性能計測

実施日: 2026-11-16

## 1. ビルド環境の診断結果

Visual Studio Enterprise 2022 17.14.40（MSBuild 17.14.51）には、次の2つのMSVCが共存している。

| 項目 | 14.38ツールセット | 14.44ツールセット |
|---|---:|---:|
| `cl.exe` | 19.38.33145.0 | 19.44.35228.0 |
| `link.exe` | 14.38.33145.0 | 14.44.35228.0 |
| `mspdb140.dll` | 14.38.33145.0 | 14.44.35228.0 |
| `mspdbcore.dll` | 14.38.33145.0 | 14.44.35228.0 |
| `mspdbsrv.exe` | 14.38.33145.0 | 14.44.35228.0 |

`Common7/IDE`側のPDB関連ファイルは14.44.35215.0。既定ファイルは次の状態だった。

- `Microsoft.VCToolsVersion.default.txt`: 14.44.35207
- `Microsoft.VCToolsVersion.v143.default.txt`: 14.38.33130
- プロジェクトの`PlatformToolset`: v143

通常のDeveloper Command Promptでは14.38が選ばれる。`-vcvars_ver=14.44`と`VCToolsVersion=14.44.35207`を同時指定すると、`cl.exe`、`link.exe`、`mspdbsrv.exe`の検索先は14.44で揃う。それでもCodexの分離実行環境では`/Zi`時だけC1902が再現した。

一方、`/Z7`でコンパイラPDBサーバーを使用しない場合はRelease x64のコンパイル・リンクが成功した。ユーザー側のVisual Studio通常ビルドも成功しているため、ゲームコードやライブラリのリンク不良ではなく、Codex側プロセスから共有PDBサーバーへ接続する経路または既起動サーバーとの版不一致が原因と判断する。

Visual Studioの修復、再インストール、DLLの差し替えは実施していない。

### Codex環境用の安全なRelease確認

`tools/release_without_compiler_pdb.targets`を`ForceImportAfterCppTargets`で読み込み、コンパイラを`/Z7`、リンカーのPDB生成を無効にする。最適化、ゲームコード、バランス値は変わらない。

```powershell
$targets = (Resolve-Path tools/release_without_compiler_pdb.targets).Path
$command = 'call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44 && "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" DX22_01_plane.sln /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207 /p:IntDir=x64\ReleaseCodexZ7b\ /p:OutDir=..\x64\ReleaseCodexZ7b\ /p:ForceImportAfterCppTargets="' + $targets + '" /m'
cmd.exe /d /s /c $command
```

通常のVisual Studioビルドが成功している環境では、この回避設定を恒久的にプロジェクトへ追加する必要はない。

## 2. MCP非依存の照準性能ログ

既存の`GameRuntimeState`計測を利用し、照準状態が300フレーム継続した時点でJSONを1回出力する。MCP接続は使用しない。

対象条件は生存敵数6体、10体、12体。出力先はゲームプロセスの作業ディレクトリを基準にした次の場所。

```text
logs/performance/aiming_YYYYMMDD_HHMMSS_enemiesN.json
```

同名ファイルが存在する場合は`_1`以降の連番を付けるため、既存ログを上書きしない。

### 取得項目

- 平均、P95、P99、最大フレーム時間と平均FPS
- 平均Update時間
- 予測線更新の平均・最大時間と回数
- WorldKey計算の平均・最大時間と回数
- キャッシュ再利用回数、再計算回数、再利用率
- 最大パワー予測キャッシュの再利用回数
- 予測時のコピー球数
- 照準期間中の物理固定Tick数
- CCD反復上限到達回数

### 計測手順

1. Release x64を起動する。
2. 設定の「ショット中フレーム時間計測」を有効にする。
3. デバッグの敵数検証を有効にし、敵数6体を選んで戦闘を開始する。
4. カードを選択し、照準状態のまま300フレーム以上待つ。
5. コンソールの`[Performance] Aiming log written:`とJSON生成を確認する。
6. 10体、12体でも同じ操作を行う。

12体のLine配置は盤面幅に収めるため二列に分ける。6/8/10体のLine配置は従来どおり一列のまま。

## 3. 今回の検証結果

### 成功

- Release x64: `/Z7`診断ターゲット使用でビルド成功
- Python: 159件すべて成功
- 敵検証: 6/8/10/12体、Dense/Line/Random、安全配置、固定seedを確認
- 固定Tickテスト: 30/60/144Hz、不規則フレーム、stall、reset、settlingを確認
- 共通予測テスト: 6/10/12体、摩擦、パワー、貫通・屈折、Anchor lock、盤面、ポケット、レリック規則を確認

共通予測テストで取得した軽量予測単体の平均値:

| 敵数 | 平均予測時間 |
|---:|---:|
| 6 | 0.049663 ms |
| 10 | 0.055552 ms |
| 12 | 0.063978 ms |

### 未検証

- 実ゲーム描画を含む平均/P95/P99フレーム時間
- 実ゲームでのWorldKey、予測更新、キャッシュ再利用率
- 実ゲームでの固定Tick数、CCD上限到達回数
- 6/10/12体それぞれのJSON実出力

これらはGPU描画と人間の照準操作を含むため、上記手順で実機取得する。軽量予測単体の値を実ゲームのフレーム時間として扱わないこと。
