#!/usr/bin/env python3
"""Send @SM requests to SHADOW through DroidNet's WCB listener, without stopping it.

Runs ON the Pi that hosts WCB 1 (c2-body). It POSTs each request, with its CRC added, to DroidNet's
/api/wcb/command and prints the CRC-checked @SM replies it finds in /api/serial/history, each with its
round-trip time. The POST always reports a timeout, because @SM replies aren't WCB replies; that's expected.

usage (from the Mac; there's no need to copy the script over):
    ssh root@c2-body.local 'python3 - [--port S2] [--wait SEC] "<VERB args>" ...' < tools/shadowmap/sm_via_api.py
e.g.
    ... 'python3 - HELLO SUM "GET F L1 UP" "EVENTS 1"' < tools/shadowmap/sm_via_api.py
    ... 'python3 - --wait 12 DUMP' < tools/shadowmap/sm_via_api.py       # a DUMP (144 slots) takes about 9 s

One requester at a time: check that nothing else (DroidNet's SHADOW tab, another session) is using @SM first.
"""
import json, sys, time, urllib.request, zlib

API = "http://localhost"
WCB = "/dev/ttyUSB0"


def crc_line(body):
    return "%s *%08X" % (body, zlib.crc32(body.encode()) & 0xFFFFFFFF)


def check(line):
    if " *" not in line:
        return False
    body, crc = line.rsplit(" *", 1)
    return crc.upper() == "%08X" % (zlib.crc32(body.encode()) & 0xFFFFFFFF)


def history():
    with urllib.request.urlopen("%s/api/serial/history?port=%s&limit=500" % (API, WCB), timeout=5) as r:
        return json.load(r)["history"]


def send(cmd):
    req = urllib.request.Request("%s/api/wcb/command" % API, method="POST",
                                 data=json.dumps({"port": WCB, "command": cmd, "timeout": 0.5}).encode(),
                                 headers={"Content-Type": "application/json"})
    try:
        urllib.request.urlopen(req, timeout=5).read()
    except Exception:
        pass  # the endpoint times out on @SM lines; replies are read from history


args = sys.argv[1:]
port, wait = "S2", 1.5
while args and args[0].startswith("--"):
    if args[0] == "--port":
        port = args[1]
    elif args[0] == "--wait":
        wait = float(args[1])
    args = args[2:]

for req in args:
    line = ";%s%s" % (port, crc_line("@SM " + req))
    t0 = time.time()
    send(line)
    time.sleep(wait)
    replies = [h for h in history() if h["timestamp"] >= t0 - 0.05 and h["line"].startswith("@SM ")]
    print("> %s" % line)
    for h in replies:
        print("  +%4d ms %s %s" % ((h["timestamp"] - t0) * 1000, "ok " if check(h["line"]) else "BAD", h["line"]))
    if not replies:
        print("  (no reply)")
