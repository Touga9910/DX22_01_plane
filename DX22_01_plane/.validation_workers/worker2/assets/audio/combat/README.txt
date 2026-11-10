Caromino 戦闘SE素材（正式採用候補）

このディレクトリのWAVは、第三者素材を使わず、
tools/generate_combat_se.ps1 で決定的に合成したオリジナル候補です。
ユーザーの試聴完了まで「完成品」とは扱いません。

基本5カテゴリ:
- direct_impact.wav: プレイヤー球と敵球の直接衝突
- enemy_impact.wav: 敵球同士の衝突
- enemy_defeat.wav: 新規撃破
- chain_milestone.wav: CHAIN段階別素材が欠損した場合の共通フォールバック
- wall_impact.wav: 有効な壁反射

CHAIN段階別:
- chain_3.wav
- chain_6.wav
- chain_10.wav
- chain_15.wav

対応形式:
- RIFF/WAVE PCM
- 16 bit
- mono または stereo
- 44.1 kHz または 48 kHz

--audio-test-tones 起動時だけ、不足素材をコード生成の短い検証音で補います。
検証音は正式な戦闘SEではなく、配布素材として採用しないでください。

詳細な再生時間・ピーク・用途・出典・許諾は manifest.json を参照してください。
