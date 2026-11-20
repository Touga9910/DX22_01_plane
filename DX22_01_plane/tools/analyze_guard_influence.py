"""Offline audit of B0007 Guard use and isolated causal fixtures."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
from statistics import mean

PROJECT=Path(__file__).resolve().parents[1]

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def analyze(labels):
    campaign=PROJECT/'logs/balance/B0007_100_runs_20261118'
    rows=[]
    ball_metrics=defaultdict(Counter)
    for batch in read(campaign/'analysis.json')['batches']:
        for line in (Path(batch)/'all_runs.jsonl').read_text(encoding='utf-8').splitlines():
            record=json.loads(line)
            raw=read(Path(record['source_log']))
            for stage in raw['stages']:
                if stage['stage_type']!='boss': continue
                events=[e for e in raw['events'] if e.get('stage_index')==stage['stage_index']]
                by_type=defaultdict(list)
                for e in events: by_type[e['event_type']].append(e['details'])
                decisions=[e['boss_evaluation'] for e in by_type['boss_ai_decision']]
                guards=[e for e in decisions if e['definition_id']=='player_guard']
                for choice in decisions:
                    group=ball_metrics[choice['definition_id']]
                    group['shots']+=1
                    for key in ('direct_damage','fixed_damage','pocket_damage','armor_removed'):
                        group[key]+=choice['metrics'][key]
                assert len(decisions)==stage['stage_result']['total_shots']
                assert all(e['metrics']['stop_shield']==3 for e in guards)
                assert all(abs(e['score_breakdown']['stop_shield']-2.4)<1e-5 for e in guards)
                shield_absorbed=sum(e['absorbed'] for e in by_type['stop_shield_absorbed'])
                expired=sum(e['amount'] for e in by_type['player_shield_expired'])
                damage=sum(e['damage'] for e in stage['damage_events'])
                assert damage==stage['stage_result']['player_damage_taken']
                rows.append(dict(build=record['build_profile'],seed=record['run_seed'],
                    shots=len(decisions),guard_shots=len(guards),
                    guard_kills=sum(e['metrics']['boss_killed'] for e in guards),
                    damage=damage,absorbed=shield_absorbed,expired=expired,
                    attack_values=[e['attack'] for e in stage['stage_start']['enemies']],
                    damage_sources=dict(Counter({source:sum(e['damage'] for e in stage['damage_events'] if e['source']==source)
                        for source in {e['source'] for e in stage['damage_events']}}))))
    grouped=defaultdict(list)
    all_trials=[]
    verified_shots=0
    captures=PROJECT/'tools/runtime_tests/boss_armor/captures'
    for label in labels:
        status=read(captures/label/'experiment_status.json')
        assert status['state']=='completed'
        trials=read(captures/label/'experiment_results.json')
        for row in trials:
            folder=captures/label/f"{row['seed']}_{row['condition']}"
            checks=list(folder.glob('shot_*/verification.json'))
            assert len(checks)==row['shots'] and all(read(c)['matched'] for c in checks)
            verified_shots+=len(checks)
            if row['condition'].endswith('_replay'):
                baseline=captures/label/f"{row['seed']}_baseline"
                for check in checks:
                    before=read(check.parent/'before.json')
                    original=read(baseline/check.parent.name/'before.json')
                    assert before['break_balls']==original['break_balls']
                    assert before['boss_state']==original['boss_state']
            grouped[row['condition']].append(row)
        all_trials.extend(trials)
    if labels:
        keys=[(r['seed'],r['condition']) for r in all_trials]
        assert len(keys)==len(set(keys))==25
        assert len(grouped)==5 and all(len(values)==5 for values in grouped.values())
    summaries={condition:dict(trials=len(values),wins=sum(r['won'] for r in values),
        mean_damage=mean(r['damage_taken'] for r in values),mean_shots=mean(r['shots'] for r in values),
        guard_shots=sum(r['guard_shots'] for r in values),shots=sum(r['shots'] for r in values),
        guard_share=sum(r['guard_shots'] for r in values)/sum(r['shots'] for r in values),
        damage_values=[r['damage_taken'] for r in values],shot_values=[r['shots'] for r in values])
        for condition,values in grouped.items()}
    return dict(campaign=dict(bosses=len(rows),shots=sum(r['shots'] for r in rows),
        guard_shots=sum(r['guard_shots'] for r in rows),guard_kills=sum(r['guard_kills'] for r in rows),
        hp_damage=sum(r['damage'] for r in rows),absorbed=sum(r['absorbed'] for r in rows),
        expired=sum(r['expired'] for r in rows),attack_values=sorted({v for r in rows for v in r['attack_values']})),
        campaign_rows=rows,ball_prediction_metrics=dict(ball_metrics),fixture_summary=summaries,
        fixtures=[{k:v for k,v in r.items() if k!='actions'} for r in all_trials],
        labels=labels,verified_fixture_shots=verified_shots,
        limitations='Fixed unupgraded mixed deck, HP50/50, one boss placement; five seeds are not five build populations. Fixed-action arithmetic assumes unchanged future actions and HP-dependent effects.')

def write_report(result):
    names=dict(baseline='現行AI・Shield3',shield0_replay='同一行動再生・Shield0',
        shield1_replay='同一行動再生・Shield1',no_flat_bonus='余剰加点なしAI・Shield3',
        restrict_guard='他球があればGuardを選ばない対照')
    lines=['作成日：2026年11月18日','Guard防御性能とAI選球の切り分け検証結果','',
        '結論：Guardの実防御とAIの選球は、どちらも最終ボス被ダメージを抑えている。AIは実際の防御価値をsafetyで評価した上で、余るシールド量まで別途加点している。まずこの重複・余剰加点を除く案を優先する。Guardの防御値だけを3から1に下げても、攻撃力1のボスに対する直接の軽減性能は変わらない。',
        '', '1. B0007の既存100ラン（最終ボス到達90戦）の実測',
        '90戦全勝、456ショット、平均撃破5.07ショット、HP被ダメージ合計191・平均2.12。ボス攻撃力は全戦1。HP被ダメージ源は全てenemy_attack。',
        'Guardは225/456ショット（49.3%）。うち175回で1ダメージを吸収し、残り2シールドは失効した。50回はボス撃破ショットで、防御を必要としなかった。付与675に対し実吸収175、未使用500（74.1%）。',
        '攻撃366回のうち175回（47.8%）をGuardが防いだ。固定行動でGuardの軽減だけを除く参考計算は、平均(191+175)/90=4.07ダメージ。これは実際のGuardなし90戦の再実行ではない。HP依存能力・死亡・その後の選択が変わらないと仮定した値であり、勝率の推定には使わない。',
        '', '軸 | ボス戦数 | Guard選択率 | 実測平均被ダメージ | 固定行動・軽減なし参考値']
    for build in ('standard','heavy','pierce','bounce','anchor'):
        rows=[r for r in result['campaign_rows'] if r['build']==build]
        lines.append(f"{build} | {len(rows)} | {sum(r['guard_shots'] for r in rows)/sum(r['shots'] for r in rows):.1%} | {mean(r['damage'] for r in rows):.2f} | {mean(r['damage']+r['absorbed'] for r in rows):.2f}")
    lines+=['', '2. 隔離した実ゲームでの対照実験',
        'B0007の同一EXEを使用。Standard6・Guard6・Heavy1の未強化デッキ、レリックなし、開始HP50/50、ボスHP60/Armor2・配置(0,20)。5シード×5条件=25戦。五つの軸別ランを再収集したものではない。',
        'シード：2026111700、2026111704、2026111709、2026111714、2026111719。',
        'Shield0/1は現行AIの同じ球・同じ速度を再生し、プレイヤー/ボスの位置、ボスHP・Armor・Break状態、中立球の状態も一致させて、防御だけを変えた。選球変更条件は以後のショットも再評価した。',
        '余剰加点なし条件はstop_shield加点のみを引き、被ダメージを減らすsafety評価を維持した。このデッキでは球ごとの加点が全照準で一定であることを毎回監査し、公開候補から再選択した。衝突時シールドやレリックを含む一般デッキの評価と同一とは限らない。',
        '', '条件 | 勝利 | 平均被ダメージ | 平均撃破ショット | Guard選択率']
    for condition,name in names.items():
        s=result['fixture_summary'][condition]
        lines.append(f"{name} | {s['wins']}/{s['trials']} | {s['mean_damage']:.2f} | {s['mean_shots']:.2f} | {s['guard_share']:.1%}")
    lines+=['',f"全{result['verified_fixture_shots']}ショットで実機と物理予測の照合に合格。固定再生ではボスと中立球の状態一致も検証した。",
        '', '3. 実装上の原因',
        'GameBossAI.cppのsafetyは、敵の予測攻撃合計からstopShieldGrantedを引いた実被ダメージを評価している（189行付近）。その上でstop_shield=付与量×urgency×0.8を追加する（196行付近）。Shield3では通常時+2.4であり、実際に必要な1を守る価値は既にsafetyにも含まれる。',
        'ボスを倒す手でも+2.4が付く。既存90戦のGuard225回全てがこの加点を得ていた。HP10以下では実装上urgencyが3なので別途加点は+7.2になるが、この低HP条件は今回の隔離試験では検証していない。',
        'Guardでも中立球の固定ダメージ・Armor削り・ポケットを使える。これは狙いと連鎖の価値として残す。防御がほぼ必ず足り、余剰分もAIが評価する状況との組合せが問題になる。',
        '', '4. 調整案（製品には未適用）',
        '優先1：ボスAIの生のstop_shield加点を除く。防いだ実ダメージはsafetyで一度評価し、撃破時や余剰シールドには価値を付けない。試験では攻撃球も選ばれ、より短い手数で撃破できた。Guard禁止は対照実験専用で、製品への適用案にはしない。',
        '優先2：ボス側に予告された強攻撃とArmor/Breakによる対処を試作する。初期候補は通常攻撃2、予告強攻撃4、Break開始で予告強攻撃を中断する構成。Shield3なら通常攻撃を守れ、強攻撃は1残るため、Guardで受けるか、中立球・ポケットでArmorを削って止めるかを選べる。数値と周期は未検証の試作値であり、採用前に比較する。予測・C++AI・MCPの攻撃予告も同じルールで更新する必要がある。',
        '優先3：Guardの基礎防御値は先に一律低下させない。3→1でも現行ボスには同じ防御となり、複数敵を相手にする通常戦への影響が先に出る。ボス側の攻撃設計を変えた後に3/2/1の差を再測定する。',
        '', '5. 次の比較と限界',
        '先に現行ボス＋AI加点修正を同じ100組のseed/軸で比較し、次にAI修正版を固定して予告攻撃案を比較する。Guard選択率、実吸収/余剰、撃破時の不要防御、被ダメージ、撃破ショット数、Break開始・Armor削り、軸ごとのボス到達後勝率と道中勝率を記録する。',
        '今回の25戦は共通の未成長デッキ・単一配置・HP50の原因確認であり、5軸の成長後勝率や人間の難しさを保証しない。AIの勝率だけで面白さを決めず、予告の理解と狙いの選び分けは人間プレイでも確認する。',
        '製品のGuard性能・AI・ボス設定は変更していない。バランス登録はB0007のまま、管理対象29ファイルのハッシュ一致を確認した。',
        '', '再現資料', '集計：DX22_01_plane/tools/runtime_tests/guard_influence_analysis.json',
        '実験：DX22_01_plane/tools/verify_guard_influence.py',
        '集計ツール：DX22_01_plane/tools/analyze_guard_influence.py',
        '採用したキャプチャ：'+', '.join(result['labels']),
        '事前のguard_influence_pilot_1118は予備実行であり、25戦の集計には含めない。']
    output=PROJECT.parent/'docs/1118_Guard防御性能とAI選球の検証結果と調整案.txt'
    output.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    return output

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--label',action='append',required=True)
    args=parser.parse_args()
    result=analyze(args.label)
    output=PROJECT/'tools/runtime_tests/guard_influence_analysis.json'
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(write_report(result))
    print(json.dumps({k:result[k] for k in ('campaign','fixture_summary')},ensure_ascii=False,indent=2))
