#!/usr/bin/env python3
"""Reference client for SHADOW's @SM button-mapping protocol.

This is what DroidNet's backend (or anything else on the Pi) needs to read and change
SHADOW's button mappings. The wire format is in WIPDocs/COMMAND_MAPPING_DESIGN.md. In short:

    request   @SM <VERB> [args] *<CRC32>     CRC32 (zlib) of everything before " *", 8 hex
    reply     @SM <WORD> [args] *<CRC32>

Through the WCB (the normal path, controllers stay connected), a request is wrapped as
";S<x>@SM ..." (SHADOW on local S-port x) or ";W<n>;S<x>@SM ..." (SHADOW on WCB n), and SHADOW
replies with ";S0@SM ...", which the WCB prints on its USB port as "@SM ...".

Over SHADOW's own USB port (bench testing) requests go as-is and the CRC is optional, but
opening that port resets the Mega.

Command line:
    shadowmap.py --wcb /dev/ttyUSB0 --shadow-port 3 dump
    shadowmap.py --wcb /dev/ttyUSB0 --shadow-port 3 set F L1 UP scream
    shadowmap.py --usb /dev/ttyACM0 get F L1 UP        (resets the Mega when it opens the port)

Only the standard library is needed, plus pyserial for the command line.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
import zlib
from dataclasses import dataclass, field
from typing import Callable, Dict, Iterable, List, Optional, Tuple

CONTROLLERS = ("F", "D")
MODIFIERS = ("NONE", "L1", "L2", "PS", "CIRCLE", "CROSS")
BUTTONS = ("UP", "RIGHT", "DOWN", "LEFT", "L3", "PS")
KEY_RE = re.compile(r"^[A-Za-z0-9_]{1,15}$")
FORBIDDEN = ("^",)  # the WCB splits commands on it


class ProtocolError(Exception):
    """SHADOW answered with @SM ERR, or the reply didn't make sense."""

    def __init__(self, code: str, line: str = ""):
        super().__init__(f"{code}: {line}" if line else code)
        self.code = code
        self.line = line


class Timeout(Exception):
    """No valid reply in time (after retries)."""


# ---------------------------------------------------------------------------------------
# Framing


def crc32_hex(body: str) -> str:
    return "%08X" % (zlib.crc32(body.encode("ascii")) & 0xFFFFFFFF)


def frame(body: str) -> str:
    """'@SM HELLO' -> '@SM HELLO *C6246128'."""
    for ch in FORBIDDEN:
        if ch in body:
            raise ValueError(f"{ch!r} can't travel through the WCB")
    if not body.startswith("@SM "):
        raise ValueError("requests start with '@SM '")
    return f"{body} *{crc32_hex(body)}"


def unframe(line: str) -> Optional[str]:
    """Returns the body of a reply line with a valid CRC, else None."""
    line = line.strip()
    star = line.rfind(" *")
    if not line.startswith("@SM ") or star < 0:
        return None
    body, crc = line[:star], line[star + 2 :]
    if len(crc) != 8 or crc.upper() != crc32_hex(body):
        return None
    return body


# ---------------------------------------------------------------------------------------
# Slots


@dataclass(frozen=True)
class Slot:
    ctl: str  # F or D
    mod: str  # NONE L1 L2 PS CIRCLE CROSS
    btn: str  # UP RIGHT DOWN LEFT L3 PS

    def __post_init__(self):
        if self.ctl not in CONTROLLERS or self.mod not in MODIFIERS or self.btn not in BUTTONS:
            raise ValueError(f"bad slot {self.ctl} {self.mod} {self.btn}")

    @property
    def index(self) -> int:
        return CONTROLLERS.index(self.ctl) * 36 + MODIFIERS.index(self.mod) * 6 + BUTTONS.index(self.btn)

    @classmethod
    def from_index(cls, i: int) -> "Slot":
        return cls(CONTROLLERS[i // 36], MODIFIERS[(i % 36) // 6], BUTTONS[i % 6])

    @classmethod
    def parse(cls, text: str) -> "Slot":
        parts = text.replace(".", " ").split()
        if len(parts) != 3:
            raise ValueError(f"slot is 'F L1 UP', got {text!r}")
        return cls(*parts)

    def __str__(self) -> str:
        return f"{self.ctl} {self.mod} {self.btn}"


def all_slots() -> List[Slot]:
    return [Slot.from_index(i) for i in range(72)]


@dataclass
class SlotState:
    slot: Slot
    key: str  # "" = unmapped
    source: str  # eeprom | default | reserved
    default: Optional[str] = None  # built-in key ("" = none); None for reserved slots
    reserved: Optional[str] = None  # psgate | overspeed

    @property
    def body(self) -> str:
        """The SLOT line's text after '@SM SLOT ', exactly as SHADOW sends it."""
        text = f"{self.slot} {self.key or '-'} {self.source}"
        if self.reserved is not None:
            return f"{text} res={self.reserved}"
        return f"{text} def={self.default or '-'}"


def map_sum(states: Iterable[SlotState]) -> str:
    """What @SM SUM returns for this map: CRC32 over each slot's SLOT body plus '\\n'."""
    ordered = sorted(states, key=lambda s: s.slot.index)
    if len(ordered) != 72:
        raise ValueError("need all 72 slots")
    data = "".join(s.body + "\n" for s in ordered)
    return "%08X" % (zlib.crc32(data.encode("ascii")) & 0xFFFFFFFF)


def _kv(tokens: List[str]) -> Dict[str, str]:
    out = {}
    for t in tokens:
        if "=" in t:
            k, v = t.split("=", 1)
            out[k] = v
    return out


def parse_slot_state(body: str) -> SlotState:
    """'@SM SLOT F L1 UP scream eeprom def=extremehappysou' -> SlotState."""
    t = body.split(" ")
    if len(t) < 7 or t[:2] != ["@SM", "SLOT"]:
        raise ProtocolError("parse", body)
    kv = _kv(t[7:])
    key = "" if t[5] == "-" else t[5]
    default = None
    if "def" in kv:
        default = "" if kv["def"] == "-" else kv["def"]
    return SlotState(Slot(t[2], t[3], t[4]), key, t[6], default, kv.get("res"))


@dataclass
class Ready:
    version: int
    fw: str
    slots: int
    gen: int
    formatted: bool
    pads: str  # "FD", "F", "D" or "-"


def parse_ready(body: str) -> Ready:
    m = re.match(r"^@SM READY v(\d+) (.*)$", body)
    if not m:
        raise ProtocolError("parse", body)
    kv = _kv(m.group(2).split(" "))
    return Ready(int(m.group(1)), kv.get("fw", ""), int(kv.get("slots", "0")), int(kv.get("gen", "0")),
                 kv.get("fmt") == "1", kv.get("pads", "-"))


@dataclass
class Event:
    kind: str  # PRESS or CTL
    slot: Optional[Slot] = None
    key: str = ""
    source: str = ""
    from_other: Optional[str] = None
    reserved: Optional[str] = None
    controller: Optional[str] = None
    connected: Optional[bool] = None


def parse_event(body: str) -> Optional[Event]:
    t = body.split(" ")
    if len(t) >= 7 and t[1] == "PRESS":
        kv = _kv(t[7:])
        return Event("PRESS", Slot(t[2], t[3], t[4]), "" if t[5] == "-" else t[5], t[6], kv.get("from"), kv.get("res"))
    if len(t) == 4 and t[1] == "CTL":
        return Event("CTL", controller=t[2], connected=t[3] == "1")
    return None


# ---------------------------------------------------------------------------------------
# Transport and routing


class Transport:
    """Moves text lines. write_line() sends one line; read_line() returns one received line
    (without its terminator) or None if nothing arrives within `timeout` seconds."""

    def write_line(self, line: str) -> None:
        raise NotImplementedError

    def read_line(self, timeout: float) -> Optional[str]:
        raise NotImplementedError


@dataclass
class WcbRoute:
    """How the Pi reaches SHADOW through the WCB it's plugged into.

    shadow_port: the S-port (1-5) SHADOW's Serial1 is wired to.
    shadow_wcb:  SHADOW's WCB number, or None when it's the Pi's own WCB.
    pi_wcb:      the Pi's WCB number; only needed when shadow_wcb is set (for the reply route).
    """

    shadow_port: int
    shadow_wcb: Optional[int] = None
    pi_wcb: Optional[int] = None

    def wrap(self, line: str) -> str:
        if not 1 <= self.shadow_port <= 5:
            raise ValueError("S-port is 1-5")
        inner = f";S{self.shadow_port}{line}"
        if self.shadow_wcb is None:
            return inner
        if self.shadow_wcb < 10:
            return f";W{self.shadow_wcb}{inner}"
        return f";W{self.shadow_wcb},{inner}"

    @property
    def hello_route(self) -> int:
        """HELLO's argument: where SHADOW should send replies (0 = its own WCB's USB)."""
        if self.shadow_wcb is None:
            return 0
        if self.pi_wcb is None:
            raise ValueError("pi_wcb is needed when SHADOW is on another WCB")
        return self.pi_wcb


# ---------------------------------------------------------------------------------------
# Client


@dataclass
class Client:
    transport: Transport
    route: Optional[WcbRoute] = None  # None = talking to SHADOW's USB port directly
    timeout: float = 1.5  # per attempt; a SET needs ~0.2-0.3 s through the WCB
    retries: int = 3
    on_event: Optional[Callable[[Event], None]] = None
    stats: Dict[str, int] = field(default_factory=lambda: {"sent": 0, "retries": 0, "bad_crc": 0})

    # -- plumbing --------------------------------------------------------------------

    def _send(self, body: str) -> None:
        line = frame(body)
        if self.route is not None:
            line = self.route.wrap(line)
        self.stats["sent"] += 1
        self.transport.write_line(line)

    def _replies(self, deadline: float):
        """Valid @SM reply bodies until the deadline. Events go to on_event, not the caller."""
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                return
            raw = self.transport.read_line(left)
            if raw is None:
                return
            raw = raw.strip()
            if not raw.startswith("@SM "):
                continue  # WCB chatter, debug text, echoes of our own ";S..." lines
            body = unframe(raw)
            if body is None:
                self.stats["bad_crc"] += 1
                continue
            event = parse_event(body)
            if event is not None:
                if self.on_event:
                    self.on_event(event)
                continue
            yield body

    def request(self, body: str, accept: Callable[[str], bool], expect_lines: int = 1) -> List[str]:
        """Sends `body` and returns the reply lines `accept` matches. Retries on silence and on
        '@SM ERR crc' / 'busy'. Raises ProtocolError on any other @SM ERR aimed at this
        request."""
        verb = body.split(" ")[1]
        last_err = None
        for attempt in range(self.retries + 1):
            if attempt:
                self.stats["retries"] += 1
            self._send(body)
            got: List[str] = []
            deadline = time.monotonic() + self.timeout * max(1, expect_lines // 8)
            for reply in self._replies(deadline):
                if reply.startswith("@SM ERR "):
                    parts = reply.split(" ")
                    code = parts[2]
                    if code in ("crc", "busy"):
                        last_err = ProtocolError(code, reply)
                        break  # retry
                    if len(parts) < 4 or parts[3] == verb:
                        raise ProtocolError(code, reply)
                    continue  # an error for some earlier request
                if accept(reply):
                    got.append(reply)
                    if len(got) >= expect_lines:
                        return got
            if got and expect_lines > 1:
                raise Timeout(f"{verb}: got {len(got)} of {expect_lines} lines")
        if last_err:
            raise last_err
        raise Timeout(f"no reply to {verb}")

    # -- verbs -----------------------------------------------------------------------

    def hello(self) -> Ready:
        body = "@SM HELLO" if self.route is None else f"@SM HELLO {self.route.hello_route}"
        return parse_ready(self.request(body, lambda r: r.startswith("@SM READY "))[0])

    def get(self, slot: Slot) -> SlotState:
        want = f"@SM SLOT {slot} "
        return parse_slot_state(self.request(f"@SM GET {slot}", lambda r: r.startswith(want))[0])

    def set(self, slot: Slot, key: str) -> None:
        """Maps `slot` to stored command `key` ("" to unmap). Returns once SHADOW has written
        and verified it."""
        if key and not KEY_RE.match(key):
            raise ValueError(f"key must be 1-15 of [A-Za-z0-9_]: {key!r}")
        token = key or "-"
        want = f"@SM OK SET {slot} {token}"
        self.request(f"@SM SET {slot} {token}", lambda r: r == want)

    def revert(self, slot: Slot) -> None:
        """Back to the built-in default."""
        want = f"@SM OK REVERT {slot}"
        self.request(f"@SM REVERT {slot}", lambda r: r == want)

    def dump(self) -> List[SlotState]:
        lines = self.request("@SM DUMP", lambda r: r.startswith("@SM SLOT ") or r.startswith("@SM END DUMP"),
                             expect_lines=73)
        if lines[-1] != "@SM END DUMP 72":
            raise ProtocolError("parse", lines[-1])
        states = [parse_slot_state(l) for l in lines[:-1]]
        if [s.slot.index for s in states] != list(range(72)):
            raise ProtocolError("dump order")
        return states

    def sum(self) -> str:
        reply = self.request("@SM SUM", lambda r: r.startswith("@SM SUM "))[0]
        m = re.match(r"^@SM SUM ([0-9A-F]{8}) slots=72$", reply)
        if not m:
            raise ProtocolError("parse", reply)
        return m.group(1)

    def defaults(self) -> int:
        reply = self.request("@SM DEFAULTS", lambda r: r.startswith("@SM OK DEFAULTS"))[0]
        return int(reply.split("gen=")[1])

    def cfg(self) -> Tuple[str, str]:
        reply = self.request("@SM CFG", lambda r: r.startswith("@SM CFG "))[0]
        kv = _kv(reply.split(" ")[2:])
        return _unhex(kv["pre"]), _unhex(kv["suf"])

    def set_prefix(self, prefix: str) -> None:
        h = _hex(prefix)
        self.request(f"@SM PREFIX {h}", lambda r: r == f"@SM OK PREFIX {h}")

    def set_suffix(self, suffix: str) -> None:
        h = _hex(suffix)
        self.request(f"@SM SUFFIX {h}", lambda r: r == f"@SM OK SUFFIX {h}")

    def events(self, on: bool) -> None:
        want = f"@SM OK EVENTS {1 if on else 0}"
        self.request(f"@SM EVENTS {1 if on else 0}", lambda r: r == want)

    def listen(self, seconds: float) -> None:
        """Pumps incoming lines for a while so on_event sees presses and controller changes."""
        for _ in self._replies(time.monotonic() + seconds):
            pass

    def read_map(self, cached: Optional[List[SlotState]] = None) -> List[SlotState]:
        """The full map, skipping the ~6 s DUMP when `cached` still matches SHADOW's SUM."""
        if cached is not None and len(cached) == 72 and map_sum(cached) == self.sum():
            return cached
        return self.dump()


def _hex(text: str) -> str:
    return text.encode("ascii").hex().upper() if text else "-"


def _unhex(text: str) -> str:
    return "" if text == "-" else bytes.fromhex(text).decode("ascii")


# ---------------------------------------------------------------------------------------
# Command line (pyserial)


class SerialTransport(Transport):
    def __init__(self, port: str, baud: int = 115200, reset_wait: float = 0.0):
        import serial  # pyserial

        self._s = serial.Serial(port, baud, timeout=0.05)
        if reset_wait:
            time.sleep(reset_wait)  # opening a Mega's USB port resets it
        self._buf = b""

    def write_line(self, line: str) -> None:
        self._s.write(line.encode("ascii") + b"\n")

    def read_line(self, timeout: float) -> Optional[str]:
        deadline = time.monotonic() + timeout
        while True:
            for sep in (b"\n", b"\r"):
                i = self._buf.find(sep)
                if i >= 0:
                    line, self._buf = self._buf[:i], self._buf[i + 1 :]
                    return line.decode("ascii", "replace")
            if time.monotonic() >= deadline:
                return None
            self._buf += self._s.read(256)


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    where = ap.add_mutually_exclusive_group(required=True)
    where.add_argument("--wcb", help="serial port of the WCB the Pi is plugged into")
    where.add_argument("--usb", help="SHADOW's own USB port (resets the Mega)")
    ap.add_argument("--shadow-port", type=int, default=0, help="WCB S-port SHADOW is wired to")
    ap.add_argument("--shadow-wcb", type=int, help="SHADOW's WCB number, if not the Pi's WCB")
    ap.add_argument("--pi-wcb", type=int, help="the Pi's WCB number (with --shadow-wcb)")
    ap.add_argument("command", choices=["hello", "get", "set", "revert", "dump", "sum", "defaults", "cfg", "watch"])
    ap.add_argument("args", nargs="*")
    a = ap.parse_args(argv)

    if a.wcb:
        if not a.shadow_port:
            ap.error("--shadow-port is required with --wcb")
        client = Client(SerialTransport(a.wcb), WcbRoute(a.shadow_port, a.shadow_wcb, a.pi_wcb))
    else:
        client = Client(SerialTransport(a.usb, reset_wait=3.0))

    ready = client.hello()
    if a.command == "hello":
        print(ready)
    elif a.command == "get":
        print(client.get(Slot.parse(" ".join(a.args))).body)
    elif a.command == "set":
        client.set(Slot.parse(" ".join(a.args[:3])), a.args[3] if len(a.args) > 3 else "")
        print("ok")
    elif a.command == "revert":
        client.revert(Slot.parse(" ".join(a.args)))
        print("ok")
    elif a.command == "dump":
        for s in client.dump():
            print(s.body)
    elif a.command == "sum":
        print(client.sum())
    elif a.command == "defaults":
        print("gen", client.defaults())
    elif a.command == "cfg":
        print("prefix=%r suffix=%r" % client.cfg())
    elif a.command == "watch":
        client.on_event = lambda e: print(e)
        client.events(True)
        try:
            while True:
                client.listen(1.0)
        except KeyboardInterrupt:
            client.events(False)
    return 0


if __name__ == "__main__":
    sys.exit(main())
