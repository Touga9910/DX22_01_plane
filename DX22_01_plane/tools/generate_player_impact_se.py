"""Generate project-original hit/block PCM cues; no external sound sources."""
import math
import struct
import wave
from pathlib import Path

def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for name, frequency, duration in [('player_hurt', 115, .13), ('shield_block', 920, .11)]:
        samples = []
        for i in range(int(48000 * duration)):
            t = i / 48000
            envelope = min(1, t / .004) * (1 - t / duration) ** 2
            phase = 2 * math.pi * frequency * t
            value = (math.sin(phase * (1 - .3*t/duration)) + .3*math.sin(phase*2.1)) * envelope*.48
            samples.append(struct.pack('<h', int(max(-1, min(1, value))*32767)))
        with wave.open(str(directory / (name + '.wav')), 'wb') as output:
            output.setparams((1, 2, 48000, 0, 'NONE', 'not compressed'))
            output.writeframes(b''.join(samples))

if __name__ == '__main__':
    generate(Path(__file__).resolve().parents[1] / 'assets/audio/combat')
