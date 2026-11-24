import copy
import unittest
from joint_context_cache import JointContextCache, TacticalContextCache
from build_shot_evaluator import build_joint_shot_context
from test_build_shot_evaluator import ball,enemy,state

class JointCacheTests(unittest.TestCase):
    def test_transport_changes_reuse_without_aliasing_results(self):
        cache=JointContextCache();calls=[]
        s=state(ball('heavy'),[enemy(0,0,15)])
        def compute(s,p):
            calls.append(1);return {'choices':[{'score':s['player']['current_hp']}]}
        first=cache.evaluate(s,{},compute);first['choices'][0]['score']=-100
        changed=copy.deepcopy(s);changed.update(sequence=99,published_at_unix_ms=123,
            aiming_performance={'fps':12},frame_timing={'ms':80},physics_clock={'tick':50})
        second=cache.evaluate(changed,{},compute)
        self.assertEqual(second['choices'][0]['score'],40)
        self.assertEqual(len(calls),1)

    def test_every_changed_combat_input_and_profile_invalidates(self):
        cache=JointContextCache(capacity=20);s=state(ball('heavy'),[enemy(0,0,15)]);calls=[]
        def compute(s,p):calls.append(1);return {'value':len(calls)}
        cache.evaluate(s,{},compute)
        for path,value in [(('player','current_hp'),39),(('enemies',0,'position','x'),1),
                           (('offered_balls',0,'status','mass'),7),
                           (('table','reflection_stacks'),3),
                           (('ball_synergies',),{'heavy_stacks':5}),
                           (('relics',),[{'index':2,'owned':True}]),
                           (('balance_validation',),{'tactical_ai':False})]:
            changed=copy.deepcopy(s);node=changed
            for key in path[:-1]:node=node[key]
            node[path[-1]]=value
            cache.evaluate(changed,{},compute)
        cache.evaluate(s,{'human_error':{'power_ratio':.05}},compute)
        self.assertEqual(len(calls),9)

    def test_lru_is_bounded_and_function_changes_invalidate(self):
        cache=JointContextCache(2);calls=[]
        def compute(s,p):calls.append(1);return dict(s)
        for n in range(3):cache.evaluate({'hp':n},{},compute)
        self.assertEqual(len(cache.entries),2)
        cache.evaluate({'hp':0},{},compute)
        self.assertEqual(len(calls),4)
        result=cache.evaluate({'hp':0},{},lambda s,p:{'new':True})
        self.assertEqual(result,{'new':True})

    def test_real_evaluator_is_identical_with_metadata_variations(self):
        cache=JointContextCache()
        for kind in ('standard','heavy','pierce','anchor'):
            s=state(ball(kind),[enemy(0,0,15,hp=3),enemy(1,4,25,hp=5)])
            s['table']['pockets']=[{'position':{'x':14,'z':28},'radius':3}]
            profile={'human_error':{'aim_radius_ratio':.02,'power_ratio':.05}}
            expected=build_joint_shot_context(s,profile)
            self.assertEqual(cache.evaluate(s,profile,build_joint_shot_context),expected)
            changed=copy.deepcopy(s)
            changed.update(sequence=123,bridge={'sequence':456},audio_feedback={'event':7},
                collision_vfx={'time':8},battle_progress_diagnostics={'age':9},
                mcp_control={'label':'view'},build_decision={'view':True},
                build_shot_choices={'view':True},shot_tactics={'view':True})
            changed['offered_balls'][0]['aim_guidance']={'text':'view'}
            self.assertEqual(cache.evaluate(changed,profile,build_joint_shot_context),
                             build_joint_shot_context(changed,profile))
        self.assertEqual(cache.hits,4)

    def test_real_tactical_context_reuses_only_equal_combat_inputs(self):
        from shot_planner import build_tactical_shot_context
        cache=TacticalContextCache();s=state(ball('guard',shotShield=3),[enemy(0,0,15,hp=3)])
        expected=build_tactical_shot_context(copy.deepcopy(s),{})
        self.assertEqual(cache.evaluate(s,{},build_tactical_shot_context),expected)
        cosmetic=copy.deepcopy(s);cosmetic.update(sequence=9,shot_tactics={'old':True},build_shot_choices={'old':True})
        self.assertEqual(cache.evaluate(cosmetic,{},build_tactical_shot_context),build_tactical_shot_context(copy.deepcopy(cosmetic),{}))
        self.assertEqual(cache.hits,1)
        changed=copy.deepcopy(s);changed['enemies'][0]['hp']=20
        self.assertEqual(cache.evaluate(changed,{},build_tactical_shot_context),build_tactical_shot_context(copy.deepcopy(changed),{}))
        self.assertEqual(cache.misses,2)
        second=JointContextCache();second.evaluate(s,{},build_tactical_shot_context);self.assertEqual(second.hits,0)
