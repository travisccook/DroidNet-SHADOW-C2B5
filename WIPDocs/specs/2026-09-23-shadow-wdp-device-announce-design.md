# SHADOW WDP Device Announce: design

**Date:** 2026-09-23
**Branch:** `feature/wdp-device-announce`
**Source:** `WIPDocs/HANDOFF_2026-09-23_shadow_wdp_device_announce.md` (protocol facts, code map, flashing procedure)

## Goal

SHADOW tells WCB 1 what it is by sending a WDP Device Announce (WDP-DA) line on Serial1:

```
@WDP1 {"type":"Shadow","fw":"<rev>"}\r
```

The first line goes out 0.5–3 s after boot, then one every 25–30 s with a fresh random interval each time. WCB 1 then
advertises its SHADOW port as `S<n> Shadow` in the mesh, and `?WDP,DA` on WCB 1 shows the firmware revision.
The new firmware is then flashed to the Mega ADK on c2-body.

## Decisions

Agreed with the owner, 2026-09-22 and 2026-09-23. Do not re-litigate.

- The WDP-DA pause is lifted for SHADOW only. SHADOW is alone on its WCB port. RSeries, Flthy and PSI stay paused
  (WCB issue #19).
- `type` is `Shadow`, exactly as in `WCB/Wizard/device-labels.js`. It is not `Shadow RC`.
- Announce only. SHADOW never reads Serial1. There are no `caps`.
- `fw` is the git revision, injected at build time.
- **`fw` is visible only on WCB 1** (`?WDP,DA`). WCB 6.2.1 does not carry it over the mesh, and the owner accepts
  that. The WDP-DA spec's claim that it is visible droid-wide is wrong for 6.2.1, which is Greg's to fix. Out of scope
  here.
- **Port label: yes.** After flashing, once the first announce reveals the port, the owner adds `?LABEL,S<n>,Shadow`
  on WCB 1. The label persists in backups, and it overrides the DA type in the mesh.
- **No level shifter.** The owner accepts the existing 5 V TX into the WCB's 3.3 V RX. The announce does not change
  the stress, because UART idles high.
- **Power stays as it is.** The flash checklist keeps a VBUS voltmeter reading only to choose a plain cable or a USB
  power blocker.
- **Structure: approach A.** Pure logic in a private library, thin glue in `main.cpp`, host unit tests in a native
  env. The alternatives were everything inline with no tests (B), and the library with an ad-hoc host compile
  script (C). A was chosen because a mistake in the timing or gating either floods WCB 1 or never announces, and it
  would otherwise be found only on the droid.

## Constraints

- **Never block the loop.** `HardwareSerial::write()` busy-waits when the TX buffer is full. The Serial1 TX ring
  is 64 bytes (AVR core 1.8.6, `framework-arduino-avr` 5.2.0), so `availableForWrite()` is at most 63.
- **The whole line is at most 63 bytes**, or the gate never opens. Enforced with a `static_assert`. A 7-character
  revision gives 39 bytes, and a 14-character one gives 46.
- **`fw` is at most 14 characters** (`?WDP,DA` column width) and contains only `[0-9a-z-]`. The WCB rewrites `,` and
  `]` and handles no escapes, so no commas, brackets or quotes may appear.
- **The line starts with `@` and nothing precedes it in the same write.** A corrupted DA line is broadcast
  droid-wide by the WCB. Every Serial1 write is already one whole CR-terminated line, and the gate keeps that true.
- **Serial1 stays at 9600.** WCB S3–S5 are software serial on V2.4.

## Components

### `lib/WdpAnnounce/WdpAnnounce.h`

Header-only. It includes only `<stdint.h>` and `<stddef.h>`, so it compiles for both the AVR and the host.

```cpp
namespace wdp {

constexpr uint32_t kFirstDelayMinMs = 500;
constexpr uint32_t kFirstDelayMaxMs = 3000;
constexpr uint32_t kIntervalMinMs = 25000;
constexpr uint32_t kIntervalMaxMs = 30000;
constexpr size_t kMaxLineLen = 63;  // Serial1 TX ring (64) minus one

class AnnounceTimer {
public:
    void start(uint32_t now, uint32_t firstDelayMs);
    bool ready(uint32_t now, int freeTx, size_t lineLen) const;
    void markSent(uint32_t now, uint32_t nextDelayMs);
};

}  // namespace wdp
```

- `start` records `now` as the reference time and `firstDelayMs` as the delay.
- `ready` is true only when all of these hold:
  - the timer has been started;
  - `now - last_ >= delay_`, in unsigned 32-bit arithmetic, so it stays correct across the `millis()` rollover;
  - `freeTx >= lineLen`, with a negative `freeTx` treated as 0.

  It has no side effects.

- `markSent` records `now` and `nextDelayMs`.
- It never calls `random()`, `millis()` or `Serial1`. The caller supplies those values.

### `scripts/git_rev.py`

It prints one build flag, `'-DGIT_REV="<rev>"'`, the form given in the PlatformIO dynamic build flags docs.

- `<rev>` is `git rev-parse --short=7 HEAD`, plus `-dirty` when `git status --porcelain --untracked-files=no`
  prints anything. It is run in the project directory. Examples: `a51a459`, or `a51a459-dirty`. This is used
  instead of `git describe` so that a future tag cannot turn it into `v1.0-3-g…`.
- If git fails, `<rev>` is `unknown`.
- `<rev>` is lower-cased, filtered to `[0-9a-z-]`, and truncated to 14 characters.

### `platformio.ini`

- `[env:SHADOWC2B5]` `build_flags` gains `!python3 scripts/git_rev.py`. Tested: this Mac has no `python` on PATH,
  and `$PYTHONEXE` is not expanded inside a `!` flag.
- `[env:SHADOWC2B5]` gains `test_ignore = *`, so `pio test` never builds or uploads a test image to the Mega.
- A new `[env:native]` is added with `platform = platformio/native` and `test_framework = unity`.
- `check_src_filters` gains `+<lib/WdpAnnounce/>`.
- `default_envs` stays `SHADOWC2B5`, so `pio run` builds only the firmware.

### `src/main.cpp`

- `#include <WdpAnnounce.h>`.
- `static const char kWdpAnnounceLine[] PROGMEM = "@WDP1 {\"type\":\"Shadow\",\"fw\":\"" GIT_REV "\"}\r";`
- `static_assert(sizeof(kWdpAnnounceLine) - 1 <= wdp::kMaxLineLen, ...)` and `static_assert(sizeof(GIT_REV) - 1 <= 14, ...)`.
- `wdp::AnnounceTimer wdpTimer;`.
- `setup()`, right after `randomSeed(analogRead(0))`: `wdpTimer.start(millis(), random(kFirstDelayMinMs, kFirstDelayMaxMs + 1));`.
- `wdpAnnounceTick()`: if `wdpTimer.ready(millis(), Serial1.availableForWrite(), sizeof(kWdpAnnounceLine) - 1)`, it
  prints the line in one call as a flash string, then calls
  `wdpTimer.markSent(millis(), random(kIntervalMinMs, kIntervalMaxMs + 1))`.
- `loop()` calls `wdpAnnounceTick()` between `toggleSettings()` and `printOutput()`.

## Behaviour

- **Timing.** The first announce goes out 0.5–3 s after boot. `readUSB()` returns true without a controller, so
  SHADOW announces whether or not a controller is paired.
- **During a fault.** The `readUSB()` early return skips the tick, so nothing is sent. After 90 s WCB 1 logs
  `Shadow stopped announcing`, which is benign. When the fault clears, the tick is overdue and sends on the next
  loop.
- **When the buffer is busy.** If Serial1 lacks room, the tick returns without rescheduling and tries again on the
  next loop. A partial line is never sent.
- **Cost.** A button command that lands during an announce waits at most about 48 ms, once every 25–30 s. Motor
  control is on Serial2 and unaffected.

## Testing

Unity tests in `test/test_wdp_announce/test_main.cpp`, run with `pio test -e native`, written before the
implementation:

1. Not ready before the first delay; ready at and after it.
2. Not ready before `start()` is called.
3. Due with `freeTx < lineLen`: not ready. Once `freeTx >= lineLen`, ready, with no reschedule in between.
4. `freeTx == lineLen` is ready; `lineLen - 1` is not; a negative `freeTx` is not.
5. After `markSent`: not ready until the new delay passes.
6. Rollover: started at `0xFFFFF000` with a 30000 ms delay. Not ready shortly after the wrap; ready once
   30000 ms have elapsed.

Build checks:

- `pio run -e SHADOWC2B5` succeeds and the `static_assert`s hold. The baseline is RAM 3855 B and flash 39572 B,
  with 4 warnings; expect only small growth and no new warnings.
- `pio check -e SHADOWC2B5` reports no new defects in the changed code.
- The verbose build shows `-DGIT_REV="<rev>"`.

## Docs

CLAUDE.md:

- Add `pio test -e native` to the commands.
- Correct "The only environment is `SHADOWC2B5`".
- Describe the WDP-DA announce under Serial Interfaces.
- After flashing, record SHADOW's WCB 1 port there.

## Flashing and verification

The owner runs every step with an explicit go-ahead. Commands and cautions are in the handoff.

1. Check with session `droidnetsignalbooster-1d` before opening any port on c2-body. Leave WCB 1 S4 alone.
2. Turn off Sabertooth and SyRen motor power, and put the wheels up for the first drive.
3. Read VBUS with a voltmeter before plugging in USB: about 5 V means use a USB power blocker, about 0 V a plain
   cable. Do not change the power wiring.
4. Build from the clean committed tree, record the commit, and confirm `include/controller_macs.h` is the real one.
5. Over SSH, confirm with `fuser -v <by-id>` that the port is free, then dump the flash with avrdude to
   `/root/shadow-backups/`, then `scp -O` it to the Mac.
6. Flash with avrdude over SSH, or with the Booster Flash tab set to **Arduino Mega**. Never use Auto-detect or
   Detect Board Only.
7. After boot, expect `<PVV100,PVA100,PVB100>` and `P1`, reconnect the controllers, and check read-only with
   `/api/serial/history`, looking for `[WDP-DA] S<n>: Shadow fw <rev>`.
8. The owner adds `?LABEL,S<n>,Shadow` on WCB 1.
9. Press buttons across several announce windows and confirm none are dropped or garbled.
10. Record the port in CLAUDE.md.

Rollback is to flash the dump, or a known build hex, over SSH (see the handoff).

## Out of scope

- Carrying `fw` over the mesh, and correcting the WDP-DA spec's wording (WCB side, Greg).
- WDP-DA for the RSeries, Flthy and PSI boards.
- A level shifter on SHADOW TX.
- Any change to the power wiring.
- The `platformio.ini` comment about the pinned core.
