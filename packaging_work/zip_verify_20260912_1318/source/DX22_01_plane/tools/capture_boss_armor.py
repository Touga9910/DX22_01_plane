"""Isolated real-game checks, using current authored data and only owned processes."""
import argparse
from contextlib import contextmanager
import ctypes
import hashlib
import math
import os
from pathlib import Path
import shutil
import subprocess
import time
import capture_fixed_timestep as h

ROOT = h.PROJECT/'tools/runtime_tests/boss_armor/captures'
h.ROOT = ROOT
h.BASELINE = h.PROJECT/'assets'  # prepare() copies BASELINE/data

@contextmanager
def game(label, case, build, boss=(0,20), configure=None, extra=(), resume=False):
    folder=h.prepare(label,case,build)
    if configure: configure(folder)
    exe=h.PROJECT.parent/'x64/Debug/DX22_01_plane.exe'
    env=dict(os.environ, DX22_TEST_PREDICTION='1', DX22_TEST_RENDER_HZ=os.environ.get('DX22_TEST_RENDER_HZ','60'))
    with (folder/'process.log').open('w',encoding='utf-8') as log:
        process=subprocess.Popen([str(exe)],cwd=folder,env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            h.wait_for(lambda:h.read(folder/'runtime/game_mcp/game_state.json').get('scene')=='title',process)
            window=h.wait_for(lambda:h.window_for(process),process)
            ctypes.windll.user32.ShowWindow(window[0],0)
            if resume:
                # Exercise the existing Continue menu on this test's own window.
                user=ctypes.windll.user32
                user.GetForegroundWindow.restype=ctypes.wintypes.HWND
                previous=user.GetForegroundWindow()
                user.ShowWindow(window[0],9); user.SetForegroundWindow(window[0]); time.sleep(1)
                try:
                    for key in (0x28,0x0D):
                        assert user.GetForegroundWindow()==window[0]
                        extended=1 if key==0x28 else 0
                        user.keybd_event(key,0,extended,0); time.sleep(0.3)
                        user.keybd_event(key,0,extended|2,0); time.sleep(0.3)
                    rect=ctypes.wintypes.RECT(); user.GetWindowRect(window[0],ctypes.byref(rect))
                    image_path=str(folder/'continue_screen.png').replace("'","''")
                    subprocess.run(['powershell','-NoProfile','-Command',
                        f"Add-Type -AssemblyName System.Drawing; $bmp = New-Object System.Drawing.Bitmap({rect.right-rect.left},{rect.bottom-rect.top}); $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen({rect.left},{rect.top},0,0,$bmp.Size); $bmp.Save('{image_path}'); $g.Dispose(); $bmp.Dispose()"],
                        check=True,creationflags=subprocess.CREATE_NO_WINDOW)
                finally:
                    user.ShowWindow(window[0],0); user.SetForegroundWindow(previous)
            else:
                h.command(folder,process,'start_new_run',run_seed=20260904,controller_profile='boss_armor_fixture',build_profile=build)
                enemies=[dict(enemy_id='enemy_boss_core',x=boss[0],z=boss[1])]+list(extra)
                h.command(folder,process,'set_next_stage_layout',layout_id=case,stage_type='boss',difficulty=3,par=7,enemies=enemies)
                h.command(folder,process,'choose_destination',route_index=0)
            initial=h.wait_for(lambda:s if 'fire_shot' in (s:=h.read(folder/'runtime/game_mcp/game_state.json')).get('available_actions',[]) else None,process)
            h.write(folder/'initial.json',initial)
            assert len(initial['break_balls'])==2
            assert initial['boss_state']['armor']==2 and initial['boss_state']['hp']==60
            assert not initial['enemies'][0]['pocket_finisher_eligible']
            yield folder,process,initial
        finally:
            if process.poll() is None:
                window=h.window_for(process)
                if window: ctypes.windll.user32.PostThreadMessageW(window[1],0x0012,0,0)
                assert process.wait(timeout=15)==0
            h.write(folder/'exe.json',{'sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})

def compare(folder):
    expected=h.read(folder/'expected.json'); actual=h.read(folder/'actual.json')
    assert expected['complete'] and expected['world_unchanged'] and not expected['path_truncated'],expected
    for name in ('player_enemy_contacts','enemy_enemy_contacts'): assert expected[name]==actual[name],(name,expected,actual)
    eb={b['id']:b for b in expected['balls']}; ab={b['id']:b for b in actual['balls']}
    assert eb.keys()==ab.keys()
    error=0
    for key,ball in eb.items():
        for name in ('position','velocity'):
            for a,b in zip(ball[name],ab[key][name]):
                error=max(error,abs(a-b)); assert abs(a-b)<1e-5,(name,ball,ab[key])
        for name in ('hp','active','defeated','pocketed','boss','break_ball','break_ball_used','armor','break_shots_remaining','break_started_this_shot'):
            assert ball[name]==ab[key][name],(name,ball,ab[key])
    return dict(matched=True,max_error=error,milliseconds=expected['milliseconds'],ticks=expected['ticks'])

def shot(folder,process,index,direction,power):
    capture=folder/f'shot_{index:02d}'; capture.mkdir()
    before=h.read(folder/'runtime/game_mcp/game_state.json'); h.write(capture/'before.json',before)
    h.command(folder,process,'fire_shot',direction_x=direction[0],direction_z=direction[1],power=power)
    frames=[]; sequence=-1
    def finished():
        nonlocal sequence
        state=h.read(folder/'runtime/game_mcp/game_state.json')
        if not state or state['sequence']==sequence: return None
        sequence=state['sequence']; frames.append(state)
        return state if state['game_state'] in ('aiming_direction','clear_reward','game_over') or state['scene']=='result' else None
    final=h.wait_for(finished,process,60)
    h.write(capture/'frames.json',frames); h.write(capture/'final.json',final)
    for kind in ('expected','actual'):
        shutil.copy2(folder/f'runtime/shot_prediction_{kind}.json',capture/f'{kind}.json')
    result=compare(capture)
    assert final['physics_clock']['substep_limit_count']==0,final['physics_clock']
    # Inactive neutrals do not cause extra enemy attacks or contribute to enemy count.
    for ball in final['break_balls']:
        if not ball['active']: continue
        p=ball['position']; radius=ball['radius']
        for other in [final['player'],*final['enemies'],*final['break_balls']]:
            if other is ball or other.get('pocketed') or other.get('active') is False: continue
            q=other.get('position'); r=other.get('radius')
            if q and r: assert math.hypot(p['x']-q['x'],p['z']-q['z'])>=radius+r-0.001,(ball,other)
    result.update(direction=direction,power=power,boss=final['boss_state'],player_hp=final['player']['current_hp'])
    h.write(capture/'verification.json',result)
    print(folder.name,index,result,flush=True)
    return final

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--label',required=True); parser.add_argument('--build'); args=parser.parse_args()
    for build in ([args.build] if args.build else ['standard','heavy','pierce','bounce','anchor']):
        with game(args.label,build,build) as (folder,process,state):
            shot(folder,process,1,(-4,10),4)

if __name__=='__main__': main()
