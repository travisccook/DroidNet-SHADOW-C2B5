#!/usr/bin/env python3
"""This tree's mapping code against the goldens recorded from 0a8b73f (tools/shadowmap/transcripts.py).

Up to the Serial1 adapter every byte matched, "!!IDLE" clocks included. Timing round 2's pass
rule (WIPDocs/TIMING_ROUND_2_DESIGN.md, section 3) moves the mapping's work after the motors and
splits it across passes: a pass where a press fired reads no request and runs no background unit
(protocol service, announce, battery report); the first pass without a press after one that
handled a request runs the background unit; any other pass handles at most one request and, if
it handled none, runs the background unit. Work that gives way waits one pass. What SHADOW sends
is checked this way:

- every session leaves the same EEPROM image;
- each stream (replies, events, battery reports, commands, announces) is the same sequence of
  lines, in every session but where two things meet in one pass (coincide), and there the only
  differences are the documented ones (COINCIDE);
- without the time-driven lines (battery reports and announces, which land wherever their time
  falls), a session with one thing at a time is the same sequence of lines as before;
- a GET's round trip is at most two passes longer (unless a battery report or announce drains in
  its window, see test_round_trips); a SUM's stays under 0.5 s.

The documented differences from 0a8b73f, each tied to the script line that causes it:

1. a press's command or PRESS event now goes before a reply, where a request landed in the press's
   pass (the per-stream comparison needs no patch for it);
2. a SET in a press's pass gives that press the slot's previous key (COINCIDE);
3. a PRESS event that 0a8b73f dropped for want of room is now sent, or the reverse, where a request
   or a full TX queue met a press (events are best-effort) (COINCIDE);
4. a READY sent in the first pass after a (re)boot names the controllers that pass sees, where
   0a8b73f, reading the request first, said "pads=-" (normalize); likewise a controller's
   "@SM CTL" event when "@SM EVENTS 1" arrives in the pass that controller connects (buttons() and
   setPads now run before the request; no session here hits it);
5. a button command that 0a8b73f dropped for want of TX-queue room is now sent later, in order
   (no session here hits it);
6. a request queued behind one that can't be answered yet is read and dropped: 0a8b73f's rule,
   which 0a8b73f also hits at 9600 baud (the paced run); the Pi sends one request at a time and
   resends on silence (COINCIDE);
7. a second request already in the RX buffer waits one pass (design section 3), so a background
   line (here a DUMP line) can go before its reply (COINCIDE and COINCIDE_PACED).

Every session runs at once and paced at 2 bytes a pass.

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

GOLDEN = os.path.join(HERE, "golden", "v3")
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


def idle_windows(text: str):
    """(how long, the lines sent) for each script line, between "!!IDLE" lines."""
    out, prev, sent = [], 0, []
    for line in text.split("\n"):
        m = re.match(r"^!!IDLE (\d+)", line)
        if m:
            out.append((int(m.group(1)) - prev, sent))
            prev, sent = int(m.group(1)), []
        elif line:
            sent.append(line)
    return out


def timed_line_in(lines) -> bool:
    """True when a battery report or an announce (lines sent on a timer, not in reply) is among them."""
    return any(line.startswith("@WDP") or re.match(r"^(;W\d+,?)?;S0@SM BAT ", line) for line in lines)


# The documented differences in the coincide session, not paced (the script lines are
# tools/shadowmap/golden/v3/coincide.in's, from 1). Where a request lands in the pass a press fires:
#   - a SET taken in during a press's pass used to be taken in before the press, which then sent
#     the new key; the press now comes first and sends the slot's previous key, and the SET takes
#     effect a pass later: line 7 (SET F NONE LEFT) meeting line 8 (press LEFT), and line 9
#     (SET F NONE DOWN) meeting line 10 (hold DOWN): commands 1 and 2, events 1 and 2;
#   - a reply used to take the room a PRESS event needed, and 0a8b73f dropped the event (events
#     are best-effort); the press now queues its event first: line 14 (press DOWN, with line 12's
#     HELLO and line 13's GET waiting) and line 18 (hold L1+UP, with lines 15-17's DUMP, GET and
#     SET waiting): the two events inserted at 3.
# Where requests queue behind a DUMP (lines 15-17, all three waiting when line 18's press fires):
#   - category 6: the press pass reads nothing; the DUMP (15) is handled the pass after and its
#     first line fills the TX queue in the background pass owed after it; the GET (16) then can't
#     be answered yet, so the bytes behind it, the whole SET F NONE RIGHT (17), are read and
#     dropped. 0a8b73f had handled all three in one pass with the queue empty and answered the SET
#     "ERR busy" (the DUMP was running); now that reply isn't sent;
#   - category 7: the GET (16) waits behind the DUMP's background pass, so its reply now follows
#     the DUMP's first line (SLOT F NONE UP) instead of going before it.
COINCIDE = {
    "commands": [("replace", 1, ";W1;Cscaredsound"), ("replace", 2, ";W1;Cangrysound")],
    "events": [
        ("replace", 1, reply("@SM PRESS F NONE LEFT scaredsound default")),
        ("replace", 2, reply("@SM PRESS F NONE DOWN angrysound default")),
        ("insert", 3, [reply("@SM PRESS F NONE DOWN pressed eeprom"), reply("@SM PRESS F L1 UP extremehappysou default")]),
    ],
    "replies": [
        ("drop", 9, reply("@SM ERR busy SET F NONE RIGHT")),  # line 17 (category 6)
        ("move", 8, 9, reply("@SM SLOT D L1 L3 frontholocenter default def=frontholocenter")),  # line 16 (category 7)
    ],
}

# The coincide session paced at 2 bytes a pass. Category 7: while the GET on line 16 waits for room
# (the DUMP from line 15 is running), a pass that couldn't answer it is followed by a background
# pass, so when room frees up the DUMP's second line (SLOT F NONE RIGHT) can take it first; 0a8b73f,
# reading requests first every pass, answered the GET between the DUMP's first and second lines.
# (The SET on line 17 is dropped here by 0a8b73f too: no category 6 patch.)
COINCIDE_PACED = {
    "replies": [
        ("move", 8, 9, reply("@SM SLOT D L1 L3 frontholocenter default def=frontholocenter")),  # line 16 (category 7)
    ],
}


def patched(lines, patches):
    """The golden lines with `patches` applied in order. "drop" and "move" name the line they act on,
    so a patch can't quietly hit a different line:
      ("replace", i, line)       line i becomes `line`
      ("insert", i, [lines])     `lines` go in before line i
      ("drop", i, line)          line i, which must be `line`, goes
      ("move", i, j, line)       line i, which must be `line`, is taken out and put back at j"""
    out = list(lines)
    for p in patches:
        op, at = p[0], p[1]
        if op == "replace":
            out[at] = p[2]
        elif op == "insert":
            out[at:at] = p[2]
        elif op == "drop":
            assert out[at] == p[2], (op, at, out[at])
            del out[at]
        elif op == "move":
            assert out[at] == p[3], (op, at, out[at])
            out.insert(p[2], out.pop(at))
        else:
            raise ValueError(op)
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
                    if name == "coincide":
                        patches = COINCIDE_PACED if paced else COINCIDE
                        if k in patches:
                            g = patched(g, patches[k])
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
                before, after = idle_windows(golden(tag)), idle_windows(run(name, paced)[0])
                self.assertEqual(len(script), len(after), tag)
                for line, (a, sent_a), (b, sent_b) in zip(script, before, after):
                    # A GET whose window, in either run, also drains a battery report or an announce
                    # isn't timed: those go out on a timer, and the window measures their drain too.
                    # The GET's own reply time is unchanged there, but Task 2's cap of 4 EEPROM reads
                    # a step makes earlier writes a few passes longer, so the periodic battery report
                    # drains in a different script line's window (requests, line 73: +50 ms, line 74:
                    # -50 ms). Every other GET keeps the two-pass limit.
                    if line.startswith("@SM GET ") and not (timed_line_in(sent_a) or timed_line_in(sent_b)):
                        with self.subTest(session=tag, line=line):
                            self.assertLessEqual(b - a, 2 * tx.PASS_MS)  # at most two passes longer
                    if line.startswith("@SM SUM *"):
                        with self.subTest(session=tag, line=line):
                            self.assertLess(b, 500)


if __name__ == "__main__":
    unittest.main()
