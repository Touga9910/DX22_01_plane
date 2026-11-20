import copy
import unittest
from boss_supply_audit import audit_supply_stage


class SupplyAuditTests(unittest.TestCase):
    def fixture(self,count=1):
        decision=dict(event_type='boss_ai_decision',details=dict(boss_evaluation=dict(metrics=dict(
            advance_supply_attempted_if_turn_continues=True,advance_supply_requested_if_turn_continues=count))))
        supply=dict(event_type='boss_advance_supply',details=dict(cycle=0,break_shots_remaining=1,living_before=2-count,requested=count))
        spawn=dict(event_type='boss_minions_summoned',details=dict(advance=True,requested=count,spawned=count))
        stage=dict(shots=[{}],stage_result=dict(result='clear'))
        return stage,[decision,supply]+([spawn] if count else [])

    def test_count_and_prepaid_completion(self):
        stage,events=self.fixture()
        events.append(dict(event_type='boss_supply_cycle_completed',details=dict(cycle=0,advance_consumed=True,requested=0)))
        result=audit_supply_stage(stage,events)
        self.assertEqual(result['advance_spawned'],1)
        self.assertEqual(result['prepaid_summons_skipped'],1)
        events.append(dict(event_type='boss_minions_summoned',details=dict(advance=False,requested=1,spawned=1)))
        with self.assertRaisesRegex(ValueError,'duplicate regular'):
            audit_supply_stage(stage,events)

    def test_zero_claim_prevents_rebreak_refund(self):
        stage,events=self.fixture(0)
        self.assertEqual(audit_supply_stage(stage,events)['advance_claims'],1)
        with self.assertRaisesRegex(ValueError,'Duplicate'):
            audit_supply_stage(stage,events+[copy.deepcopy(events[1])])

    def test_wrong_timing_and_prediction_are_rejected(self):
        stage,events=self.fixture(2)
        events[1]['details']['break_shots_remaining']=2
        with self.assertRaisesRegex(ValueError,'timing'):audit_supply_stage(stage,events)
        stage,events=self.fixture()
        events[0]['details']['boss_evaluation']['metrics']['advance_supply_requested_if_turn_continues']=2
        with self.assertRaisesRegex(ValueError,'Predicted deficit'):audit_supply_stage(stage,events)


if __name__=='__main__':unittest.main()
