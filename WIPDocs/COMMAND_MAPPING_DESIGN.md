# Runtime Button Mapping

**Status:** implemented, and merged to `main` in [PR #1](https://github.com/travisccook/SHADOW-C2B5/pull/1). It is
tested on the host, on an emulated ATmega2560 (simavr), and against the reference Pi client.
**It has not run on the droid yet.** The DroidNet side is designed but not built. Its design and UI live in the
DroidNetSignalBooster repo: [backend design](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-09-24-shadow-button-map-design.md) and
[UI design and mockup](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-09-24-shadow-button-map-ui-design.md). Work in progress is tracked in
[HANDOFF_BUTTON_MAP.md](HANDOFF_BUTTON_MAP.md).

## What it does

- **Every combo maps to a WCB stored command.** When a combo is pressed, SHADOW sends
  `<prefix><key><suffix>\r` to the WCB on Serial1. By default that's `;W1;C<key>\r`, the same
  bytes as before.
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
   start with `@SM ` without buffering it. `?BCAST,OUT,S<x>,OFF` on SHADOW's port cuts the
   traffic, since SHADOW doesn't need it.
4. **WCB debug echoes are ignored.** With WCB debug on, the Pi also sees `Sent to USB: @SM ...`
   and similar lines. Clients only act on lines that *start* with `@SM `.
5. **The link runs at 9600 baud in a noisy droid.** Every line in both directions carries a
   CRC32.

## Slots

The mapping is a fixed grid: `index = controller*36 + modifier*6 + button`, which gives 72 slots.

- Controllers: `F` (foot), `D` (dome)
- Modifiers: `NONE L1 L2 PS CIRCLE CROSS`
- Buttons: `UP RIGHT DOWN LEFT L3 PS`

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

**Reserved slots** are hardcoded in `main.cpp` or never reach the mapping. They can't be
mapped, and `SLOT` lines say why:

| Slot | `res=` | Why |
| --- | --- | --- |
| `PS` + `NONE` / `PS` / `CIRCLE` / `CROSS`, both controllers | `psgate` | PS is only a button with L1/L2 held. PS+CROSS and PS+CIRCLE toggle the drive stick. |
| `F L1 L3` | `overspeed` | Over-speed toggle |

That leaves 31 foot slots and 32 dome slots.

**Fixed along the way:** L3+L1 on the *dome* controller used to toggle foot over-speed as well
as firing Front Holo Center. Over-speed now only listens to the foot controller.

## EEPROM layout

The layout uses 1360 of the 4096 bytes. Nothing else uses EEPROM.

```
0x000  header A  32 B   magic "SM", layout version, slot count, sequence, generation,
0x020  header B  32 B   prefix (<= 11, default ";W1;C"), suffix (<= 7, default ""), CRC16
0x040  72 slots x 18 B  key (<= 15 chars, NUL-padded; "" = unmapped) +
                        CRC16 over (layout version, generation, slot index, key)
0x550  free
```

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

## Protocol reference (v1)

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
- **Slots:** written as `<C> <M> <B>`, e.g. `F L1 UP`.
- **One write-type operation at a time.** SET, REVERT, DEFAULTS, PREFIX, SUFFIX, DUMP and SUM are
  refused with `ERR busy` while another one runs. HELLO, CFG, GET and EVENTS are always answered.

### Requests

| Request | Reply |
| --- | --- |
| `HELLO [route]` | `READY v1 fw=<ver> slots=72 gen=<g> fmt=<0\|1> pads=<FD\|F\|D\|->`. `route` sets where replies go on the WCB port: `0` (the default) = `;S0`, `n` = `;W<n>;S0`. It is kept in RAM, so the Pi should send HELLO after SHADOW reboots. |
| `CFG` | `CFG pre=<hex> suf=<hex\|->` |
| `GET <slot>` | `SLOT <slot> <key\|-> <src> def=<key\|->`, or for a reserved slot `SLOT <slot> - reserved res=<reason>`. `src` is `eeprom`, `default` or `reserved`. |
| `SET <slot> <key\|->` | `OK SET <slot> <key\|->` once written and verified. `-` unmaps the slot, as a custom value. |
| `REVERT <slot>` | `OK REVERT <slot>`. The slot is back on its built-in default (`src=default`). |
| `DUMP` | 72 `SLOT` lines in slot order, then `END DUMP 72`. About 4.2 s at 9600 baud. |
| `SUM` | `SUM <8 hex> slots=72`: the CRC32 over, for each slot in order, its `SLOT` line text after `@SM SLOT ` plus `\n`. The Pi can compute the same over a cached DUMP and skip the DUMP when they match. About 0.2 s. |
| `DEFAULTS` | `OK DEFAULTS gen=<g>`. Every slot is back on its built-in default. |
| `PREFIX <hex>` | `OK PREFIX <hex>`. The prefix must start with `;`, can't end with `;`, is at most 11 printable characters, and has no `^` and no `;T`/`;t`. |
| `SUFFIX <hex\|->` | `OK SUFFIX <hex\|->`. At most 7 printable characters, no `^`, no `;T`/`;t`. |
| `EVENTS <0\|1>` | `OK EVENTS <0\|1>`. With 1, this port gets the events below. |

### Events

These are only sent with `EVENTS 1`, and are dropped rather than queued if the link is busy.

| Event | When |
| --- | --- |
| `PRESS <slot> <key\|-> <src> [from=<F\|D>] [res=<reason>]` | Any combo that reaches the mapping, mapped or not. `from=` means the modifier was held on that other controller. |
| `CTL <F\|D> <0\|1>` | A controller connected or dropped |

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
  when you aren't expecting them.

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

- **Loop time.** Worst-case time the mapping adds to one loop pass, measured on the emulated
  ATmega2560 at 16 MHz:
  - handling a request: 0.59 ms
  - a pass during a SET: 0.52 ms
  - a pass during a DUMP: 0.90 ms
  - a button press: 0.20 ms

  A test fails if any of these reach 1 ms. Motor updates run every 25 ms. Nothing in the mapping
  waits:
  - A lookup that would have to wait on an EEPROM write is deferred to the next pass. That
    press is dropped if its controller has gone, or if it's more than 250 ms old.
  - A request that can't be answered yet stays queued, and bytes arriving meanwhile are read
    and dropped so Serial1's 64-byte RX buffer can't overflow.
  - USB replies wait until the debug output is nearly empty, so `printOutput()` never writes
    more than about 128 bytes for them.
- **Round trips on SHADOW's side,** from the simulator, 9600 baud: GET about 75 ms, SET about
  120 ms (250 ms for the first write, which formats the EEPROM), SUM about 185 ms, DUMP about
  4.2 s. The inbound leg adds about 1 ms per character.
- **Memory:**
  - Static SRAM is 2712 bytes, down from 3801 before this change (the tables, descriptions and
    switch strings are gone).
  - Flash is 49,306 of 253,952 bytes.

## Testing

```bash
pio test -e native                        # host: 80 tests (+2 AVR-only, skipped)
pio test -e simavr --without-uploading    # emulated ATmega2560: all 82
python3 -m unittest tools/shadowmap/test_shadowmap.py   # reference client vs. the firmware logic
```

- **What's covered:**
  - slot layout and tokens, and the resolver
  - old-vs-new equivalence for every button combination
  - the store: power cuts at every byte of slot, header and revert writes, header wraparound, a
    stuck EEPROM bit, stale-slot resurrection
  - the protocol: every verb and error, CRC handling, routing, DUMP/SUM consistency, flow control,
    button commands always fitting during a DUMP
  - runtime timing, and the real EEPROM under simavr
  - the Python client through a WCB emulator, including corrupted and dropped lines and a
    remote-WCB route
- **If a simavr run hangs:** the test program has probably run out of the Mega's 8 KB of SRAM. On
  AVR every expected-string literal lives in RAM, so split the suite, as `test_protocol` and
  `test_protocol_more` are.

## Not done yet

- Run it on the droid. That means flashing, checking `[WDP-DA]` shows SHADOW on the Pi's WCB, then
  `tools/shadowmap/shadowmap.py --wcb ... dump`.
- The DroidNet tab and backend (see the [backend design](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-09-24-shadow-button-map-design.md)).
- New WCB stored commands for volume and brightness, then map them.
- Maybe: a "learn" mode (report a press without firing it) and a lock notice. Neither is built.
