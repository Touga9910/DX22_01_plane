# 変更記録

作成日: 2026年11月16日

## 戦闘進行ルールの小規模分離

- `BattleFlowRules.h`: 11tick停止確定、フェーズ初期値、進行可否、最終勝利、クリア回復可否を副作用のない関数として追加。
- `BattleController.cpp/.h`: 既存の条件式を共通ルール呼び出しへ置換。定数11、フェーズ進行タイミング、勝利判定は維持。
- `DX22_01_plane.vcxproj/.filters`: 新規ヘッダーをプロジェクトのBattleフィルターへ登録。
- `tools/test_battle_flow_rules.cpp/.vcxproj`: 独立回帰テストを追加。
- `tools/test_battle_controller_runtime.cpp`: 実 `BattleController` と実ステージデータを使う統合テストを追加。テスト時だけメインプロジェクトから起動できる条件を `.vcxproj` に追加。
- Python契約テスト: `BattleFlowRules`、`BallPhysicsWorldContext`、プロトタイプステージ、17球、ルート責務分割の現状に合わせて検査先を更新。
- `PlayerBall.cpp`: 未使用になっていた `Game.h` 依存を削除。

変更前後でプレイ仕様とバランス値は同一。純粋ルールと実コントローラの両層で回帰を検出する。
