#!/usr/bin/env python3
"""Contract tests: the reference client against SHADOW's real protocol code.

shadow_sim (the ShadowMap library built for the host) plays SHADOW; a small WCB emulator
sits in between, routing ";S<x>" / ";W<n>" both ways the way the WCB firmware does, and
failing the test if either side sends something the WCB would mishandle.

    python3 -m unittest tools/shadowmap/test_shadowmap.py      (from the repo root)
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from typing import List, Optional

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import shadowmap as smc  # noqa: E402

# HeldBit masks, as in lib/ShadowMap/src/sm_resolve.h
UP, RIGHT, DOWN, LEFT, L3, PS, L1, L2, CIRCLE, CROSS = (1 << i for i in range(10))

_BUILD_DIR = None


def build_sim() -> str:
    global _BUILD_DIR
    if _BUILD_DIR is None:
        _BUILD_DIR = tempfile.mkdtemp(prefix="shadow_sim_")
        src = os.path.join(ROOT, "lib", "ShadowMap", "src")
        sources = [os.path.join(src, f) for f in sorted(os.listdir(src)) if f.endswith(".cpp")]
        cmd = ["g++", "-std=gnu++17", "-O1", "-Wall", "-Wextra", "-Werror", "-I", src, "-I", os.path.join(ROOT, "include"),
               os.path.join(HERE, "shadow_sim.cpp"), *sources, os.path.join(ROOT, "src", "default_mappings.cpp"),
               "-o", os.path.join(_BUILD_DIR, "shadow_sim")]
        subprocess.run(cmd, check=True)
    return os.path.join(_BUILD_DIR, "shadow_sim")


class WcbEmulator(smc.Transport):
    """The Pi's WCB and SHADOW's WCB (the same board unless shadow_wcb is set)."""

    def __init__(self, eeprom: str, shadow_port: int = 3, shadow_wcb: Optional[int] = None, pi_wcb: int = 1):
        self.shadow_port = shadow_port
        self.shadow_wcb = shadow_wcb
        self.pi_wcb = pi_wcb
        self.proc = subprocess.Popen([build_sim(), eeprom], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.usb: List[str] = []  # what the Pi will read
        self.commands: List[str] = []  # what the WCB would run for SHADOW
        self.announces: List[str] = []
        self.violations: List[str] = []
        self.lost: List[str] = []  # replies that went to a USB port the Pi isn't on
        self.to_shadow: List[str] = []
        self.corrupt_next_request = False
        self.drop_next_reply = False
        self.corrupt_next_reply = False

    # -- simulator plumbing ------------------------------------------------------------

    def _shadow(self, line: str) -> None:
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        while True:
            out = self.proc.stdout.readline()
            if out == "":
                raise RuntimeError("shadow_sim exited")
            out = out.rstrip("\n")
            if out.startswith("!!IDLE"):
                return
            self._from_shadow(out)

    def _from_shadow(self, line: str) -> None:
        if "^" in line:
            self.violations.append(f"'^' from SHADOW: {line}")
        if line.startswith("@WDP"):
            self.announces.append(line)
            return
        if not line.startswith(";"):
            self.violations.append(f"bare line from SHADOW (would broadcast): {line}")
            return
        if re.search(r";[Tt]", line):
            self.violations.append(f"';T' from SHADOW (timer command): {line}")
        payload = None
        if line.startswith(";S0"):
            if self.shadow_wcb is None:
                payload = line[3:]
            else:
                self.lost.append(line)  # SHADOW's own WCB's USB: the Pi never sees it
                return
        m = re.match(r"^;W(\d),?;S0(.*)$", line) or re.match(r"^;W(\d{2}),;S0(.*)$", line)
        if m:
            if int(m.group(1)) != self.pi_wcb:
                self.lost.append(line)
                return
            payload = m.group(2)
        if payload is None:
            self.commands.append(line)  # a stored-command trigger for the WCB to run
            return
        if self.drop_next_reply:
            self.drop_next_reply = False
            return
        if self.corrupt_next_reply:
            self.corrupt_next_reply = False
            payload = payload.replace("@SM ", "@SM X", 1) if len(payload) < 8 else payload[:8] + "#" + payload[9:]
        self.usb.append(payload.strip())

    # -- smc.Transport -----------------------------------------------------------------

    def write_line(self, line: str) -> None:
        if "^" in line:
            self.violations.append(f"'^' from the Pi: {line}")
        target = self.shadow_wcb
        m = re.match(r"^;W(\d{2}),(.*)$", line) or re.match(r"^;W(\d)(;.*)$", line)
        if m:
            if target is None or int(m.group(1)) != target:
                self.violations.append(f"routed to WCB{m.group(1)}, SHADOW isn't there: {line}")
                return
            line = m.group(2)
        elif target is not None:
            self.violations.append(f"SHADOW is on WCB{target} but the line isn't routed there: {line}")
            return
        m = re.match(r"^;S(\d),?(.*)$", line)
        if not m or int(m.group(1)) != self.shadow_port:
            self.violations.append(f"not sent to SHADOW's port S{self.shadow_port}: {line}")
            return
        payload = m.group(2).strip()  # the WCB trims and appends \r
        if self.corrupt_next_request:
            self.corrupt_next_request = False
            payload = payload[:6] + ("Q" if payload[6] != "Q" else "R") + payload[7:]
        self._shadow(payload)

    def read_line(self, timeout: float) -> Optional[str]:
        return self.usb.pop(0) if self.usb else None

    # -- controls ------------------------------------------------------------------------

    def press(self, foot: int = 0, dome: int = 0, foot_on: bool = True, dome_on: bool = True) -> None:
        self._shadow(f"!!PRESS {foot:x} {dome:x} {int(foot_on)} {int(dome_on)}")

    def tick(self, ms: int) -> None:
        self._shadow(f"!!TICK {ms}")

    def cut_after(self, writes: int) -> None:
        self._shadow(f"!!CUT {writes}")

    def reboot(self) -> None:
        self._shadow("!!REBOOT")

    def close(self) -> None:
        self.proc.stdin.close()
        self.proc.wait(timeout=5)
        self.proc.stdout.close()


class ContractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="shadow_eeprom_")
        self.eeprom = os.path.join(self.tmp, "eeprom.bin")
        self.wcb = WcbEmulator(self.eeprom)
        self.client = smc.Client(self.wcb, smc.WcbRoute(shadow_port=3), timeout=0.05, retries=3)
        self.ready = self.client.hello()

    def tearDown(self):
        self.wcb.close()
        shutil.rmtree(self.tmp, ignore_errors=True)
        self.assertEqual([], self.wcb.violations)

    def slot(self, text):
        return smc.Slot.parse(text)

    def test_hello(self):
        self.assertEqual(1, self.ready.version)
        self.assertEqual(72, self.ready.slots)
        self.assertFalse(self.ready.formatted)
        self.assertEqual("2.0.0-sim", self.ready.fw)

    def test_dump_shows_the_built_in_map(self):
        states = {s.slot: s for s in self.client.dump()}
        self.assertEqual(72, len(states))
        s = states[self.slot("F L1 UP")]
        self.assertEqual(("extremehappysou", "default", "extremehappysou"), (s.key, s.source, s.default))
        self.assertEqual("overspeed", states[self.slot("F L1 L3")].reserved)
        self.assertEqual("psgate", states[self.slot("D NONE PS")].reserved)
        self.assertIsNone(states[self.slot("D L1 L3")].reserved)  # dome L3+L1 is Front Holo Center
        for d in ("UP", "RIGHT", "DOWN", "LEFT"):
            self.assertEqual("", states[self.slot(f"F PS {d}")].key)  # brightness removed
            self.assertEqual("", states[self.slot(f"D PS {d}")].key)  # volume removed
        self.assertEqual(35, sum(1 for s in states.values() if s.key))

    def test_python_sum_matches_shadow(self):
        self.assertEqual(smc.map_sum(self.client.dump()), self.client.sum())
        self.client.set(self.slot("D CIRCLE UP"), "yes")
        self.assertEqual(smc.map_sum(self.client.dump()), self.client.sum())

    def test_remap_changes_what_a_press_sends(self):
        self.wcb.press(foot=UP | L1)
        self.assertEqual(";W1;Cextremehappysou", self.wcb.commands[-1])
        self.client.set(self.slot("F L1 UP"), "scream")
        self.wcb.press(foot=UP | L1)
        self.assertEqual(";W1;Cscream", self.wcb.commands[-1])
        self.client.set(self.slot("F L1 UP"), "")
        n = len(self.wcb.commands)
        self.wcb.press(foot=UP | L1)
        self.assertEqual(n, len(self.wcb.commands))  # unmapped: nothing sent
        self.client.revert(self.slot("F L1 UP"))
        self.wcb.press(foot=UP | L1)
        self.assertEqual(";W1;Cextremehappysou", self.wcb.commands[-1])

    def test_mapping_survives_reboot_and_defaults_resets_it(self):
        self.client.set(self.slot("D CROSS UP"), "GDc2song")
        self.wcb.reboot()
        self.assertTrue(self.client.hello().formatted)
        self.assertEqual("GDc2song", self.client.get(self.slot("D CROSS UP")).key)
        gen = self.client.defaults()
        self.assertEqual(self.ready.gen + 1, gen)
        s = self.client.get(self.slot("D CROSS UP"))
        self.assertEqual(("", "default"), (s.key, s.source))

    def test_power_cut_mid_write_leaves_old_new_or_default(self):
        self.client.set(self.slot("F NONE LEFT"), "firstvalue")
        for cut in range(0, 20):
            self.wcb.cut_after(cut)
            try:
                self.client.set(self.slot("F NONE LEFT"), "secondvalue")
            except (smc.ProtocolError, smc.Timeout):
                pass  # verify failed after the cut: expected
            self.wcb.reboot()
            self.client.hello()
            s = self.client.get(self.slot("F NONE LEFT"))
            self.assertIn((s.key, s.source), [("firstvalue", "eeprom"), ("secondvalue", "eeprom"), ("scaredsound", "default")])
            self.assertEqual(smc.map_sum(self.client.dump()), self.client.sum())
            self.client.set(self.slot("F NONE LEFT"), "firstvalue")

    def test_retries_a_corrupted_request(self):
        self.wcb.corrupt_next_request = True
        self.client.set(self.slot("F CROSS LEFT"), "raspberry")
        self.assertEqual(1, self.client.stats["retries"])
        self.assertEqual("raspberry", self.client.get(self.slot("F CROSS LEFT")).key)

    def test_retries_a_lost_or_corrupted_reply(self):
        self.wcb.drop_next_reply = True
        self.client.set(self.slot("F CROSS DOWN"), "raspberry")
        self.wcb.corrupt_next_reply = True
        self.assertEqual("raspberry", self.client.get(self.slot("F CROSS DOWN")).key)
        self.assertEqual(2, self.client.stats["retries"])
        self.assertEqual(1, self.client.stats["bad_crc"])

    def test_errors_surface(self):
        with self.assertRaises(smc.ProtocolError) as e:
            self.client.set(self.slot("F L1 L3"), "nope")
        self.assertEqual("reserved", e.exception.code)
        with self.assertRaises(ValueError):
            self.client.set(self.slot("F L1 UP"), "has-dash")
        with self.assertRaises(ValueError):
            smc.frame("@SM SET F L1 UP a^b")

    def test_read_map_skips_dump_when_sum_matches(self):
        cached = self.client.dump()
        sent = self.client.stats["sent"]
        self.assertIs(cached, self.client.read_map(cached))
        self.assertEqual(sent + 1, self.client.stats["sent"])  # just the SUM
        self.client.set(self.slot("D L2 PS"), "mahna")
        fresh = self.client.read_map(cached)
        self.assertIsNot(cached, fresh)
        self.assertEqual("mahna", {s.slot: s for s in fresh}[self.slot("D L2 PS")].key)

    def test_events(self):
        events = []
        self.client.on_event = events.append
        self.client.events(True)
        self.wcb.press(foot=UP, dome=CROSS)
        self.client.listen(0.01)
        press = [e for e in events if e.kind == "PRESS"]
        self.assertEqual(1, len(press))
        self.assertEqual((self.slot("F CROSS UP"), "leia", "D"), (press[0].slot, press[0].key, press[0].from_other))
        self.wcb.press(dome_on=False)  # dome drops
        self.client.listen(0.01)
        self.assertIn(("D", False), [(e.controller, e.connected) for e in events if e.kind == "CTL"])

    def test_prefix_and_suffix(self):
        self.client.set_prefix(";SEQ")
        self.client.set_suffix(",L")
        self.assertEqual((";SEQ", ",L"), self.client.cfg())
        self.wcb.press(dome=L2 | LEFT)
        self.assertEqual(";SEQGDcollar,L", self.wcb.commands[-1])
        with self.assertRaises(smc.ProtocolError):
            self.client.set_prefix("W1;C")  # no leading ';': would be broadcast
        self.client.set_prefix(";W1;C")
        self.client.set_suffix("")
        self.wcb.press(dome=L2 | LEFT)
        self.assertEqual(";W1;CGDcollar", self.wcb.commands[-1])

    def test_announce(self):
        self.wcb.tick(6000)
        self.assertEqual(['@WDP1 {"type":"Shadow","fw":"2.0.0-sim","caps":["shadow.map"]}'], self.wcb.announces)


class RemoteRouteTest(unittest.TestCase):
    """SHADOW on WCB 2 S-port 5, the Pi on WCB 4: every line crosses the ESP-NOW mesh."""

    def test_remote_round_trip(self):
        tmp = tempfile.mkdtemp(prefix="shadow_eeprom_")
        wcb = WcbEmulator(os.path.join(tmp, "e.bin"), shadow_port=5, shadow_wcb=2, pi_wcb=4)
        try:
            client = smc.Client(wcb, smc.WcbRoute(shadow_port=5, shadow_wcb=2, pi_wcb=4), timeout=0.05)
            self.assertEqual(72, client.hello().slots)
            client.set(smc.Slot.parse("F NONE L3"), "scanning")
            self.assertEqual("scanning", client.get(smc.Slot.parse("F NONE L3")).key)
            wcb.press(foot=L3)
            self.assertEqual(";W1;Cscanning", wcb.commands[-1])
            self.assertEqual([], wcb.violations)
        finally:
            wcb.close()
            shutil.rmtree(tmp, ignore_errors=True)

    def test_client_recovers_after_shadow_reboots(self):
        """A reboot resets SHADOW's reply route to its own WCB; the client says HELLO again."""
        tmp = tempfile.mkdtemp(prefix="shadow_eeprom_")
        wcb = WcbEmulator(os.path.join(tmp, "e.bin"), shadow_port=5, shadow_wcb=2, pi_wcb=4)
        try:
            client = smc.Client(wcb, smc.WcbRoute(shadow_port=5, shadow_wcb=2, pi_wcb=4), timeout=0.05)
            client.hello()
            events = []
            client.on_event = events.append
            client.events(True)
            client.set(smc.Slot.parse("D NONE L3"), "yes")
            wcb.reboot()
            self.assertEqual("yes", client.get(smc.Slot.parse("D NONE L3")).key)
            self.assertTrue(wcb.lost, "replies right after the reboot should have gone astray")
            wcb.press(dome=L3)  # EVENTS came back with the new HELLO
            client.listen(0.01)
            self.assertEqual(["yes"], [e.key for e in events if e.kind == "PRESS"])
            self.assertEqual([], wcb.violations)
        finally:
            wcb.close()
            shutil.rmtree(tmp, ignore_errors=True)

    def test_two_digit_wcb_numbers(self):
        self.assertEqual(";W12,;S3@SM X", smc.WcbRoute(3, 12, 1).wrap("@SM X"))
        self.assertEqual(";W2;S3@SM X", smc.WcbRoute(3, 2, 1).wrap("@SM X"))
        self.assertEqual(";S3@SM X", smc.WcbRoute(3).wrap("@SM X"))
        self.assertEqual(14, smc.WcbRoute(3, 12, 14).hello_route)


class FramingTest(unittest.TestCase):
    def test_frame_and_unframe(self):
        line = smc.frame("@SM HELLO")
        self.assertEqual("@SM HELLO *C6246128", line)
        self.assertEqual("@SM HELLO", smc.unframe(line))
        self.assertIsNone(smc.unframe("@SM HELLO *C6246129"))
        self.assertIsNone(smc.unframe("Sent to USB: @SM HELLO *C6246128"))

    def test_slot_index_matches_firmware(self):
        self.assertEqual(0, smc.Slot.parse("F NONE UP").index)
        self.assertEqual(71, smc.Slot.parse("D CROSS PS").index)
        self.assertEqual([s.index for s in smc.all_slots()], list(range(72)))


if __name__ == "__main__":
    unittest.main()
