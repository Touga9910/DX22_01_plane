"""Paired boss campaign audit: shield policy, escorts and opening formation."""
import json
from collections import Counter
from datetime import datetime
from pathlib import Path
from compare_balance_campaigns import BUILDS, average, boss_summary, collection_target, load_campaign, summary, write_table
from boss_finish_audit import audit_run
from boss_strong_audit import audit_strong_run
from boss_supply_audit import audit_supply_run


def boss_shot_telemetry(stage, events):
    decisions=[e for e in events if e['event_type']=='boss_ai_decision']
    count=stage['stage_result']['total_shots']
    actual=stage.get('shots',[])
    core_shots=count
    if len(decisions)!=count:
        # A pocketed core is destroyed before the old end-of-shot cleanup sees
        # it. Surviving escorts can then receive ordinary (non-boss-AI) shots.
        # Accept only a verified suffix after the core actually died.
        death=next((e for e in events if e['event_type']=='enemy_defeated' and
            e.get('details',{}).get('enemy_id')=='enemy_boss_core'),None)
        k=len(decisions)
        if not (0<k<count and len(actual)==count and death and
            actual[k-1]['elapsed_from_run_start_ms']<=death['elapsed_from_run_start_ms']<actual[k]['elapsed_from_run_start_ms']):
            raise ValueError('Incomplete boss decision telemetry')
        core_shots=k
    if actual:
        if len(actual)!=count or any(s['ball_id']!=d['details']['boss_evaluation']['definition_id']
            for s,d in zip(actual,decisions)):
            raise ValueError('Boss shot records disagree with decision telemetry')
        guards=sum(s['ball_id']=='player_guard' for s in actual)
    else:
        guards=sum(d['details']['boss_evaluation']['definition_id']=='player_guard' for d in decisions)
    return dict(boss_total_shots=count,boss_guard_shots=guards,
        boss_core_kill_shots=core_shots,boss_cleanup_shots=count-core_shots)


def guard_metrics(directory, version):
    manifest=json.loads((directory/'analysis.json').read_text(encoding='utf-8-sig'))
    rows={}
    for batch in manifest['batches']:
        for line in (Path(batch)/'all_runs.jsonl').read_text(encoding='utf-8').splitlines():
            record=json.loads(line)
            raw=json.loads(Path(record['source_log']).read_text(encoding='utf-8-sig'))
            key=(record['run_seed'],record['build_profile'])
            shots=guards=0
            impacts=summons=interruptions=strong_interruptions=opening_breaks=cleanup=core_shots=0
            for stage in raw['stages']:
                if stage['stage_type']!='boss':continue
                events=[e for e in raw['events'] if e.get('stage_index')==stage['stage_index'] and e['event_type']=='boss_ai_decision']
                telemetry=boss_shot_telemetry(stage,[e for e in raw['events'] if e.get('stage_index')==stage['stage_index']])
                shots+=telemetry['boss_total_shots'];guards+=telemetry['boss_guard_shots']
                cleanup+=telemetry['boss_cleanup_shots'];core_shots+=telemetry['boss_core_kill_shots']
                opening_breaks+=bool(events and events[0]['details']['boss_evaluation'].get('metrics',{}).get('break_started',False))
                for event in raw['events']:
                    if event.get('stage_index')!=stage['stage_index']:continue
                    impacts+=event['event_type']=='boss_minion_impact'
                    summons+=event['event_type']=='boss_minions_summoned'
                    interruptions+=event['event_type']=='boss_attack_interrupted'
                    strong_interruptions+=event['event_type']=='boss_attack_interrupted' and event.get('details',{}).get('intent')=='strong_attack'
                for event in events:
                    decision=event['details']['boss_evaluation']
                    if version in ('B0008','B0009','B0010','B0011','B0012','B0013') and (decision['score_breakdown'].get('stop_shield')!=0 or
                            event['details']['model']!=('boss_shared_ccd_v4_escorts' if version in ('B0009','B0010','B0011','B0012','B0013') else 'boss_shared_ccd_v3_safety_only')):
                        raise ValueError(version+' contains old shield scoring or wrong boss model')
                    if version=='B0010' and 'break_started' not in decision.get('metrics',{}):
                        raise ValueError('Missing opening Break telemetry')
            rows[key]=dict(boss_total_shots=shots,boss_guard_shots=guards,minion_impacts=impacts,summons=summons,interruptions=interruptions,strong_interruptions=strong_interruptions,boss_opening_breaks=opening_breaks,
                boss_core_kill_shots=core_shots,boss_cleanup_shots=cleanup)
            if version in ('B0010','B0011','B0012','B0013'):
                audits=audit_run(raw,strict=version in ('B0011','B0012','B0013'))
                rows[key].update(residual_battles=sum(a['residual_battles'] for a in audits),
                    post_kill_shot_damage=sum(a['post_kill_shot_damage'] for a in audits),
                    core_guard_shots=sum(a['core_guard_shots'] for a in audits),
                    actual_core_shots=sum(a['core_shots'] for a in audits),
                    retirement_expected=sum(a['retirement_expected'] for a in audits))
            if version in ('B0011','B0012','B0013') and all('shots' in s for s in raw['stages'] if s['stage_type']=='boss'):
                rows[key].update(audit_strong_run(raw))
            if version=='B0013':rows[key].update(audit_supply_run(raw))
    return rows


def describe(rows):
    boss=boss_summary(rows)
    shots=sum(r['boss_total_shots'] for r in rows)
    guards=sum(r['boss_guard_shots'] for r in rows)
    openings=sum(r.get('boss_opening_breaks',0) for r in rows)
    return dict(summary(rows),boss_reached=boss['reached'],boss_wins=boss['wins'],
        boss_win_rate=boss['win_rate_after_reaching'],boss_mean_damage=boss['mean_damage_all_attempts'],
        boss_mean_kill_shots=boss['mean_shots_to_kill'],boss_median_kill_shots=boss['median_shots_to_kill'],
        boss_mean_stage_clear_shots=average(r['boss_total_shots'] for r in rows if r['boss_won']),
        boss_cleanup_shots=sum(r.get('boss_cleanup_shots',0) for r in rows),
        boss_cleanup_battles=sum(r.get('boss_cleanup_shots',0)>0 for r in rows),
        residual_battles=sum(r.get('residual_battles',0) for r in rows),
        post_kill_shot_damage=sum(r.get('post_kill_shot_damage',0) for r in rows),
        core_guard_shots=sum(r.get('core_guard_shots',0) for r in rows),
        actual_core_shots=sum(r.get('actual_core_shots',0) for r in rows),
        boss_min_kill_shots=boss['min_shots_to_kill'],boss_max_kill_shots=boss['max_shots_to_kill'],
        boss_guard_shots=guards,boss_total_shots=shots,guard_selection_rate=guards/shots if shots else None,
        boss_opening_breaks=openings,boss_opening_break_rate=openings/boss['reached'] if boss['reached'] else None,
        minion_impacts=sum(r.get('minion_impacts',0) for r in rows),summons=sum(r.get('summons',0) for r in rows),
        interruptions=sum(r.get('interruptions',0) for r in rows),strong_interruptions=sum(r.get('strong_interruptions',0) for r in rows),
        strong_executions=sum(r.get('strong_executions',0) for r in rows),
        strong_announced=sum(r.get('strong_announced',0) for r in rows),
        strong_kill_cancellations=sum(r.get('strong_kill_cancellations',0) for r in rows),
        recovery_announcements=sum(r.get('recovery_announcements',0) for r in rows),
        advance_claims=sum(r.get('advance_claims',0) for r in rows),
        advance_requested=sum(r.get('advance_requested',0) for r in rows),
        advance_spawned=sum(r.get('advance_spawned',0) for r in rows),
        prepaid_summons_skipped=sum(r.get('prepaid_summons_skipped',0) for r in rows))


def compare_campaigns(baseline,current,target=10):
    baseline,current=Path(baseline).resolve(),Path(current).resolve()
    target=collection_target(current,target)
    before,after=load_campaign(baseline),load_campaign(current)
    if (before['version'],after['version']) not in (('B0007','B0008'),('B0008','B0009'),('B0009','B0010'),('B0010','B0011'),('B0011','B0012'),('B0012','B0013')):
        raise ValueError('Unsupported boss campaign version pair')
    expected=Counter(dict.fromkeys(BUILDS,target))
    baseline_available_runs=len(before['runs'])
    if Counter(r['build'] for r in after['runs'])!=expected or not after['pairs'].issubset(before['pairs']):
        raise ValueError('Require per-build quotas and matching baseline seed/build pairs')
    # Preserve the baseline archive, but compare only the same paired seeds.
    before['runs']=[r for r in before['runs'] if (r['seed'],r['build']) in after['pairs']]
    if Counter(r['build'] for r in before['runs'])!=expected:
        raise ValueError('Incomplete paired baseline subset')
    for data,directory in ((before,baseline),(after,current)):
        metrics=guard_metrics(directory,data['version'])
        for row in data['runs']:
            row.update(metrics[(row['seed'],row['build'])])
            if row['boss_won']:row['boss_shots']=row['boss_core_kill_shots']
    comparisons=[]
    for build in (*BUILDS,'all'):
        left=[r for r in before['runs'] if build=='all' or r['build']==build]
        right=[r for r in after['runs'] if build=='all' or r['build']==build]
        index={(r['seed'],r['build']):r for r in left}
        both=[(index[(r['seed'],r['build'])],r) for r in right if r['boss_reached'] and index[(r['seed'],r['build'])]['boss_reached']]
        killed=[(a,b) for a,b in both if a['boss_won'] and b['boss_won']]
        comparisons.append(dict(build=build,before=describe(left),after=describe(right),
            loss_to_win=sum(not index[(r['seed'],r['build'])]['won'] and r['won'] for r in right),
            win_to_loss=sum(index[(r['seed'],r['build'])]['won'] and not r['won'] for r in right),
            pre_boss_damage_changed=sum(index[(r['seed'],r['build'])]['pre_boss_damage']!=r['pre_boss_damage'] for r in right),
            boss_reach_changed=sum(index[(r['seed'],r['build'])]['boss_reached']!=r['boss_reached'] for r in right),
            paired_boss_attempts=len(both),paired_boss_damage_delta=average(b['boss_damage']-a['boss_damage'] for a,b in both),
            paired_boss_wins=len(killed),paired_kill_shot_delta=average(b['boss_shots']-a['boss_shots'] for a,b in killed)))
    result=dict(created_at=datetime.now().astimezone().isoformat(),before_version=before['version'],after_version=after['version'],
        before_directory=str(baseline),after_directory=str(current),before_signature=before['signature'],after_signature=after['signature'],
        paired_seed_build_pairs=len(after['pairs']),baseline_available_runs=baseline_available_runs,
        paired_baseline_subset=baseline_available_runs!=len(after['pairs']),comparisons=comparisons,
        definitions=dict(win_rate='final boss wins / all valid runs',boss_mean_damage='HP damage in all reached final-boss battles, including losses',
            boss_mean_kill_shots='shots until core death in won final-boss stages; verified post-core cleanup suffix excluded',
            boss_mean_stage_clear_shots='all shots until final-boss stage clear, including post-core escort cleanup',
            guard_selection_rate='Guard choices / all final-boss-stage shots including cleanup (pooled)',
            paired_deltas='Same seed/build; damage among both-reached, shots among both-won',
            boss_opening_break_rate='First boss shot starts Break / all reached final-boss battles'))
    result['runtime_patch_provenance']=[json.loads(path.read_text(encoding='utf-8-sig')) for path in sorted(current.glob('crashfix_provenance_*.json'))]
    prefix=before['version']+'_vs_'+after['version']
    result['baseline_runtime_patch_provenance']=[json.loads(path.read_text(encoding='utf-8-sig')) for path in sorted(baseline.glob('crashfix_provenance_*.json'))]
    (current/(prefix+'_comparison.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    write_table(current/(prefix+'_builds.csv'),comparisons)
    num=lambda x:'未測定' if x is None else f'{x:.2f}'
    pct=lambda x:'未測定' if x is None else f'{x:.1%}'
    lines=[datetime.now().astimezone().strftime('作成日：%Y年%m月%d日'),before['version']+'と'+after['version']+f'：{len(after["pairs"])}ラン比較','',
        f'各軸{target}有効ラン。共通seed/build {len(after["pairs"])}組、中級・legacy・standard方針・動的バランスOFF。',
        f'比較元の全{baseline_available_runs}ランは保持し、今回と同じseed/buildの{len(after["pairs"])}組を比較対象にした。',
        ('最後のBreakショット入力前に取り巻きを生存2体まで補充。同じ強攻撃周期で再Breakしても補充権を戻さず、後の召集を前払い済みにする。Armor2・Break2・Guard・初期配置・ポケット報酬・AIスコアは維持。' if after['version']=='B0013' else
         'Break復帰時の次ショット予告だけを強攻撃4へ変更。復帰ショットの本体攻撃は0のまま。Armor2・Break2・Guard・初期配置・ポケット報酬・AIスコアを維持。' if after['version']=='B0012' else
         '本体ポケット撃破後の終了処理を修正。Armor2・Break後2ショット・Guard性能・初期配置・AI加点は維持。各ランの実際の撃破時刻とショット終了時生存数で、追加ショットと取り巻き残留を監査した。' if after['version']=='B0011' else
         '初期取り巻きの配置と生成経路のみを変更。Armor2・Break後2ショット・Guard性能・召集配置・AI加点は維持した。全ボス判断で共通モデルと余剰加点0を監査した。' if after['version']=='B0010' else
         'Guard性能を維持し、大型本体＋初期取り巻き3体・Armor2・Break後2ショット・予告攻撃を導入した。全ボス判断で新モデルと余剰加点0を監査した。' if after['version']=='B0009' else 'Guard性能・ボス設定を維持し、ボスAIの余剰シールド加点を除去した。全ボス判断で新モデルと加点0を監査した。'),'',
        '軸 | ラン勝率 | ボス到達/勝利 | ボス平均HP被ダメージ | 平均撃破ショット数 | Guard選択率（全て修正前→修正後）']
    for row in comparisons:
        a,b=row['before'],row['after']
        lines.append(f"{row['build']} | {pct(a['win_rate'])}→{pct(b['win_rate'])} | {a['boss_reached']}/{a['boss_wins']}→{b['boss_reached']}/{b['boss_wins']} | {num(a['boss_mean_damage'])}→{num(b['boss_mean_damage'])} | {num(a['boss_mean_kill_shots'])}→{num(b['boss_mean_kill_shots'])} | {pct(a['guard_selection_rate'])}→{pct(b['guard_selection_rate'])}")
    lines+=['','同一seedの変化と分母']
    for row in comparisons:
        a,b=row['before'],row['after']
        lines.append(f"{row['build']}: 敗北→勝利{row['loss_to_win']}、勝利→敗北{row['win_to_loss']}、ボス到達変化{row['boss_reach_changed']}、道中被ダメージ変化{row['pre_boss_damage_changed']}。共通到達{row['paired_boss_attempts']}戦の被ダメージ差{num(row['paired_boss_damage_delta'])}、共通勝利{row['paired_boss_wins']}戦の撃破ショット差{num(row['paired_kill_shot_delta'])}。Guard {a['boss_guard_shots']}/{a['boss_total_shots']}→{b['boss_guard_shots']}/{b['boss_total_shots']}。")
    for patch in result['runtime_patch_provenance']:
        lines+=['',f"収集中の安定性修正：{patch['revision']}。修正前の{patch['retained_valid_runs']}有効ランを原本のまま保持し、不足分を修正版EXEで収集した。Guard・ボス設定・AI加点は変更していない。旧EXE SHA256={patch['old_exe_sha256']}、新EXE SHA256={patch['new_exe_sha256']}。原ログのビルド情報は改変していない。検証と適用境界は同梱crashfix_provenanceファイルを参照。"]
    lines+=['',f'解釈の注意：各軸{target}ランでは1勝で{100/target:g}ポイント変わる。撃破ショット数は勝利戦だけ、被ダメージは失敗戦も含む。到達数が変わった場合は生存者の偏りがあるため、共通到達・共通勝利の差も併記した。Guard選択率はショット数を分母とし、戦闘ごとの百分率の単純平均ではない。AIの選球と勝率だけで人間の操作感や面白さは決めない。']
    for row in comparisons:
        b=row['after'];lines.append(f"{row['build']}: 取り巻き本体命中{b['minion_impacts']}、召集{b['summons']}、攻撃中断{b['interruptions']}（強攻撃中断{b['strong_interruptions']}）。")
    if after['version']=='B0010':
        lines+=['','初手Break率（ボス到達戦を分母にする）']
        for row in comparisons:
            a,b=row['before'],row['after']
            lines.append(f"{row['build']}: {a['boss_opening_breaks']}/{a['boss_reached']}→{b['boss_opening_breaks']}/{b['boss_reached']}、{pct(a['boss_opening_break_rate'])}→{pct(b['boss_opening_break_rate'])}。")
        lines+=['','本体撃破後の取り巻き処理を分離（既存終了処理の問題を記録し、ゲームルールは今回変更しない）']
        for row in comparisons:
            a,b=row['before'],row['after']
            lines.append(f"{row['build']}: 本体撃破後の追加ショット{a['boss_cleanup_shots']}→{b['boss_cleanup_shots']}、発生戦闘{a['boss_cleanup_battles']}→{b['boss_cleanup_battles']}。ステージ終了までの平均ショット数{num(a['boss_mean_stage_clear_shots'])}→{num(b['boss_mean_stage_clear_shots'])}。被ダメージとGuard率は追加処理も含むステージ全体、本体撃破ショット数は追加処理を除く。")
    report=current/(datetime.now().astimezone().strftime('%m%d_')+before['version']+'と'+after['version']+f'の{len(after["pairs"])}ラン比較分析.txt')
    if after['version'] in ('B0011','B0012','B0013'):
        lines+=['','本体撃破後の終了監査']
        for row in comparisons:
            a,b=row['before'],row['after']
            lines.append(f"{row['build']}: 追加ショット{a['boss_cleanup_shots']}→{b['boss_cleanup_shots']}、撃破ショット終了時の残留戦闘{a['residual_battles']}→{b['residual_battles']}、撃破ショット終了後の被ダメージ合計{a['post_kill_shot_damage']}→{b['post_kill_shot_damage']}。ステージ終了までの平均ショット{num(a['boss_mean_stage_clear_shots'])}→{num(b['boss_mean_stage_clear_shots'])}。")
        lines+=['Guard率とHP被ダメージの主表はステージ全体。本体生存中のGuardは次の分子/分母でも比較する。']
        for row in comparisons:
            a,b=row['before'],row['after']
            lines.append(f"{row['build']}: 本体撃破までのGuard {a['core_guard_shots']}/{a['actual_core_shots']}→{b['core_guard_shots']}/{b['actual_core_shots']}。")
    if after['version'] in ('B0012','B0013'):
        lines+=['','強攻撃の予告・中断・実行（シールド全吸収も実行に含む）']
        for row in comparisons:
            a,b=row['before'],row['after']
            lines.append(f"{row['build']}: 強攻撃予告{a['strong_announced']}→{b['strong_announced']}、Break中断{a['strong_interruptions']}→{b['strong_interruptions']}、実行{a['strong_executions']}→{b['strong_executions']}、撃破による取消{a['strong_kill_cancellations']}→{b['strong_kill_cancellations']}。Break復帰{a['recovery_announcements']}→{b['recovery_announcements']}。")
        lines+=['B0011はショット前予告・Break中断・本体死亡を照合し、実際の強攻撃HP被ダメージイベントで実行を確認。B0012以降は同じ復元とboss_attack_executedの実行イベントを突き合わせ、シールドでHP被ダメージ0の攻撃も数えた。']
    if after['version']=='B0013':
        lines+=['','前倒し補充の監査']
        for row in comparisons:
            b=row['after']
            lines.append(f"{row['build']}: 前借り権消費{b['advance_claims']}、補充要求/実生成{b['advance_requested']}/{b['advance_spawned']}、前払い済み召集スキップ{b['prepaid_summons_skipped']}。選択候補の予測要求数と実機の不足分を照合し、同周期の再補充は許容しない。")
    report.write_text('\n'.join(lines)+'\n',encoding='utf-8-sig')
    return str(report)
