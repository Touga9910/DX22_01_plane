# 生成AIを使った自動バランス調整

この機能はOpenAI Responses APIの生成AIを使用します。
ローカルのルールだけで調整値を決定するものではありません。

## 事前設定

OpenAI PlatformでAPIキーを発行し、PowerShellの現在のセッションへ設定します。
APIキーはJSONやソースコードへ保存しないでください。

```powershell
$env:OPENAI_API_KEY = "取得したAPIキー"
```

使用モデルなどは
`assets/data/balance_ai_openai.json`で変更できます。
初期モデルは`gpt-5.6-sol`です。

## 1. AI調整案を生成

ゲームの実行フォルダー（`assets`と`logs`がある場所）で実行します。

```powershell
tools\adjust_balance.cmd
```

処理内容：

1. `logs/balance/run_*.json`をローカルで集計
2. 最低サンプル数などの安全条件を確認
3. 集計した特徴量をOpenAI Responses APIへ送信
4. 生成AIがJSON Schema準拠の調整案を返す
5. ローカルで対象・方向・現在値・変更幅を再検証
6. `logs/balance/balance_adjustment_plan.json`へ保存

元のステータスはまだ変更されません。

## 2. 確認後に適用

```powershell
tools\adjust_balance.cmd --apply
```

適用前の`enemy_data.json`は
`logs/balance/backups`へ日時付きで保存されます。

## 安全条件

- ステージごとに30件以上の完了ログが必要
- ローカル評価を通過した敵だけをAIへ提示
- AIが変更できるのは敵の`maxHp`と`attack`だけ
- 1回の変更量は最大±1
- 難易度の変更方向とログ評価が一致しない案は拒否
- 同じ敵を一度に複数変更する案は拒否
- AIが参照したステージIDも検証
- `--apply`なしではステータスを変更しない
- 適用前に必ずバックアップを作成
- APIキーがない、通信失敗、形式不正の場合は何も変更しない

調整基準は`assets/data/balance_targets.json`、
安全制限は`assets/data/balance_adjustment_policy.json`、
AI設定は`assets/data/balance_ai_openai.json`で管理します。
