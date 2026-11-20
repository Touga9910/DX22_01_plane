"""B0013 paired cohort, prepaid-cycle audit and unchanged initial fight prefix."""
import argparse
from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'game_mcp'))
from analyze_boss_formation import raw_runs
from boss_finish_audit import audit_run
from boss_strong_audit import audit_strong_run
from boss_supply_audit import audit_supply_run
from collect_isolated_campaign import validate_entry


def strong_materials(raw):
    counts=Counter();strong=False;current=None;next_is_last=False
    for stage in raw['stages']:
        if stage['stage_type']!='boss':continue
        shot_index=-1
        for event in raw['events']:
            if event.get('stage_index')!=stage['stage_index']:continue
            name=event['event_type'];d=event.get('details',{})
            if name=='enemy_action_plan':
                core=next((a for a in d['actions'] if a['enemy_id']=='enemy_boss_core'),None)
                strong=bool(core and core['action']=='normal_attack' and core['predicted_damage_before_defense']==4)
            if name=='boss_advance_supply':next_is_last=True
            if name=='boss_ai_decision':
                shot_index+=1;shot=stage['shots'][shot_index];current=d['boss_evaluation']
                if next_is_last:
                    counts['last_break_inputs_after_claim']+=1
                    hits=current['metrics']['minion_boss_hits']
                    counts['last_break_inputs_using_minions']+=bool(hits)
                    counts['last_break_minion_hits']+=hits
                    next_is_last=False
                if strong:
                    n=sum(e['enemy_id']=='enemy_boss_minion' and e['initial_hp']>0
                          for e in shot['physics_chain']['enemy_distances'])
                    counts['strong_with_minions_'+str(n)]+=1
                    counts['strong_guard_choices']+=shot['ball_id']=='player_guard'
    return counts


def analyze(baseline,current):
    left,right=raw_runs(baseline),raw_runs(current)
    assert len(right)==50 and set(left)==set(right)
    fields=('ball_id','upgrade_level','power','position','velocity','player_hp_before',
            'player_hp_after','player_enemy_damage','enemy_enemy_damage','total_enemy_damage')
    rows=[];before_behavior=Counter();after_behavior=Counter()
    for key,raw in sorted(right.items()):
        old=left[key];validate_entry(old,raw)
        before=audit_run(old,strict=True);after=audit_run(raw,strict=True)
        a=audit_strong_run(old);b=audit_strong_run(raw);supply=audit_supply_run(raw)
        sa=next((s for s in old['stages'] if s['stage_type']=='boss'),None)
        sb=next((s for s in raw['stages'] if s['stage_type']=='boss'),None)
        same=None;n=0
        if sb:
            first=next((e for e in raw['events'] if e.get('stage_index')==sb['stage_index'] and e['event_type']=='boss_advance_supply'),None)
            n=max(i for i,s in enumerate(sb['shots']) if s['elapsed_from_run_start_ms']<=first['elapsed_from_run_start_ms'])+1 if first else len(sb['shots'])
            same=[{f:s[f] for f in fields} for s in sa['shots'][:n]]==[{f:s[f] for f in fields} for s in sb['shots'][:n]]
            assert same,('Initial fight changed before advance supply',key)
        before_behavior.update(strong_materials(old));after_behavior.update(strong_materials(raw))
        rows.append(dict(seed=key[0],build=key[1],before_finish=before,after_finish=after,
            before_strong=a,after_strong=b,supply=supply,initial_prefix_shots=n,initial_prefix_identical=same))
    def totals(label):
        battles=[x for r in rows for x in r[label]]
        return dict(battles=len(battles),core_defeats=sum(x['core_defeated'] for x in battles),
            **{k:sum(x[k] for x in battles) for k in ('extra_shots','residual_battles','post_kill_shot_damage')})
    result=dict(created_at=datetime.now().astimezone().isoformat(),paired_runs=50,
        initial_prefixes_checked=sum(r['initial_prefix_identical'] is not None for r in rows),initial_prefix_mismatches=0,
        before_finish=totals('before_finish'),after_finish=totals('after_finish'),
        before_behavior=dict(before_behavior),after_behavior=dict(after_behavior),
        supply_totals={k:sum(r['supply'][k] for r in rows) for k in rows[0]['supply']},pairs=rows)
    (current/'supply_integrity_audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    data=json.loads((current/'B0012_vs_B0013_comparison.json').read_text(encoding='utf-8'))
    num=lambda x:'未測定' if x is None else f'{x:.3f}'
    pct=lambda x:'未測定' if x is None else f'{x:.1%}'
    lines=[datetime.now().astimezone().strftime('作成日：%Y年%m月%d日'),
        'B0013：Break最後のショット前の前倒し補充・同一シード50ラン比較','',
        '維持：Armor2、Break発生後2ショット、Guard基本/強化性能、初期3体の配置、ポケット報酬、強攻撃4、AIスコアと候補生成。',
        '変更：残り1の入力前に生存2体まで不足分を補充。前借り済み権利は再Breakでも戻さず、後の召集をスキップ。召集フェーズ完了時だけ次周期へ更新する。',
        '前倒し補充は(-24,-8),(24,12)を希望位置として交互に使い、安全な局所位置へ配置する。初期配置と通常の未前借り召集は変更しない。',
        '条件：2026111700～2026111709、各軸10有効ラン計50。中級・legacy・standard方針・DDA OFF・18球解放。B0012原本は保持。','',
        '軸 | ラン勝率 | ボス勝利/到達 | 到達後勝率 | 平均HP被ダメージ | 本体撃破平均ショット | Guard率 | 強攻撃中断/実行（B0012→B0013）']
    for row in data['comparisons']:
        a,b=row['before'],row['after']
        lines.append(f"{row['build']} | {pct(a['win_rate'])}→{pct(b['win_rate'])} | {a['boss_wins']}/{a['boss_reached']}→{b['boss_wins']}/{b['boss_reached']} | {pct(a['boss_win_rate'])}→{pct(b['boss_win_rate'])} | {num(a['boss_mean_damage'])}→{num(b['boss_mean_damage'])} | {num(a['boss_mean_kill_shots'])}→{num(b['boss_mean_kill_shots'])} | {pct(a['guard_selection_rate'])}→{pct(b['guard_selection_rate'])} | {a['strong_interruptions']}/{a['strong_executions']}→{b['strong_interruptions']}/{b['strong_executions']}")
    a,b=data['comparisons'][-1]['before'],data['comparisons'][-1]['after'];s=result['supply_totals']
    lines+=['','補充と終了の監査',
        f"前借り権消費{s['advance_claims']}、要求/実生成{s['advance_requested']}/{s['advance_spawned']}、後払い召集スキップ{s['prepaid_summons_skipped']}。同周期の二重補充0、残り1以外の前倒し補充0。全局面の選択候補の条件付き補充予測と実測を照合した。",
        f"道中とボス開始HP/デッキ/レリックは50組一致。最初の前借りまでの選球・物理・ダメージ{result['initial_prefixes_checked']}組で一致。",
        f"本体撃破{result['after_finish']['core_defeats']}戦、追加ショット{result['after_finish']['extra_shots']}、取り巻き残留戦闘{result['after_finish']['residual_battles']}、撃破ショット終了後HP被ダメージ{result['after_finish']['post_kill_shot_damage']}。",'',
        '結果の読み取り',
        f"到達後勝率{pct(a['boss_win_rate'])}→{pct(b['boss_win_rate'])}、ボスHP被ダメージ{num(a['boss_mean_damage'])}→{num(b['boss_mean_damage'])}。強攻撃中断{a['strong_interruptions']}→{b['strong_interruptions']}、実行{a['strong_executions']}→{b['strong_executions']}、撃破による取消{a['strong_kill_cancellations']}→{b['strong_kill_cancellations']}。",
        f"Guard実回数/ショット{a['boss_guard_shots']}/{a['boss_total_shots']}→{b['boss_guard_shots']}/{b['boss_total_shots']}。召集{a['summons']}→{b['summons']}、取り巻き本体命中{a['minion_impacts']}→{b['minion_impacts']}。",
        '強攻撃時の材料分布（生存取り巻き数→局面数）：',
        'B0012 '+str({n:before_behavior['strong_with_minions_'+str(n)] for n in range(5)}),
        'B0013 '+str({n:after_behavior['strong_with_minions_'+str(n)] for n in range(5)}),
        f"前借り後の最後のBreak入力{after_behavior['last_break_inputs_after_claim']}回のうち、取り巻きを本体へ当てた入力{after_behavior['last_break_inputs_using_minions']}回、命中合計{after_behavior['last_break_minion_hits']}。材料の供給と、実際に強攻撃まで残す選択を分けて評価する。",'',
        '実装と検証の範囲',
        '共有EndEnemyTurnで前倒し条件・不足分・消費権・後払い抑止を決定する。実プレイは入力確定前に生成し、予測は物理終端を維持して次ターン継続時の補充予定を別結果として返し、AIも同じ結果を報告する。',
        'ネイティブルールとPython36対象テスト、実機/予測100ショット（5軸66、Guard通常発射制御8、強攻撃再Breakと権利保持4、ポケット撃破10、通常予告周期12）に合格。通常起動先へ反映してタイトル起動確認。',
        '既知の別件：Guard自己ポケット時のシールドとHPの既存予測差を検出し、1120_Guard自己ポケットの既存予測差の記録.txtへ保存した。比較の条件を保つためこの修正を混ぜず、通常発射制御では自己ポケットを含めない。50ランの統計は実測ログから計算する。',
        '初回ビルド失敗はディスク容量不足による。保存EXE・収集データを保持し、こちらの再生成可能なビルド中間ファイルだけを削除。最終ビルドは従来と同じMSVC19.38 Debug形式。','',
        '分母：ラン勝率は全50有効ラン、HP被ダメージはボス到達した敗北戦も含む。本体撃破ショットは勝利戦のみ。Guard率はボス全ショットで回数合算。強攻撃実行はシールド全吸収も含む。',
        '各軸10ランでは1勝で10ポイント動く。面白さ・人間の狙いやすさ・対象シード外の勝率はこの集計だけでは確定しない。',
        '根拠：B0012_vs_B0013_comparison.json、supply_integrity_audit.json、campaign_verification_1120.json、host_shutdown_audit_1120.json、B0013_implementation_audit_1120.json。']
    report=current/(datetime.now().astimezone().strftime('%m%d_')+'B0013の50ラン比較と前倒し補充検証結果.txt')
    report.write_text('\n'.join(lines)+'\n',encoding='utf-8-sig')
    return result,report


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--baseline',type=Path,required=True);parser.add_argument('--current',type=Path,required=True)
    args=parser.parse_args();r,report=analyze(args.baseline,args.current)
    print(json.dumps({k:v for k,v in r.items() if k!='pairs'},ensure_ascii=True));print(report)
