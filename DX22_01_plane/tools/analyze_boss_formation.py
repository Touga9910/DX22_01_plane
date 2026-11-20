"""Audit actual opening Break events and matching boss-entry conditions offline."""
import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path
import statistics
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'game_mcp'))
from compare_shield_campaigns import boss_shot_telemetry


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def raw_runs(directory):
    manifest=read(directory/'overnight_status.json')
    result={}
    for batch in manifest['batches']:
        path=Path(batch)/'all_runs.jsonl'
        if not path.exists():continue
        for line in path.read_text(encoding='utf-8').splitlines():
            r=json.loads(line)
            key=(r['run_seed'],r['build_profile'])
            assert key not in result
            result[key]=read(r['source_log'])
    return result


def boss_metrics(raw):
    boss=next((s for s in raw['stages'] if s['stage_type']=='boss'),None)
    if boss is None:return dict(reached=False)
    events=[e for e in raw['events'] if e.get('stage_index')==boss['stage_index']]
    decisions=[e for e in events if e['event_type']=='boss_ai_decision']
    telemetry=boss_shot_telemetry(boss,events)
    shot=0;break_shots=[];impacts=0;strong=0
    for e in events:
        name=e['event_type']
        if name=='boss_ai_decision':shot+=1
        if name=='boss_break_started':break_shots.append(shot)
        if name=='boss_minion_impact':impacts+=1
        if name=='boss_attack_interrupted' and e['details'].get('intent')=='strong_attack':strong+=1
    first=decisions[0]['details']['boss_evaluation']
    opening=first['metrics']['break_started']
    assert bool(opening)==(1 in break_shots),'Predicted and actual opening Break disagree'
    return dict(reached=True,opening_break=bool(opening),break_shots=break_shots,
        opening_ball=first['definition_id'],opening_minion_hits=first['metrics']['minion_boss_hits'],
        first_break_shot=break_shots[0] if break_shots else None,
        minion_impacts=impacts,strong_interruptions=strong,
        won=boss['stage_result']['result']=='clear',
        damage=boss['stage_result']['player_damage_taken'],shots=telemetry['boss_core_kill_shots'],
        stage_shots=telemetry['boss_total_shots'],cleanup_shots=telemetry['boss_cleanup_shots'])


def analyze(baseline,current):
    left,right=raw_runs(baseline),raw_runs(current)
    assert set(right).issubset(left)
    pairs=[]
    for key,b in sorted(right.items()):
        a=left[key]
        def before_boss(raw):
            return [(s['stage_id'],s['stage_index'],s['stage_result']['result'],
                s['stage_result']['player_damage_taken'],s['stage_result']['total_shots'])
                for s in raw['stages'] if s['stage_type']!='boss']
        ab=next((s for s in a['stages'] if s['stage_type']=='boss'),None)
        bb=next((s for s in b['stages'] if s['stage_type']=='boss'),None)
        entry_same=None
        if ab and bb:
            entry_same=(ab['stage_start']['player_hp']==bb['stage_start']['player_hp'] and
                ab['stage_context']['deck']==bb['stage_context']['deck'] and
                ab['stage_context']['owned_relics']==bb['stage_context']['owned_relics'])
        pairs.append(dict(seed=key[0],build=key[1],pre_boss_identical=before_boss(a)==before_boss(b),
            boss_entry_hp_deck_relics_identical=entry_same,before=boss_metrics(a),after=boss_metrics(b)))
    groups=[]
    for build in ('standard','heavy','pierce','bounce','anchor','all'):
        rows=[p for p in pairs if build=='all' or p['build']==build]
        group=dict(build=build,runs=len(rows))
        for label in ('before','after'):
            reached=[p[label] for p in rows if p[label]['reached']]
            breaks=[x['first_break_shot'] for x in reached if x['first_break_shot'] is not None]
            group[label]=dict(bosses=len(reached),opening_breaks=sum(x['opening_break'] for x in reached),
                opening_ball_usage=dict(Counter(x['opening_ball'] for x in reached)),
                opening_guard_shots=sum(x['opening_ball']=='player_guard' for x in reached),
                opening_minion_hits=sum(x['opening_minion_hits'] for x in reached),
                first_break_shots=breaks,no_break_bosses=sum(x['first_break_shot'] is None for x in reached),
                mean_first_break_shot=statistics.mean(breaks) if breaks else None,
                cleanup_shots=sum(x['cleanup_shots'] for x in reached),
                minion_impacts=sum(x['minion_impacts'] for x in reached),
                strong_interruptions=sum(x['strong_interruptions'] for x in reached))
        groups.append(group)
    result=dict(created_at=datetime.now().astimezone().isoformat(),paired_runs=len(pairs),
        pre_boss_mismatches=sum(not p['pre_boss_identical'] for p in pairs),
        boss_entry_mismatches=sum(p['boss_entry_hp_deck_relics_identical'] is False for p in pairs),
        groups=groups,pairs=pairs,opening_break_audit='Prediction agrees with actual boss_break_started before the second decision')
    (current/'formation_effects_detail.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    comparison=current/'B0009_vs_B0010_comparison.json'
    if comparison.exists():
        data=read(comparison)
        lines=[datetime.now().astimezone().strftime('作成日：%Y年%m月%d日'),
            'B0010：初期取り巻き配置修正の同一シード50ラン比較','',
            '変更：ポケット復帰用の座標探索を初期生成から分離し、希望座標そのものを検証する生成処理に変更。',
            '初期3体のXZ座標は(-32,-18),(32,-10),(-44,22)。重なり時は希望座標周辺の決定的な安全位置を探す。',
            '維持：本体HP60・Armor2・Break発生後2ショット・Guardの防御量/強化値・取り巻き衝突効果・召集配置・AIスコア。',
            '比較条件：各軸10有効ラン、同一seed/build50組、中級・legacy・standard方針・DDA OFF。MSVC19.38と全18球解放カタログをそろえた。',
            f'道中結果の不一致{result["pre_boss_mismatches"]}組、共通ボス到達戦の開始HP・デッキ・レリック不一致{result["boss_entry_mismatches"]}組。',
            '初手Breakは選択ショットの予測だけでなく、2回目のAI判断より前の実際のboss_break_startedイベントと照合した。','',
            '軸 | ラン勝率 | ボス平均HP被ダメージ | 平均撃破ショット数 | ボス戦Guard選択率 | 初手Break（B0009→B0010）']
        pct=lambda x:'未測定' if x is None else f'{x:.1%}'
        num=lambda x:'未測定' if x is None else f'{x:.2f}'
        for row in data['comparisons']:
            a,b=row['before'],row['after']
            lines.append(f"{row['build']} | {pct(a['win_rate'])}→{pct(b['win_rate'])} | {num(a['boss_mean_damage'])}→{num(b['boss_mean_damage'])} | {num(a['boss_mean_kill_shots'])}→{num(b['boss_mean_kill_shots'])} | {pct(a['guard_selection_rate'])}→{pct(b['guard_selection_rate'])} | {a['boss_opening_breaks']}/{a['boss_reached']}→{b['boss_opening_breaks']}/{b['boss_reached']}")
        lines+=['','Breakと取り巻きの利用']
        for group in groups:
            a,b=group['before'],group['after']
            lines.append(f"{group['build']}: 初回Breakの平均ショット番号（Breakが発生した戦闘のみ）{num(a['mean_first_break_shot'])}→{num(b['mean_first_break_shot'])}。Breakなし{a['no_break_bosses']}→{b['no_break_bosses']}戦。本体への取り巻き衝突{a['minion_impacts']}→{b['minion_impacts']}回。強攻撃中断{a['strong_interruptions']}→{b['strong_interruptions']}回。初手Guard {a['opening_guard_shots']}/{a['bosses']}→{b['opening_guard_shots']}/{b['bosses']}。初手取り巻き命中合計{a['opening_minion_hits']}→{b['opening_minion_hits']}。")
        total=data['comparisons'][-1];a,b=total['before'],total['after']
        opening_before,opening_after=groups[-1]['before'],groups[-1]['after']
        lines+=['','結果の解釈',
            f"初手Breakは{a['boss_opening_breaks']}/{a['boss_reached']}→{b['boss_opening_breaks']}/{b['boss_reached']}。初期配置の連続衝突による全戦初手Breakを解消できたかは、この実測値で判定する。",
            f"本体撃破ショットは平均{num(a['boss_mean_kill_shots'])}→{num(b['boss_mean_kill_shots'])}、ステージHP被ダメージは{num(a['boss_mean_damage'])}→{num(b['boss_mean_damage'])}。ラン勝率{pct(a['win_rate'])}→{pct(b['win_rate'])}、ボス到達後勝率{pct(a['boss_win_rate'])}→{pct(b['boss_win_rate'])}。初手Break率の改善と難易度の改善は別の指標として評価する。",
            f"Guard実選択数{a['boss_guard_shots']}→{b['boss_guard_shots']}、分母の全ショット数{a['boss_total_shots']}→{b['boss_total_shots']}。初手Guardも{opening_before['opening_guard_shots']}/{opening_before['bosses']}→{opening_after['opening_guard_shots']}/{opening_after['bosses']}へ変化した。Guard防御量や余剰シールド加点を変更した結果ではなく、配置変更に対して同じAIが選球を変えた結果。",
            f"取り巻きの本体命中は{opening_after['minion_impacts']}回あり、弾にしてArmorを削る仕組みは残っている。"]
        timings=Counter(opening_after['first_break_shots'])
        if len(timings)==1 and opening_after['no_break_bosses']==0:
            turn=next(iter(timings))
            lines.append(f'一方、初回Breakは到達した全戦で{turn}ショット目に集中した。初手Break問題の解消だけで狙いの多様化まで達成したとは判断しない。')
        if b['boss_cleanup_battles']:
            lines.append(f"本体撃破後の処理漏れは{b['boss_cleanup_battles']}戦・合計{b['boss_cleanup_shots']}ショットで顕在化した。取り巻きを離して残りやすくしたことで既存の終了処理条件の不足が表面化した。これは次に直すべき正しさの問題であり、追加処理の長さをボス耐久の増加として評価しない。")
        lines+=['','分母と注意',
            '勝率は全有効ランを分母とする。HP被ダメージとGuard選択率は最終ボスのステージ全体（本体撃破後の追加処理も含む）。HP被ダメージは到達した敗北戦も含み、本体撃破ショット数は勝利戦の本体死亡まで。Guard選択率は選択回数/全ショット数の合算。',
            '既存不具合：本体がポケットで消滅すると、終了処理のbossPresent条件が偽になり、残存取り巻きの退場が漏れる場合がある。追加処理は本体撃破ショット数から分離し、元の全ステージ指標と別記した。今回は初期配置の比較に範囲を限定し、終了処理のゲームルールは変更していない。',
            '各軸10ランでは1勝が10ポイント。全勝でも難易度や面白さを確定しない。人間の狙いの理解と操作感は別途確認する。',
            '元のB0009の50ランおよびB0008の100ランは保持した。先行試行の解放カタログ不一致データは分母から除外し、原本を別フォルダに保持した。',
            '検証：最終EXEの5軸64ショット＋Guard/予告12ショットで実機/予測一致。Armor・Break・ポケット・初期生成のネイティブテスト、比較と停止/道中一致条件のPython17テストに合格。',
            '補足：広範な既存Python集計テストでは47件中1件がGuardを軸専用球リストへ含める前提で失敗。今回変更していない既存分類の問題で、ボス戦Guard率は全boss_ai_decision原ログから直接再計数し、当該リストを利用しない。',
            '根拠：B0009_vs_B0010_comparison.json、formation_effects_detail.json、overnight_status.json、B0010_implementation_audit_1119.json。']
        report=current/(datetime.now().astimezone().strftime('%m%d_')+'初手Break配置修正の50ラン検証.txt')
        report.write_text('\n'.join(lines)+'\n',encoding='utf-8-sig')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--baseline',type=Path,required=True);parser.add_argument('--current',type=Path,required=True)
    args=parser.parse_args();result=analyze(args.baseline,args.current)
    print(json.dumps({k:v for k,v in result.items() if k!='pairs'},ensure_ascii=True))
