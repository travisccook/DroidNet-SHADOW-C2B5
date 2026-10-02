#!/usr/bin/env python3
"""This tree's mapping code against the goldens recorded from the firmware it replaces
(tools/shadowmap/transcripts.py): every byte SHADOW sends, every "!!IDLE" clock, and the EEPROM
image each session leaves.

    python3 -m unittest tools/shadowmap/test_transcripts.py      (from the repo root)
"""

import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import transcripts as tx  # noqa: E402

GOLDEN = os.path.join(HERE, "golden", "v3")

_SIM = None
_TMP = None


def sim() -> str:
    global _SIM, _TMP
    if _SIM is None:
        _TMP = tempfile.mkdtemp(prefix="shadow_tx_sim_")
        _SIM = tx.build_sim(tx.ROOT, _TMP)
    return _SIM


def tearDownModule():
    if _TMP is not None:
        shutil.rmtree(_TMP, ignore_errors=True)


def golden(tag: str) -> str:
    with open(os.path.join(GOLDEN, tag + ".out"), encoding="latin-1", newline="\n") as f:
        return f.read()


def golden_eeprom() -> dict:
    with open(os.path.join(GOLDEN, "eeprom.json")) as f:
        return json.load(f)


class TranscriptTest(unittest.TestCase):
    maxDiff = None

    def test_the_scripts_are_the_recorded_ones(self):
        for name, make in tx.SESSIONS.items():
            with open(os.path.join(GOLDEN, name + ".in"), encoding="latin-1", newline="\n") as f:
                self.assertEqual(f.read(), "".join(line + "\n" for line in make()), name)

    def test_byte_identical(self):
        eeprom = golden_eeprom()
        for name, make in tx.SESSIONS.items():
            for paced in (0, tx.PACED):
                tag = name + (".paced" if paced else "")
                with self.subTest(session=tag):
                    out, sha = tx.run_session(sim(), make(), paced)
                    self.assertEqual(golden(tag), out)
                    self.assertEqual(eeprom[tag], sha, "EEPROM image")


if __name__ == "__main__":
    unittest.main()
