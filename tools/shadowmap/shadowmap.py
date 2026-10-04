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
    shadowmap.py --wcb /dev/ttyUSB0 --shadow-port 3 set F L1 UP2 scream   (UP2 = UP tapped twice)
    shadowmap.py --wcb /dev/ttyUSB0 --shadow-port 3 set F L1 UPL quiet   (UPL = UP held for a long press)
    shadowmap.py --wcb /dev/ttyUSB0 --shadow-port 3 set --raw F L1 UP ';W2;S4PLAY:3'   (a raw command, sent as typed)
    shadowmap.py --usb /dev/ttyACM0 get F L1 UP        (resets the Mega when it opens the port)
    shadowmap.py --wcb /dev/ttyUSB0 --shadow-port 3 battery

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
PROTOCOL_VERSION = 4  # v2 added the double-tap slots, v3 the long presses, v4 raw commands
SINGLE_SLOTS = 72  # 0-71 single presses; 72-143 tapped twice; 144-215 held
TAPS = (1, 2, 3)  # single, double tap, long press
TAP_MARK = {1: "", 2: "2", 3: "L"}
SLOT_COUNT = len(TAPS) * SINGLE_SLOTS
KEY_RE = re.compile(r"^[A-Za-z0-9_]{1,15}\Z")  # \Z, not $: "$" also matches before a trailing "\n"
RAW_RE = re.compile(r"[\x20-\x7e]{1,15}")  # a raw command; use with fullmatch
FORBIDDEN = ("^",)  # the WCB splits commands on it


class ProtocolError(Exception):
    """SHADOW answered with @SM ERR, or the reply didn't make sense."""

    def __init__(self, code: str, line: str = ""):
        super().__init__(f"{code}: {line}" if line else code)
        self.code = code
        self.line = line


class Timeout(Exception):
    """No valid reply in time (after retries)."""


def valid_raw(text) -> bool:
    """A raw command: 1-15 printable ASCII characters, space included, sent as typed."""
    return isinstance(text, str) and RAW_RE.fullmatch(text) is not None


def raw_token(text: str) -> str:
    """How a raw command travels in an @SM line: '!' and its bytes in upper-case hex."""
    return "!" + text.encode("ascii").hex().upper()


def parse_value(token: str) -> Tuple[str, bool]:
    """A slot's command token -> (text, raw): '-' is unmapped, '!<hex>' a raw command, else a key."""
    if token == "-":
        return "", False
    if token.startswith("!"):
        if not re.fullmatch(r"[0-9A-Fa-f]*", token[1:]):  # bytes.fromhex would skip whitespace
            raise ProtocolError("parse", token)
        try:
            text = bytes.fromhex(token[1:]).decode("ascii")
        except ValueError:  # odd length, a bad digit, or a byte over 0x7F
            raise ProtocolError("parse", token) from None
        if not valid_raw(text):
            raise ProtocolError("parse", token)
        return text, True
    return token, False


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
    tap: int = 1  # 1 single, 2 double tap ("UP2" on the wire), 3 long press ("UPL")

    def __post_init__(self):
        if self.ctl not in CONTROLLERS or self.mod not in MODIFIERS or self.btn not in BUTTONS:
            raise ValueError(f"bad slot {self.ctl} {self.mod} {self.btn}")
        if isinstance(self.tap, bool) or self.tap not in TAPS:
            raise ValueError(f"tap is 1, 2 or 3, got {self.tap!r}")

    @property
    def index(self) -> int:
        single = CONTROLLERS.index(self.ctl) * 36 + MODIFIERS.index(self.mod) * 6 + BUTTONS.index(self.btn)
        return single + (self.tap - 1) * SINGLE_SLOTS

    @classmethod
    def from_index(cls, i: int) -> "Slot":
        j = i % SINGLE_SLOTS
        return cls(CONTROLLERS[j // 36], MODIFIERS[(j % 36) // 6], BUTTONS[j % 6], i // SINGLE_SLOTS + 1)

    @classmethod
    def from_tokens(cls, ctl: str, mod: str, btn: str) -> "Slot":
        """The three wire tokens, e.g. ("F", "L1", "UP2") or ("F", "L1", "UPL")."""
        if btn in BUTTONS:
            return cls(ctl, mod, btn)
        for tap, mark in TAP_MARK.items():
            if mark and btn.endswith(mark) and btn[: -len(mark)] in BUTTONS:
                return cls(ctl, mod, btn[: -len(mark)], tap)
        raise ValueError(f"bad button {btn!r}")

    @classmethod
    def parse(cls, text: str) -> "Slot":
        parts = text.replace(".", " ").split()
        if len(parts) != 3:
            raise ValueError(f"slot is 'F L1 UP' ('UP2' double tap, 'UPL' long press), got {text!r}")
        return cls.from_tokens(*parts)

    def __str__(self) -> str:
        return f"{self.ctl} {self.mod} {self.btn}{TAP_MARK[self.tap]}"


def all_slots() -> List[Slot]:
    return [Slot.from_index(i) for i in range(SLOT_COUNT)]


@dataclass
class SlotState:
    slot: Slot
    key: str  # "" = unmapped; with raw, a raw command's text
    source: str  # eeprom | default | reserved
    default: Optional[str] = None  # built-in key ("" = none); None for reserved slots
    reserved: Optional[str] = None  # psgate | overspeed | stick
    raw: bool = False  # key is a raw command (v4): SHADOW sends it to the WCB as typed

    @property
    def token(self) -> str:
        """The command as SHADOW writes it: a key, '-' (unmapped) or '!<hex>' (raw)."""
        if self.raw and self.key:
            return raw_token(self.key)
        return self.key or "-"

    @property
    def body(self) -> str:
        """The SLOT line's text after '@SM SLOT ', exactly as SHADOW sends it."""
        text = f"{self.slot} {self.token} {self.source}"
        if self.reserved is not None:
            return f"{text} res={self.reserved}"
        return f"{text} def={self.default or '-'}"


def map_sum(states: Iterable[SlotState]) -> str:
    """What @SM SUM returns for this map: CRC32 over each slot's SLOT body plus '\\n'."""
    ordered = sorted(states, key=lambda s: s.slot.index)
    if len(ordered) != SLOT_COUNT:
        raise ValueError(f"need all {SLOT_COUNT} slots")
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
    key, raw = parse_value(t[5])
    default = None
    if "def" in kv:
        default = "" if kv["def"] == "-" else kv["def"]
    return SlotState(Slot.from_tokens(t[2], t[3], t[4]), key, t[6], default, kv.get("res"), raw)


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
class Battery:
    """An '@SM BAT' line: both controllers' battery levels.

    SHADOW sends one when a level changes and every `period_s` anyway, whether or not EVENTS
    is on, and also in reply to '@SM BAT'.
    """

    foot: Optional[str]  # OK, LOW or CRIT; None while unknown (not connected, or settling)
    dome: Optional[str]
    foot_status: Optional[str]  # the controller's own reading: "0".."5", "chg", "usb"; None
    dome_status: Optional[str]
    uptime_s: Optional[int]
    period_s: Optional[int]


def parse_battery(body: str) -> Optional[Battery]:
    """'@SM BAT F=OK D=- fs=4 ds=- up=65 hb=30' -> Battery; None if it isn't a BAT line."""
    t = body.split(" ")
    if len(t) < 2 or t[:2] != ["@SM", "BAT"]:
        return None
    kv = _kv(t[2:])

    def text(key: str) -> Optional[str]:
        v = kv.get(key)
        return None if v in (None, "-") else v

    def number(key: str) -> Optional[int]:
        v = kv.get(key, "")
        return int(v) if v.isdigit() else None

    return Battery(text("F"), text("D"), text("fs"), text("ds"), number("up"), number("hb"))


@dataclass
class Event:
    kind: str  # PRESS, CTL or BAT
    slot: Optional[Slot] = None
    key: str = ""
    source: str = ""
    from_other: Optional[str] = None
    reserved: Optional[str] = None
    controller: Optional[str] = None
    connected: Optional[bool] = None
    battery: Optional[Battery] = None
    raw: bool = False  # PRESS: key is a raw command


def parse_event(body: str) -> Optional[Event]:
    t = body.split(" ")
    if len(t) >= 7 and t[1] == "PRESS":
        kv = _kv(t[7:])
        key, raw = parse_value(t[5])
        return Event("PRESS", Slot.from_tokens(t[2], t[3], t[4]), key, t[6], kv.get("from"), kv.get("res"), raw=raw)
    if len(t) == 4 and t[1] == "CTL":
        return Event("CTL", controller=t[2], connected=t[3] == "1")
    battery = parse_battery(body)
    if battery is not None:
        return Event("BAT", battery=battery)
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
    _events_on: bool = False

    # -- plumbing --------------------------------------------------------------------

    def _send(self, body: str) -> None:
        line = frame(body)
        if self.route is not None:
            line = self.route.wrap(line)
        self.stats["sent"] += 1
        self.transport.write_line(line)

    def _replies(self, deadline: float):
        """Valid @SM reply bodies until the deadline. Events go to on_event, not the caller.

        A BAT line goes to both: it's the reply to '@SM BAT' and also arrives unasked."""
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
                if event.kind != "BAT":
                    continue
            yield body

    def request(self, body: str, accept: Callable[[str], bool], expect_lines: int = 1) -> List[str]:
        """Sends `body` and returns the reply lines `accept` matches. Retries on silence and on
        '@SM ERR crc' / 'busy'. Raises ProtocolError on any other @SM ERR aimed at this
        request.

        If SHADOW stays silent, it may have rebooted: it keeps the reply route and the EVENTS
        subscription in RAM, so after a reboot its replies can go to the wrong WCB. In that
        case this says HELLO again (restoring EVENTS if they were on) and tries once more."""
        try:
            return self._request(body, accept, expect_lines)
        except Timeout:
            if body.startswith("@SM HELLO"):
                raise
            self.hello()
            if self._events_on:
                self.events(True)
            return self._request(body, accept, expect_lines)

    def _request(self, body: str, accept: Callable[[str], bool], expect_lines: int) -> List[str]:
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
                        if code == "busy":
                            time.sleep(0.05 * (attempt + 1))
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
        ready = parse_ready(self.request(body, lambda r: r.startswith("@SM READY "))[0])
        if ready.version != PROTOCOL_VERSION:
            raise ProtocolError("version", f"SHADOW speaks v{ready.version}; this client speaks v{PROTOCOL_VERSION}")
        return ready

    def get(self, slot: Slot) -> SlotState:
        want = f"@SM SLOT {slot} "
        return parse_slot_state(self.request(f"@SM GET {slot}", lambda r: r.startswith(want))[0])

    def set(self, slot: Slot, key: str, raw: bool = False) -> None:
        """Maps `slot` to stored command `key` ("" to unmap), or with `raw` to a raw command that
        SHADOW sends exactly as typed. Returns once SHADOW has written and verified it."""
        if raw:
            if not valid_raw(key):
                raise ValueError(f"a raw command is 1-15 printable ASCII characters: {key!r}")
            token = raw_token(key)
        else:
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
                             expect_lines=SLOT_COUNT + 1)
        if lines[-1] != f"@SM END DUMP {SLOT_COUNT}":
            raise ProtocolError("parse", lines[-1])
        states = [parse_slot_state(l) for l in lines[:-1]]
        if [s.slot.index for s in states] != list(range(SLOT_COUNT)):
            raise ProtocolError("dump order")
        return states

    def sum(self) -> str:
        reply = self.request("@SM SUM", lambda r: r.startswith("@SM SUM "))[0]
        m = re.match(rf"^@SM SUM ([0-9A-F]{{8}}) slots={SLOT_COUNT}$", reply)
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

    def battery(self) -> Battery:
        """Both controllers' battery levels. The unasked report is just as current, so either will do."""
        return parse_battery(self.request("@SM BAT", lambda r: r.startswith("@SM BAT "))[0])

    def events(self, on: bool) -> None:
        want = f"@SM OK EVENTS {1 if on else 0}"
        self.request(f"@SM EVENTS {1 if on else 0}", lambda r: r == want)
        self._events_on = on

    def listen(self, seconds: float) -> None:
        """Pumps incoming lines for a while so on_event sees presses and controller changes."""
        for _ in self._replies(time.monotonic() + seconds):
            pass

    def read_map(self, cached: Optional[List[SlotState]] = None) -> List[SlotState]:
        """The full map, skipping the ~9 s DUMP when `cached` still matches SHADOW's SUM."""
        if cached is not None and len(cached) == SLOT_COUNT and map_sum(cached) == self.sum():
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
    ap.add_argument("command", choices=["hello", "get", "set", "revert", "dump", "sum", "defaults", "cfg", "battery", "watch"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--raw", action="store_true", help="set: the value is a raw command, sent exactly as typed")
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
        client.set(Slot.parse(" ".join(a.args[:3])), a.args[3] if len(a.args) > 3 else "", raw=a.raw)
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
    elif a.command == "battery":
        print(client.battery())
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
