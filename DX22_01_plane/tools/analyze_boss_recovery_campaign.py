"""Audit B0012 paired runs and the unchanged prefix before the first recovery."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'game_mcp'))
from analyze_boss_formation import raw_runs, boss_metrics
from boss_finish_audit import audit_run
from boss_strong_audit import audit_strong_run
from collect_isolated_campaign import validate_entry


def analyze(baseline,current):
    left,right=raw_runs(baseline),raw_runs(current)
    assert len(right)==50 and set(left)==set(right)
    rows=[]
    fields=('ball_id','upgrade_level','power','position','velocity','player_hp_before',
            'player_hp_after','player_enemy_damage','enemy_enemy_damage','total_enemy_damage')
    for key,raw in sorted(right.items()):
        before=left[key];validate_entry(before,raw)
        a=audit_run(before,strict=True);b=audit_run(raw,strict=True)
        strong_a=audit_strong_run(before);strong_b=audit_strong_run(raw)
        sa=next((s for s in before['stages'] if s['stage_type']=='boss'),None)
        sb=next((s for s in raw['stages'] if s['stage_type']=='boss'),None)
        prefix_count=0;same=None
        if sa:
            recovery=next((e for e in before['events'] if e.get('stage_index')==sa['stage_index'] and
                           e['event_type']=='boss_break_ended'),None)
            prefix_count=(max(i for i,s in enumerate(sa['shots']) if s['elapsed_from_run_start_ms']<=recovery['elapsed_from_run_start_ms'])+1
                          if recovery else len(sa['shots']))
            x=[{f:s[f] for f in fields} for s in sa['shots'][:prefix_count]]
            y=[{f:s[f] for f in fields} for s in sb['shots'][:prefix_count]]
            same=x==y
            assert same,('Physics or choice changed before first recovery',key)
        rows.append(dict(seed=key[0],build=key[1],before_finish=a,after_finish=b,
            before_strong=strong_a,after_strong=strong_b,unchanged_prefix_shots=prefix_count,prefix_identical=same))
    def finish_totals(label):
        battles=[a for r in rows for a in r[label]]
        return dict(battles=len(battles),core_defeats=sum(x['core_defeated'] for x in battles),
            **{k:sum(x[k] for x in battles) for k in ('extra_shots','residual_battles','post_kill_shot_damage')})
    result=dict(created_at=datetime.now().astimezone().isoformat(),paired_runs=len(rows),
        boss_prefixes_checked=sum(r['prefix_identical'] is not None for r in rows),
        boss_prefix_mismatches=sum(r['prefix_identical'] is False for r in rows),
        before_finish=finish_totals('before_finish'),after_finish=finish_totals('after_finish'),pairs=rows)
    def behavior(runs):
        data=dict(opening_guard=0,first_break_shot_2=0,zero_damage_bosses=0,strong_executions_on_guard=0)
        for raw in runs.values():
            boss=next((s for s in raw['stages'] if s['stage_type']=='boss'),None)
            if not boss:continue
            metrics=boss_metrics(raw)
            data['opening_guard']+=metrics['opening_ball']=='player_guard'
            data['first_break_shot_2']+=metrics['first_break_shot']==2
            data['zero_damage_bosses']+=boss['stage_result']['player_damage_taken']==0
            selected=''
            for event in raw['events']:
                if event.get('stage_index')!=boss['stage_index']:continue
                if event['event_type']=='boss_ai_decision':selected=event['details']['boss_evaluation']['definition_id']
                if event['event_type']=='boss_attack_executed' and event['details']['intent']=='strong_attack':
                    data['strong_executions_on_guard']+=selected=='player_guard'
        if next(iter(runs.values()))['balance_version']=='B0011':data['strong_executions_on_guard']=None
        return data
    result['before_behavior']=behavior(left);result['after_behavior']=behavior(right)
    (current/'recovery_integrity_audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    comparison=json.loads((current/'B0011_vs_B0012_comparison.json').read_text(encoding='utf-8'))
    implementation=json.loads((Path(__file__).parent/'runtime_tests/B0012_implementation_audit_1119.json').read_text(encoding='utf-8'))
    num=lambda x:'未測定' if x is None else f'{x:.3f}'
    pct=lambda x:'未測定' if x is None else f'{x:.1%}'
    lines=[datetime.now().astimezone().strftime('作成日：%Y年%m月%d日'),
        'B0012：Break復帰後の強攻撃4予告・同一シード50ラン比較結果','',
        '変更はBreak復帰後の次ショット予告を通常攻撃2から強攻撃4へ切り替えること。復帰ショット自体の攻撃停止を維持する。',
        'Armor2・Break発生後2ショット・Guard基本/強化性能・HP60・初期3体の配置・ポケット報酬・AIの数値スコアを維持した。',
        '条件：2026111700～2026111709を5軸各10ラン、中級・legacy・standard方針・DDA OFF・18球解放。B0011原本を保持して比較した。','',
        '軸 | ラン勝率 | ボス勝利/到達 | ボス到達後勝率 | 平均HP被ダメージ | 本体撃破平均ショット | Guard率 | 強攻撃中断/実行（B0011→B0012）']
    for row in comparison['comparisons']:
        a,b=row['before'],row['after']
        lines.append(f"{row['build']} | {pct(a['win_rate'])}→{pct(b['win_rate'])} | {a['boss_wins']}/{a['boss_reached']}→{b['boss_wins']}/{b['boss_reached']} | {pct(a['boss_win_rate'])}→{pct(b['boss_win_rate'])} | {num(a['boss_mean_damage'])}→{num(b['boss_mean_damage'])} | {num(a['boss_mean_kill_shots'])}→{num(b['boss_mean_kill_shots'])} | {pct(a['guard_selection_rate'])}→{pct(b['guard_selection_rate'])} | {a['strong_interruptions']}/{a['strong_executions']}→{b['strong_interruptions']}/{b['strong_executions']}")
    total=comparison['comparisons'][-1];a,b=total['before'],total['after']
    lines+=['','解釈',
        f"全体のボス到達後勝率{pct(a['boss_win_rate'])}→{pct(b['boss_win_rate'])}、HP被ダメージ{num(a['boss_mean_damage'])}→{num(b['boss_mean_damage'])}。",
        f"強攻撃予告{a['strong_announced']}→{b['strong_announced']}、Break中断{a['strong_interruptions']}→{b['strong_interruptions']}、実行{a['strong_executions']}→{b['strong_executions']}、本体撃破による取消{a['strong_kill_cancellations']}→{b['strong_kill_cancellations']}。",
        ('今回の到達44戦は全勝のままで、到達後勝率100%を改善するにはこの一変更だけでは足りなかった。' if b['boss_win_rate']==1 else '到達後勝率の変化は上表の同一シード結果を参照。'),
        f"B0012の強攻撃実行{b['strong_executions']}回のうちGuard選択中が{result['after_behavior']['strong_executions_on_guard']}回。再Breakでの強攻撃中断は0回で、実際の選択経路はGuardで受ける方向へ寄った。専用実機ケースでは再Break中断を確認しており、ルール上の中断禁止ではない。",
        f"初手Guard {result['before_behavior']['opening_guard']}→{result['after_behavior']['opening_guard']}、初回Breakが2手目の戦闘 {result['before_behavior']['first_break_shot_2']}→{result['after_behavior']['first_break_shot_2']}、無傷ボス戦 {result['before_behavior']['zero_damage_bosses']}→{result['after_behavior']['zero_damage_bosses']}。初期配置を維持したため、序盤の固定化は残った。",
        f"召集{a['summons']}→{b['summons']}、取り巻き本体命中{a['minion_impacts']}→{b['minion_impacts']}。解釈上の仮説：強攻撃後の召集が攻撃圧と本体へ当てる弾の両方を供給し、撃破平均ショット短縮にもつながった可能性がある。集計だけで因果を断定しない。",
        '被ダメージの増加と、狙いの多様化・面白さは別に評価する。人間プレイでの理解と操作感の確認は別途必要。今回は指定された一変更の比較を確定し、追加の性能調整は行わない。','',
        '共通化と検証',
        'BossEncounterRules::CompleteShotを実プレイと予測の復帰判定で共有。予測の物理終端を保持し、復帰後強攻撃の予告予定を別結果としてAIにも返す。AIスコアへの加点はない。',
        f"実機/予測{implementation['verification_shots']}ショット：復帰後の通常発射・AI発射・再Break6、5軸通常戦67、本体ポケット撃破10、通常予告周期12。ネイティブルールとPython32テスト成功。",
        '復帰したショットは攻撃0、次のショットで強攻撃4が実行されGuard3で合計攻撃6を3へ軽減。再Breakなら強攻撃中断・Break残り2を確認。',
        f"道中およびボス開始条件50組が一致。本体戦の最初の復帰までの物理・選球・ダメージ{result['boss_prefixes_checked']}組を照合し、不一致{result['boss_prefix_mismatches']}。",
        f"終了監査：修正後本体撃破{result['after_finish']['core_defeats']}戦、追加ショット{result['after_finish']['extra_shots']}、取り巻き残留戦闘{result['after_finish']['residual_battles']}、撃破ショット終了後HP被ダメージ{result['after_finish']['post_kill_shot_damage']}。",'',
        '定義と分母',
        'ラン勝率は全有効ラン、ボス到達後勝率は到達戦闘を分母とする。HP被ダメージは到達した敗北戦も含む。本体撃破ショット数は勝利戦のみ。Guard率はボスステージ全ショットの回数合算。',
        '強攻撃の中断はboss_attack_interruptedのstrong_attack。本体撃破による取消は別集計。実行はシールド全吸収も含め、計画数やHPが減った回数と区別する。',
        'B0011の6実行は予告・ショット・Break・本体死亡から復元し、全6回の実際の強攻撃HP被ダメージイベントと照合した。B0012は同じ復元とboss_attack_executedを全戦で照合した。',
        '各軸10ランでは1勝で10ポイント変動する。対象シード外の勝率は確定しない。',
        '詳細：B0011_vs_B0012_comparison.json、recovery_integrity_audit.json、overnight_status.json。']
    report=current/(datetime.now().astimezone().strftime('%m%d_')+'B0012の50ラン比較と復帰予告検証結果.txt')
    report.write_text('\n'.join(lines)+'\n',encoding='utf-8-sig')
    return result,report


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--baseline',type=Path,required=True);parser.add_argument('--current',type=Path,required=True)
    args=parser.parse_args();result,report=analyze(args.baseline,args.current)
    print(json.dumps({k:v for k,v in result.items() if k!='pairs'},ensure_ascii=True));print(str(report))
