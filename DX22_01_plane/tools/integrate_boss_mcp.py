from pathlib import Path
P=Path(__file__).resolve().parent/'game_mcp'
p=P/'server.py';s=p.read_text(encoding='utf-8')
s=s.replace('from build_shot_evaluator import build_joint_shot_context','from build_shot_evaluator import build_joint_shot_context\nfrom boss_shot_policy import evaluate_boss_choices, fire_boss_choice',1)
old='''        state["shot_tactics"] = build_tactical_shot_context(
            state,
            profile,
        )
        state["build_shot_choices"] = build_joint_shot_context(state, profile)'''
new='''        if state.get("boss_state"):
            evaluation = (evaluate_boss_choices(store, state)
                          if "evaluate_boss_shots" in state.get("available_actions", []) else None)
            state["boss_shot_choices"] = evaluation
            state["shot_tactics"] = {"model": "boss_shared_ccd_v1", "use_tool": "fire_boss_shot"}
            state["build_shot_choices"] = evaluation or {
                "model": "boss_shared_ccd_v1", "recommended": None, "choices": []}
        else:
            state["shot_tactics"] = build_tactical_shot_context(state, profile)
            state["build_shot_choices"] = build_joint_shot_context(state, profile)'''
assert old in s;s=s.replace(old,new,1)
anchor='''    @mcp.tool(
        title="ビルド完成のためのリスクを評価",'''
insert='''    @mcp.tool(
        title="最終ボスのショット候補を評価",
        description=("停止中の最終ボス戦を共通CCD/TOIで予測します。ボール候補、中立球の押し込み、"
                     "Armor破壊、Break中の直接攻撃、次の攻撃位置を比較します。"
                     "recommended/choicesのcandidate_idとstate_keyをfire_boss_shotへ渡してください。"
                     "状態を変更しません。次ショットの位置価値は概算です。"),
        annotations=read_only, structured_output=True,
    )
    def evaluate_boss_shots() -> GameToolResult:
        return GameToolResult(result=evaluate_boss_choices(store, read_control_state()))

    @mcp.tool(
        title="最終ボスの評価済みショットを実行",
        description=("評価結果の候補を実行します。必要なボール選択も同時に行います。"
                     "黄色い球への押し込み、本体攻撃、次の押し込みのための移動が選べます。"
                     "盤面や候補球が変わった古い評価は拒否します。"
                     "C++ AIと同じ決定的な評価で、プレイヤーレベル別の照準誤差は加えません。"),
        annotations=local_write, structured_output=True,
    )
    def fire_boss_shot(candidate_id: str, state_key: str) -> GameToolResult:
        return GameToolResult(result=fire_boss_choice(store, read_control_state(), candidate_id, state_key))

'''
assert anchor in s;s=s.replace(anchor,insert+anchor,1)
anchor='''        resolved_target_id = resolve_target_id_argument(
            target_id,'''
assert anchor in s;s=s.replace(anchor,'''        if state.get("boss_state"):
            raise GameBridgeError("最終ボス戦はevaluate_boss_shotsで比較し、fire_boss_shotで実行してください。")
'''+anchor,1)
p.write_text(s,encoding='utf-8')
p=P/'shot_planner.py';s=p.read_text(encoding='utf-8')
s=s.replace('"ショットでは必ず生存中の敵をenemies[].target_idで"','''"最終ボス戦でboss_stateがある場合は、boss_shot_choicesまたはevaluate_boss_shotsで"
        "評価し、candidate_idとstate_keyをfire_boss_shotへ渡してください。"
        "候補に含まれる中立球への押し込みや位置調整も有効です。ボール選択は同時に行います。"
        "このボス専用経路はC++ AIと同じ決定的な評価で、照準誤差を加えません。"
        "以下の敵への標的指定・fire_shot・照準誤差の規則は通常戦と中ボス戦に適用します。"
        "通常戦と中ボス戦のショットでは必ず生存中の敵をenemies[].target_idで"''',1)
p.write_text(s,encoding='utf-8')
print('MCP boss tools and control instructions integrated')
