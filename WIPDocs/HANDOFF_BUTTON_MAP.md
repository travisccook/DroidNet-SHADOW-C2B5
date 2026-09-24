# Handoff: runtime button mapping (SHADOW-C2B5 side)

**Written:** 2026-09-24, at the end of the cloud session that designed and built this feature. You are the
Claude Code session working in the **SHADOW-C2B5** repo. A second session is working in the
**DroidNetSignalBooster** repo at the same time, building the Pi side. Section 6 covers how you two talk. Please
do talk to each other.

Claims below are marked **VERIFIED** (with how) or **ASSUMED**. Re-check anything ASSUMED before you rely on it.

---

## 1. What you own

- **The firmware side of runtime button mapping.** DroidNet edits SHADOW's button → WCB-command mappings over the
  WCB link. SHADOW stores them in EEPROM and the controllers stay connected.
- **The `@SM` protocol contract.** The reference is `WIPDocs/COMMAND_MAPPING_DESIGN.md`, section "Protocol
  reference (v1)". Its executable form is `tools/shadowmap/` (reference client, host simulator, contract tests).
  The DroidNet session builds against these, so any change to them goes through section 6.

Start by reading `WIPDocs/COMMAND_MAPPING_DESIGN.md`. It is the spec for everything on this branch.

## 2. State of the world

**Branch:** `claude/arduino-command-mapping-config-x49d73`. The PR against `main` is open and **not merged**. The
firmware hasn't run on the droid, and it controls motors, so it merges only after the hardware checks in section 3
pass and the user says so. `main` is at `13c7dbc`, the branch point. VERIFIED (`git log`).

**What changed:**

- `lib/ShadowMap/` (new, no Arduino dependency) holds the whole feature:
  - slot layout and lookup rule
  - EEPROM store (A/B headers, a CRC per slot, a background writer)
  - the `@SM` protocol
  - the Serial1 TX queue
  - `sm::Runtime`, the per-pass glue
- `src/main.cpp` now reads the controllers into `sm::PadState` and calls the runtime. The old function-code
  `switch` and the SimpleButtonMapping tables are gone.
- `src/default_mappings.cpp` holds the built-in mappings. That's today's mapping, with volume and brightness
  removed as the user asked: PS + direction is unmapped on both controllers.
- **Bug fixed:** L3+L1 on the dome controller used to toggle foot over-speed as well as firing Front Holo Center.
  It now only fires Front Holo Center.
- SHADOW announces itself to its WCB every 30 s (`@WDP1 {"type":"Shadow",...}`), for port discovery.
  `SHADOW_WDP_ANNOUNCE` in `include/config.h` controls it.

**Tests, all passing on the branch head.** VERIFIED (ran them at the end of the session):

| Command | Result |
| --- | --- |
| `pio test -e native` | 80 passed, 2 skipped (AVR-only) |
| `pio test -e simavr --without-uploading` | 82 passed (emulated ATmega2560, real avr-libc EEPROM) |
| `python3 -m unittest tools/shadowmap/test_shadowmap.py` | 18 passed |
| `pio run` | builds; static SRAM 2712 B (was 3801), flash 49,306 B |
| `pio check` | 0 high, 87 medium (the baseline on `main` was 95) |

- **Old-vs-new sweep.** `test_legacy_equivalence` replays all 32,128 button states through the old table code,
  frozen from `13c7dbc`, and through the new resolver. They are byte-identical except the removed
  volume/brightness combos. VERIFIED.
- **Loop cost.** Worst-case time the mapping adds to one loop pass, measured on the emulated 16 MHz part, is
  0.9 ms. `test_avr` fails if it reaches 1 ms. VERIFIED under simavr only; ASSUMED to hold on hardware.

**Never observed on real hardware.** All ASSUMED from reading the WCB 6.2.1 source
(`travisccook/Wireless_Communication_Board-WCB` at `66845b9`):

- that the WCB delivers `;S<x>@SM ...` from the Pi to SHADOW's Serial1 as `@SM ...\r`
- that it prints SHADOW's `;S0@SM ...` replies on its USB port
- that it captures `@WDP1` lines and prints `[WDP-DA] S<x>: Shadow fw 2.0.0`

## 3. Your first job: help the user validate on the droid

Only the user can run these. Walk them through, record the results in the PR, and fix anything that breaks.

1. **Build and upload.** Copy `include/controller_macs.example.h` to `include/controller_macs.h` with the real
   MACs if it isn't there, then `pio run -t upload`. Opening the USB port resets the Mega.
2. **Boot.** In the serial monitor (115200) you should see
   `Button mapping: built-in defaults, prefix ;W1;C`.
3. **Driving is unchanged:**
   - foot drive and dome
   - PS+✕ / PS+○ disable and enable the stick
   - foot L3+L1 toggles over-speed, and dome L3+L1 does **not** (it fires `frontholocenter`)
4. **Every mapped combo still sends what it used to.** The verbose log prints
   `ACTIVATED: F L1 UP -> extremehappysou`. PS + direction prints `(unmapped)`.
5. **USB bench test.** Type these into the serial monitor; the CRC is optional on USB:
   - `@SM HELLO`
   - `@SM GET F L1 UP`
   - `@SM SET F NONE L3 scanning`, then click L3 on the foot stick: the WCB should run `scanning`
   - `@SM REVERT F NONE L3`
   - `@SM DUMP`
6. **Persistence.** Set a slot, power-cycle, and check it's still set. Re-upload the firmware and check again:
   uploads don't erase EEPROM.
7. **WCB path.** Close SHADOW's USB monitor.
   - In DroidNet's serial monitor on the **WCB** port, wait up to 35 s for `[WDP-DA] S<x>: Shadow fw 2.0.0`.
     That gives SHADOW's S-port `x`.
   - Send `;S<x>@SM HELLO *C6246128` and expect `@SM READY v1 fw=2.0.0 ...`. The CRC is for exactly `@SM HELLO`.
     VERIFIED with `zlib.crc32`.
   - Then run `tools/shadowmap/shadowmap.py --wcb /dev/ttyUSB0 --shadow-port <x> dump` on the Pi, with DroidNet's
     listener for that port stopped (two programs can't share a serial port).
8. **Editing while driving.** Run repeated `dump`s while driving. Driving must feel exactly as before.
9. **Optional.** Cut power during a `SET`. After reboot the slot is the old value, the new one, or its default,
   and every other slot is intact.

Tell the DroidNet session when steps 7-8 pass, along with the S-port and any timings you saw: that's when it can
test against the real droid. Then ask the user whether to merge the PR.

## 4. After that

- **Volume and brightness.** The user will create new WCB stored commands on WCB 1. They get mapped at runtime
  (DroidNet, or `shadowmap.py ... set D PS UP <key>`). Only add them to `src/default_mappings.cpp` if the user wants
  them as defaults, and if you do, run the tests and the spreadsheet script.
- **Spreadsheet.** `WIPDocs/scripts/update_control_mapping.py` now reads `src/default_mappings.cpp`. CLAUDE.md says
  never to change `Reference/`, yet the script writes `Reference/ControlMapping.xlsx`, so ask the user before you
  run it. The sheet is already stale for dome L2+DOWN (it says UNUSED; the firmware sends `redlight`). VERIFIED
  by running the script on a copy.
- **Protocol requests from DroidNet.** See section 6. One idea on the table is a "learn" mode: report a press
  without firing it. It needs the user's go-ahead and an automatic timeout so the droid can never be left muted.
- **Optional tidy-up.** `?BCAST,OUT,S<x>,OFF` on SHADOW's WCB port stops mesh broadcasts reaching SHADOW. The
  firmware ignores them anyway.

## 5. Rules and traps

- **Safety.** `loop()` must never block. Everything in the mapping is bounded and non-blocking, and `test_avr`
  guards the timing. Anything written to Serial1 after `setup()` must go through `sm::TxQueue` (`wcbTx`); a direct
  `Serial1.print` can land inside a reply line.
- **The WCB rules the protocol depends on** are in the design doc:
  - no bare lines to the WCB (it broadcasts them)
  - no `^`, and no `;T`/`;t`
  - replies wrapped in `;S0`
- **Don't run `pio pkg install ... -e <env>`.** It rewrote `platformio.ini` and stripped every comment once. That
  was repaired, and the diff against `main` is only the added test envs. VERIFIED.
- **simavr.**
  - Always pass `--without-uploading`, or PlatformIO tries to flash a board.
  - If a simavr test hangs, the test program ran out of the 8 KB SRAM (Unity string literals live in RAM on
    AVR), so split the suite.
  - Test files must `#include <unity.h>` themselves.
- **Reading the WCB source.** `Code/WCB/WCB.ino` has non-UTF8 bytes, so use `grep -a`.
- **Before every push:** run all three test commands and `pio run`. Keep the design doc's numbers and protocol
  reference in step with the code.

## 6. Working with the DroidNet session

The DroidNet session builds the Pi side: a backend (`ShadowLink`) on DroidNet's existing WCB listener, HTTP/SSE
endpoints, and the SHADOW tab. Its docs were merged to DroidNetSignalBooster `main` in
[PR #8](https://github.com/travisccook/DroidNetSignalBooster/pull/8) (`42fad93`):

- `docs/superpowers/specs/2026-09-24-shadow-button-map-design.md`: backend design
- `docs/superpowers/specs/2026-09-24-shadow-button-map-ui-design.md` plus `shadow-button-map-ui/`: UI and
  clickable mockup
- `docs/superpowers/HANDOFF_2026-09-24_shadow_button_map.md`: its handoff

**How to talk:**

1. **Directly.** Both sessions run on the user's machine. Use `ListAgents` to find the other local session (it's
   working in DroidNetSignalBooster) and `SendMessage` to reach it. Introduce yourself when you start, with your
   branch and head commit. If your build doesn't have these tools, use 2.
2. **In writing.** Comment on the other repo's PR: this one is SHADOW-C2B5 #1, and the DroidNet session will open
   its own implementation PR and post the link on #1. This is the durable record for decisions and protocol changes.
3. **Through the user,** for decisions that are theirs (merging, hardware, product questions).

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
  session if the command changes.
- **Things to send it without being asked:**
  - hardware validation results (the S-port, round-trip times)
  - any protocol behaviour you find on the droid that differs from the spec
  - firmware that's merged or flashed

## 7. Open questions for the user

1. Is the Pi plugged into WCB 1, where SHADOW's `;W1;C<key>` commands run? If not, DroidNet's stored-command picker
   needs another way to reach them.
2. Merge the firmware PR after the section 3 checks?
3. Add a learn mode for shows?
4. Should the new volume and brightness commands become built-in defaults, or stay DroidNet-only?
