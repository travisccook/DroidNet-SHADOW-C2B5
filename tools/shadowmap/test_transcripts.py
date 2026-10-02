#!/usr/bin/env python3
"""This tree's mapping code against the goldens recorded from 0a8b73f (tools/shadowmap/transcripts.py).

Up to the Serial1 adapter every byte matched, "!!IDLE" clocks included. Since the per-pass rule
(a request is taken in after the presses, at most one heavy thing a pass besides them, a SLOT
reply read on one pass and sent on a later one), what SHADOW sends is checked this way:

- every session leaves the same EEPROM image;
- each stream (replies, events, battery reports, commands, announces) is the same sequence of
  lines, in every session but where two things meet in one pass (coincide), and there the only
  differences are the documented ones (COINCIDE);
- without the time-driven lines (battery reports and announces, which land wherever their time
  falls), a session with one thing at a time is the same sequence of lines as before;
- a GET's round trip is at most two passes longer; a SUM's stays under 0.5 s.

One difference applies to every session: a READY sent in the first pass after a (re)boot names the
controllers that pass sees (presses are read before requests now); 0a8b73f, reading the request
first, said "pads=-". Every session runs at once and paced at 2 bytes a pass.

    python3 -m unittest tools/shadowmap/test_transcripts.py      (from the repo root)
"""

import json
import os
import re
import shutil
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import transcripts as tx  # noqa: E402

GOLDEN = os.path.join(HERE, "golden", "v4")
ROUTE = ";W20,;S0"  # the coincide session's reply route (HELLO 20)
STREAMS = ("replies", "events", "bat", "commands", "announces")

_SIM = None
_TMP = None
_RUNS = {}


def sim() -> str:
    global _SIM, _TMP
    if _SIM is None:
        _TMP = tempfile.mkdtemp(prefix="shadow_tx_sim_")
        _SIM = tx.build_sim(tx.ROOT, _TMP)
    return _SIM


def run(name: str, paced: int):
    key = (name, paced)
    if key not in _RUNS:
        _RUNS[key] = tx.run_session(sim(), tx.SESSIONS[name](), paced)
    return _RUNS[key]


def tearDownModule():
    if _TMP is not None:
        shutil.rmtree(_TMP, ignore_errors=True)


def golden(tag: str) -> str:
    with open(os.path.join(GOLDEN, tag + ".out"), encoding="latin-1", newline="\n") as f:
        return f.read()


def reply(body: str, route: str = ROUTE) -> str:
    return "%s%s *%08X" % (route, body, zlib.crc32(body.encode("ascii")) & 0xFFFFFFFF)


def kind(line: str) -> str:
    if line.startswith("@WDP"):
        return "announces"
    m = re.match(r"^(;W\d+,?)?;S0(@SM .*)$", line)
    if not m:
        return "commands"
    body = m.group(2)
    if body.startswith(("@SM PRESS", "@SM CTL")):
        return "events"
    if body.startswith("@SM BAT"):
        return "bat"
    return "replies"


def ready_pads(line: str) -> str:
    """A READY line with its pads= and CRC masked (the first-pass-after-boot difference)."""
    return re.sub(r" pads=\S+ \*[0-9A-F]{8}$", " pads=? *????????", line)


def lines_of(text: str):
    return [line for line in text.split("\n") if line and not line.startswith("!!")]


def normalize(golden_lines, new_lines):
    """Masks pads= on READY lines where the golden one says pads=- (see the module docstring)."""
    g, n = list(golden_lines), list(new_lines)
    for i, line in enumerate(g):
        if "@SM READY " in line and " pads=- " in line and i < len(n) and ready_pads(n[i]) == ready_pads(line):
            g[i] = n[i] = ready_pads(line)
    return g, n


def streams(text: str):
    out = {k: [] for k in STREAMS}
    for line in lines_of(text):
        out[kind(line)].append(line)
    return out


def idle_blocks(text: str):
    """How long each script line took (the clock between "!!IDLE" lines)."""
    out, prev = [], 0
    for line in text.split("\n"):
        m = re.match(r"^!!IDLE (\d+)", line)
        if m:
            out.append(int(m.group(1)) - prev)
            prev = int(m.group(1))
    return out


# The documented differences where a request lands in the pass a press fires (coincide, not paced):
#   - a SET taken in during a press's pass used to be taken in before the press, which then sent
#     the new key; the press now comes first and sends the slot's previous key (as if the SET had
#     arrived a pass later): F NONE LEFT and F NONE DOWN;
#   - a reply used to take the room a PRESS event needed, and the event was dropped; the press now
#     queues its event first: F NONE DOWN (HELLO and GET waiting) and F L1 UP (DUMP, GET and SET).
COINCIDE = {
    "commands": [("replace", 1, ";W1;Cscaredsound"), ("replace", 2, ";W1;Cangrysound")],
    "events": [
        ("replace", 1, reply("@SM PRESS F NONE LEFT scaredsound default")),
        ("replace", 2, reply("@SM PRESS F NONE DOWN angrysound default")),
        ("insert", 3, [reply("@SM PRESS F NONE DOWN pressed eeprom"), reply("@SM PRESS F L1 UP extremehappysou default")]),
    ],
}


def patched(lines, patches):
    out = list(lines)
    for op, at, value in sorted(patches, key=lambda p: -p[1]):
        if op == "replace":
            out[at] = value
        else:
            out[at:at] = value
    return out


class TranscriptTest(unittest.TestCase):
    maxDiff = None

    def test_the_scripts_are_the_recorded_ones(self):
        for name, make in tx.SESSIONS.items():
            with open(os.path.join(GOLDEN, name + ".in"), encoding="latin-1", newline="\n") as f:
                self.assertEqual(f.read(), "".join(line + "\n" for line in make()), name)

    def test_the_same_eeprom_image(self):
        with open(os.path.join(GOLDEN, "eeprom.json")) as f:
            want = json.load(f)
        for name in tx.SESSIONS:
            for paced in (0, tx.PACED):
                tag = name + (".paced" if paced else "")
                with self.subTest(session=tag):
                    self.assertEqual(want[tag], run(name, paced)[1])

    def test_each_stream_is_the_same(self):
        for name in tx.SESSIONS:
            for paced in (0, tx.PACED):
                tag = name + (".paced" if paced else "")
                want, got = streams(golden(tag)), streams(run(name, paced)[0])
                for k in STREAMS:
                    g, n = normalize(want[k], got[k])
                    if name == "coincide" and not paced and k in COINCIDE:
                        g = patched(g, COINCIDE[k])
                    with self.subTest(session=tag, stream=k):
                        self.assertEqual(g, n)

    def test_one_activity_sessions_keep_their_order(self):
        for name in tx.ONE_ACTIVITY:
            for paced in (0, tx.PACED):
                tag = name + (".paced" if paced else "")
                timed = ("bat", "announces")
                g = [line for line in lines_of(golden(tag)) if kind(line) not in timed]
                n = [line for line in lines_of(run(name, paced)[0]) if kind(line) not in timed]
                g, n = normalize(g, n)
                with self.subTest(session=tag):
                    self.assertEqual(g, n)

    def test_round_trips(self):
        for name, make in tx.SESSIONS.items():
            script = [line for line in make() if not line.startswith("!!QUEUE")]
            for paced in (0, tx.PACED):
                tag = name + (".paced" if paced else "")
                before, after = idle_blocks(golden(tag)), idle_blocks(run(name, paced)[0])
                self.assertEqual(len(script), len(after), tag)
                for line, a, b in zip(script, before, after):
                    if line.startswith("@SM GET "):
                        with self.subTest(session=tag, line=line):
                            self.assertLessEqual(b - a, 2 * tx.PASS_MS)  # at most two passes longer
                    if line.startswith("@SM SUM *"):
                        with self.subTest(session=tag, line=line):
                            self.assertLess(b, 500)


if __name__ == "__main__":
    unittest.main()
