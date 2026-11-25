"""Asset regressions for the recorded billiard clack and deterministic SE export."""

from array import array
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import wave


TOOLS = Path(__file__).resolve().parent
ASSETS = TOOLS.parent / "assets" / "audio" / "combat"
SOURCE_HASH = "e2e14a8910278f3559d5f0a86d8e098fb8d7c4f78bc7221e9e93d2beaa9064e7"
UNCHANGED_HASHES = {
    "chain_10.wav": "6539d2512d6af5ec5428b5e344b18caf0cfc9a3c9a96722e03d7be37b9d625dd",
    "chain_15.wav": "74fa546473849b63f9dfd7e922b2714bd0882645363c6943721038b9fa5bd9f7",
    "chain_3.wav": "d35c83f519bdfa46e7d24a6812a41a0207f2110805e7ac7f42eaa9f776a06c4d",
    "chain_6.wav": "ec132d6b23329f2f94701be7235b70aa9bda59d69debef63b3edb41fd1b60bb7",
    "chain_milestone.wav": "d35c83f519bdfa46e7d24a6812a41a0207f2110805e7ac7f42eaa9f776a06c4d",
    "enemy_defeat.wav": "55189f34a5eef8b6b7b72de745eb363ff329efbe0530f48fe1fa5883b5b0c345",
    "wall_impact.wav": "107ebc304d8f32f0cf6c14a4c9d3c48079e8128b9634257c4e589e9b625761f4",
    "player_hurt.wav": "b304a2726217cc0c9802912cec437ef80c0d8eff3362851b078d35906f099aa6",
    "shield_block.wav": "a0aa8a2cc7426c2cd4050bcf155b61edf9a63dc11be1113448de2228a24338e3",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_pcm(path: Path) -> tuple[int, list[float]]:
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, 48000):
            raise AssertionError("Expected 48 kHz / 16-bit / mono PCM")
        values = array("h", source.readframes(source.getnframes()))
        if sys.byteorder != "little":
            values.byteswap()
        return source.getframerate(), [value / 32768.0 for value in values]


def energy(values: list[float]) -> float:
    return sum(value * value for value in values)


class BilliardImpactAudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.shell = shutil.which("pwsh.exe") or shutil.which("pwsh") or shutil.which("powershell.exe")
        if not cls.shell:
            raise unittest.SkipTest("PowerShell is required for the project's SE generator")
        cls.directory = tempfile.TemporaryDirectory(prefix="caromino_billiard_audio_")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.output = Path(cls.directory.name) / "generated"
        cls.generate(cls.output)
        cls.first_hashes = {path.name: sha256(path) for path in cls.output.glob("*.wav")}
        cls.generate(cls.output)

    @classmethod
    def generate(cls, output: Path, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [cls.shell, "-NoProfile", "-NonInteractive", "-File", str(TOOLS / "generate_combat_se.ps1"),
             "-OutputDirectory", str(output), *extra],
            check=True, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )

    def test_regeneration_matches_committed_assets_and_is_repeatable(self):
        self.assertEqual(len(self.first_hashes), 9)
        for name, before in self.first_hashes.items():
            with self.subTest(name=name):
                self.assertEqual(sha256(self.output / name), before)
                self.assertEqual(sha256(ASSETS / name), before)

    def test_other_cues_are_byte_identical(self):
        for name, expected in UNCHANGED_HASHES.items():
            with self.subTest(name=name):
                self.assertEqual(sha256(ASSETS / name), expected)
                if (self.output / name).exists():
                    self.assertEqual(sha256(self.output / name), expected)

    def test_recorded_clacks_have_headroom_short_attack_and_dry_decay(self):
        for name, expected_duration, expected_peak in (
            ("direct_impact.wav", 0.18, 0.74), ("enemy_impact.wav", 0.16, 0.58),
        ):
            with self.subTest(name=name):
                rate, values = read_pcm(self.output / name)
                self.assertAlmostEqual(len(values) / rate, expected_duration, places=5)
                peak_index = max(range(len(values)), key=lambda index: abs(values[index]))
                peak = abs(values[peak_index])
                self.assertAlmostEqual(peak, expected_peak, delta=0.002)
                self.assertLess(peak, 0.8)
                self.assertLess(peak_index / rate, 0.01)
                self.assertGreater(energy(values[:int(rate * 0.04)]), energy(values) * 0.75)
                self.assertLess(energy(values[-int(rate * 0.02):]), energy(values[:int(rate * 0.02)]) * 0.01)
                self.assertLess(abs(sum(values) / len(values)), 0.002)
                self.assertEqual(values[0], 0.0)
                self.assertEqual(values[-1], 0.0)

    def test_direct_and_enemy_keep_the_same_recorded_material(self):
        rate, direct = read_pcm(self.output / "direct_impact.wav")
        _, enemy = read_pcm(self.output / "enemy_impact.wav")
        count = int(rate * 0.14)
        first, second = direct[:count], enemy[:count]
        correlation = sum(a * b for a, b in zip(first, second)) / math.sqrt(energy(first) * energy(second))
        self.assertGreater(correlation, 0.995)

    def test_source_and_manifest_keep_public_preview_provenance(self):
        self.assertEqual(sha256(ASSETS / "source" / "billiard_ball_clack_539854.wav"), SOURCE_HASH)
        manifest = json.loads((self.output / "manifest.json").read_text(encoding="utf-8-sig"))
        for entry in manifest[:2]:
            self.assertEqual(entry["author"], "Za-Games")
            self.assertEqual(entry["license"], "CC0 1.0 Universal")
            self.assertEqual(entry["source_pcm_sha256"], SOURCE_HASH)
            self.assertIn("preview", entry["source_origin"].lower())
            self.assertIn("original WAV not downloaded", entry["source_origin"])
            self.assertTrue(entry["source_page"].startswith("https://freesound.org/"))

    def test_changed_source_is_rejected_before_export(self):
        altered = Path(self.directory.name) / "altered.wav"
        changed = bytearray((ASSETS / "source" / "billiard_ball_clack_539854.wav").read_bytes())
        changed[-10] ^= 1
        altered.write_bytes(changed)
        output = Path(self.directory.name) / "rejected"
        with self.assertRaises(subprocess.CalledProcessError):
            self.generate(output, "-BilliardSourcePath", str(altered))
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
