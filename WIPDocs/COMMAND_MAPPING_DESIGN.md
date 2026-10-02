# Runtime Button Mapping

**Status:** implemented, and merged to `main` in [PR #1](https://github.com/travisccook/DroidNet-SHADOW-C2B5/pull/1). It is
tested on the host, on an emulated ATmega2560 (simavr), and against the reference Pi client.
**It runs on the droid** (`e3f6b82`, flashed 2026-09-24). Verified there: the WCB link in both directions, every
verb, and EEPROM persistence across a reset. Not yet tested: real button presses, driving, and a power cycle
([results on PR #1](https://github.com/travisccook/DroidNet-SHADOW-C2B5/pull/1#issuecomment-5817847387)). The DroidNet
side is being built. Its design and UI live in the
DroidNetSignalBooster repo: [backend design](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-09-24-shadow-button-map-design.md) and
[UI design and mockup](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-09-24-shadow-button-map-ui-design.md). Work in progress is tracked in
[HANDOFF_BUTTON_MAP.md](HANDOFF_BUTTON_MAP.md).

## What it does

- **Every combo maps to a WCB stored command.** When a combo is pressed, SHADOW sends
  `<prefix><key><suffix>\r` to the WCB on Serial1. By default that's `;W1;C<key>\r`, the same
  bytes as before.
- **Every combo can also be double-tapped** (except the stick click, L3) and map to a second
  stored command (see [Double taps](#double-taps)).
- **Every combo can also be held** for a long press (except L3) and map to a third stored
  command (see [Long presses](#long-presses)).
- **Where the mappings live:**
  - The built-in defaults are in `src/default_mappings.cpp`.
  - DroidNet can change any combo at runtime, over the WCB link, with the controllers connected.
  - Changes are stored in EEPROM, so they survive reboots and firmware uploads.
- **SHADOW never needs the Pi to run.**

## Decisions

- **Volume and brightness are removed.** SHADOW keeps no volume or brightness state. The four
  PS + direction combos on each controller are unmapped until they're pointed at WCB stored
  commands.
- **Editing is allowed any time,** with no lock. Writes never touch motor control (see Timing
  below).
- **Transport: the WCB link.** Opening the Mega's USB port resets it and drops the controllers
  (Linux raises DTR on open, and DroidNet also toggles it on purpose). So the Pi reaches SHADOW
  through the WCB it's already plugged into. The Pi and SHADOW are on the same WCB; SHADOW's
  S-port is found automatically (see Discovery).
- **Fallback:** keeping the whole mapping on the WCB as stored commands named after each combo.
  It's not needed so far.

## How messages travel

```
DroidNet --USB--> Pi's WCB --(ESP-NOW if another board)--> SHADOW's WCB --S<x>, 9600--> SHADOW Serial1
DroidNet <--USB-- Pi's WCB <--(ESP-NOW if another board)-- SHADOW's WCB <--S<x>, 9600-- SHADOW Serial1
```

- **Pi to SHADOW:** the Pi writes `;S<x>@SM ...` to its WCB, or `;W<n>;S<x>@SM ...` if SHADOW is on
  WCB `n` (`;W<nn>,;S<x>...` for 10 and above). The WCB strips the route and writes `@SM ...\r` to
  SHADOW (`processSerialMessage`, WCB `Code/WCB/WCB.ino:6444`).
- **SHADOW to Pi:** SHADOW writes `;S0@SM ...`, or `;W<n>;S0@SM ...` when HELLO named the Pi's WCB
  as `n`. Port 0 is USB, so the WCB prints `@SM ...` to the Pi (`WCB.ino:6468`). Commands that
  arrive over ESP-NOW go through the same parser (`WCB.ino:4694`); the remote form is read from
  source, not tested on hardware.
- **The same protocol also works directly on SHADOW's USB port,** for bench testing. There the CRC
  is optional, but opening the port resets the Mega.

WCB references are to `travisccook/Wireless_Communication_Board-WCB` `main` at `66845b9` (6.2.1).

### Rules the WCB imposes (and how the firmware keeps them)

1. **No bare lines from SHADOW.** A line that doesn't start with `;` or `?` is broadcast to every
   S-port on every WCB (`WCB.ino:6853`). Every reply is wrapped in `;S0`. The command prefix
   must start with `;`. The one exception is `@WDP1 ...`, which the WCB captures first
   (`WCB.ino:7015`).
2. **No `^`, and no `;T` / `;t`, in anything SHADOW sends.** The WCB splits on `^` and treats
   `;T`/`;t` as timer commands. Keys are `[A-Za-z0-9_]`. The prefix and suffix are refused if
   they contain either sequence, and the prefix can't end in `;`. Prefix and suffix travel
   hex-encoded. The line checksum is ` *<crc>`, not `^?CHK`.
3. **SHADOW's port hears every mesh broadcast.** The line reader drops anything that doesn't
   start with `@SM ` without buffering it. Noise bytes before a line starts are ignored, so a glitch on the
   wire (seen when WCB 1 resets at power-up) can't swallow the next request. `?BCAST,OUT,S<x>,OFF` on SHADOW's port cuts the
   traffic, since SHADOW doesn't need it.
4. **WCB debug echoes are ignored.** With WCB debug on, the Pi also sees `Sent to USB: @SM ...`
   and similar lines. Clients only act on lines that *start* with `@SM `.
5. **The link runs at 9600 baud in a noisy droid.** Every line in both directions carries a
   CRC32.

## Slots

The mapping is a fixed grid: `index = tap*72 + controller*36 + modifier*6 + button`, which gives
216 slots: 0-71 are single presses, 72-143 the same combos tapped twice, 144-215 the same combos
held for a long press.

- Controllers: `F` (foot), `D` (dome)
- Modifiers: `NONE L1 L2 PS CIRCLE CROSS`
- Buttons: `UP RIGHT DOWN LEFT L3 PS`, and `UP2 RIGHT2 DOWN2 LEFT2 L32 PS2` for a double tap, `UPL RIGHTL DOWNL LEFTL L3L PSL` for a long press

**Which slot a press uses:**

- **The gate.** A combo only reaches the mapping when the controller has a direction or L3 held,
  or PS with L1/L2. This is the same gate as before.
- **The layer.** The highest-priority held modifier picks the layer: `L1, L2, PS, CIRCLE,
  CROSS`, then no modifier. CROSS, CIRCLE and PS also count when they're held on the other
  controller, if it's connected. L1 and L2 only count on the same controller.
- **The slot.** The first held primary in the order `UP, LEFT, RIGHT, DOWN`, then `PS` and `L3`,
  picks the slot within the layer. The L1 layer checks L3 before PS and every other layer PS
  before L3, as the old tables' row order did.
- **No fall-through.** An unmapped slot does nothing; it doesn't fall back to another layer.
  That's how the old `UNUSED` table rows behaved.
- **Repeat timing.** A combo fires when pressed and again every second while it's held. Taps
  within the same second are ignored. The timer is shared by both controllers, and the dome
  controller is checked first. This is unchanged from before.

A test replays every button combination (32,128 states) through the old table code and the new
resolver. The results are identical except volume and brightness, which were removed on
purpose.

### Double taps

A double tap is the same combo pressed twice quickly: the second press has to come within
`SHADOW_DOUBLE_TAP_MS` (350 ms, `include/config.h`) of the first. Both presses have to resolve to
the same slot, so a modifier can stay held between them or be pressed again with the button.
The owner's decisions (2026-09-27):

- **Exclusive.** A double tap sends only its own command, never the single press's command
  first. So a combo whose double tap is mapped **waits**: its single press fires when the
  window closes (350 ms late), or at once when another combo on that controller is pressed.
- **Only combos with a double tap mapped wait.** Every other combo fires at once and repeats
  exactly as before, shared one-second timer included. Unmapping a double tap restores the old
  timing for that combo.
- **Nothing on a held double tap.** Holding the second press does nothing more; there's no
  repeat.
- **No double tap on L3,** the stick click (`res=stick`).

The rest:

- **Held past the window,** the first press fires the single-press command at the window's
  end and then repeats every second while held, as a held combo always has.
- **Taps right after a double tap are ignored** for a second from its first press, the way the
  old code ignored taps inside the same second. So tapping three times gives the double tap
  once, not a double tap and then a single press.
- **Each controller is on its own.** A first tap waiting on the foot doesn't hold up the dome,
  and the other way round.
- **The window counts from the first press.** A first tap still waiting when a controller drops
  is dropped. One whose window closed more than 250 ms before the next pass (a controller fault
  skips the button passes) is dropped too, rather than fired late.
- **Pressing a modifier just after the button** (UP, then L1 a pass later) first resolves to the
  plain combo. When that combo's double tap is mapped, the plain combo fires as a single press
  the moment the modifier lands and the new combo takes over, much as the old code fired the
  plain combo at once.
- **Whether a combo has a double tap** comes from a copy in RAM (9 bytes, one bit per slot),
  loaded at boot and updated when a write finishes. A press never waits on the EEPROM to find
  out.

### Long presses

Added 2026-09-29. Design: DroidNet's
[spec](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-09-29-shadow-long-press-design.md).
A long press is a combo held for `SHADOW_LONG_PRESS_MS` (600 ms, `include/config.h`). A combo whose
long press is mapped waits, and starts its timer from the press:

| What happens next | What fires |
| --- | --- |
| Still held at 0.6 s | The long press, at once. Then nothing until release (`Latched`). |
| Released before 0.6 s, no double tap mapped | The single tap, on release. |
| Released before 0.35 s, double tap mapped, second press in time | The double tap, on the second press (exactly as before). |
| Released before 0.35 s, double tap mapped, no second press | The single tap, when the window closes (as before). |
| Released between 0.35 s and 0.6 s, double tap mapped | The single tap, on release (the window has already closed). |
| Another combo pressed on the same controller while it waits | The waiting combo's single tap, at once; the new combo is handled on its own. |
| The controller drops while it waits | Nothing: the press is dropped (as before). |

- **The long press only fires if the combo is still held on the pass that reaches the threshold.**
  A release, or another combo, seen first on that pass gives the single press instead.
- **The double-tap window no longer fires a held first press.** Before, a first press still held
  when the window closed fired its single tap and then repeated. With a long press mapped, a held
  first press keeps waiting for either the release or the threshold.
- **A long press only ever comes from the first press.** Tap, then tap and hold: the double tap
  fires on the second press, and holding it does nothing more.
- **Taps right after a long press** are ordinary presses. The one-second quiet period after a
  double tap stays as it is and doesn't apply to long presses.
- **Pressing a modifier just after the button** (UP, then L1) fires the plain combo's single tap
  when the modifier lands, as for a double-tap combo. The new combo starts its own wait.
- **Stale presses.** A pass held up by a controller fault must not fire a long or single press
  late: one whose moment (the threshold, the release, or the window's close, whichever it waited
  for) passed more than 250 ms (`kDeferMaxMs`) before the next pass is dropped. For the long press
  that is threshold + 250 ms. The check counts from that moment, not from the press: a single tap
  released at 0.5 s is fresh, although the press is 0.5 s old. So a first pass more than 850 ms
  after the press drops it, held or released, and another combo on that pass fires alone.
- **Whether a combo has a long press** comes from a RAM copy (9 bytes, one bit per slot), loaded
  at boot and updated when a write finishes, like the double taps' copy. A press never waits on
  the EEPROM.
- **Limit: stalled reports.** SHADOW reads the held state from the controller's reports. If
  Bluetooth reports stall for about 500 ms or more during a short tap on a long-press combo, the
  last "held" report stands and the tap reads as a hold, so the long press fires. No firmware can
  tell those apart.
- **No long press on L3,** the stick click (`res=stick`).

**Reserved slots** are hardcoded in `main.cpp` or never reach the mapping. They can't be
mapped, and `SLOT` lines say why:

| Slot | `res=` | Why |
| --- | --- | --- |
| `PS` + `NONE` / `PS` / `CIRCLE` / `CROSS`, both controllers | `psgate` | PS is only a button with L1/L2 held. PS+CROSS and PS+CIRCLE toggle the drive stick. |
| `F L1 L3` | `overspeed` | Over-speed toggle |
| every `L32` (double tap of L3) | `stick` | The stick click has no double tap |
| every `L3L` (long press of L3) | `stick` | The stick click has no long press |

The double tap of a reserved slot is reserved for the same reason (`F L1 L32` is `overspeed`).
The long press of a reserved slot is reserved too. That leaves 31 foot slots and 32 dome slots
for single presses, and 26 on each controller for double taps and for long presses.

**Fixed along the way:** L3+L1 on the *dome* controller used to toggle foot over-speed as well
as firing Front Holo Center. Over-speed now only listens to the foot controller.

## EEPROM layout

The layout uses 3952 of the 4096 bytes. Nothing else uses EEPROM.

```
0x000  header A  32 B   magic "SM", layout version, slot count, sequence, generation,
0x020  header B  32 B   prefix (<= 11, default ";W1;C"), suffix (<= 7, default ""), CRC16
0x040  72 slots x 18 B  single presses: key (<= 15 chars, NUL-padded; "" = unmapped) +
                        CRC16 over (layout version, generation, slot index, key)
0x550  72 slots x 18 B  double taps (slot index 72-143), the same records
0xA60  72 slots x 18 B  long presses (slot index 144-215), the same records
0xF70  free
```

- **Double taps came later, without a new layout.** The header still says layout version 1 and
  slot count 72 (the single presses), so custom mappings survive the upgrade, and firmware from
  before double taps still reads the same EEPROM (it never looks past 0x550). Double-tap slots
  read as blank, which is unmapped, until DroidNet sets one. A DEFAULTS from either firmware
  clears both banks, because the generation is in every slot's CRC.

- **Long presses came later, without a new layout.** As with double taps, the header still says
  layout version 1 and slot count 72, so custom mappings survive the upgrade, and older firmware
  still reads the same EEPROM (v1 stops at 0x550, v2 at 0xA60). Long-press slots read as blank,
  which is unmapped, until DroidNet sets one. A DEFAULTS from any firmware clears all three banks.

- **Two header copies.** Headers are written alternately, and the valid one with the newer
  sequence wins (wraparound-safe). A power cut mid-header-write leaves the other copy in force.
- **Per-slot CRC.** A slot is live as soon as it's written. A slot that fails its CRC reads as its
  built-in default, without affecting the others. That covers a power cut mid-write, an erased
  slot, and a slot written under an older generation.
- **DEFAULTS** writes a header with generation + 1. That invalidates every slot at once.
- **REVERT** erases one slot to 0xFF.
- **First write.** A blank or unrecognised EEPROM reads as all defaults. The first write formats it
  with a generation newer than anything left in either header copy, so stale slots can't come
  back.
- **Writes happen one byte per loop pass,** only when the EEPROM is idle. Bytes that already match
  are skipped, and each record is read back and verified (one retry, then `ERR eeprom`).
- **Firmware uploads keep it.** Bootloader uploads don't erase EEPROM (`avrdude -D`, PlatformIO).
  A future firmware with a new layout version falls back to defaults.

## Protocol reference (v3)

v2 added the double-tap slots: `slots=144`, 144 lines in a DUMP, and button tokens like `UP2`.
v3 adds the long-press slots: `slots=216`, 216 lines in a DUMP, and `UPL` tokens. Nothing else
changed. A v1 or v2 client refuses a v3 SHADOW at HELLO. DroidNet speaks all three (it only shows
double taps to a v2 SHADOW and long presses to a v3 one); the reference client (`tools/shadowmap/`)
speaks v3 only.

Every line is one of these:

- **Request:** `@SM <VERB> [args] [*<CRC>]`
- **Reply or event:** `@SM <WORD> [args] *<CRC>`

`<CRC>` is the CRC32 (IEEE, the same as `zlib.crc32`) of everything before ` *`, as 8 hex
digits. SHADOW accepts upper or lower case and sends upper case.

- **CRC on the WCB port:** required. A request without it, or with a wrong one, gets
  `@SM ERR crc`. On the USB port the CRC is optional.
- **Lengths:** requests are at most 72 characters; replies at most 80, before the route.
- **Terminators:** lines end in `\r` or `\n`.
- **Tokens:** separated by single spaces.
- **Slots:** written as `<C> <M> <B>`, e.g. `F L1 UP`, `F L1 UP2` for its double tap, or `F L1 UPL` for its long press.
- **One write-type operation at a time.** SET, REVERT, DEFAULTS, PREFIX, SUFFIX, DUMP and SUM are
  refused with `ERR busy` while another one runs. HELLO, CFG, GET, EVENTS and BAT are always answered.

### Requests

| Request | Reply |
| --- | --- |
| `HELLO [route]` | `READY v3 fw=<ver> slots=216 gen=<g> fmt=<0\|1> pads=<FD\|F\|D\|->`. `route` sets where replies go on the WCB port: `0` (the default) = `;S0`, `n` = `;W<n>;S0`. It is kept in RAM, so the Pi should send HELLO after SHADOW reboots. |
| `CFG` | `CFG pre=<hex> suf=<hex\|->` |
| `GET <slot>` | `SLOT <slot> <key\|-> <src> def=<key\|->`, or for a reserved slot `SLOT <slot> - reserved res=<reason>`. `src` is `eeprom`, `default` or `reserved`. |
| `SET <slot> <key\|->` | `OK SET <slot> <key\|->` once written and verified. `-` unmaps the slot, as a custom value. |
| `REVERT <slot>` | `OK REVERT <slot>`. The slot is back on its built-in default (`src=default`). |
| `DUMP` | 216 `SLOT` lines in slot order (single presses, double taps, then long presses), then `END DUMP 216`. About 12.5 s at 9600 baud. |
| `SUM` | `SUM <8 hex> slots=216`: the CRC32 over, for each slot in order, its `SLOT` line text after `@SM SLOT ` plus `\n`. The Pi can compute the same over a cached DUMP and skip the DUMP when they match. About 0.4 s. |
| `DEFAULTS` | `OK DEFAULTS gen=<g>`. Every slot is back on its built-in default. |
| `PREFIX <hex>` | `OK PREFIX <hex>`. The prefix must start with `;`, can't end with `;`, is at most 11 printable characters, and has no `^` and no `;T`/`;t`. |
| `SUFFIX <hex\|->` | `OK SUFFIX <hex\|->`. At most 7 printable characters, no `^`, no `;T`/`;t`. |
| `EVENTS <0\|1>` | `OK EVENTS <0\|1>`. With 1, this port gets the events below. |
| `BAT` | `BAT F=<lvl> D=<lvl> fs=<st> ds=<st> up=<s> hb=<s>`: the controllers' battery levels (see [Battery reports](#battery-reports)). |

### Events

These are only sent with `EVENTS 1`, and are dropped rather than queued if the link is busy.

| Event | When |
| --- | --- |
| `PRESS <slot> <key\|-> <src> [from=<F\|D>] [res=<reason>]` | Any combo that reaches the mapping, mapped or not, when it fires: a double tap as `UP2`, a long press as `UPL` when it fires, a combo that waited for a second tap when its window closes. `from=` means the modifier was held on that other controller. |
| `CTL <F\|D> <0\|1>` | A controller connected or dropped |

### Battery reports

SHADOW reads each controller's battery from the status byte in every input report the
controller sends. It sends this line to the WCB port by itself, whether or not EVENTS is on:

```text
@SM BAT F=<lvl> D=<lvl> fs=<st> ds=<st> up=<s> hb=<s> *<CRC>
```

- **When:** at once when either controller's level changes, and otherwise every `hb` seconds
  (30), starting 5 s after boot. A change restarts the 30 s period. The same line answers a `BAT`
  request, on either port.
- **`F=` / `D=`:** SHADOW's verdict for the foot and dome controller: `OK`, `LOW`, `CRIT`, or `-`
  when there's no level (not connected, or the first reading of a connection is still settling).
  These are the words Sabé uses for its remotes. The level is the value to display; don't work
  it out again from `fs`/`ds`.
- **`fs=` / `ds=`:** the controller's own reading, detail only: `0`-`5` on battery (5 is full;
  the Linux driver reads them as 0/1/25/50/75/100 %), `chg` charging on a USB cable, `usb`
  charged on a cable, `-` none. Not a fuel gauge: five coarse steps.
- **`up=`:** SHADOW's uptime in seconds. A decrease means SHADOW rebooted (it has forgotten the
  reply route and EVENTS).
- **`hb=`:** the report period in seconds. A client that has heard nothing for 3 periods can
  treat SHADOW as gone.
- The line goes out through the TX queue like everything else, only when there's room, so it never
  delays a button command. It goes out during a controller fault too.
- Comment out `SHADOW_BATTERY_REPORT` in `include/config.h` to stop the unasked lines; `BAT`
  requests still work.
- **Where it goes on the Pi:** DroidNet (DroidNetSignalBooster branch `claude/magical-pasteur-1rbyte`, not yet
  merged) latches the line whenever its SHADOW feature is on and routed, tab open or not. It shows the level on the
  SHADOW tab's Foot and Dome buttons and publishes it to Comlink as a `shadow` announce object shaped like `sabe`
  (foot = `drive`, `st` in place of `mv`, `ttl` 90). Comlink (droidnet-comlink branch `claude/magical-pasteur-1rbyte`,
  not yet merged or built in Xcode) shows it on the Voice tab chip ("FOOT LOW", "CONTROLLERS OK") and the Watch, and
  alerts once when a controller enters CRIT.

**How the level is decided** (`lib/ShadowMap/src/sm_battery.cpp`):

| Status byte | Reported as | Level |
| --- | --- | --- |
| `0x03`-`0x05` | `3`-`5` | `OK` |
| `0x02` | `2` | `LOW` |
| `0x00`-`0x01` | `0`-`1` | `CRIT` |
| `0xEE` and up | `chg` (even) / `usb` (odd) | `OK` |
| anything else | ignored | |

A new level has to hold before SHADOW adopts it, so a reading flickering at a step boundary
doesn't flap: 2 s for the first reading of a connection, 3 s for a worse level, 30 s for a
better one. A controller that disconnects goes to `-` at once.

The status byte comes from `PS3BT::getBatteryStatus()`, added to the bundled USB Host Shield
library. It's saved only from input reports, so unlike `getStatus()` it never reads a byte of
some other packet. **Not yet measured on the droid:** how long a Navigation controller keeps
going after it first reports `LOW` (`2`) or `CRIT` (`1`).

### Errors

The format is `ERR <code> [<VERB> [<slot>]]`.

| Code | Meaning |
| --- | --- |
| `crc` | The CRC was missing or wrong (the verb isn't echoed). Resend. |
| `busy` | Another operation is running. Retry shortly. |
| `args` | Wrong number or shape of arguments |
| `verb` | Unknown verb |
| `slot` | Not a valid `<C> <M> <B>` |
| `reserved` | The slot is reserved |
| `key` | Not `[A-Za-z0-9_]{1,15}` or `-` |
| `prefix` / `suffix` | The value breaks the rules above |
| `eeprom` | The write didn't verify after one retry |

### Client rules

- **One request at a time.** Wait for its reply, or time out after about 1 s, before sending the
  next. Everything is idempotent, so resend on silence, `ERR crc` or `ERR busy`.
- **After repeated silence, send HELLO again.** SHADOW keeps the reply route and the EVENTS
  subscription in RAM, so after a reboot its replies go to its own WCB until it hears HELLO.
  `shadowmap.Client` does this, and restores EVENTS.
- **Ignore everything that isn't a CRC-valid line starting with `@SM `.** That includes events
  when you aren't expecting them, and the `BAT` lines SHADOW sends unasked.

### Discovery

With `SHADOW_WDP_ANNOUNCE` defined (the default), SHADOW sends this to its WCB 0.5-3 s after boot,
then every 25-30 s, each interval drawn at random:

```text
@WDP1 {"type":"Shadow","fw":"<commit>"}
```

This is the WCB 6.2.1+ device announce (WDP-DA). The WCB prints `[WDP-DA] S<x>: Shadow fw <commit>`
on USB when it first hears it (and again after a 90 s silence), and `?WDP,DA` lists it. That's how
DroidNet finds SHADOW's S-port. Only the type leaves that WCB: the mesh shows the port as
`S<x> Shadow`, while `fw` is visible only on SHADOW's own WCB. Comment the define out for WCB
firmware older than 6.2.1, which would broadcast the line instead.

- **`fw` is the git commit** the firmware was built from, with `+` when tracked files had
  uncommitted changes (`scripts/git_rev.py`, run from `platformio.ini`). HELLO's READY reports the
  same value.
- **No announce during a controller fault** (the `readUSB()` early return). A due announce waits
  for the fault to clear. Replies to `@SM` requests still go out.
- **It goes through the TX queue** like everything else, so it never blocks and never lands inside a
  command or a reply. A `static_assert` in `main.cpp` keeps the line within the 64-byte UART buffer.

These follow the owner's announce decisions in
[HANDOFF_2026-09-23_shadow_wdp_device_announce.md](HANDOFF_2026-09-23_shadow_wdp_device_announce.md),
with one change the owner made on 2026-09-24: SHADOW now also *reads* Serial1, for `@SM`.

## DroidNet's export (the mapping on paper)

DroidNet's SHADOW tab (DroidNetSignalBooster `c6902014`, DroidNet 2.3.6) exports and imports the whole mapping as JSON.
That replaces the old control-mapping spreadsheet, which is retired. DroidNet owns this format; its handoff
(`docs/superpowers/HANDOFF_2026-09-24_shadow_button_map.md`) is the reference. Version 1 looks like this:

```json
{ "format": "droidnet-shadow-map", "version": 1, "exported": "<ISO time>", "booster": "c2-body",
  "shadow": { "fw": "af4db5d", "gen": 1 }, "prefix": ";W1;C", "suffix": "",
  "slots": { "F": { "L1": { "UP": { "key": "extremehappysou", "src": "default" },
                          "PS": { "key": null, "src": "default" } } }, "D": { } },
  "labels": { "extremehappysou": "HCR Extreme Happy" } }
```

- Slots use the same `<C> <M> <B>` names as the protocol. Each is `{key: string|null, src: "default"|"eeprom"}`, so a
  custom value that happens to equal the default stays distinct.
- Reserved slots are left out.
- Version 2 (DroidNetSignalBooster `193851b`) adds the double taps under their wire names (`"UP2": {...}`) next to the
  single presses. DroidNet imports version 1 and 2 files.
- `labels` are DroidNet's descriptions of the WCB stored commands, kept on the Pi. SHADOW never sees them.

## Firmware structure

| File | What |
| --- | --- |
| `lib/ShadowMap/src/sm_slots.*` | Slot layout, tokens, reserved slots |
| `lib/ShadowMap/src/sm_resolve.*` | Held buttons to slot (the lookup rule) |
| `lib/ShadowMap/src/sm_store.*` | EEPROM store (headers, slots, the background writer) |
| `lib/ShadowMap/src/sm_protocol.*` | The `@SM` protocol, `WcbPort` reply routing |
| `lib/ShadowMap/src/sm_line.*`, `sm_tx.*` | Non-blocking line reader, Serial1 TX queue that always keeps room for a button command |
| `lib/ShadowMap/src/sm_runtime.*` | What `loop()` does for the mapping: repeat timing, deferred presses, request pumping, the announce |
| `lib/ShadowMap/src/sm_crc.*`, `sm_eeprom.h`, `sm_pgm.h` | CRC16/CRC32, EEPROM access, flash-string shims for host builds |
| `src/default_mappings.cpp` | The built-in mappings (edit these; see CLAUDE.md) |
| `src/main.cpp` | Reads the controllers into `sm::PadState`, calls the runtime; `UsbReplyPort` sends USB replies with the debug output |

The library has no Arduino dependency, so it builds and tests on the host.

## Timing and memory

- **The per-pass rule.** A loop pass is `readUSB`, the motors, `mappedButtons()`,
  `toggleSettings()`, `serviceMapping()` (pump the WCB, pump USB, batteries, `Runtime::service`),
  `printOutput()`. Presses can't wait (their timing rules are fixed), so everything else fits
  around them:
  - A request is taken in only in a pass that fired no press, read fewer than 16 bytes and
    doesn't finish an EEPROM job (`MapStore::finishDue`), one a pass, and nothing more is read
    after it. A pass that fired reads 16 bytes at most over both ports. A line held for a later
    pass, and the bytes behind it, wait in the UART.
  - In a pass with none of that, one background unit runs, the first that has work: the
    announce, the protocol's next line (a controller event, the SLOT reply in hand, the SUM
    reply, `END DUMP`, a DUMP slot read, a write's reply), the battery report, a SUM step. A DUMP
    holds the battery report until `END DUMP`, as before.
  - A SLOT reply (a GET, each DUMP line) reads its slot on one pass and sends the line on a later
    one. One is in hand at a time; a port's requests wait behind its reply, and a GET on any
    port. A request that can't be answered yet keeps the old rule: bytes behind it are read and
    dropped so the 64-byte RX buffer can't overflow (behind a reply in hand with room, they wait
    a pass in the UART).
  - The EEPROM writer's step and the drain into the UART run every pass, except that the
    writer's verify-and-finish step (never a write) waits one pass when a press fired in its
    pass, once: with @SM v4's PRESS events, three presses and a finishing job in one pass came to
    1.04 ms. The drain reaches
    Serial1 through `sm::Uart1` (`include/sm_uart_avr.h`): HardwareSerial's own buffers, the same
    bytes and counts, without a virtual call per byte.

  What that changes on the wire (`tools/shadowmap/test_transcripts.py` checks it against
  `0a8b73f`): a press in the pass a request arrives goes out first, so its PRESS event is sent
  where `0a8b73f` dropped it for want of room, and a SET in that pass gives the press the old key
  (as if the SET had come a pass later); a READY in the first pass after a boot names that pass's
  controllers. GET takes up to two passes longer; SUM stays under 0.5 s.
- **Loop time.** Worst passes on the emulated ATmega2560 at 16 MHz (`test_avr_timing_*`; Timer1,
  interrupts off, Serial1 idle so each drain moves 64 bytes, 15-character keys and defaults, an
  11-character prefix, a 7-character suffix, stale records, HELLO 20, EVENTS on):

  | Class | Target | Worst | Where |
  | --- | --- | --- | --- |
  | T1: one heavy unit | 750 us | 628.5 us | a SET of 15 raw bytes |
  | T2: a press as a write finishes, or two presses | 900 us | 739.5 us | a deferred press and a long press |
  | T3: three or four presses (report only) | under 1 ms | 971.5 us | four presses, one deferred |

  Raw commands at their longest (`test_avr_timing_v4`): a GET's passes with the 93-character SLOT
  line 489 us (the line itself 382 us), a DUMP's 487 us, SUM over raw records 435 us, a SET of 15
  raw bytes 628.5 us (the first write 562.5 us), a raw press with the 81-character PRESS line
  557.5 us, a raw SET arriving as a raw press fires 607.5 us.

  Some T1 passes (v3 figures; v4's are within a few us): a press with its PRESS line 567.5 us
  (EVENTS off 287.5 us), a request arriving as a press fires 620.5 us, HELLO 20 575 us, BAT
  546.5 us, a GET's passes 444.5 us, a DUMP's 444 us, SUM 372 us, SET 489.5 us, the battery report
  474 us, 63 bytes on both ports 369 us. Before this
  rule and the speedups that came with it (`0a8b73f`), 61 of these figures reached 1 ms (a GET
  1.37 ms, a press 1.07 ms, DEFAULTS' last step 10.7 ms, a GET and a long press in one pass
  2.84 ms). A test fails if a T1 pass is over 750 us, a T2 pass over 900 us, or anything reaches
  1 ms. Four presses with an EEPROM job finishing can't happen: a deferred press always skips the
  writer's next step. Motor updates run every 25 ms. Nothing in the mapping waits:
  - A lookup that would have to wait on an EEPROM write is deferred to the next pass. That
    press is dropped if its controller has gone, or if it's more than 250 ms old.
  - A request that can't be answered yet stays queued (see above).
  - USB replies wait until the debug output is nearly empty, so `printOutput()` never writes
    more than about 128 bytes for them.

  Boot reads the 144 double-tap and long-press records once, in `setup()`: 10 ms with the
  firmware's defaults (21.6 ms at `0a8b73f`).
- **Round trips on SHADOW's side,** from the simulator, 9600 baud: GET about 75 ms, SET about
  120 ms (250 ms for the first write, which formats the EEPROM), SUM about 0.48 s, DUMP about
  11.7 s for 216 slots. The inbound leg adds about 1 ms per character.
- **Round trips on the droid** (2026-09-24, WCB 1 S2, from DroidNet's listener API on the Pi to the reply in
  its serial history): HELLO 91-129 ms, GET 107-136 ms, SUM about 140 ms, SET 205 ms (303 ms for the first
  write), REVERT 159-175 ms, DUMP 4.41 s to `END DUMP 72`. Those were v1, before double taps; expect a
  DUMP of about 12.5 s (v3, 216 slots) and a SUM of about 0.5 s now (not yet measured on the droid).
- **Memory:**
  - Static SRAM is 2998 bytes (`0a8b73f`: 2968 with a version string of the same length; v3 with
    this timing work 2978). The SLOT reply in hand takes 20 and the pass ledger 3; the hex digit
    table (17) went to flash; v4 adds the longer OK SET echo and the raw flags.
  - Flash is 58,064 of 253,952 bytes (`0a8b73f`: 53,178): the CRC byte planes (1.5 KB) and the
    assembly, the head CRC states, the Serial1 adapter, raw commands.

## Testing

```bash
pio test -e native                        # host: 202 tests (+37 AVR-only, skipped)
pio test -e simavr --without-uploading    # emulated ATmega2560: 238 (+1 host-only, skipped)
python3 -m unittest tools/shadowmap/test_shadowmap.py   # reference client vs. the firmware logic
python3 -m unittest tools/shadowmap/test_transcripts.py # what SHADOW sends, against 0a8b73f
```

- **What's covered:**
  - slot layout and tokens, and the resolver
  - old-vs-new equivalence for every button combination
  - the store: power cuts at every byte of slot, header and revert writes, header wraparound, a
    stuck EEPROM bit, stale-slot resurrection
  - the protocol: every verb and error, CRC handling, routing, DUMP/SUM consistency, flow control,
    button commands always fitting during a DUMP
  - runtime timing, and the real EEPROM under simavr
  - every mapping pass's worst time (`test_avr_timing_*`: requests, writes, DUMP and SUM, every kind
    of press, RX passes, requests meeting presses, two to four presses in a pass)
  - the faster code against the code it replaced: CRC16 for every (state, byte), CRC32, the AVR
    assembly for every length, every slot's parts and text, the parsers, record validity, the
    defaults' RAM bits, the line builder, the TX copies at every ring position, `sm::Uart1`
    against HardwareSerial (`test_equiv_*`, `test_avr_crc_asm`, `test_avr_uart1`)
  - the per-pass rule (`test_scheduler`), and transcripts of eight scripted sessions against
    goldens from `0a8b73f`, at once and at 2 bytes a pass (`tools/shadowmap/test_transcripts.py`)
  - double taps (`test_double_tap`): exclusive firing, the window, holds, taps after a double
    tap, both controllers, drops and faults, events
  - long presses (`test_long_press`, 24 cases): the threshold, releases before and after it, with
    and without a double tap, a double tap on a long-press combo, no repeat, another combo pressed
    while waiting, a modifier landing late, both controllers, drops and faults, the staleness rule,
    events
  - the Python client through a WCB emulator, including corrupted and dropped lines and a
    remote-WCB route
- **If a simavr run hangs:** the test program has probably run out of the Mega's 8 KB of SRAM. On
  AVR every expected-string literal lives in RAM, so split the suite, as `test_protocol` and
  `test_protocol_more` are. The fake EEPROM only backs the headers and single-press slots in
  full; double-tap records come from a pool of 6 (`FakeEeprom::kPoolRecords`).

## Not done yet

- On the droid: real button presses, driving while editing, and a power cycle (HANDOFF_BUTTON_MAP.md §0).
- On the droid: long presses. Worth checking: 600 ms on a Navigation controller, and the v3 DUMP
  time through the WCB.
- On the droid: double taps (`bde79a4`). Tested on the host, simavr and through DroidNet against the simulator only.
  Worth checking: that 350 ms suits a Navigation controller's D-pad, and the v2 DUMP time through the WCB.
- The DroidNet tab and backend (see the [backend design](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-09-24-shadow-button-map-design.md)).
- New WCB stored commands for volume and brightness, then map them.
- Maybe: a "learn" mode (report a press without firing it) and a lock notice. Neither is built.
