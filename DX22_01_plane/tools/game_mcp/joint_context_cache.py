"""Per-server memoization of pure joint evaluations; no game-state cache."""
from collections import OrderedDict
import copy
import hashlib
import json

IGNORED_TOP_LEVEL = frozenset({
    'sequence','published_at_unix_ms','aiming_performance','frame_timing',
    'physics_clock','bridge','audio_feedback','collision_vfx',
    'battle_progress_diagnostics','mcp_control','build_decision',
    'build_shot_choices','shot_tactics',
})

def _input_value(value, top=False):
    if isinstance(value,dict):
        if any(not isinstance(k,str) for k in value):raise TypeError('JSON keys required')
        return {k:_input_value(v) for k,v in value.items()
                if not top or k not in IGNORED_TOP_LEVEL}
    if isinstance(value,list):return [_input_value(v) for v in value]
    if value is None or isinstance(value,(str,int,float,bool)):return value
    raise TypeError('JSON inputs required')

class JointContextCache:
    def __init__(self,capacity=4):
        self.capacity=max(1,int(capacity))
        self.entries=OrderedDict()
        self.hits=self.misses=0

    def evaluate(self,state,profile,compute):
        try:
            # Preserve dictionary order and every unexcluded input value.
            normalized=_input_value(state,True)
            # attach_ball_aim_guidance only decorates these ball records.
            for collection in ('offered_balls','deck_balls','catalog_balls'):
                for ball in normalized.get(collection,[]):
                    if isinstance(ball,dict):ball.pop('aim_guidance',None)
            payload=json.dumps([normalized,_input_value(profile)],
                               ensure_ascii=False,separators=(',',':'),allow_nan=False)
            key=(id(compute),hashlib.sha256(payload.encode('utf-8')).digest())
        except (TypeError,ValueError,RecursionError):
            return compute(state,profile)
        if key in self.entries:
            self.hits+=1
            self.entries.move_to_end(key)
            return copy.deepcopy(self.entries[key])
        self.misses+=1
        value=compute(state,profile)
        self.entries[key]=copy.deepcopy(value)
        while len(self.entries)>self.capacity:self.entries.popitem(last=False)
        return value


class TacticalContextCache(JointContextCache):
    """Reuse only pure tactics; always stamp the caller's fresh state sequence."""
    def evaluate(self,state,profile,compute):
        value=super().evaluate(state,profile,compute)
        if isinstance(value,dict) and 'state_sequence' in value:
            value['state_sequence']=state.get('sequence')
        return value
