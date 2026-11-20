import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import collect_until_deadline as collector
from stop_collection_at_target import should_stop, finalize_target_metadata


class CollectionTargetTests(unittest.TestCase):
    def test_finish_only_the_last_required_build(self):
        counts=dict.fromkeys(collector.BUILDS,10)
        counts['anchor']=9
        self.assertTrue(should_stop(counts,'anchor',10))
        self.assertFalse(should_stop(counts,'heavy',10))
        counts['bounce']=9
        self.assertFalse(should_stop(counts,'anchor',10))
        self.assertTrue(should_stop(dict.fromkeys(collector.BUILDS,10),'anchor',10))

    def test_default_ten_and_explicit_override(self):
        for extra,expected in (([],10),(['--target-runs-per-build','20'],20)):
            captured=[]
            with patch.object(sys,'argv',['collector','--output','unused']+extra), patch.object(collector,'run',lambda args:captured.append(args)), patch.object(collector.asyncio,'run',lambda value:None):
                collector.main()
            self.assertEqual(captured[0].target_runs_per_build,expected)

    def test_terminal_metadata_updated_without_changing_records(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)
            value=dict(state='completed',target_runs_per_build=20,deadline='target_20_valid_runs_per_build',valid_runs=50)
            for name in ('overnight_status.json','analysis.json'):
                (directory/name).write_text(json.dumps(value),encoding='utf-8')
            raw=directory/'all_runs.jsonl';raw.write_bytes(b'preserved original records\n')
            finalize_target_metadata(directory,10)
            for name in ('overnight_status.json','analysis.json'):
                updated=json.loads((directory/name).read_text(encoding='utf-8'))
                self.assertEqual(updated['target_runs_per_build'],10)
                self.assertEqual(updated['original_target_runs_per_build'],20)
                self.assertEqual(updated['valid_runs'],50)
            self.assertEqual(raw.read_bytes(),b'preserved original records\n')


if __name__=='__main__':unittest.main()
