"""Real-game checks of the compiled safety-only boss AI, without editing live data."""
import json
import hashlib
import verify_guard_influence as g


def main():
    label='B0008_shield_score_verification_1118'
    results=[]
    for seed in g.SEEDS:
        result=g.trial(label,seed,'baseline')
        folder=g.b.ROOT/label/f'{seed}_baseline'
        for shot in folder.glob('shot_*'):
            evaluation=g.b.h.read(shot/'evaluation.json')
            assert evaluation['model']=='boss_shared_ccd_v3_safety_only'
            for choice in evaluation['choices']:
                assert choice['score_breakdown']['stop_shield']==0
                assert abs(sum(choice['score_breakdown'].values())-choice['score'])<1e-4
                if choice['definition_id']=='player_guard':
                    assert choice['metrics']['stop_shield']==3
                    assert choice['metrics']['expected_enemy_turn_damage']==0
        results.append({k:v for k,v in result.items() if k!='actions'})
    root=g.b.ROOT/label
    output=dict(state='completed',exe_sha256=hashlib.sha256(g.b.EXE.read_bytes()).hexdigest(),
                trials=results,verified_shots=sum(r['shots'] for r in results))
    g.b.h.write(root/'verification_summary.json',output)
    print(json.dumps(output,ensure_ascii=True,indent=2))


if __name__=='__main__':main()
