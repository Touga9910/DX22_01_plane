# DX22 Game MCP

起動中のゲーム状態をChatGPT/Codexへ公開し、許可されたゲーム操作だけを
MCPツールとして実行します。ゲームとMCPサーバーは
`runtime/game_mcp` のJSONブリッジで接続されます。

実装の責務、採用理由、代替技術との比較、既知の制約、方式を変更する判断基準は
[`ARCHITECTURE_AND_DECISIONS.md`](ARCHITECTURE_AND_DECISIONS.md) にまとめています。

## 初回セットアップ

```bat
tools\game_mcp\setup_game_mcp.cmd
```

## ローカル起動

1. Debug版ゲームをプロジェクトディレクトリから起動します。
2. 別のターミナルでMCPサーバーを起動します。

```bat
tools\game_mcp\start_game_mcp.cmd
```

MCPエンドポイントは `http://127.0.0.1:8765/mcp` です。

## プレイヤーレベル

既定値は中級者です。起動時に次のいずれかを指定できます。

```bat
tools\game_mcp\start_game_mcp.cmd --player-level beginner
tools\game_mcp\start_game_mcp.cmd --player-level intermediate
tools\game_mcp\start_game_mcp.cmd --player-level advanced
```

環境変数を使う場合は、サーバーを起動するコマンドプロンプトで設定します。

```bat
set GAME_MCP_PLAYER_LEVEL=advanced
tools\game_mcp\start_game_mcp.cmd
```

接続中は、ユーザーが「初心者に変更して」などと指示すると
`set_player_level` ツールで切り替えられます。この変更は実行中の
MCPサーバーだけに適用され、再起動時は起動オプションまたは環境変数へ戻ります。

レベル別の具体的な行動指示は
`tools/game_mcp/player_profiles.json` の `levels` にあります。
指示文、壁反射の許可、推奨パワー範囲を調整する場合はこのファイルを編集し、
MCPサーバーを再起動してください。

## 動的バランス調整

既定で有効です。各戦闘の勝敗、残HP率、ショット数、敵に一度も
当たらなかったショットの割合をゲーム側で評価し、難易度レベルを
`-3`～`+3`の範囲で1段階ずつ変更します。補正は戦闘途中ではなく、
次の戦闘で生成される敵のHPと攻撃力へ適用されます。

現在値は`get_game_state`の`dynamic_balance`で確認できます。

- `level`: 現在の難易度レベル
- `next_enemy_modifier`: 次戦の敵HP・攻撃力への補正
- `current_stage_metrics`: 現在の戦闘で収集中の指標
- `last_evaluation`: 直近の判定結果と理由

接続中は`set_dynamic_balance`で有効・無効、レベルのリセット、
開始レベルの直接指定ができます。例えば、自動調整を有効にして
基準レベルへ戻す場合は`enabled=true, reset_level=true`を指定します。
実行中の戦闘の敵は変わらず、次に生成される敵から反映されます。

判定しきい値と補正量は
`assets/data/dynamic_balance.json`で設定します。変更後はゲームを
再起動してください。MCPの行動指示ではなく、ゲーム本体の設定なので、
動的調整のルールを変更する場合に編集する場所はこのJSONです。

## ステージ配置のバランス調整

`set_next_stage_layout`で、次の戦闘に限り敵の種類・数・配置を
上書きできます。現在の戦闘は途中で変更されません。

1. `get_game_state`の`stage_layout_control.allowed_enemy_ids`で
   使用可能な敵IDを確認します。
2. `enemies`へ`enemy_id`、`x`、`z`を持つ配置を1～12体指定します。
3. 通常どおり次の戦闘へ進むと、予約した構成が一度だけ使用されます。

盤面外、プレイヤー初期位置との重なり、敵同士の重なり、不明な敵IDは
ゲーム側で拒否されます。予約内容は
`stage_layout_control.queued_override`、適用中の内容は
`stage_layout_control.current_override`で確認できます。
予約を取り消す場合は`clear_next_stage_layout`を使用します。

## ショットの照準

`fire_shot` は任意の方向ではなく、必ず生存中の敵個体に付与された
`target_id`を受け取ります。`enemy_id`は敵の種類IDなので、
同じ種類の敵が複数いる場合は重複します。照準には必ず
`get_game_state`の`enemies[].target_id`を使用します。
旧接続のツール定義が`target_enemy_id`を要求する場合は、
その引数へ`enemy_id`ではなく同じ`target_id`の値を渡します。
サーバー側でプレイヤーと対象の
現在位置から方向を計算するため、何もない方向へのショットはできません。
旧ゲーム実行ファイルが`target_id`をまだ公開していない場合も、
MCPサーバーが敵の列挙順から同じ形式のIDを補完します。

- `shot_type=direct`: 対象へ直接照準します。
- `shot_type=bank`: 壁で1回反射して対象を狙います。
- `wall_index=-1`: `table.walls` から有効な最短反射経路を自動選択します。
- `wall_index=0`以上: `get_game_state` の `table.walls` にある特定の壁を使います。

初心者は直射のみ、中級者と上級者は1回反射を使用できます。
壁反射は壁の端やポケット開口部を避けて計算されます。

## ローカル検証

MCP InspectorでStreamable HTTPを選び、上記URLへ接続します。

```bat
npx @modelcontextprotocol/inspector@latest
```

付属のスモークテストでも初期化とツール一覧を確認できます。

```bat
tools\game_mcp\.venv\Scripts\python.exe tools\game_mcp\smoke_test.py
```

ゲーム状態まで確認する場合は、ゲーム起動中に次を実行します。

```bat
tools\game_mcp\.venv\Scripts\python.exe tools\game_mcp\smoke_test.py --read-game-state
```

## ChatGPTへ接続

ChatGPTからlocalhostへ直接接続することはできないため、OpenAIの
Secure MCP Tunnelを利用します。これは受信ポートを公開せず、
ローカルのMCPサーバーへOpenAI側から安全に接続するための仕組みです。

1. OpenAI PlatformのTunnel settingsでトンネルを作成します。
2. `tunnel-client` とruntime API keyを取得します。
3. コマンドプロンプトでキーとトンネルIDを、その実行中だけ設定します。

```bat
set CONTROL_PLANE_API_KEY=取得したruntime API key
set GAME_MCP_TUNNEL_ID=tunnel_xxxxxxxxx
tools\game_mcp\init_secure_tunnel.cmd
```

ゲームとMCPサーバーを起動した状態で、トンネルを起動します。

```bat
tools\game_mcp\start_secure_tunnel.cmd
```

ChatGPTのDeveloper modeを有効にし、Settings → Pluginsの追加画面で
ConnectionにTunnelを選択して、作成したトンネルを接続してください。
トンネル権限とChatGPT Developer modeの権限は別に必要です。

runtime API keyやトンネルIDはリポジトリへ保存しないでください。

## 公開ツール

- `get_game_state`
- `set_player_level`
- `set_dynamic_balance`
- `set_next_stage_layout`
- `clear_next_stage_layout`
- `start_new_run`
- `choose_destination`
- `select_ball`
- `fire_shot`
- `heal`
- `upgrade_ball`
- `remove_ball`
- `buy_relic`
- `continue_to_battle`
- `choose_reward`
- `continue_after_reward`

ボール操作の条件は`get_game_state`で確認できます。

- `upgrade_ball`: 休憩所で`deck_balls[].can_upgrade`が`true`の任意のボールを1段階強化
- `remove_ball`: ショップで15 Moneyを支払い任意のボールを削除（デッキの最小数は5個）
- `buy_relic`: ショップで`relics[].index`を指定して未所持のレリックを購入

`relic_effects`にはレリックの現在補正を公開します。
Impact Acceleratorは自ボール×敵ボールと敵ボール×敵ボールを対象とし、
敵に与えるダメージが衝突後に+1ずつ増加します。
`current_shot_player_enemy_collisions`と
`current_shot_enemy_enemy_collisions`でショット中の対象衝突数を確認できます。
- `choose_reward`の`upgrade_ball`: `deck_balls[].can_upgrade`が`true`の任意のボールを強化
- `deck_rule`: 現在数、最小数、削除費用、現在削除可能かを公開

書き込みツールはゲーム側でもシーン、ID、HP、Money、方向、パワーを検証します。
任意ファイルの編集や任意コード実行は公開していません。
