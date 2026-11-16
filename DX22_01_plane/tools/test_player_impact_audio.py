import hashlib
from pathlib import Path
import tempfile
import unittest
import wave
from generate_player_impact_se import generate

class PlayerImpactAudioTests(unittest.TestCase):
    def test_cues_are_distinct_valid_pcm_and_reproducible(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            generate(root)
            before = {}
            for name in ('player_hurt', 'shield_block'):
                path = root / (name + '.wav')
                before[name] = hashlib.sha256(path.read_bytes()).digest()
                with wave.open(str(path), 'rb') as source:
                    self.assertEqual((source.getnchannels(), source.getsampwidth(), source.getframerate()),
                                     (1, 2, 48000))
                    self.assertGreater(source.getnframes(), 4000)
            self.assertNotEqual(before['player_hurt'], before['shield_block'])
            generate(root)
            for name in before:
                self.assertEqual(before[name], hashlib.sha256((root/(name+'.wav')).read_bytes()).digest())
