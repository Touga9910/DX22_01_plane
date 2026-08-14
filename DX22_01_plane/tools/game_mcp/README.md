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

### 人間的なショット誤差

MCPが計算した理想照準に対し、`player_profiles.json`の
`human_error` を使って毎ショットに少量の誤差を加えます。

- `aim_radius_ratio`: 敵ボール半径に対する左右の照準誤差上限
- `power_ratio`: AIが指定したパワーに対する誤差上限

誤差は0付近が出やすい三角分布で、設定した±上限内に収まります。
既定値の照準誤差は全レベルで半径の±2%、パワー誤差は
初心者±10%、中級者±5%、上級者±2%です。実際に適用された
照準点、パワー、誤差量は `fire_shot` 応答の
`shot_plan.human_error` に含まれます。

## 動的バランス調整

既定で有効です。各戦闘の勝敗、残HP率、ショット数、敵に一度も
当たらなかったショットの割合をゲーム側で評価し、難易度レベルを
`-3`～`+3`の範囲で1段階ずつ変更します。補正は戦闘途中ではなく、
次の戦闘で生成される敵へ適用されます。正方向のレベルはHPだけを
増加させ、敵数に比例して総攻撃力が急増しないよう攻撃力は増やしません。
負方向では救済としてHPを下げ、レベル-2以下では攻撃力も下げます。

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

DDAのON/OFFとは別に、進行度20から敵攻撃力を+1、以後5進行度ごとに
+1する後半スケーリングがあります（最大+8）。これはDDA OFFの
固定条件でもランが永続しないための基礎難易度曲線です。
`get_game_state.progression_scaling`で現在の補正値を確認できます。

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

### ポケット狙い

`get_game_state.table.pockets`に6個のポケット位置、
`pocket_rules`に現在の処理とフィニッシュしきい値が公開されます。

- 自ボールが入ると最大HPの5%ダメージ（切り上げ、防御無視）を受け、中央付近へランダム復帰
- 敵が通常戦30%、中ボス戦20%、ボス戦10%以下で入るとフィニッシュ
- しきい値よりHPが多い敵はそのターンの攻撃を行わず復帰キューへ入る
- 復帰キューの敵は敵攻撃フェーズ終了時に1体ずつ固定返却エリアへ戻る

`get_game_state.shot_tactics`に、各敵のdamage/pocket比較、
推奨対象、推奨ポケット、判断理由が公開されます。
判定にはフィニッシュ可否、防げる敵攻撃力、押し出し角度、
ポケットまでの距離、自ボールの接触後軌道と落下ダメージを使います。

`fire_shot`は`shot_goal=auto`が既定値です。実行直前の状態と
指定パワーで全ポケット経路を再評価し、damageまたはpocketを
自動選択します。通常ランではautoを使います。

`shot_goal=damage` は敵中心への通常攻撃、
`shot_goal=pocket` は敵を指定ポケットへ押すための接触点を計算します。
`pocket_index=-1`で戦術スコアが最も高いポケットを自動選択します。
ポケット狙いは初期実装では`shot_type=direct`のみ対応します。

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

- `choose_destination`: `wanted_rewards`に`money`、`new_ball`、`ball_upgrade`、`hp_recovery`、`relic`の5種を欲しい順で指定。希望に合う`route_options`がなければ次順位へ自動フォールバック。`route_index`は同種ノードが複数ある場合の位置指定として任意
- MCP経路ポリシー: HP25以下で回復可能な休憩所があれば休憩へ補正。回復クールダウン中は強制しない。購入もボール削除もできないショップは、回復可能な休憩または戦闘へ補正し、理由を`route_policy`へ記録
- `heal`: 休憩所で最大HPの25%を回復（切り上げ、最大HP上限）。回復後は2戦クリアするまで再回復不可
- `upgrade_ball`: 休憩所で`deck_balls[].can_upgrade`が`true`の任意のボールを1段階強化
- `remove_ball`: ショップで15 Moneyを支払い任意のボールを削除（デッキの最小数は5個）
- `buy_relic`: ショップで`relics[].index`を指定して未所持のレリックを購入。購入直前のHPとデッキ平均attackを確認し、低HP時はEmergency Repair Kit→Guard Core、平均attackが基準未満ならPower Core→Impact Accelerator→Bank Shotの順で購入可能な対象へ自動補正

`player_profiles.json`の`relic_policy`で、低HP判定の
`low_hp_ratio`（既定0.5）、攻撃不足判定の
`minimum_average_attack`（既定2.0）、系統別の優先順位を調整できます。
`buy_relic`応答の`relic_policy`に、要求されたレリック、
実際に購入したレリック、判定値、補正理由が返ります。

`get_game_state` の`stage_choice`には、現在のHP比率、Money、
デッキ数、平均attack、強化可能ボール・購入可能レリックの有無、
各希望報酬に対応できる経路を公開します。
`choose_destination`は受け取った希望順をそのまま固定せず、
`need_signals=true`の報酬を`hp_recovery` → `relic` → `ball_upgrade` →
`new_ball` → `money`の戦略順で先頭へ並べ直します。必要のない報酬は
要求時の相対順を維持します。実際に使用した順序は応答の
`effective_wanted_rewards`で確認できます。
`stage_choice_policy`の既定判定値はHP50%、Money 20未満、
デッキ8個未満、平均attack 2.0未満です。

`relic_effects`にはレリックの現在補正を公開します。
Impact Acceleratorは自ボール×敵ボールと敵ボール×敵ボールを対象とし、
敵に与えるダメージが衝突後に+1ずつ増加します。
`current_shot_player_enemy_collisions`と
`current_shot_enemy_enemy_collisions`でショット中の対象衝突数を確認できます。
Bank Shotはプレイヤーボールが壁で反射した後、そのショット中で最初に敵へ与える
直接衝突ダメージを2倍にします。`bank_shot_ready`で発動待ちか確認できます。
Emergency Repair Kitは1ショット中にプレイヤー×敵または敵×敵の接触が合計3回以上なら、
ショット終了時にHPを1回復します。合計は`current_shot_ball_collisions`で確認できます。
- `choose_reward`の`upgrade_ball`: `deck_balls[].can_upgrade`が`true`の任意のボールを強化
- `deck_rule`: 現在数、最小数、削除費用、現在削除可能かを公開

書き込みツールはゲーム側でもシーン、ID、HP、Money、方向、パワーを検証します。
任意ファイルの編集や任意コード実行は公開していません。
