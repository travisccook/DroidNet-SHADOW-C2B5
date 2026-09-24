# Handoff: runtime button mapping (SHADOW-C2B5 side)

**Written:** 2026-09-24, at the end of the cloud session that designed and built this feature. You are the
Claude Code session working in the **SHADOW-C2B5** repo. A second session is working in the
**DroidNetSignalBooster** repo at the same time, building the Pi side. Section 7 covers how you two talk. Please
do talk to each other.

Claims below are marked **VERIFIED** (with how) or **ASSUMED**. Re-check anything ASSUMED before you rely on it.

---

## 0. Status, 2026-09-24 afternoon (the second SHADOW-C2B5 session)

Section 4 is partly done. Results are on
[PR #1](https://github.com/travisccook/SHADOW-C2B5/pull/1#issuecomment-5817847387).

**The droid runs `af4db5d`** (flashed 2026-09-24 about 17:05Z). That's PR #1, plus #2 (the build flag is now
`!python3`, because macOS has no `python`), #3 (a boot-log line break), and the line-reader fix for the lost first
request after power-up (below). That fix was verified on hardware through the USB port, which uses the same
reader: a request with leading noise bytes is answered, and junk before `@SM` or noise inside a line still drops it.

**How the owner works:** commit straight to `main`, with no branches or PRs, because they're the sole developer. This
replaces the branch/PR advice in sections 2, 4 and 7.

**VERIFIED on the droid:**

- boot
- the announce: `?WDP,DA` on WCB 1 shows `S2 Shadow fw e3f6b82`
- the whole WCB path in both directions, **on port S2**
- every verb
- CRCs on both legs
- EEPROM persistence across a Mega reset
- `@SM` over the Mega's USB port (step 11)
- a full droid power cycle (step 10, 16:44Z). EEPROM (fmt=1, gen, slots, prefix) and WCB 1's NVS (S2 broadcast output
  off, `alarm`/`sadsound`) all survived. But the **first `@SM` request after power-up got no reply**; the retry and
  everything after answered normally. The likely cause (not proven): the listener opening ttyUSB0 about 35 s after
  power-on resets WCB 1, the reset glitches S2's TX, and SHADOW's line reader is left holding an unterminated noise
  byte, so it skips the next line. With broadcast output off, nothing else ends that line first. Fixed in `af4db5d`:
  leading noise bytes are now ignored. Confirmed on the droid: after DroidNet 2.3.6's deploy reset WCB 1
  (listener reopened at 17:16:54Z), the first `;S2@SM HELLO` was answered on the first attempt, in 98 ms.

**Still to do** (the controllers were away):

- section 4 step 5 (driving), step 6 (real presses, including foot CIRCLE+LEFT `alarm` and foot RIGHT `sadsound`)
  and step 9 (editing while driving)

State left on the droid: EEPROM formatted, all slots on their defaults (SUM `65096899`), EVENTS off.

**WCB 1 S2 has broadcast output off** (`?BCAST,OUT,S2,OFF`, the owner's OK). Mesh broadcasts no longer reach
SHADOW; routed `;S2...` lines still do. It's in WCB 1's NVS, and the hourly backups capture it.

**Before today's flash:**

- The Mega ran `e88b489`, an earlier announce-only build (branch `wdp-announce-standalone`).
- Rollback images in `/root/shadow-backups/` on c2-body:
  - `SHADOWC2B5_wdp_e88b489.hex` (e88b489)
  - `shadow-pre-wdp-2026-09-24*.hex` (the original firmware)

**Reaching c2-body:**

- **Host key.** It was reimaged from the 2026-09-14 DroidNet image, so its SSH host key changed to ED25519
  `SHA256:fLk1ucrsuW3AfUnto+1ulgYxd1Kh6VlM7OSiBB+VZL0`. That key is baked into
  `DroidNetSignalBooster/images/droidnet-lite-zero2-w-debug-20260914-223145.img`, so every Pi flashed from that image
  shares it. Verify against the image; don't turn host-key checking off.
- **Login:** root, with the DroidNet project's standard password (see DroidNetSignalBooster's docs).
- **Refused logins:** logins are sometimes refused. Retry only when the failure is "Permission denied".
- **Sending `@SM` without stopping DroidNet's listener:**
  1. `POST http://localhost/api/wcb/command` with `{"port":"/dev/ttyUSB0","command":";S2@SM ... *<crc>","timeout":0.5}`.
     It always reports a timeout.
  2. Read the reply from `GET /api/serial/history?port=/dev/ttyUSB0`.

**WCB 1 changes:**

- **Two stored commands added.** `alarm` and `sadsound` were referenced by the built-in defaults but missing from WCB 1,
  which was already true under the old firmware. At the owner's request, both were recreated:
  - `alarm` from `C2B5/WCB/wcb-commands-2025-07-10.json`
  - `sadsound` from `C2B5/WIPDocs/NEWSERIAL.md`
  - the old HCR codes converted to `;H,...`, following the conversion WCB 1's other commands went through
- **Hourly backup.** DroidNet runs a WCB 1 `?BACKUP` every hour, at about :46 on 2026-09-24 (it's timed from boot).
  Its parser on c2-body takes the line after each "For Configured Boards" header as the restore line. So keep EVENTS
  off across it until DroidNet ships its fix.

## 1. What you own

- **The firmware side of runtime button mapping.** DroidNet edits SHADOW's button → WCB-command mappings over the
  WCB link. SHADOW stores them in EEPROM and the controllers stay connected.
- **The `@SM` protocol contract.** The reference is `WIPDocs/COMMAND_MAPPING_DESIGN.md`, section "Protocol
  reference (v1)". Its executable form is `tools/shadowmap/` (reference client, host simulator, contract tests).
  The DroidNet session builds against these, so any change to them goes through section 7.
- **SHADOW's WCB device announce (WDP-DA).** This branch also implements
  `WIPDocs/HANDOFF_2026-09-23_shadow_wdp_device_announce.md`, the owner's earlier handoff for it. Section 3 says how
  the two fit together.

Read, in this order:

1. `WIPDocs/COMMAND_MAPPING_DESIGN.md`: the spec for everything on this branch.
2. `WIPDocs/HANDOFF_2026-09-23_shadow_wdp_device_announce.md`: the owner's announce decisions, and the only
   flashing procedure for c2-body. Section 4 below uses it.

## 2. State of the world

**It's on `main`.** [PR #1](https://github.com/travisccook/SHADOW-C2B5/pull/1) was merged on 2026-09-24 at the
owner's request, **before any hardware test**. So `main` holds firmware that has never run on the droid, and it
controls motors: nobody drives on it until the section 4 checks pass. What the droid runs today is unknown; the dump
in section 4 captures it. Make any fix on a new branch (in a worktree) with its own PR, because `main` is what gets
flashed.

**What changed:**

- `lib/ShadowMap/` (new, no Arduino dependency) holds the whole feature:
  - slot layout and lookup rule
  - EEPROM store (A/B headers, a CRC per slot, a background writer)
  - the `@SM` protocol
  - the Serial1 TX queue
  - `sm::Runtime`, the per-pass glue, including the announce
- `src/main.cpp` now reads the controllers into `sm::PadState` and calls the runtime. The old function-code
  `switch` and the SimpleButtonMapping tables are gone.
- `src/default_mappings.cpp` holds the built-in mappings. That's today's mapping, with volume and brightness
  removed as the user asked: PS + direction is unmapped on both controllers.
- **Bug fixed:** L3+L1 on the dome controller used to toggle foot over-speed as well as firing Front Holo Center.
  It now only fires Front Holo Center.
- **The announce.** SHADOW sends `@WDP1 {"type":"Shadow","fw":"<commit>"}` to its WCB 0.5-3 s after boot, then
  every 25-30 s. It sends none while a controller fault is active. `SHADOW_WDP_ANNOUNCE` in `include/config.h` turns
  it off.
- **The firmware version is the git commit.** `scripts/git_rev.py` runs from `platformio.ini`
  (`build_flags = !python ...`) and sets `SHADOW_FW_VERSION`, for example `dd69480`, with `+` appended when tracked
  files had uncommitted changes. It shows in the announce, in `@SM READY fw=`, and on the boot log (`Firmware ...`).
  VERIFIED (built, found the string in the ELF). Every new commit rebuilds everything.

**Tests, all passing on the branch head.** VERIFIED (ran them at the end of the session):

| Command | Result |
| --- | --- |
| `pio test -e native` | 82 passed, 2 skipped (AVR-only) |
| `pio test -e simavr --without-uploading` | 84 passed (emulated ATmega2560, real avr-libc EEPROM) |
| `python3 -m unittest tools/shadowmap/test_shadowmap.py` | 18 passed |
| `pio run` | builds; static SRAM 2733 B (was 3801 on `main`), flash 49,706 B |
| `pio check` | 0 high, 66 medium (the baseline on `main` was 95) |

- **Old-vs-new sweep.** `test_legacy_equivalence` replays all 32,128 button states through the old table code,
  frozen from `13c7dbc`, and through the new resolver. They are byte-identical except the removed
  volume/brightness combos. VERIFIED.
- **Loop cost.** The worst-case time the mapping adds to one loop pass, measured on the emulated 16 MHz part, is
  0.9 ms (a pass during `DUMP`). A pass that sends the announce takes 0.24 ms. `test_avr` fails if any pass reaches
  1 ms. VERIFIED under simavr only; ASSUMED to hold on hardware.

**Never observed on real hardware** when this was written. Update: all five were VERIFIED on the droid on 2026-09-24
(section 0). The one exception: after a quick reflash, the WCB prints no new `[WDP-DA]` line, because SHADOW's
record doesn't expire in under 90 s; `?WDP,DA` shows the new `fw`. Originally ASSUMED, from reading the WCB 6.2.1
source (`travisccook/Wireless_Communication_Board-WCB` at `66845b9`):

- that the WCB delivers `;S<x>@SM ...` from the Pi to SHADOW's Serial1 as `@SM ...\r`
- that it prints SHADOW's `;S0@SM ...` replies on its USB port
- that it captures `@WDP1` lines and prints `[WDP-DA] S<x>: Shadow fw <commit>`
- **that the WCB → SHADOW wire exists at all.** Until now SHADOW only ever *sent* on Serial1, so nothing proves the
  WCB port's TX is wired to the Mega's RX1 (pin 19). If it isn't, the USB bench test works and every request over the
  WCB goes unanswered.
- that the Mega reads the WCB's 3.3 V TX reliably. The ATmega2560 needs 0.6 × Vcc = 3.0 V for a high, so the margin
  is about 0.3 V. That usually works; the droid is electrically noisy.

## 3. How this branch relates to the 2026-09-23 announce handoff

The owner recorded the announce design in that handoff and asked for it not to be re-litigated. This branch follows it
with one exception, which the owner chose later.

| Owner's decision (2026-09-23) | This branch |
| --- | --- |
| Type `Shadow` | Same |
| `@WDP1` every 25-30 s, a fresh random interval each time | Same (xorshift, seeded from `random()` after `randomSeed(analogRead(0))`) |
| First announce soon after `setup()` | 0.5-3 s after `setup()`, as the research recommended |
| Non-blocking, gated by `availableForWrite()`, line ≤ 63 bytes with a `static_assert` | Goes through `sm::TxQueue`, which writes only what `availableForWrite()` allows, so the line never interleaves with a command or reply. The `static_assert` is in `main.cpp`; the line is 39-43 bytes. |
| `fw` = the git commit, from a PlatformIO `!python` build flag | Same (`scripts/git_rev.py`), plus `+` for a tree with uncommitted changes |
| No `caps` | Same (an earlier draft here sent `shadow.map`; it's gone) |
| No announce while a controller fault is active | Same (`serviceMapping(true)` on the `readUSB()` fault path skips it; a due announce goes out when the fault clears) |
| **Announce only. SHADOW never reads Serial1.** | **Superseded.** On 2026-09-24 the owner chose to edit mappings over the WCB link so the controllers stay connected. SHADOW now reads `@SM` lines on Serial1, at most 64 bytes a pass, and drops everything else. |

The rest of that handoff still stands and is yours to use:

- the flashing procedure for c2-body (section 4 uses it)
- "Checking it worked" (the read-only `curl` of the serial history first)
- the electrical and wiring facts
- the owner's open questions (port label, level shifting, power feed)
- recording SHADOW's WCB port in `CLAUDE.md` once the announce reveals it

Treat it as done by PR #1; don't implement the announce a second time. A note at its top says so.

## 4. Your first job: help the user validate on the droid

Only the user can run these, and each step that touches the droid needs their explicit go-ahead. Walk them through,
record the results on the PR, and fix anything that breaks.

**Where things are** (from the 2026-09-23 handoff, VERIFIED by the owner on that date):

- the Mega ADK is plugged into c2-body at
  `/dev/serial/by-id/usb-Arduino_Srl_Arduino_Mega_ADK_75533353437351216112-if00`. Always use the by-id path.
- WCB 1 is `/dev/ttyUSB0` on c2-body. So the Pi and SHADOW share WCB 1, where SHADOW's `;W1;C<key>` commands run.
- `/dev/ttyACM0` on c2-body is the Pololu Maestro, not SHADOW
- SHADOW's port on WCB 1 is S1, S2 or S5 (S3 is the HCR, S4 the CHIRP)

**Steps:**

1. **Coordinate.** Tell the DroidNet session before anything opens a port or power-cycles c2-body. Also ask the user
   whether session `droidnetsignalbooster-1d` (CHIRP work, named in the 2026-09-23 handoff) is still active on
   c2-body.
2. **Build.** From a clean tree, with the real `include/controller_macs.h` in place, run `pio run`. Record the commit;
   the boot log and the announce will show it, with no `+`.
3. **Dump the current firmware, then flash.** Follow steps 2-6 of "Flashing SHADOW" in the 2026-09-23 handoff
   exactly:
   - motor power off, and wheels up for the first drive
   - the USB power check
   - `avrdude` dump over SSH first, since it is the only rollback
   - flash with Board Type **Arduino Mega**, **never Auto-detect**

   `pio run -t upload` from the user's machine doesn't apply: the Mega is on c2-body. `avrdude -D` doesn't erase
   EEPROM. The old firmware never used it, so the first boot should find no valid mapping and use the built-in
   defaults (ASSUMED; step 4 shows it).
4. **Boot.** Expect the boot lines from that handoff's step 7 (`<PVV100,PVA100,PVB100>` and `P1` on Serial1). The
   debug log (115200 on the Mega's USB) shows `Button mapping: built-in defaults, prefix ;W1;C` and
   `Firmware <commit>`. Re-pair the controllers with the PS button.
5. **Driving is unchanged:**
   - foot drive and dome
   - PS+✕ / PS+○ disable and enable the stick
   - foot L3+L1 toggles over-speed, and dome L3+L1 does **not** (it fires `frontholocenter`)
6. **Every mapped combo still sends what it used to.** The verbose log prints
   `ACTIVATED: F L1 UP -> extremehappysou`. PS + direction prints `(unmapped)`.
7. **The announce.** Use the least invasive check first:
   - on c2-body, run `curl -s 'http://localhost/api/serial/history?port=/dev/ttyUSB0&limit=500' | grep WDP-DA` and
     look for `[WDP-DA] S<x>: Shadow fw <commit>`. That gives SHADOW's S-port `x`.
   - the WCB prints that line only when it first hears SHADOW, or after a 90 s silence. If it scrolled away, send
     `?WDP,DA` from the Booster's WCB tab.
   - once `x` is known, record it in `CLAUDE.md` (Serial Interfaces)
8. **The WCB path.** From the Booster's WCB tab on WCB 1, send `;S<x>@SM HELLO *C6246128` and expect
   `@SM READY v1 fw=<commit> ...`. The CRC is for exactly `@SM HELLO`. VERIFIED with `zlib.crc32`.
   - **No reply**, but the announce arrived: suspect the WCB TX → Mega RX1 wire (section 2), and ask the user to
     check it.
   - Then, on c2-body with DroidNet's listener for `/dev/ttyUSB0` stopped (two programs can't share a serial port), run
     `tools/shadowmap/shadowmap.py --wcb /dev/ttyUSB0 --shadow-port <x> dump`.
   - Then `set F NONE L3 scanning`, click L3 on the foot stick (the WCB should run `scanning`), and
     `revert F NONE L3`.
9. **Editing while driving.** Run repeated `dump`s while driving. Driving must feel exactly as before.
10. **Persistence.** Set a slot, power-cycle the droid, and check it's still set.
11. **Optional USB bench test.** This resets the Mega and drops the controllers, so only with the user's go-ahead.
    Open the Mega's port at 115200 (the Booster's serial monitor, or `miniterm` over SSH) and type `@SM HELLO`,
    `@SM GET F L1 UP` or `@SM DUMP`. The CRC is optional on USB.
12. **Optional.** Cut power during a `SET`. After reboot the slot is the old value, the new one, or its default,
    and every other slot is intact.

Tell the DroidNet session when steps 7-9 pass, along with the S-port and any timings you saw: that's when it can test
against the real droid. If anything goes wrong, roll back with the dump (the 2026-09-23 handoff, "Rollback"), then fix
it on a branch and PR.

## 5. After that

- **Volume and brightness.** The user will create new WCB stored commands on WCB 1. They get mapped at runtime
  (DroidNet, or `shadowmap.py ... set D PS UP <key>`). Only add them to `src/default_mappings.cpp` if the user wants
  them as defaults, and if you do, run the tests.
- **Spreadsheet: retired** (the owner, 2026-09-24). DroidNet replaces it. The update script is gone, and
  `Reference/ControlMapping.xlsx` is left as it is.
- **Protocol requests from DroidNet.** See section 7. One idea on the table is a "learn" mode: report a press
  without firing it. It needs the user's go-ahead and an automatic timeout so the droid can never be left muted.
- **Optional tidy-up.** `?BCAST,OUT,S<x>,OFF` on SHADOW's WCB port stops mesh broadcasts reaching SHADOW. The
  firmware ignores them anyway. It's a WCB config write, so the owner approves it.

## 6. Rules and traps

- **Safety.** `loop()` must never block. Everything in the mapping is bounded and non-blocking, and `test_avr`
  guards the timing. Anything written to Serial1 after `setup()` must go through `sm::TxQueue` (`wcbTx`); a direct
  `Serial1.print` can land inside a reply line.
- **The WCB rules the protocol depends on** are in the design doc:
  - no bare lines to the WCB (it broadcasts them); the `@WDP1` announce is the one exception
  - no `^`, and no `;T`/`;t`
  - replies wrapped in `;S0`
- **Never commit real MAC addresses.** Copy `include/controller_macs.h` into any fresh clone or worktree to build;
  it's gitignored. From the 2026-09-23 handoff: work in a worktree, and never use bare `git stash`.
- **Commit trailers.** Follow the repo's convention: `Co-Authored-By:` naming the model that wrote the commit, and
  `Claude-Session:` with your session's URL.
- **Don't run `pio pkg install ... -e <env>`.** It rewrote `platformio.ini` and stripped every comment once. That
  was repaired, and the diff against `main` is only the added test envs and the `git_rev.py` flag. VERIFIED.
- **simavr.**
  - Always pass `--without-uploading`, or PlatformIO tries to flash a board.
  - If a simavr test hangs, the test program ran out of the 8 KB SRAM (Unity string literals live in RAM on
    AVR), so split the suite.
  - Test files must `#include <unity.h>` themselves.
- **Reading the WCB source.** `Code/WCB/WCB.ino` has non-UTF8 bytes, so use `grep -a`.
- **Before every push:** run all three test commands and `pio run`. Keep the design doc's numbers and protocol
  reference in step with the code.

## 7. Working with the DroidNet session

The DroidNet session builds the Pi side: a backend (`ShadowLink`) on DroidNet's existing WCB listener, HTTP/SSE
endpoints, and the SHADOW tab. Its docs are on DroidNetSignalBooster `main`
([PR #8](https://github.com/travisccook/DroidNetSignalBooster/pull/8), and
[PR #9](https://github.com/travisccook/DroidNetSignalBooster/pull/9), which brought discovery in line with section 3):

- `docs/superpowers/specs/2026-09-24-shadow-button-map-design.md`: backend design
- `docs/superpowers/specs/2026-09-24-shadow-button-map-ui-design.md` plus `shadow-button-map-ui/`: UI and
  clickable mockup
- `docs/superpowers/HANDOFF_2026-09-24_shadow_button_map.md`: its handoff

**How to talk:**

1. **Directly.** Both sessions run on the user's machine. Use `ListAgents` to find the other local session (it's
   working in DroidNetSignalBooster) and `SendMessage` to reach it. Introduce yourself when you start, with your
   branch and head commit. If your build doesn't have these tools, use 2.
2. **In writing.** Comment on the PRs: SHADOW-C2B5 #1 (merged, but still this feature's thread), your fix PRs, and
   the DroidNet session's implementation PR, whose link it will post on #1. This is the durable record for decisions
   and protocol changes.
3. **Through the user,** for decisions that are theirs (merging, hardware, flashing, product questions).

**Contract rules:**

- **Each session edits only its own repo.** To change something in the other repo, send a message and let that
  session do it.
- **You own the protocol.** When DroidNet needs a change:
  1. agree it in a message
  2. implement it here: firmware, `shadow_sim.cpp`, `shadowmap.py`, the tests, and the design doc's protocol
     reference
  3. bump the READY version (`v1` → `v2`) if old clients would break
  4. message the DroidNet session with the commit SHA
- **The DroidNet session tests against your simulator.** It builds `tools/shadowmap/shadow_sim.cpp` from a local
  checkout of this repo, so keep that building with one `g++` command (see its header), and tell the DroidNet
  session if the command changes. The simulator reports `fw=sim`.
- **One device on c2-body at a time.** Agree in a message before either of you opens a port on c2-body, flashes, or
  power-cycles it.
- **Things to send it without being asked:**
  - hardware validation results (the S-port, round-trip times)
  - any protocol behaviour you find on the droid that differs from the spec
  - firmware that's merged or flashed, with its commit

## 8. Open questions for the user

1. ~~Is the WCB 1 → SHADOW wire (to the Mega's RX1, pin 19) connected?~~ Yes: `@SM` requests on S2 are answered
   (2026-09-24).
2. From the 2026-09-23 handoff:
   - ~~label SHADOW's WCB port?~~ Done: WCB 1 S2 is labeled `Shadow` (`?LABEL,S2,Shadow`, 2026-09-24 15:07Z).
   - a divider on SHADOW's TX to the WCB? Still open.
   - is SHADOW fed on VIN or 5 V? That decides the USB power blocker. Still open.
3. ~~Add a learn mode for shows?~~ Not for now (the owner, via the DroidNet session, 2026-09-24); maybe later.
4. Should the new volume and brightness commands become built-in defaults, or stay DroidNet-only?
