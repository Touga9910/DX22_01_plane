"""Collect paired builds with independent local games and the existing MCP runner.

Only data/source snapshots and private saves are written. Rendering assets are
read-only junctions. Gameplay data, bridge cadence and collector policy stay exact.
"""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from collect_until_deadline import analyze, read_lines, save
from compare_balance_campaigns import BUILDS, compare_campaigns, load_campaign
from boss_finish_audit import audit_run
from boss_strong_audit import audit_strong_run
from boss_supply_audit import audit_supply_run

PROJECT = Path(__file__).resolve().parents[2]


def read(path):
    try:
        return json.loads(path.read_text(encoding='utf-8-sig'))
    except (OSError, ValueError):
        return {}


def windows(pid):
    found = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    @callback_type
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        thread = ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(128)
        ctypes.windll.user32.GetClassNameW(hwnd, name, 128)
        if owner.value == pid and name.value == 'DX22_01_plane_WindowClass':
            found.append((hwnd, thread))
        return True
    ctypes.windll.user32.EnumWindows(visit, 0)
    return found


def validate_entry(before, after):
    def route(raw):
        return [(s['stage_id'],s['stage_index'],s['stage_result']['result'],
            s['stage_result']['player_damage_taken'],s['stage_result']['total_shots'])
            for s in raw['stages'] if s['stage_type']!='boss']
    if route(before)!=route(after):
        raise ValueError('Pre-boss stage outcomes differ from paired baseline')
    left=next((s for s in before['stages'] if s['stage_type']=='boss'),None)
    right=next((s for s in after['stages'] if s['stage_type']=='boss'),None)
    if bool(left)!=bool(right):raise ValueError('Boss reach differs before placement change')
    if left and (left['stage_start']['player_hp']!=right['stage_start']['player_hp'] or
        left['stage_context']['deck']!=right['stage_context']['deck'] or
        left['stage_context']['owned_relics']!=right['stage_context']['owned_relics']):
        raise ValueError('Boss entry HP, deck or relics differ from paired baseline')


def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    baseline = load_campaign(args.baseline.resolve())
    assert (baseline['version'],args.version) in (('B0009','B0010'),('B0010','B0011'),('B0011','B0012'),('B0012','B0013'))
    registry = read(PROJECT/'assets/data/balance_version.json')
    exe = args.exe.resolve()
    target = 10
    assert registry['current_version'] == args.version
    assert all(sum(r['build']==b for r in baseline['runs']) == target for b in BUILDS)
    baseline_paths={}
    for batch in read(args.baseline.resolve()/'analysis.json')['batches']:
        for record in read_lines(Path(batch)/'all_runs.jsonl'):
            baseline_paths[(record['run_seed'],record['build_profile'])]=Path(record['source_log'])
    verified=set()
    verified_signatures=set()
    metadata = dict(started_at=datetime.now().astimezone().isoformat(),
        deadline='target_10_valid_runs_per_build',
        runner_pid=os.getpid(), required_balance_version=args.version,
        target_runs_per_build=target, builds_requested=BUILDS,
        player_level='intermediate', validation_variant='fixed', dynamic_balance=False,
        comparison_baseline=str(args.baseline.resolve()),
        exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        profile_progress_sha256=hashlib.sha256((PROJECT/'saves/profile_progress.json').read_bytes()).hexdigest(),
        isolation='One local game, MCP endpoint and save directory per build; identical authored files',
        batches=[], workers={})
    children, streams = [], []
    def launch(command, directory, log):
        stream=(output/(log+'.log')).open('w',encoding='utf-8')
        streams.append(stream)
        process=subprocess.Popen(command,cwd=directory,stdout=stream,stderr=stream,
            stdin=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
        children.append(process)
        return process
    def status(state, **extra):
        counts={b:len(read_lines(Path(w['batch'])/'all_runs.jsonl')) for b,w in metadata['workers'].items()}
        save(output/'overnight_status.json',dict(metadata,state=state,
            completed_runs_by_build={b:counts.get(b,0) for b in BUILDS},
            updated_at=datetime.now().astimezone().isoformat(),**extra))
    ctypes.windll.kernel32.SetThreadExecutionState(0x80000001)
    try:
        for index,build in enumerate(BUILDS):
            folder=output/'isolated'/build
            (folder/'assets').mkdir(parents=True)
            shutil.copytree(PROJECT/'assets/data',folder/'assets/data')
            for resource in (PROJECT/'assets').iterdir():
                if resource.name=='data':continue
                dest=folder/'assets'/resource.name
                if resource.is_dir():
                    escaped=str(dest).replace("'","''");source=str(resource).replace("'","''")
                    subprocess.run(['powershell','-NoProfile','-Command',
                        f"New-Item -ItemType Junction -Path '{escaped}' -Target '{source}' | Out-Null"],
                        check=True,creationflags=subprocess.CREATE_NO_WINDOW)
                else:shutil.copy2(resource,dest)
            shutil.copytree(PROJECT/'shader',folder/'shader')
            for source in PROJECT.glob('*.cso'):shutil.copy2(source,folder/source.name)
            for name in registry['managed_files']:
                source=PROJECT/name;dest=folder/name;dest.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(source,dest)
                assert source.read_bytes()==dest.read_bytes()
            # Keep all logger inputs byte-identical; only private visual preferences.
            (folder/'saves').mkdir()
            shutil.copy2(PROJECT/'saves/profile_progress.json',folder/'saves/profile_progress.json')
            save(folder/'saves/settings.json',dict(bgm_volume=0,se_volume=0,fullscreen=False,
                resolution_index=0,camera_shake_enabled=False))
            game=launch([str(exe)],folder,build+'_game')
            until=time.monotonic()+60
            while True:
                if game.poll() is not None:raise RuntimeError(f'{build} game exited at startup: {game.returncode}')
                for hwnd,_ in windows(game.pid):ctypes.windll.user32.ShowWindow(hwnd,0)
                initial=read(folder/'runtime/game_mcp/game_state.json')
                if initial.get('scene')=='title':
                    expected=[b['id'] for b in read(PROJECT/'assets/data/player_status.json')['balls']]
                    actual=[b['definition_id'] for b in initial['catalog_balls']]
                    if actual!=expected:raise ValueError('Unlock catalog differs from fully unlocked baseline')
                    break
                if time.monotonic()>until:raise TimeoutError(build+' game startup')
                time.sleep(.2)
            port=args.first_port+index
            endpoint=f'http://127.0.0.1:{port}/mcp'
            server=launch([sys.executable,str(PROJECT/'tools/game_mcp/server.py'),
                '--port',str(port),'--bridge-directory',str(folder/'runtime/game_mcp'),
                '--build-profile',build,'--command-timeout-seconds','60',
                '--maximum-state-age-seconds','120'],PROJECT,build+'_server')
            batch=output/'batches'/build
            seeds=sorted(r['seed'] for r in baseline['runs'] if r['build']==build)
            command=[sys.executable,str(PROJECT/'tools/game_mcp/collect_fixed_balance_runs.py'),
                '--url',endpoint,'--comparison-suite','--comparison-build',build,
                '--valid-runs-per-build',str(target),'--profile','intermediate',
                '--validation-variant','fixed','--output-directory',str(batch),
                '--balance-log-directory',str(folder/'logs/balance')]
            for seed in seeds:command.extend(('--comparison-seed',str(seed)))
            time.sleep(2)
            collector=launch(command,PROJECT,build+'_collector')
            metadata['batches'].append(str(batch))
            metadata['workers'][build]=dict(game_pid=game.pid,server_pid=server.pid,
                collector_pid=collector.pid,folder=str(folder),batch=str(batch),endpoint=endpoint)
            status('starting')
        while True:
            finished=True
            for build,w in metadata['workers'].items():
                processes={p.pid:p for p in children}
                for name in ('game_pid','server_pid'):
                    if processes[w[name]].poll() is not None:raise RuntimeError(f'{build}: {name} exited')
                code=processes[w['collector_pid']].poll()
                if code is None:finished=False
                elif code!=0:raise RuntimeError(f'{build} collector exited {code}')
                w['collector_status']=read(Path(w['batch'])/'comparison_status.json')
                for record in read_lines(Path(w['batch'])/'all_runs.jsonl'):
                    key=(record['run_seed'],record['build_profile'])
                    if key in verified:continue
                    raw=read(Path(record['source_log']))
                    if raw.get('balance_version')!=args.version:raise ValueError('Unexpected balance version')
                    validate_entry(read(baseline_paths[key]),raw)
                    if args.version in ('B0011','B0012','B0013'):audit_run(raw,strict=True)
                    if args.version in ('B0012','B0013'):audit_strong_run(raw)
                    if args.version=='B0013':audit_supply_run(raw)
                    verified_signatures.add(record['configuration_signature'])
                    if len(verified_signatures)!=1:raise ValueError('Configuration changed across workers')
                    verified.add(key)
            metadata['paired_entry_verified_count']=len(verified)
            status('collecting')
            if finished:break
            time.sleep(10)
        status('analyzing')
        signatures=set()
        pairs=set()
        for batch in metadata['batches']:
            for record in read_lines(Path(batch)/'all_runs.jsonl'):
                raw=read(Path(record['source_log']))
                assert raw['balance_version']==args.version
                signatures.add(record['configuration_signature'])
                pair=(record['run_seed'],record['build_profile'])
                assert pair not in pairs
                pairs.add(pair)
        assert len(signatures)==1 and pairs==baseline['pairs']
        result=analyze(output,metadata['batches'],metadata)
        report=compare_campaigns(args.baseline.resolve(),output,target)
        status('completed',valid_runs=result['valid_runs'],comparison_report=report)
    except BaseException as error:
        status('failed',error=repr(error))
        raise
    finally:
        # Graceful closure of only the games owned by this isolated campaign.
        for w in metadata['workers'].values():
            for _,thread in windows(w['game_pid']):ctypes.windll.user32.PostThreadMessageW(thread,0x0012,0,0)
        for process in children:
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:process.terminate()
        for stream in streams:stream.close()
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000000)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--version',required=True)
    parser.add_argument('--first-port',type=int,default=8870)
    run(parser.parse_args())
