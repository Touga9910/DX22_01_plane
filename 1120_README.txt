作成日：2026年11月20日
CAROMINO — ビリヤード × ローグライク

作品の狙い
提示された球とレリックで戦い方を組み立て、自分のショットでそのビルドを成立させるゲームです。
選択・狙い・連鎖を軸に、衝突、壁反射、配置、ポケットを読み、一つの球から複数の敵へ攻撃をつなげます。

開発・実行環境
Windows / Direct3D 11 / C++20 / Visual Studio 2022（C++によるデスクトップ開発、v143、Windows SDK）。
対応確認の中心はx64です。Win32構成は存在しますが、今回の検証対象ではありません。
third_partyにDirectXTKとAssimpのヘッダー、ライブラリ、実行用DLLを同梱しています。
ImGui、nlohmann/json等のライセンスと出典は各同梱フォルダーを参照してください。

ビルド
このファイルがあるフォルダーでDX22_01_plane.slnを開き、x64 / Releaseをビルドします。
コマンドラインの場合：
  & .\tools\build.ps1 -Configuration Release
  & .\tools\build.ps1 -Configuration Debug
標準スクリプトの出力はx64/VerifiedReleaseまたはx64/VerifiedDebugです。
VSのインストール先はvswhereで検出します。ビルドログはlogs/buildに日時別で保存します。
クリーンビルドは-Rebuild、コンパイラの並列数は-CompileProcesses 2等で指定できます。

既知のローカル環境問題と診断経路
このPCでは、コンパイラのPDBマネージャー不整合C1902を確認しています。
通常設定でこのエラーが出る場合は、VS/MSVCの環境整合を確認してください。
検証用の回避経路：
  & .\tools\build.ps1 -Configuration Release -EmbeddedDebugInfo -ToolsetVersion 14.44.35207
  & .\tools\build.ps1 -Configuration Debug -EmbeddedDebugInfo -ToolsetVersion 14.44.35207
上記のバージョンは、このPCで確認したインストール済みMSVCです。別PCでは利用可能なバージョンを指定するか省略してください。
回避経路はコンパイラのデバッグ情報を/Z7にし、リンクPDB出力を無効化します。最適化とゲームルールは変えません。
通常設定の成功と診断設定の成功は、作業記録で区別しています。
診断版の出力はx64/VerifiedReleaseZ7またはx64/VerifiedDebugZ7です。
このPCではPowerShell 7.6.9からスクリプトを直接実行して確認しました。Windows PowerShell 5.1の-File起動は既存ポリシーで拒否されたため、成功扱いしていません。ポリシーを変更せず、VSからのビルドも利用できます。

起動
重要：assetsとshaderを相対パスで読み込むため、実行時のカレントフォルダーをDX22_01_planeにしてください。
このファイルがあるフォルダーからPowerShellで：
  Set-Location DX22_01_plane
  ..\x64\VerifiedRelease\DX22_01_plane.exe
診断版はVerifiedReleaseをVerifiedReleaseZ7に読み替えます。
Assimpの対応DLLはビルド後にEXEと同じ場所へコピーされます。

基本操作
タイトル：STARTで開始、CONTINUEで中断ランを再開、SETTINGSで設定、QUITで終了。
メニュー：マウスのほか、上下キーまたはW/Sで選択し、Enterで決定できます。
戦闘：提示された球をクリック、または数字キー1～7で使用球を選びます。
照準：マウスで方向を合わせ、左ボタンを押したまま手前にドラッグして威力を決め、離すと発射します。
右クリックで照準や球選択をやり直せます。球が止まると敵の行動を経て次のショットへ進みます。
報酬・ショップ：左クリックで内容を確認して取得・購入、右クリックで球の詳細を確認します。
休憩所：回復か球強化を1回選び、次へ進みます。利用可能な特典がある間は未取得のまま進めません。
Esc：ポーズ・設定。F11：全画面切替。F1：プレイガイド。F2：分析画面。F6：保存可能な画面で手動保存。
WASDによる発射後の加速はデバッグ戦闘だけの検証操作です。通常プレイでは無効です。

セーブと設定
ランはsaves/run_save.json、設定はsaves/settings.jsonに保存します。実行時のカレントフォルダーが保存先の基準です。
安全な画面遷移時にチェックポイントを保存します。戦闘中の中断再開は戦闘開始時点の再開で、ショット途中の物理状態の保存ではありません。
手動保存はマップ・休憩所・ショップで使用できます。報酬が未確定の場合は確定後に次のルートへ進んで保存してください。
自動プレイ・バランス検証・デバッグ戦闘は通常セーブと区別します。
既存セーブを消して検証せず、検証CLIの隔離フォルダーを利用してください。

テストと技術説明
Python 3.11以降を想定。以下のpythonは利用可能なPython本体のパスでも指定できます。
  python -m unittest discover -s DX22_01_plane/tools -p "test_*.py"
  .\DX22_01_plane\tools\game_mcp\.venv\Scripts\python.exe -m unittest discover -s DX22_01_plane/tools/game_mcp -p "test_*.py"
  python tools/run_native_tests.py --label native_01 --embedded-debug-info --toolset-version 14.44.35207 --discard-intermediates
  python DX22_01_plane/tools/validate_portfolio_runtime.py --exe x64/VerifiedDebugZ7/DX22_01_plane.exe --label shots_01 --mode shots
  python DX22_01_plane/tools/verify_autoplay.py --exe x64/VerifiedReleaseZ7/DX22_01_plane.exe --label release_01 --trials 1 --cases maximum_runs --conditions visible
各CLIの--helpで選択テスト・出力ラベル等を確認できます。ネイティブテストでは外部Cドライブ直下の依存配置を必要としないランナーを用意しています。
MCPテストはmcp等の依存が必要です。初回の環境作成はDX22_01_plane/tools/game_mcp/README.mdを参照してください。既存venvは別PCへそのままコピーする環境ではありません。
ビルドログと検証キャプチャーはlogs配下に保存します。既存の比較ログを上書きしないよう、ラベルは新規の名前にします。
進行試験は3GiB以上の空き容量を確認し、隔離したアセットの複製分の容量も使用します。ReleaseはDebug専用の非表示起動条件を使わず、上記のvisible条件を指定してください。
技術構成と検証範囲：docs/1120_技術概要と検証手順.txt
今回の作業記録：docs/1120_就職作品改善の作業計画と検証記録.txt
