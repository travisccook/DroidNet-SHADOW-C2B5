#!/usr/bin/env python3
"""Fails when a firmware .hex holds a controller address: as text ("00:1A:2B:3C:4D:05", either
case) or as its six bytes in either order. The release build runs it so no owner's addresses
ship (scripts/build_release.sh).

    python3 scripts/check_release_hex.py <firmware.hex> [--macs-from include/controller_macs.h] [--mac <addr> ...]

Exit 0: none found. 1: found (each is printed). 2: bad input.
"""

import argparse
import re
import sys

MAC_RE = re.compile(r"\b([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})\b")
NOT_ADDRESSES = {"00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF"}


def read_ihex(path):
    """Every data byte of an Intel HEX file (record types 00, 01, 02, 04), in address order; gaps
    read as 0xFF."""
    data = {}
    base = 0
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError(f"line {n}: not Intel HEX")
            raw = bytes.fromhex(line[1:])
            if len(raw) < 5 or len(raw) != raw[0] + 5 or sum(raw) & 0xFF:
                raise ValueError(f"line {n}: bad length or checksum")
            addr, kind, payload = (raw[1] << 8) | raw[2], raw[3], raw[4:4 + raw[0]]
            if kind == 0:
                for i, b in enumerate(payload):
                    data[base + addr + i] = b
            elif kind == 1:
                break
            elif kind == 2:
                base = ((payload[0] << 8) | payload[1]) << 4
            elif kind == 4:
                base = ((payload[0] << 8) | payload[1]) << 16
    if not data:
        return b""
    lo, hi = min(data), max(data)
    return bytes(data.get(a, 0xFF) for a in range(lo, hi + 1))


def macs_in_header(path):
    """The addresses in a controller_macs.h, upper case, placeholders and all-0/all-FF left out."""
    with open(path) as f:
        found = [m.group(1).upper() for m in MAC_RE.finditer(f.read())]
    return [m for m in found if m not in NOT_ADDRESSES]


def findings(image, macs):
    out = []
    upper = image.upper()
    for mac in macs:
        raw = bytes.fromhex(mac.replace(":", ""))
        if mac.upper().encode() in upper:
            out.append(f"{mac} as text")
        if raw in image:
            out.append(f"{mac} as bytes")
        if raw[::-1] in image:
            out.append(f"{mac} as bytes, least significant first")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("hex")
    ap.add_argument("--macs-from", action="append", default=[])
    ap.add_argument("--mac", action="append", default=[])
    args = ap.parse_args(argv)
    try:
        image = read_ihex(args.hex)
        macs = [m.upper() for m in args.mac]
        for header in args.macs_from:
            macs += macs_in_header(header)
    except (OSError, ValueError) as e:
        print(f"check_release_hex: {e}", file=sys.stderr)
        return 2
    if not macs:
        print("check_release_hex: no addresses to look for (no controller_macs.h here)")
        return 0
    found = findings(image, macs)
    for f in found:
        print(f"FOUND {f} in {args.hex}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
