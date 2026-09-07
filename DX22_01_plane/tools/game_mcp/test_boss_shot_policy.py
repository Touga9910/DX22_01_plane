import unittest
from boss_shot_policy import evaluate_boss_choices, fire_boss_choice
from bridge_store import GameBridgeError

class Store:
    def __init__(self): self.calls=[]
    def submit_command(self, action, arguments):
        self.calls.append((action, arguments))
        if action=='evaluate_boss_shots': return {'ok':True,'evaluation':{'model':'boss_shared_ccd_v1','choices':[]}}
        return {'ok':False,'message':'Stale or invalid plan. Evaluate again.'}

class BossPolicyTests(unittest.TestCase):
    def setUp(self):
        self.store=Store()
        self.state={'boss_state':{'hp':60},'available_actions':['evaluate_boss_shots','fire_boss_shot']}
    def test_query_uses_shared_solver_without_firing(self):
        self.assertEqual(evaluate_boss_choices(self.store,self.state)['model'],'boss_shared_ccd_v1')
        self.assertEqual(self.store.calls,[('evaluate_boss_shots',{})])
    def test_moving_state_rejected_without_command(self):
        self.state['available_actions']=[]
        with self.assertRaises(GameBridgeError): fire_boss_choice(self.store,self.state,'0:1','key')
        self.assertFalse(self.store.calls)
    def test_stale_is_reported_without_fallback_or_retry(self):
        result=fire_boss_choice(self.store,self.state,'0:1','old-key')
        self.assertFalse(result['ok']); self.assertEqual(len(self.store.calls),1)
        self.assertNotIn('direction_x',self.store.calls[0][1])
    def test_missing_key_rejected(self):
        with self.assertRaises(GameBridgeError): fire_boss_choice(self.store,self.state,'0:1','')
        self.assertFalse(self.store.calls)

if __name__=='__main__': unittest.main()
