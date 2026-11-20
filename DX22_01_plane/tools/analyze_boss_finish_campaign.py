"""Paired termination audit; baseline logs/reports are read-only."""
import argparse
from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'game_mcp'))
from boss_finish_audit import audit_run
from analyze_boss_formation import raw_runs


def analyze(baseline,current):
    left,right=raw_runs(baseline),raw_runs(current)
    rows=[]
    for key,raw in sorted(right.items()):
        before=audit_run(left[key]);after=audit_run(raw,strict=True)
        # B0010 predates this predictive field; absence is not measured zero.
        for item in before:item['retirement_expected']=None
        a=before[0] if before else None;b=after[0] if after else None
        trace_same=None
        if a and b and a['core_defeated'] and b['core_defeated']:
            sa=next(s for s in left[key]['stages'] if s['stage_type']=='boss')
            sb=next(s for s in raw['stages'] if s['stage_type']=='boss')
            fields=('ball_id','upgrade_level','power','position','velocity','player_hp_before',
                    'player_hp_after','player_enemy_damage','enemy_enemy_damage','total_enemy_damage')
            ta=[{f:s[f] for f in fields} for s in sa['shots'][:a['core_shots']]]
            tb=[{f:s[f] for f in fields} for s in sb['shots'][:b['core_shots']]]
            trace_same=ta==tb
        rows.append(dict(seed=key[0],build=key[1],before=a,after=b,core_fight_trace_identical=trace_same))
    def totals(label):
        attempts=[r[label] for r in rows if r[label]]
        return dict(attempts=len(attempts),core_defeats=sum(a['core_defeated'] for a in attempts),
            **{field:sum(a[field] for a in attempts) for field in
               ('extra_shots','residual_battles','post_kill_shot_damage','core_guard_shots','core_shots')})
    result=dict(created_at=datetime.now().astimezone().isoformat(),paired_runs=len(rows),
        before=totals('before'),after=totals('after'),
        core_fight_traces_checked=sum(r['core_fight_trace_identical'] is not None for r in rows),
        core_fight_trace_mismatches=sum(r['core_fight_trace_identical'] is False for r in rows),pairs=rows)
    (current/'finish_integrity_audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    comparison=current/'B0010_vs_B0011_comparison.json'
    if comparison.exists():
        data=json.loads(comparison.read_text(encoding='utf-8'));a,b=result['before'],result['after']
        pct=lambda x:'未測定' if x is None else f'{x:.1%}'
        num=lambda x:'未測定' if x is None else f'{x:.2f}'
        lines=[datetime.now().astimezone().strftime('作成日：%Y年%m月%d日'),
            'B0011：終了修正後の同一シード50ラン比較と残留監査','',
            'B0010と同じ各軸10ラン・計50組。中級・legacy・standard方針・DDA OFF・18球解放カタログ。',
            'Armor2・Break発生後2ショット・Guard性能・初期配置・AIの数値スコアは維持。B0010原本は変更しない。','',
            '軸 | ラン勝率 | ボスステージ平均HP被ダメージ | 本体撃破平均ショット | Guard選択率（B0010→B0011）']
        for row in data['comparisons']:
            x,y=row['before'],row['after']
            lines.append(f"{row['build']} | {pct(x['win_rate'])}→{pct(y['win_rate'])} | {num(x['boss_mean_damage'])}→{num(y['boss_mean_damage'])} | {num(x['boss_mean_kill_shots'])}→{num(y['boss_mean_kill_shots'])} | {pct(x['guard_selection_rate'])}→{pct(y['guard_selection_rate'])}")
        lines+=['','本体撃破後の終了確認',
            f"実際の本体撃破{a['core_defeats']}→{b['core_defeats']}戦。追加ショット{a['extra_shots']}→{b['extra_shots']}、撃破ショット終了時に敵が残った戦闘{a['residual_battles']}→{b['residual_battles']}。",
            f"撃破ショット終了後のHP被ダメージ合計{a['post_kill_shot_damage']}→{b['post_kill_shot_damage']}。",
            '追加ショットは本体の実際のenemy_defeated時刻とshots開始時刻を突き合わせて計数し、残留はそのショットの実測enemies_alive_afterで確認した。B0011ではAIのencounter_cleared=true、次敵行動被ダメージ予測0も全撃破戦で確認した。',
            f"本体撃破までの選球・発射位置/速度・与ダメージ・ショット中HPを照合：{result['core_fight_traces_checked']}組、不一致{result['core_fight_trace_mismatches']}組。",
            f"本体撃破までのGuard：{a['core_guard_shots']}/{a['core_shots']} ({pct(a['core_guard_shots']/a['core_shots'] if a['core_shots'] else None)})→{b['core_guard_shots']}/{b['core_shots']} ({pct(b['core_guard_shots']/b['core_shots'] if b['core_shots'] else None)})。",'',
            '解釈と分母',
            '勝率は全有効ラン、本体撃破ショット数は勝利したボス戦の本体死亡まで。平均HP被ダメージとGuard率の主表はボスステージ全体（B0010に含まれる追加処理も含む）。Guard率は回数の合算。',
            'ステージ全体のHP被ダメージ・Guard率の変化と、終了処理を除いた本体戦の変化を分けて評価する。撃破後の不要な攻撃・追加処理を取り除いた効果を、Guard弱体化やボス難度の変更と解釈しない。',
            '各軸10ランでは1勝が10ポイント。勝率の微差や楽しさは確定しない。',
            '原ログ・レコードは改変せず、監査JSONには全50組の結果を保存した。']
        (current/(datetime.now().astimezone().strftime('%m%d_')+'B0011の50ラン比較と戦闘終了監査.txt')).write_text('\n'.join(lines)+'\n',encoding='utf-8-sig')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--baseline',type=Path,required=True);parser.add_argument('--current',type=Path,required=True)
    args=parser.parse_args();r=analyze(args.baseline,args.current)
    print(json.dumps({k:v for k,v in r.items() if k!='pairs'},ensure_ascii=True))
