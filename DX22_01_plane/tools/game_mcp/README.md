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

## ビルド別収集方針

操作精度を表す`player_level`とは別に、取得・強化・射撃戦術を表す
`build_profile`を指定できます。用意されている方針は`standard`、
`heavy`、`pierce`、`bounce`、`anchor`です。

```bat
tools\game_mcp\start_game_mcp.cmd --player-level intermediate --build-profile pierce
```

接続後はタイトルまたはリザルト画面で`set_build_profile`を使って
切り替えられます。ラン途中の切り替えは比較条件が混ざるため拒否されます。
優先する新規ボール・強化対象・報酬・レリック・反射・ポケット制御は
`tools/game_mcp/build_profiles.json`にあり、変更時はMCPサーバーを
再起動してください。設定内容はSHA-256とともにランログへ残ります。

固定条件の自動収集は次のように実行します。

```bat
tools\game_mcp\.venv\Scripts\python.exe tools\game_mcp\collect_fixed_balance_runs.py --profile intermediate --build-profile pierce --runs 10
```

調整前後を同じ乱数条件で比較する場合は、`--run-seed` を繰り返し指定し、
`--validation-variant` でDDAなどの検証条件を固定します。

```bat
tools\game_mcp\.venv\Scripts\python.exe tools\game_mcp\collect_fixed_balance_runs.py --profile intermediate --build-profile standard --runs 2 --run-seed 20260807 --run-seed 20260817 --validation-variant dda_off
```

シードは指定順に使われ、ラン数のほうが多い場合は循環します。
`start_new_run` からも任意の `run_seed` と `validation_variant` を渡せます。
固定シード時はステージ・経路・ポケット配置・山札シャッフルと、
MCPの人間的ショット誤差が再現されます。

収集AIはビルド方針に従って経路、報酬、新規ボール、強化、レリック、
射撃種別を選び、判断理由もログへ送ります。MCPサーバーと収集AIで設定
ハッシュが異なる場合は、比較不能なログを作らず開始前に停止します。

### 人間的なショット誤差

MCPが計算した理想照準に対し、`player_profiles.json`の
`human_error` を使って毎ショットに少量の誤差を加えます。

- `aim_radius_ratio`: 敵ボール半径に対する左右の照準誤差上限
- `power_ratio`: 自動計算またはmanual指定したパワーに対する誤差上限

誤差は0付近が出やすい三角分布で、設定した±上限内に収まります。
既定値の照準誤差は全レベルで半径の±2%、パワー誤差は
初心者±10%、中級者±5%、上級者±2%です。実際に適用された
照準点、パワー、誤差量は `fire_shot` 応答の
`shot_plan.human_error` に含まれます。

## 動的バランス調整

既定で有効です。各戦闘の勝敗、残HP率、ショット数、敵に一度も
当たらなかったショットの割合をゲーム側で評価し、救済レベルを
`-3`～`0`の範囲で1段階ずつ変更します。補正は戦闘途中ではなく、
次の戦闘で生成される敵へ適用されます。好成績時は救済を基準値0へ
戻すだけで、基準より敵を強くしません。負方向ではHPを下げ、
レベル-2以下では攻撃力も下げます。

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

DDAのON/OFFとは別に、進行度10から敵HPを+1、以後5進行度ごとに
+1して最大+4、進行度20から敵攻撃力を+1、以後5進行度ごとに
+1して最大+8とする後半スケーリングがあります。これはDDA OFFの
固定条件でも後半ビルドへ基礎難易度が追従するための曲線です。
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

パワーは`power_mode=auto`が既定です。現在のボールの摩擦、質量、貫通回数・減速、
敵配置、レリックを使って、レベル別の範囲内で複数パワーを比較します。
通常プレイでは`power`を省略します。ボール情報がない旧状態のみ距離方式へ戻ります。
固定パワーを検証するときだけ`power_mode=manual`と`power=1～8`を
同時に指定します。実際の選択理由と補正値は応答およびログの
`shot_plan.power_policy`で確認できます。

- `shot_type=auto`: 既定値。直射・連鎖接触・1回反射を評価して選びます。
- `shot_type=direct`: 直射に限定し、中心と特性に合った接触点を比較します。
- `shot_type=bank`: 壁で1回反射して対象を狙います。
- `wall_index=-1`: `table.walls` から有効な反射経路をビルド別スコアで比較します。
- `wall_index=0`以上: `get_game_state` の `table.walls` にある特定の壁を使います。

初心者は直射のみ、中級者と上級者は1回反射を使用できます。
壁反射は壁の端やポケット開口部を避けて計算されます。

### ポケット狙い

`get_game_state.table.pockets`に6個のポケット位置、
`pocket_rules`に現在の処理とフィニッシュしきい値が公開されます。

- 自ボールが入ると最大HPの4%ダメージ（切り上げ、防御無視）を受け、中央付近へランダム復帰
- 敵が通常戦30%、中ボス戦20%、ボス戦10%以下で入るとフィニッシュ
- しきい値よりHPが多い敵はそのターンの攻撃を行わず復帰キューへ入る
- 復帰キューの敵は敵攻撃フェーズ終了時に1体ずつ固定返却エリアへ戻る

`get_game_state.shot_tactics`に、各敵のdamage/pocket比較、
推奨対象、推奨ポケット、判断理由が公開されます。
判定にはフィニッシュ可否、防げる敵攻撃力、押し出し角度、
ポケットまでの距離、自ボールの接触後軌道と落下ダメージを使います。

`fire_shot`は`shot_goal=auto`が既定値です。実行直前の状態と
ボール特性を使って接触点とパワー、成立するポケット経路を再評価し、damageまたはpocketを
自動選択します。通常ランではautoを使います。

`shot_goal=damage` はダメージ狙いに限定した接触点の比較、
`shot_goal=pocket` は敵を指定ポケットへ押すための接触点を計算します。
`pocket_index=-1`で戦術スコアが最も高いポケットを自動選択します。
ポケット狙いの経路は直射のみです。`shot_type=auto`でも候補に含まれます。

### ビルド別ショット評価（2026-09-03）

`build_shot_evaluator.py`が、ボール・標的・接触点・パワーを組として評価します。
`build_profiles.json`の各`shot_evaluation`が重みです。ボールの種類そのものへの
固定加点ではなく、配置から推定した利益と被害に重みを掛けます。

| ビルド | 重視する評価 |
|---|---|
| standard | 撃破、攻撃を防ぐ量、安定した接触、精密照準器が有効なパワー |
| heavy | 質量・押し出し倍率から届く敵同士の衝突と、そのダメージ |
| pierce | 前方の敵への追加命中数、貫通回数と速度維持率による到達性 |
| bounce | 壁反射による実ダメージ増加、正面ガード回避、残りの接触見込み |
| anchor | 防御適用後の残る敵攻撃、ポケット制御、接触位置での停止と落下リスク |

操作の流れ：

1. `get_game_state.build_shot_choices.recommended`で候補ボールと攻撃を比較。
2. `select_ball`後に`get_game_state`を再取得。
3. 現在のボール用`shot_tactics`を確認して、`fire_shot`を
   `shot_type=auto`, `power_mode=auto`, `shot_goal=auto`で実行。
4. `shot_plan.build_evaluation`の評価内訳・予測命中・停止位置を実測ログと比較。

自動収集も持ち替え後に再取得し、同じ評価と自動パワーで発射します。
HP不足ならビルド特性の加点より致死的な被害を強く避けます。
衝突ダメージは敵HPで上限を設け、防御しても最低1ダメージのルールを反映します。

これは厳密な物理シミュレーションではありません。敵の押し出し連鎖は1衝突、
残りの自球接触は1回までを概算し、後者は動く敵に対する不確実性を割り引きます。
発射前の反射は1回、停止位置の概算は接触後1回の反射までです。
敵の質量等が旧ゲームから届かない場合だけ、ローカルenemy_data.jsonから補完します。
新しいゲーム本体は敵の質量・摩擦・反発係数と選択中のボールを直接公開します。

新しい`shot_type=auto`が接続側に見えない場合はMCP接続のツール情報を更新してください。

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
- `set_build_profile`
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
- `choose_relic`
- `continue_to_battle`
- `choose_reward`
- `continue_after_reward`

ボール操作の条件は`get_game_state`で確認できます。

- `choose_destination`: `wanted_rewards`に`money`、`new_ball`、`ball_upgrade`、`hp_recovery`、`relic`を欲しい順で指定。省略時は既定順を使用し、一部だけ指定した場合は不足項目を自動補完。希望に合う`route_options`がなければ次順位へ自動フォールバック。マップがある場合は`route_index`で経路を明示するとその選択を実行（HP・報酬方針で変更しない）。省略時のみ上記の自動選択を使用
- `get_game_state.run_progress`: `phase`、`area_progress`、15エリアの`area_goal`、総戦闘数、中ボス戦績、最終ボス到達・撃破状態を公開
- 通常ルートの候補1枠は通常戦闘63.33%、中ボス12.67%、ショップ12%、休憩所12%（戦闘内の比率は5:1）。15エリア後は保証休憩を経て`final_boss`一択になり、撃破後は報酬選択を行わずResultへ移行
- MCP経路ポリシー: HP25以下で回復可能な休憩所があれば休憩へ補正。購入もボール削除もできないショップは、回復可能な休憩または戦闘へ補正し、理由を`route_policy`へ記録
- `heal`: 休憩所で最大HPの25%を回復（切り上げ、最大HP上限）。HPが減っていれば、訪れた各休憩所で回復可能
- `upgrade_ball`: 休憩所で`deck_balls[].can_upgrade`が`true`の任意のボールを1段階強化
- `remove_ball`: ショップで15 Moneyを支払い任意のボールを削除（デッキの最小数は5個）
- `buy_relic`: ショップ入店時に抽選された`relics[].shop_offered=true`の3候補から1つを購入。1回の入店で購入できるレリックは1つまで。購入直前のHPとデッキ平均attackを確認し、購入可能な候補内で低HP時は回復・防御系、攻撃不足時は攻撃系を優先
- `choose_relic`: 中ボス撃破後、`relics[].midboss_offered=true`の3候補から1つを無料獲得。この選択を終えてから通常の`choose_reward`を行う

`relics[]`は`rarity`、`midboss_weight`、`shop_weight`、`price`を公開します。現在は全レリックが同価格・同ウェイトですが、後から抽選率とショップ価格を個別に調整できます。

`player_profiles.json`の`relic_policy`で、低HP判定の
`low_hp_ratio`（既定0.5）、攻撃不足判定の
`minimum_average_attack`（既定2.0）、系統別の優先順位を調整できます。
`buy_relic`応答の`relic_policy`に、要求されたレリック、
実際に購入したレリック、判定値、補正理由が返ります。

`get_game_state` の`stage_choice`には、現在のHP比率、Money、
デッキ数、平均attack、強化可能ボール・購入可能レリックの有無、
各希望報酬に対応できる経路を公開します。
`route_index`を省略した`choose_destination`は受け取った希望順をそのまま固定せず、
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


### 2026-09-03 ルートマップ

`get_game_state.run_map`はラン開始時に固定生成したマップです。`nodes`の`node_id`、`destination`、`next_node_ids`、`visited`、`active`、`reachable`で通った道と今後の経路を確認できます。`selectable=true`は今すぐ入れるノードだけです。操作には現在の`route_options`の`node_id`と照合した`route_index`を使用します。同じ通常戦闘でも接続先が違うため、別ノードへ置き換えません。

通常15エリア → ボス前休憩（自動入場）→ 最終ボス。4・8・12エリアの中央休憩所には直前のどのレーンからも進めます。MCPの明示経路はそのまま実行し、省略時の自動報酬選択は従来どおりです。戦闘の敵配置はノード入場時に従来のステージ選択処理で決まります。旧形式セーブは再開地点以降のマップを生成します。

### 2026-09-04 Armorボス戦の共通評価

Armorボス戦はC++自動操作と同じ `boss_shared_ccd_v1` を使います。通常戦・中ボス戦の幾何学的な評価は従来どおりです。

1. 停止・照準待ちに `get_game_state` の `boss_shot_choices`、または `evaluate_boss_shots()` を取得します。
2. `recommended` または `choices` から選びます。`offer_choices` は候補球ごとの最良案です。直接／固定ダメージ、Armor減少、Break開始、停止位置、採点内訳を比較できます。
3. 同じ応答の `candidate_id` と `state_key` を `fire_boss_shot` に渡します。球選択と発射が一度に実行され、照準誤差は加えません。

盤面や候補球が変われば再取得してください。古いキーは拒否されます。中立球押しや位置取りも候補になるため、通常戦の「生存敵への接触経路が必須」という制約はArmorボス戦には適用しません。現在の1ショットは実物理と共通ですが、終了後の再配置・次ショット・敵ターン被害は完全な先読みではありません。

同じ状態の評価はキャッシュします。異なる性能の候補球ごとに最大128案を計算するため、Debug版では応答に時間がかかります。人間操作の毎フレームには評価しません。

最新ゲームとMCPサーバーを起動し直して再接続すると専用ツールが公開されます。旧ツール一覧のクライアント向けに `fire_shot` も対応しました。選択中の球を保ち、ボスの `target_id` 指定時は中立球・位置取りも含めたボス攻略候補を比較します。中立球指定はその対象の候補に限定します。手動条件に合う公開候補がなければエラーです。

比較記録は `../runtime_tests/boss_ai/final_verification.json`、再現用は `../compare_boss_builds.py` です。固定配置・レリックなしの比較であり、通しランの勝率を示すものではありません。

### 2026-09-04 ステージエディターとAI配置提案

タイトルのデバッグモード → 「ステージエディター」で、敵の種類を選び、盤面へのクリックで追加・ドラッグで移動できます。複製・削除・元に戻す・やり直し・グリッド吸着・試遊に対応します。通常難易度は現在の抽選範囲である1～3です。

MCPサーバーとゲームを起動し直し、再接続すると以下のツールが公開されます。

1. `open_stage_editor()`：タイトル画面から編集画面を開く。
2. `get_stage_editor()`：draft、revision、敵カタログ、盤面・ポケット寸法、配置評価を取得する。
3. `validate_stage_layout(layout)`：候補をゲーム側の共通検証に通す。
4. `propose_stage_layout(layout, expected_revision)`：最新revisionを指定し、黄色い輪で案を表示する。

layoutは `{ "id":"custom_1", "stage_type":"normal", "difficulty":1, "par":4, "enemies":[{"enemy_id":"enemy_normal", "x":-30, "z":12}, {"enemy_id":"enemy_normal", "x":30, "z":12}] }` の形式です。敵は1～32体、ボス種別にはArmor球がちょうど1体必要です。盤面外・壁への接触・自球開始位置や敵同士の重なり・ポケットへの近接を拒否します。通常ランの途中でエディターを開くことはできません。

AI案は「AI案を採用」で下書きに取り込み、「この配置で試遊」または保存を選びます。提案だけでは下書きとファイルは変化しません。人が編集した後の古い提案は採用できないため、最新revisionで提案し直してください。`nearby_collision_pairs`（敵表面間が8単位以内）と`pierce_aligned_pairs`（開始位置から同方向に並ぶ敵ペア）は幾何的な目安で、勝率や実際の衝突を保証する評価ではありません。アンカーを含むビルドの有効性はデバッグデッキを使って試遊します。

保存先は `assets/data/stage_01.json`。同名なら上書き、別名なら追加し、直前のファイルを `.editor.bak` に残します。外部で元ファイルが変わった場合は保存を拒否します。エディター保存したステージには `preserveLayout:true` を付け、敵が4体以上でも自動整列しません。通常ランでは既存の難易度補正が引き続き適用され、試遊ではデバッグ条件を使用します。デバッグ専用のHP・個別性能・デッキはステージJSONには保存されません。

既存の `set_next_stage_layout` は次の1戦を予約する別機能です。ファイル保存やエディター案の採用は行いません。
