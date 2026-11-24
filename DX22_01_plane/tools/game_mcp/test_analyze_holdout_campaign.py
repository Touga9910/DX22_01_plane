import copy
import unittest

from analyze_holdout_campaign import validate_contract, summarize


class HeldoutContractTests(unittest.TestCase):
    def setUp(self):
        self.reference = dict(exe_sha256="exe", profile_progress_sha256="save",
            required_balance_version="B0014", player_level="intermediate", validation_variant="fixed",
            dynamic_balance=False, policy_source_hashes={"policy.py": "same"})
        self.current = dict(self.reference, seeds_requested=list(range(2001, 2011)), started_at="2026-11-21T11:00:00+09:00")
        self.audit = dict(seeds=list(range(2001, 2011)), matches=[], calibration_updated=False,
                          created_at="2026-11-21T10:00:00+09:00")

    def test_fixed_policy_disjoint_predeclared_seeds_are_accepted(self):
        validate_contract(self.reference, self.current, self.audit, range(1001, 1011))

    def test_same_seeds_and_changed_policy_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlap"):
            validate_contract(self.reference, self.current, self.audit, range(2001, 2011))
        changed = copy.deepcopy(self.current)
        changed["policy_source_hashes"]["policy.py"] = "new"
        with self.assertRaisesRegex(ValueError, "conditions changed"):
            validate_contract(self.reference, changed, self.audit, range(1001, 1011))

    def test_post_launch_audit_and_recalibration_are_rejected(self):
        audit = dict(self.audit, created_at="2026-11-21T12:00:00+09:00")
        with self.assertRaisesRegex(ValueError, "precede"):
            validate_contract(self.reference, self.current, audit, range(1001, 1011))
        with self.assertRaisesRegex(ValueError, "calibration"):
            validate_contract(self.reference, self.current, dict(self.audit, calibration_updated=True), range(1001, 1011))

    def test_unfinished_runs_are_excluded_from_performance(self):
        result = summarize([dict(complete=False)])
        self.assertEqual(result["runs"], 0)
        self.assertIsNone(result["win_rate"])


if __name__ == "__main__":
    unittest.main()
