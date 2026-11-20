import json
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

import collect_until_deadline as subject


class DeadlineTests(unittest.IsolatedAsyncioTestCase):
    async def test_game_crash_marks_failed_instead_of_collecting_forever(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            class Session:
                async def __aenter__(self): return self
                async def __aexit__(self,*args): pass
                async def initialize(self): pass
                async def call_tool(self,name,arguments):
                    data={'scene':'title'}
                    if name=='run_build_comparison':
                        data={'ok':True,'output_paths':{'output_directory':str(root/'batch')}}
                    return SimpleNamespace(isError=False,structuredContent={'result':data})
            class Transport:
                async def __aenter__(self): return (None,None)
                async def __aexit__(self,*args): pass
            process=SimpleNamespace(pid=1234,poll=lambda:3221225477,returncode=3221225477)
            args=SimpleNamespace(output=root/'output',deadline=None,url='http://127.0.0.1:8766/mcp',
                game=root/'fixture.exe',launch_server=False,first_seed=1001,target_runs_per_build=20,
                balance_version='B0008',compare_with=None)
            with patch.object(subject,'ClientSession',lambda *a:Session()), patch.object(subject,'streamable_http_client',lambda *a:Transport()), patch.object(subject.asyncio,'sleep',AsyncMock()), patch.object(subject.subprocess,'Popen',return_value=process), patch.object(subject,'analyze') as analyze:
                with self.assertRaisesRegex(RuntimeError,'Game process exited'):
                    await subject.run(args)
                analyze.assert_not_called()
            status=json.loads((args.output/'overnight_status.json').read_text(encoding='utf-8'))
            self.assertEqual(status['state'],'failed')
            self.assertIn('3221225477',status['error'])
            self.assertEqual(sum(status['completed_runs_by_build'].values()),0)

    async def test_quota_stops_exactly_after_one_valid_run_for_each_build(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            requested = []
            class Session:
                async def __aenter__(self): return self
                async def __aexit__(self, *args): pass
                async def initialize(self): pass
                async def call_tool(self, name, arguments):
                    data = {"scene": "title"}
                    if name == "run_build_comparison":
                        build = arguments['build_profiles'][0]
                        requested.append((arguments['seeds'][0], build))
                        batch = root/build
                        batch.mkdir()
                        log = batch/'run.json'
                        log.write_text(json.dumps({'balance_version':'B0007'}), encoding='utf-8')
                        record = dict(run_seed=arguments['seeds'][0],build_profile=build,
                                      source_log=str(log),configuration_signature='stable')
                        (batch/'all_runs.jsonl').write_text(json.dumps(record)+'\n',encoding='utf-8')
                        data = {"ok":True,"output_paths":{"output_directory":str(batch)}}
                    if name == "get_build_comparison_status":
                        data = {"state":"completed","running":False,"completed":True}
                    return SimpleNamespace(isError=False,structuredContent={"result":data})
            class Transport:
                async def __aenter__(self): return (None,None)
                async def __aexit__(self,*args): pass
            args = SimpleNamespace(output=root/'output',deadline=None,url='http://127.0.0.1:8766/mcp',
                                   game=None,launch_server=False,first_seed=1001,
                                   target_runs_per_build=1,balance_version='B0007',compare_with=None)
            with patch.object(subject,'ClientSession',lambda *a:Session()), patch.object(subject,'streamable_http_client',lambda *a:Transport()), patch.object(subject.asyncio,'sleep',AsyncMock()), patch.object(subject,'analyze',return_value={'valid_runs':5}):
                await subject.run(args)
            self.assertEqual(requested,[(1001,build) for build in subject.BUILDS])
            status=json.loads((args.output/'overnight_status.json').read_text(encoding='utf-8'))
            self.assertEqual(status['state'],'completed')
            self.assertEqual(status['completed_runs_by_build'],dict.fromkeys(subject.BUILDS,1))

    async def test_run_crossing_deadline_finishes_then_analyzes_without_starting_another(self):
        with tempfile.TemporaryDirectory() as folder:
            deadline = datetime.now(timezone.utc) + timedelta(hours=1)
            clock = [deadline - timedelta(minutes=1)]
            class Clock(datetime):
                @classmethod
                def now(cls):
                    return clock[0]
            calls = []
            class Session:
                async def __aenter__(self): return self
                async def __aexit__(self, *args): pass
                async def initialize(self): pass
                async def call_tool(self, name, arguments):
                    calls.append(name)
                    data = {"scene": "title"}
                    if name == "run_build_comparison":
                        data = {"ok": True, "output_paths": {"output_directory": folder}}
                    if name == "get_build_comparison_status":
                        clock[0] = deadline + timedelta(seconds=1)
                        data = {"state": "completed", "running": False, "completed": True}
                    return SimpleNamespace(isError=False, structuredContent={"result": data})
            class Transport:
                async def __aenter__(self): return (None, None)
                async def __aexit__(self, *args): pass
            args = SimpleNamespace(output=Path(folder)/"output", deadline=deadline.isoformat(),
                                   url="http://127.0.0.1:8766/mcp", game=None,
                                   launch_server=False, first_seed=1001)
            with patch.object(subject, "datetime", Clock), patch.object(subject, "ClientSession", lambda *a: Session()), patch.object(subject, "streamable_http_client", lambda *a: Transport()), patch.object(subject.asyncio, "sleep", AsyncMock()):
                await subject.run(args)
            self.assertEqual(calls.count("run_build_comparison"), 1)
            self.assertEqual(calls.count("get_build_comparison_status"), 1)
            status = json.loads((args.output/"overnight_status.json").read_text(encoding="utf-8"))
            self.assertEqual(status["state"], "completed")

    async def test_stage_denominators_and_damage_include_losses_but_not_unfinished(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            raw = {"stages": [
                {"stage_index": 1, "stage_id": "a", "stage_type": "normal", "stage_result": {"result": "clear", "player_damage_taken": 4}},
                {"stage_index": 2, "stage_id": "b", "stage_type": "normal", "stage_result": {"result": "game_over", "player_damage_taken": 10}},
                {"stage_index": 3, "stage_id": "c", "stage_type": "normal", "damage_events": [{"damage": 99}]}]}
            log = root/"run.json"
            log.write_text(json.dumps(raw), encoding="utf-8")
            record = {"run_id": "one", "run_seed": 1, "build_profile": "heavy", "source_log": str(log), "final_boss_defeated": False, "run_result": "game_over", "total_shots": 2, "final_hp": 0}
            (root/"all_runs.jsonl").write_text(json.dumps(record)+"\n", encoding="utf-8")
            result = subject.analyze(root, [root], {"deadline": "2026-11-18T06:00:00+09:00"})
            stages = {row["stage_index"]: row for row in result["stages"]}
            self.assertEqual(stages[1]["win_rate"], 1)
            self.assertEqual(stages[2]["win_rate"], 0)
            self.assertEqual(stages[2]["mean_damage_taken"], 10)
            self.assertIsNone(stages[3]["win_rate"])
            self.assertEqual(result["builds"][1]["losses"], 1)


if __name__ == "__main__":
    unittest.main()
