# Handoff: SHADOW sends WDP Device Announce, then flash it (2026-09-23)

> **Update 2026-09-24: the announce is implemented** by [PR #1](https://github.com/travisccook/DroidNet-SHADOW-C2B5/pull/1)
> (runtime button mapping), following the decisions below with one exception: SHADOW now also reads Serial1, for
> DroidNet's `@SM` mapping requests. [HANDOFF_BUTTON_MAP.md](HANDOFF_BUTTON_MAP.md) section 3 compares the two point by
> point. Don't implement the announce again. The flashing, checking and electrical sections below still apply, and
> that handoff's section 4 uses them.

**For:** a new session started in `~/Repos/SHADOW-C2B5`.
**Goal:** SHADOW announces itself to WCB 1 with WDP-DA, and the new firmware is flashed to the Mega ADK on c2-body.
**Status:** Nothing is written yet. The design was agreed on 2026-09-22 and is recorded below. Everything here was
researched read-only on 2026-09-23 and adversarially verified; no serial port on the droid was opened.

## The owner's decisions (do not re-litigate)

- **WDP-DA for our boards was paused** on 2026-09-22 until Greg Hulette ships multi-device-per-port WDP-DA
  (`greghulette/Wireless_Communication_Board-WCB` issue #19; still OPEN, no comments on 2026-09-23).
- **On 2026-09-23 the owner lifted the pause for SHADOW only.** SHADOW is alone on its WCB port, so #19 does not
  block it. The RSeries, Flthy and PSI announces stay paused.
- **The Mega ADK on c2-body is SHADOW.** The owner confirmed it on 2026-09-23. It is on port `1-1.1.1`, 16U2 serial
  `75533353437351216112`, `/dev/serial/by-id/usb-Arduino_Srl_Arduino_Mega_ADK_75533353437351216112-if00` (today
  `ttyACM2`, but ACM numbers move, so always use the by-id path).
- **The agreed design.** Source: the Booster project's memory, `wdp-da-multi-device-effort.md`.
  - The type is **`Shadow`**, spelled exactly as in the shared list (`WCB/Wizard/device-labels.js:28`). `Shadow RC` is
    a different entry.
  - **Announce only.** SHADOW never reads Serial1.
  - `@WDP1` every 25–30 s, with a fresh random interval each time.
  - **Non-blocking, gated by `Serial1.availableForWrite()`.**
  - **`fw` = the git commit, injected at build time through a PlatformIO `!python` dynamic build flag.** Check the
    exact `build_flags = !python <script>` syntax in the PlatformIO docs before writing it.
  - **No `caps`.** The `droidnet.contract` cap is for the forks, not SHADOW.

## Process

Use the full design process: brainstorming, then a spec and a plan, then execution. The design is already agreed, so
brainstorming should be short: confirm the open questions below, then write the spec. Following this repo's CLAUDE.md,
generated `.md` files go in `WIPDocs/`. Work in a worktree, never bare `git stash`, and copy the gitignored
`include/controller_macs.h` into any fresh clone or worktree, or no controller can connect. Put these trailers on
every commit:

```
Co-Authored-By: <the Claude model that actually wrote the commit> <noreply@anthropic.com>
Claude-Session: <this session's URL>
```

## The protocol, as the WCB firmware actually implements it

Spec: `~/Repos/Wireless_Communication_Board-WCB/docs/WDP_DEVICE_ANNOUNCE.md`. Receiver: `Code/WCB/WCB_WDP.cpp`
(about lines 790-883) and `WCB.ino` (the `processIncomingSerial` hook near line 7017).

**Line format**

- One line, starting with exactly `@WDP1`, then one JSON object: `@WDP1 {"type":"Shadow","fw":"<ver>"}`.
- End it with CR or LF. The WCB splits lines on either, and SHADOW's other lines all end in `\r`.
- The spec caps a line at 200 characters. The receiver truncates `type` to 24 characters and `fw` to 27, and
  `?WDP,DA` prints `fw` in a 14-character column, so **keep `fw` to 14 characters or fewer**.
- The receiver replaces `,` and `]` with `_` and handles no escapes, so no commas, brackets or quotes in values.

**How the WCB handles it**

- The line is consumed. After `trim()`, a line that starts with `@WDP` goes to `wdpDaHandleLine` and **is never
  executed, broadcast to other ports, or sent over ESP-NOW.**
- **But a corrupted line is broadcast.** Any line that is not a DA line and does not start with `?` or `;` is
  broadcast to every port and the mesh. If a noise byte lands before the `@`, or the `@` is garbled, the whole JSON
  line goes droid-wide. So start the line with `@` and nothing before it, and keep it on a 9600 baud port.
- Ports S1-S5 are listened on, except MP3, DFPlayer and HCR ports and a few mode-specific exclusions. WCB 1's S3 is
  the HCR port, so an announce there would be ignored.
- There is one RAM record per port, with a **90 s TTL**. About three missed announces print
  `[WDP-DA] S<n>: Shadow stopped announcing`.

**What becomes visible**

- **Only `type` leaves WCB 1.** It is advertised as that port's label in the mesh, and only if the port has no user
  label. The mesh and the Booster's WDP view will show `S<n> Shadow`.
- **`fw` stays on WCB 1.** It is visible only through `?WDP,DA` on WCB 1, which prints
  `  S<n>  Shadow  fw <ver>  <age>s ago`. The spec's claim that `fw` is visible droid-wide is wrong for WCB 6.2.1.
  Tell the owner. Carrying `fw` over the mesh would be WCB-side work, which is Greg's.
- **The receiver is already on WCB 1.** Its firmware is `6.2.1_201933RSEP2026` (`bbc8971`, `origin/WIFI`). DA
  landed in `c461e31` on 2026-07-08, which is an ancestor of that build.

## SHADOW code map (from `main` `13c7dbc`)

- **Serial1 at 9600 is the link to WCB 1** (`src/config.cpp:81`). Every Serial1 write is one whole CR-terminated line
  in a single `Serial1.print()`: all of them in `marcDuinoButtonPush()`, plus two in `setup()`. There is no firmware
  version constant anywhere today.
- **The compiled core** is `framework-arduino-avr` 5.2.0 (Arduino AVR core 1.8.6), even though the platform is pinned
  to `atmelavr@5.1.0` and the `platformio.ini` comment implies otherwise.
  - The Serial1 TX ring buffer is **64 bytes**, so `availableForWrite()` is at most 63.
  - `HardwareSerial::write()` busy-waits when the buffer is full.
  - **So the announce line must be 63 bytes or fewer, or an `availableForWrite()` gate never lets it through.** Add a
    `static_assert`. `@WDP1 {"type":"Shadow","fw":"<7-char sha>"}\r` is 39 bytes.
- **Recommended shape** (from the research; the design process decides):
  - The line is a PROGMEM or `F()` constant, built from the injected `GIT_REV` macro.
  - A `wdpAnnounceTick()` does four things:
    1. Returns until `millis()` passes `wdpNextMs`.
    2. Returns if `Serial1.availableForWrite() < len`, so a later loop retries and a partial line is never sent.
    3. Otherwise prints the whole line in one call.
    4. Sets `wdpNextMs = millis() + random(25000, 30001)`.
  - Initialise `wdpNextMs = millis() + random(500, 3000)` at the end of `setup()`, after the existing
    `randomSeed(analogRead(0))` (`src/main.cpp:447`).
  - Call the tick in `loop()` after the command handlers, e.g. after `toggleSettings()` and before `printOutput()`
    (`src/main.cpp:~1756`). That puts it after the `readUSB()` early return, so no announce goes out while a
    controller fault is active. After 90 s the WCB logs "stopped announcing", which is benign.
- **Cost.** A 39-46 byte announce takes about 40-48 ms on the wire at 9600. A button command in that window queues
  behind it, so it is delayed by at most about 48 ms, once every 25-30 s. The motors are unaffected: the Sabertooth
  and SyRen are on Serial2, which has its own buffer.
- **Build baseline** (scratch copy, 2026-09-23): RAM 3855/8192 (47.1%), flash 39572/253952 (15.6%), 4 warnings
  (none new). The command is `~/.platformio/penv/bin/pio run -e SHADOWC2B5`. The AVR toolchain may need Rosetta on
  Apple silicon. `scripts/post_build.py` copies each build to `build/`.
- **Tests.** SHADOW has no host test harness. At minimum, the `static_assert`, `pio check` and the build must pass.
  Consider a tiny host test of the tick's timing and gating logic, factored so it can compile without Arduino.

## Electrical and wiring facts

- **SHADOW's 5 V TX already overdrives WCB 1's RX pin.** WCB V2.4 serial RX goes straight to an ESP32 GPIO with no
  level shifter or resistor. This is from the Booster memory `wcb-serial-logic-levels.md`, verified from Greg's KiCad
  schematics. The stress already exists and is continuous, because UART idles high. The announce adds nothing new.
  The fix is a divider on that one wire (for example 1 kΩ series and 2 kΩ to GND). That is a hardware decision for
  the owner.
- **Which WCB 1 port SHADOW is on is not recorded.** S3 is the HCR and S4 is the CHIRP, which leaves S1, S2 or S5.
  All three run at 9600 with broadcast in and out on. On WCB V2.4, S3-S5 are software serial: fine at 9600, but
  garbled at 115200, so never raise the baud. **The first announce identifies the port on its own.**

## Flashing SHADOW (the owner runs every step, with an explicit go-ahead)

1. **Coordinate first.** Session `droidnetsignalbooster-1d` has been power-cycling c2-body for the CHIRP work. Ask it
   before opening any port there. Also leave WCB1 S4 alone: it is deliberately 9600 with broadcast in and out off,
   so never restore a backup over it.
2. **Make the drive safe.** Disconnect or E-stop motor power to the Sabertooth and SyRen for the dump and the flash,
   and put the wheels up for the first drive afterwards. Opening the port resets the Mega. SHADOW sets 100 ms serial
   timeouts on both motor controllers, so a clean reset stops them. A half-written flash could still run garbage.
3. **USB power.** With the droid's 5 V rail on and USB unplugged, measure VBUS to GND on the Mega's USB-B socket.
   About 5 V means use a USB power blocker. The Mega ADK's power switch watches only VIN; it does not isolate a board
   fed on 5 V, or on VIN below about 6.6 V. About 0 V means use a plain cable. Nothing records whether SHADOW is fed on
   VIN or 5 V.
4. **Build.** Build from a clean tree and record the commit. Check that `include/controller_macs.h` is the real one.
5. **Dump first, over SSH on c2-body.** Free the port: close the Serial Monitor on it in every browser, then check that
   `fuser -v <by-id>` prints nothing. `lsof` is not installed.
   ```
   mkdir -p /root/shadow-backups
   avrdude -C /etc/avrdude.conf -v -p m2560 -c wiring -P /dev/serial/by-id/usb-Arduino_Srl_Arduino_Mega_ADK_75533353437351216112-if00 -b 115200 -U flash:r:/root/shadow-backups/shadow-pre-wdp-<date>.hex:i && sha256sum /root/shadow-backups/*.hex
   ```
   Copy it to the Mac with `scp -O`; plain `scp` fails on these Pis. Nothing records what the Mega runs today, so
   **this dump is the only exact rollback.**
6. **Flash**, with Booster Flash tab Board Type **Arduino Mega**, or over SSH:
   ```
   avrdude -C /etc/avrdude.conf -v -p m2560 -c wiring -P <by-id> -b 115200 -D -U flash:w:/root/shadow-backups/<new>.hex:i
   ```
   **Never use Auto-detect or Detect Board Only on a Mega.** They send a 1200-baud touch and then probe
   `/dev/ttyACM0`, which is the Pololu Maestro on c2-body. The Flash tab's avrdude has a 60 s timeout. A SHADOW-sized
   image (about 155 pages) should fit, but that is not measured.
7. **After boot:**
   - Every SHADOW boot sends `<PVV100,PVA100,PVB100>` (all three HCR volumes to 100) and `P1` on Serial1, so expect
     them.
   - The PS3 Navigation controllers must reconnect (PS button).
   - Drive comes back enabled.
8. **Rollback** over SSH, not the Flash tab (60 s timeout):
   `avrdude … -D -U flash:w:/root/shadow-backups/shadow-pre-wdp-<date>.hex:i`. A full-flash dump includes the
   bootloader region from `0x3E000`; the research suggests trimming the dump below `0x3E000`, or restoring a known
   build hex (`C2B5/build/SHADOWC2B5_20260123_112901.hex`, or this repo's `build/SHADOWC2B5_20260921_160303.hex`) if
   the dump matches one.

## Checking it worked

Least invasive first. The first-detection line appears once per WCB 1 boot or TTL lapse.

1. **Read-only, over SSH on c2-body:**
   `curl -s 'http://localhost/api/serial/history?port=/dev/ttyUSB0&limit=500' | grep 'WDP-DA'`. Look for
   `[WDP-DA] S<n>: Shadow fw <ver>`. The buffer is volatile and a reboot clears it, so read it promptly.
2. **The Booster WCB tab:** select WCB 1 (`/dev/ttyUSB0`) and type `?WDP,DA`. If this is scripted with
   `POST /api/wcb/command`, expect a timeout even when it works, because `?WDP,DA` has no terminator line. Read
   `/api/serial/history` afterwards instead.
3. **The WCB Wizard's WDP Mesh view:** WCB 1's row should show `S<n> Shadow` in its Devices column.
4. **Commands still work:** press buttons repeatedly across a few announce windows and confirm none are dropped or
   garbled.

Record SHADOW's WCB port in this repo's CLAUDE.md (Serial Interfaces) once the announce reveals it.

## Questions for the owner (ask during brainstorming)

1. **Label the port?** Should WCB 1's port also get `?LABEL,S<n>,Shadow`? A label persists in Booster backups, while
   the DA record lives in RAM only. It also overrides the DA type in the mesh. It is a WCB config write, so the owner
   has to approve it.
2. **Firmware version on the mesh?** Is `fw` visible only through `?WDP,DA` on WCB 1 acceptable? Showing it droid-wide
   needs a WCB-side change (Greg), and the WDP-DA spec's wording should be corrected either way.
3. **Level shifting?** Add a divider on SHADOW TX to WCB RX?
4. **Power?** Is SHADOW fed on VIN (7 V or more) or on 5 V? This decides the power blocker.
5. **Platform comment?** Fix the `platformio.ini` comment about the pinned core? (Optional.)

## Related records

- **This decision** is also noted in `~/Repos/droidnet-comlink-brightness/docs/superpowers/HANDOFF_2026-09-23_board_flashing.md`,
  the broader flashing handoff for the Holos and RSeries forks, which stay without WDP-DA.
- **Booster memory:**
  - `~/.claude/projects/-Users-travisccook-Repos-DroidNetSignalBooster/memory/wdp-da-multi-device-effort.md`
  - `wcb-serial-logic-levels.md`
  - `c2-body-chirp-wiring.md`
- **Booster side:** `docs/superpowers/2026-09-22-wcb-terminator-audit.md:432` notes that unsolicited `[WDP-DA]` lines
  match the listener's `[WDP` prefix and can merge into a pending `?WDP,DUMP`. That is informational; SHADOW needs
  nothing for it.
