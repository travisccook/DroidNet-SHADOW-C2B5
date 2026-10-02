#!/usr/bin/env python3
"""Session transcripts: what SHADOW's mapping sends, byte for byte, for a fixed set of scripted
sessions, recorded from shadow_sim so a change to the firmware's mapping code can be checked
against the firmware it replaces.

Each session is a shadow_sim script (stdin): @SM requests with their CRCs, button presses and
holds, battery bytes, power cuts and reboots, and requests queued to land during a press. A
golden is shadow_sim's stdout for it (every line SHADOW writes to Serial1, and the "!!IDLE <ms>"
clock after each script line) plus the SHA-256 of the EEPROM image it leaves. Every session runs
twice: with input there at once, and paced at 2 bytes a loop pass (about 9600 baud).

    python3 tools/shadowmap/transcripts.py --record golden/v3    record goldens from this tree
    python3 -m unittest tools/shadowmap/test_transcripts.py      compare this tree with them
"""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import zlib
from typing import Dict, List, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# HeldBit masks (lib/ShadowMap/src/sm_resolve.h)
UP, RIGHT, DOWN, LEFT, L3, PS, L1, L2, CIRCLE, CROSS = (1 << i for i in range(10))

PACED = 2  # bytes a pass in the paced runs
PASS_MS = 2  # shadow_sim's loop pass


def req(body: str) -> str:
    """An @SM request as SHADOW receives it, CRC added."""
    return "%s *%08X" % (body, zlib.crc32(body.encode("ascii")) & 0xFFFFFFFF)


def hexs(text: str) -> str:
    return text.encode("ascii").hex().upper()


def press(foot: int = 0, dome: int = 0, foot_on: int = 1, dome_on: int = 1) -> str:
    return f"!!PRESS {foot:x} {dome:x} {foot_on} {dome_on}"


def hold(ms: int, foot: int = 0, dome: int = 0) -> str:
    return f"!!HOLD {foot:x} {dome:x} {ms}"


def tick(ms: int) -> str:
    return f"!!TICK {ms}"


def queue(body: str) -> str:
    return "!!QUEUE " + req(body)


K15 = "zyxwvutsrqponml"
D15 = "abcdefghijklmno"


def s_requests() -> List[str]:
    """Every verb, its replies and its errors, one at a time."""
    lines = [
        req("@SM HELLO"),
        req("@SM CFG"),
        req("@SM BAT"),
        req("@SM GET F NONE UP"),
        req("@SM GET F L1 UP"),
        req("@SM GET D L1 L3"),
        req("@SM GET F L1 L3"),  # reserved: overspeed
        req("@SM GET D NONE PS"),  # psgate
        req("@SM GET F NONE L32"),  # stick
        req("@SM GET F CIRCLE RIGHTL"),
        req("@SM GET D CIRCLE RIGHT2"),
        req("@SM SET F CIRCLE RIGHTL " + K15),
        req("@SM GET F CIRCLE RIGHTL"),
        req("@SM SET D CIRCLE RIGHT " + D15),
        req("@SM SET F NONE UP -"),
        req("@SM GET F NONE UP"),
        req("@SM SET F NONE UP2 doubled"),
        req("@SM GET F NONE UP2"),
        req("@SM REVERT F NONE UP"),
        req("@SM GET F NONE UP"),
        req("@SM REVERT F CIRCLE RIGHTL"),
        req("@SM GET F CIRCLE RIGHTL"),
        req("@SM PREFIX " + hexs(";W12,;Cabcd")),
        req("@SM SUFFIX " + hexs("abcdefg")),
        req("@SM CFG"),
        req("@SM HELLO 20"),
        req("@SM GET D CIRCLE RIGHT"),
        req("@SM HELLO 7"),
        req("@SM CFG"),
        req("@SM SUFFIX -"),
        req("@SM PREFIX " + hexs(";W1;C")),
        req("@SM SUM"),
        req("@SM DEFAULTS"),
        req("@SM GET D CIRCLE RIGHT"),
        req("@SM SUM"),
        req("@SM HELLO 0"),
        req("@SM EVENTS 1"),
        req("@SM EVENTS 0"),
        # errors
        req("@SM SET F L1 L3 x"),  # reserved
        req("@SM SET F NONE UP bad-key"),
        req("@SM SET F NONE UP abcdefghijklmnop"),  # 16 characters
        req("@SM SET F NONE UPX x"),
        req("@SM SET F"),
        req("@SM REVERT F L1 L3"),
        req("@SM GET F CIRCLE RIGHTL x"),
        req("@SM GET"),
        req("@SM NOPE"),
        req("@SM ABCDEFGHIJ abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg"),
        req("@SM ABCDEFGHIJK"),  # over-long verb: not echoed
        req("@SM lower"),
        req("@SM HELLO 21"),
        req("@SM HELLO x"),
        req("@SM HELLO 1 2"),
        req("@SM BAT x"),
        req("@SM CFG x"),
        req("@SM EVENTS 2"),
        req("@SM EVENTS"),
        req("@SM DUMP x"),
        req("@SM SUM x"),
        req("@SM DEFAULTS x"),
        req("@SM PREFIX 3"),
        req("@SM PREFIX -"),
        req("@SM PREFIX 3B5E"),  # '^'
        req("@SM PREFIX 3B543B"),  # ";T;"
        req("@SM PREFIX " + hexs(";W1;Cabcdefg")),  # 12 characters
        req("@SM SUFFIX 00"),
        req("@SM SUFFIX " + hexs("abcdefgh")),
        req("@SM SUFFIX 3B74"),
        req("@SM  GET F NONE UP"),  # double space
        req("@SM GET F NONE UP "),  # trailing space
        req("@SM GET F NONE UP a b c d"),  # 8 tokens
        "@SM GET F NONE UP *00000000",  # bad CRC
        "@SM GET F NONE UP",  # no CRC on the WCB port
        "@SM GET F NONE UP *1234567",  # 7 digits
        "@SM GET F NONE UP *123456789",  # 9 digits
        "@SM GET F NONE UP *1234567G",
        "@SM GET F NONE UP * *ABCDEF01",
        "@SM GET F NONE UP *" + ("%08x" % (zlib.crc32(b"@SM GET F NONE UP") & 0xFFFFFFFF)),  # lower case: fine
        "@SM *00000000",
        "@SM " + "x" * 68,  # 72 characters: the longest kept
        "@SM " + "x" * 69,  # 73: dropped as noise
        "random chatter from another board",
        "\x01\x02@SM HELLO *XXXXXXXX",
        "@SX HELLO",
        req("@SM HELLO"),
    ]
    return lines


def s_dump() -> List[str]:
    """DUMP and SUM with the longest lines the firmware can make."""
    return [
        req("@SM HELLO 20"),
        req("@SM PREFIX " + hexs(";W12,;Cabcd")),
        req("@SM SUFFIX " + hexs("abcdefg")),
        req("@SM SET F CIRCLE RIGHTL " + K15),
        req("@SM SET D CIRCLE RIGHT " + K15),
        req("@SM SET D L1 L3 " + K15),
        req("@SM SET F L1 UP " + K15),
        req("@SM SET F NONE UP2 " + K15),
        req("@SM SET F L2 PS -"),
        req("@SM DUMP"),
        req("@SM SUM"),
        req("@SM REVERT D L1 L3"),
        req("@SM SUM"),
        req("@SM DEFAULTS"),
        req("@SM DUMP"),
        req("@SM SUM"),
    ]


def s_presses() -> List[str]:
    """Button presses with EVENTS on: singles, double taps, long presses, modifiers from the other
    controller, unmapped and reserved combos, drops and faults."""
    lines = [req("@SM HELLO"), req("@SM EVENTS 1")]
    for foot, dome in [
        (UP, 0),
        (L1 | UP, 0),
        (L1 | RIGHT, 0),
        (L2 | PS, 0),
        (PS, 0),  # reserved
        (L1 | L3, 0),  # reserved
        (CIRCLE | RIGHT, 0),
        (RIGHT, CIRCLE),  # from=D
        (CIRCLE, RIGHT),  # from=F
        (0, L1 | L3),
        (0, CROSS | DOWN),
        (CROSS, DOWN),
        (PS | CROSS, 0),
        (L2 | LEFT, UP),
        (0, L2 | LEFT),
        (DOWN, DOWN),
        (L3, 0),  # unmapped
    ]:
        lines.append(press(foot, dome))
    lines += [
        req("@SM SET F NONE UP2 dbl"),
        req("@SM SET F NONE UPL lng"),
        req("@SM SET D CIRCLE RIGHT2 " + K15),
        req("@SM SET F CIRCLE RIGHTL " + K15),
        # double tap of F NONE UP
        hold(40, UP),
        tick(60),
        hold(40, UP),
        tick(1100),
        # long press of F NONE UP
        hold(700, UP),
        tick(1100),
        # single press of F NONE UP: released before the long press, after the double-tap window
        hold(400, UP),
        tick(1100),
        # a quick single that waits out the double-tap window
        hold(40, UP),
        tick(1100),
        # double tap of D CIRCLE RIGHT with CIRCLE on the foot
        hold(40, CIRCLE, RIGHT),
        tick(60),
        hold(40, CIRCLE, RIGHT),
        tick(1100),
        # long press of F CIRCLE RIGHT with CIRCLE on the dome
        hold(700, RIGHT, CIRCLE),
        tick(1100),
        # a third tap right after a double tap
        hold(40, UP),
        tick(60),
        hold(40, UP),
        tick(60),
        hold(40, UP),
        tick(1100),
        # held past the repeat second (legacy combo, fires again)
        hold(2600, DOWN),
        tick(1100),
        # held double-tap combo past the window: repeats
        hold(2600, UP | L2),
        tick(1100),
        # controller drops mid-press
        press(UP, 0, 0, 1),
        press(0, UP, 1, 0),
        press(L1 | UP, 0, 1, 0),
        req("@SM EVENTS 0"),
        press(UP, 0),
        req("@SM EVENTS 1"),
        press(UP, 0),
        tick(2000),
    ]
    return lines


def s_battery() -> List[str]:
    """Battery reports: first, changes, the 30-second heartbeat, and requests among them."""
    return [
        req("@SM HELLO"),
        "!!BATT 05 05",
        tick(6000),
        "!!BATT 02 05",
        tick(4000),
        req("@SM BAT"),
        "!!BATT 01 01",
        tick(4000),
        "!!BATT ee ef",
        tick(31000),
        req("@SM BAT"),
        "!!BATT 07 00",
        tick(3000),
        press(UP, 0, 0, 0),
        tick(3000),
        "!!BATT 03 04",
        press(UP, UP),
        tick(35000),
        req("@SM BAT"),
    ]


def s_power() -> List[str]:
    """Power cuts during writes, reboots, and what reads back."""
    lines = [req("@SM HELLO"), req("@SM SET F NONE LEFT firstvalue")]
    for cut in (0, 1, 3, 7, 12, 17, 18, 40):
        lines += [
            f"!!CUT {cut}",
            req("@SM SET F NONE LEFT " + K15),
            "!!REBOOT",
            req("@SM HELLO"),
            req("@SM GET F NONE LEFT"),
            req("@SM SET F NONE LEFT firstvalue"),
        ]
    lines += [
        "!!CUT 5",
        req("@SM DEFAULTS"),
        "!!REBOOT",
        req("@SM HELLO"),
        req("@SM SUM"),
        "!!CUT 2",
        req("@SM PREFIX " + hexs(";W2;C")),
        "!!REBOOT",
        req("@SM CFG"),
        req("@SM SET F NONE UPL held"),
        "!!REBOOT",
        req("@SM GET F NONE UPL"),
        hold(700, UP),
        tick(1100),
        "!!CUT 4",
        req("@SM REVERT F NONE UPL"),
        "!!REBOOT",
        req("@SM GET F NONE UPL"),
        req("@SM SUM"),
    ]
    return lines


def s_announce() -> List[str]:
    """The device announce, and the way it waits for room behind a DUMP."""
    return [
        tick(4000),
        tick(30000),
        req("@SM DUMP"),
        tick(30000),
        req("@SM HELLO"),
    ]


def s_coincide() -> List[str]:
    """Requests that land while a combo is pressed or held, and while writes are in flight: the
    cases where one loop pass has more than one thing to do."""
    return [
        req("@SM HELLO 20"),
        req("@SM EVENTS 1"),
        req("@SM SET F NONE UPL " + K15),
        queue("@SM GET F CIRCLE RIGHTL"),
        hold(700, UP),
        tick(1100),
        queue("@SM SET F NONE LEFT " + K15),
        press(LEFT, 0),
        queue("@SM SET F NONE DOWN pressed"),
        hold(40, DOWN),
        tick(1100),
        queue("@SM HELLO"),
        queue("@SM GET F NONE DOWN"),
        press(DOWN, 0),
        queue("@SM DUMP"),
        queue("@SM GET D L1 L3"),
        queue("@SM SET F NONE RIGHT busy"),
        hold(40, L1 | UP),
        tick(500),
        hold(40, DOWN),
        tick(20000),
        queue("@SM SUM"),
        hold(40, UP | L1),
        tick(100),
        "!!BATT 05 05",
        tick(6000),
        queue("@SM BAT"),
        queue("@SM CFG"),
        hold(800, UP),
        tick(1100),
        queue("@SM DEFAULTS"),
        hold(40, LEFT),
        tick(1100),
        req("@SM SUM"),
    ]


def s_shipped() -> List[str]:
    """A plain session against the built-in defaults: what the droid does most."""
    lines = [req("@SM HELLO"), req("@SM SUM"), req("@SM DUMP")]
    for foot in (UP, LEFT, RIGHT, DOWN, L1 | UP, L2 | DOWN, CIRCLE | LEFT, CROSS | RIGHT):
        lines.append(press(foot, 0))
    for dome in (UP, L1 | L3, L2 | UP, CROSS | DOWN):
        lines.append(press(0, dome))
    lines += [req("@SM GET F L1 UP"), tick(1000)]
    return lines


SESSIONS = {
    "requests": s_requests,
    "dump": s_dump,
    "presses": s_presses,
    "battery": s_battery,
    "power": s_power,
    "announce": s_announce,
    "coincide": s_coincide,
    "shipped": s_shipped,
}

# Sessions with no request queued to land during a press (the per-pass rule of the scheduler can
# only reorder lines when two things happen in one pass).
ONE_ACTIVITY = ("requests", "dump", "presses", "battery", "power", "announce", "shipped")


# --- running -------------------------------------------------------------------------------


def build_sim(root: str, out_dir: str) -> str:
    src = os.path.join(root, "lib", "ShadowMap", "src")
    sources = [os.path.join(src, f) for f in sorted(os.listdir(src)) if f.endswith(".cpp")]
    exe = os.path.join(out_dir, "shadow_sim")
    cmd = ["g++", "-std=gnu++17", "-O1", "-Wall", "-Wextra", "-Werror", "-I", src, "-I", os.path.join(root, "include"),
           os.path.join(root, "tools", "shadowmap", "shadow_sim.cpp"), *sources,
           os.path.join(root, "src", "default_mappings.cpp"), "-o", exe]
    subprocess.run(cmd, check=True)
    return exe


def run_session(sim: str, lines: List[str], paced: int = 0) -> Tuple[str, str]:
    """(stdout, EEPROM SHA-256) of one session from a fresh EEPROM."""
    with tempfile.TemporaryDirectory(prefix="shadow_tx_") as tmp:
        eeprom = os.path.join(tmp, "eeprom.bin")
        args = [sim, eeprom] + ([str(paced)] if paced else [])
        stdin = "".join(line + "\n" for line in lines)
        out = subprocess.run(args, input=stdin.encode("latin-1"), capture_output=True, check=True, timeout=600)
        if os.path.exists(eeprom):
            with open(eeprom, "rb") as f:
                image = f.read()
        else:
            image = b"\xff" * 4096
        return out.stdout.decode("latin-1"), hashlib.sha256(image).hexdigest()


def record(sim: str, golden_dir: str) -> None:
    os.makedirs(golden_dir, exist_ok=True)
    manifest: Dict[str, str] = {}
    for name, make in SESSIONS.items():
        lines = make()
        with open(os.path.join(golden_dir, name + ".in"), "w", encoding="latin-1", newline="\n") as f:
            f.write("".join(line + "\n" for line in lines))
        for paced in (0, PACED):
            out, sha = run_session(sim, lines, paced)
            tag = name + (".paced" if paced else "")
            with open(os.path.join(golden_dir, tag + ".out"), "w", encoding="latin-1", newline="\n") as f:
                f.write(out)
            manifest[tag] = sha
    with open(os.path.join(golden_dir, "eeprom.json"), "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
        f.write("\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--record", metavar="DIR", help="golden directory under tools/shadowmap/, e.g. golden/v3")
    ap.add_argument("--root", default=ROOT, help="the firmware tree to build shadow_sim from")
    a = ap.parse_args()
    if not a.record:
        ap.print_help()
        return 2
    with tempfile.TemporaryDirectory(prefix="shadow_sim_") as tmp:
        sim = build_sim(a.root, tmp)
        record(sim, os.path.join(HERE, a.record))
    return 0


if __name__ == "__main__":
    sys.exit(main())
