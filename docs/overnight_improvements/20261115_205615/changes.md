# 変更記録

項目ごとに、客観的な問題が確認できた場合だけ追記する。

## 作業記録ファイル

- 本フォルダを新規作成した。既存ログ・正式検証データは変更していない。

## P0-1 詳細予測の貫通スタック過剰消費

- 変更前: `BallShotPrediction` はミニオン球選択時に貫通痕数を見ず最大15スタックを消費していた。
- 変更後: 実ゲームと同じ `CalculateEffectiveMinionStackConsume()` で効果に寄与する量だけを算出し、0の場合は「0=全消費」のAPIを呼ばない。
- 理由: MCP詳細予測の残スタック・生成ミニオンと実ショットを一致させるため。
- 変更ファイル: `BallShotPrediction.cpp`, `tools/test_ball_synergy_rules.cpp`

## P2-3 テスト保守

### ステージエディタ回帰のデータ依存除去

- 変更前: `test_stage_layout_editor` は製品データの最初のボスがbreak-ball未記述であると決め打ちし、現行の明示配置で失敗。
- 変更後: 現行ボスのテスト内コピーから配置を除いて旧形式を再現し、製品データを変更せずフォールバックを検証。
- 変更ファイル: `tools/test_stage_layout_editor.cpp`

### ボス物理テストのinclude修正

- 変更前: ポケット座標APIが `TableGeometry.h` へ分離された後もテストが直接includeせず、単独ビルドに失敗。
- 変更後: 所有APIのヘッダーを明示include。
- 変更ファイル: `tools/test_boss_armor_runtime.cpp`

## 今回の新規コード変更一覧

- `BallShotPrediction.cpp`: 詳細予測のミニオン有効消費量を実ゲームと統一。
- `tools/test_ball_synergy_rules.cpp`: 貫通痕0/1本の消費回帰を追加。
- `tools/test_stage_layout_editor.cpp`: 旧形式回帰を自己完結化。
- `tools/test_boss_armor_runtime.cpp`: テーブル幾何ヘッダーのincludeを追加。
- `docs/overnight_improvements/20261115_205615/*`: 全10項目の調査・変更・検証記録。
