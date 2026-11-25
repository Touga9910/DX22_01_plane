Caromino 戦闘SE素材

球同士の衝突音 direct_impact.wav / enemy_impact.wav は、
実録のビリヤード球衝突音を加工したCC0素材です。
壁・撃破・CHAINは tools/generate_combat_se.ps1 の決定的な自作合成、
被弾・シールドは tools/generate_player_impact_se.py の自作合成です。
音の好みと戦闘中の聞き取りやすさの最終判断は、人間による試聴で行います。

球同士の衝突音の出典:
作者: Za-Games
作品名: billiard ball clack
一次ページ: https://freesound.org/people/Za-Games/sounds/539854/
ライセンス: CC0 1.0 Universal
https://creativecommons.org/publicdomain/zero/1.0/

取得元は上記ページで公開されているHQ MP3試聴ファイルです。
原本WAVをダウンロードしたものではありません。
https://cdn.freesound.org/previews/539/539854_12029332-hq.mp3
取得MP3のSHA-256:
8fba3f6faccc03d06ac6fa7f1a3e496b7dba18468364082526a9b8b52af0950d

MP3をデコードし、線形補間で48kHz・16bit・mono PCMに変換した加工元を保存しています。
source/billiard_ball_clack_539854.wav
加工元PCMのSHA-256:
e2e14a8910278f3559d5f0a86d8e098fb8d7c4f78bc7221e9e93d2beaa9064e7

tools/generate_combat_se.ps1 は加工元PCMから、直接衝突180ms・敵同士160msを切り出します。
DC成分を除き、冒頭0.25ms・末尾12msをフェードし、ピークをそれぞれ0.74・0.58に調整します。
元の鋭い立ち上がりと自然減衰を残し、直接衝突と敵同士で同じ球の材質感を使います。
再生成にMP3デコーダーや追加Pythonパッケージは必要ありません。

基本5カテゴリ:
- direct_impact.wav: プレイヤー球と敵球の直接衝突（実録CC0、180ms）
- enemy_impact.wav: 敵球同士の衝突（同じ実録CC0、160ms、音量を抑えて連鎖に対応）
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
