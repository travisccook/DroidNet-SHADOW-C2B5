# Runtime Button Mapping

**Status:** implemented (first merged to `main` in [PR #1](https://github.com/travisccook/DroidNet-SHADOW-C2B5/pull/1)),
tested on the host, on an emulated ATmega2560 (simavr), and against the reference Pi client. **C2-B5 runs
`9acc997`** (release v1.0.0, flashed 2026-10-16), which has everything below: double taps, long presses, raw
commands and the allowed-controller list ([CONTROLLER_ADDRESSES_DESIGN.md](CONTROLLER_ADDRESSES_DESIGN.md)).
Verified on the droid: the WCB link in both directions, every verb, EEPROM persistence across a reset and a full
power cycle (2026-09-24, [results on PR #1](https://github.com/travisccook/DroidNet-SHADOW-C2B5/pull/1#issuecomment-5817847387)),
and real presses with live `PRESS` and `CTL` events, and driving with both controllers (2026-10-16). What's still
open is under [Not done yet](#not-done-yet). DroidNet's side lives in the DroidNetSignalBooster repo:
[backend design](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-09-24-shadow-button-map-design.md) and
[UI design and mockup](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-09-24-shadow-button-map-ui-design.md).

## What it does

- **Every combo maps to a WCB stored command.** When a combo is pressed, SHADOW sends
  `<prefix><key><suffix>\r` to the WCB on Serial1. By default that's `;W1;C<key>\r`, the same
  bytes as before.
- **Every combo can also be double-tapped** (except the stick click, L3) and map to a second
  stored command (see [Double taps](#double-taps)).
- **Every combo can also be held** for a long press (except L3) and map to a third stored
  command (see [Long presses](#long-presses)).
- **Any slot can hold a raw command instead:** up to 15 characters that SHADOW sends to the WCB
  exactly as typed, with no prefix and no suffix (see [Raw commands](#raw-commands)).
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
   and similar lines. Clients only act on lines that _start_ with `@SM `.
5. **The link runs at 9600 baud in a noisy droid.** Every line in both directions carries a
   CRC32.

The one deliberate exception to rules 1 and 2 is a [raw command](#raw-commands): the owner typed it,
so SHADOW sends it as typed, bare or with `^` or `;T`. It never appears unencoded in an `@SM` line.

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
- **The layer.** The highest-priority held modifier picks the layer:
  `L1, L2, PS, CIRCLE, CROSS`, then no modifier. CROSS, CIRCLE and PS also count when they're held on the other
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

### Raw commands

Added 2026-10-03 (protocol v4). Design: DroidNetSignalBooster's
[raw commands spec](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-10-02-shadow-raw-commands-design.md). Any slot (single press, double
tap or long press) can hold a raw command instead of a stored-command key: 1-15 printable ASCII
characters (0x20-0x7E, space included), never empty (empty means unmapped).

- **Sent exactly as typed:** the text and `\r`, with no prefix, no suffix and no respelling for a
  board's `?CMDCHAR`/`?FUNCCHAR`/`?DELIM`. 15 + 1 bytes is within `kCommandHeadroom` (34), so a raw
  press can't be crowded out either (`pushRaw`).
- **What the WCB does with it** depends on its first character: the command character (`;`) makes it
  a WCB command, the function character (`?`) a settings command on SHADOW's WCB, anything else is
  broadcast to every serial port on every board. `^` and `;T` inside it do what they always do on a
  WCB.
- **Stored as a record with bit 7 of its first byte set** (`kRawFlag`): printable text never sets it,
  so the flag costs no characters, and no caller of `MapStore` sees it (`keyForSlot` reports it
  separately). See EEPROM layout.
- **On the wire** a raw command is `!` and its bytes in upper-case hex (see Protocol reference).
- **Built-in defaults stay stored-command keys.**

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

| What happens next                                               | What fires                                                                    |
| --------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| Still held at 0.6 s                                             | The long press, at once. Then nothing until release (`Latched`).              |
| Released before 0.6 s, no double tap mapped                     | The single tap, on release.                                                   |
| Released before 0.35 s, double tap mapped, second press in time | The double tap, on the second press (exactly as before).                      |
| Released before 0.35 s, double tap mapped, no second press      | The single tap, when the window closes (as before).                           |
| Released between 0.35 s and 0.6 s, double tap mapped            | The single tap, on release (the window has already closed).                   |
| Another combo pressed on the same controller while it waits     | The waiting combo's single tap, at once; the new combo is handled on its own. |
| The controller drops while it waits                             | Nothing: the press is dropped (as before).                                    |

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

| Slot                                                        | `res=`      | Why                                                                                 |
| ----------------------------------------------------------- | ----------- | ----------------------------------------------------------------------------------- |
| `PS` + `NONE` / `PS` / `CIRCLE` / `CROSS`, both controllers | `psgate`    | PS is only a button with L1/L2 held. PS+CROSS and PS+CIRCLE toggle the drive stick. |
| `F L1 L3`                                                   | `overspeed` | Over-speed toggle                                                                   |
| every `L32` (double tap of L3)                              | `stick`     | The stick click has no double tap                                                   |
| every `L3L` (long press of L3)                              | `stick`     | The stick click has no long press                                                   |

The double tap of a reserved slot is reserved for the same reason (`F L1 L32` is `overspeed`).
The long press of a reserved slot is reserved too. That leaves 31 foot slots and 32 dome slots
for single presses, and 26 on each controller for double taps and for long presses.

**Fixed along the way:** L3+L1 on the _dome_ controller used to toggle foot over-speed as well
as firing Front Holo Center. Over-speed now only listens to the foot controller.

## EEPROM layout

The mapping uses 3952 of the 4096 bytes; the controller addresses take the next 36 (see
`WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md`). Nothing else uses EEPROM.

```
0x000  header A  32 B   magic "SM", layout version, slot count, sequence, generation,
0x020  header B  32 B   prefix (<= 11, default ";W1;C"), suffix (<= 7, default ""), CRC16
0x040  72 slots x 18 B  single presses: key (<= 15 chars, NUL-padded; "" = unmapped) +
                        CRC16 over (layout version, generation, slot index, key)
0x550  72 slots x 18 B  double taps (slot index 72-143), the same records
0xA60  72 slots x 18 B  long presses (slot index 144-215), the same records
0xF70  4 x 9 B          controller addresses F, D, F2, D2: state, address, CRC16
0xF94  free
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

- **Raw commands came later, without a new layout.** A raw record is the text with bit 7 of its first
  byte set, then NULs, under the same CRC (the flag is covered). `slotRecordValid` accepts a key or a
  raw record (flag set, 1-15 bytes, each 0x20-0x7E with the flag cleared, NUL-terminated). Firmware
  from before raw commands fails its key check on a raw record and reads that slot's built-in default,
  as for a record that failed its CRC, so going back to older firmware is safe.

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

## Protocol reference (v4)

`MAC` and `REFUSED` came later without a version change: a client tells firmware without them by
`ERR verb` to `MAC`.

v2 added the double-tap slots: `slots=144`, 144 lines in a DUMP, and button tokens like `UP2`.
v3 adds the long-press slots: `slots=216`, 216 lines in a DUMP, and `UPL` tokens. Nothing else
changed. v4 adds raw commands: wherever a slot's command appears (`OK SET`, `SLOT` in GET and DUMP,
`PRESS`), a raw command is `!` and its bytes in upper-case hex (`!3B57323B5334504C41593A33` is
`;W2;S4PLAY:3`), and `SET` takes one; keys are `[A-Za-z0-9_]`, so a `!` token is never a key, and the
WCB never sees a raw command's `;`, `^` or `;T` in an `@SM` line. SHADOW accepts the hex in either
case and sends upper case. `def=` never carries a raw command. `SUM` is unchanged in definition: the
CRC32 over the `SLOT` bodies, `!<hex>` included. A client from before v4 refuses a v4 SHADOW at
HELLO. DroidNet speaks v1 to v4 (it only shows double taps to a v2 SHADOW, long presses to a v3 one
and raw commands to a v4 one, and sends `SET ... !<hex>` only to v4); the reference client
(`tools/shadowmap/`) speaks v4 only.

Every line is one of these:

- **Request:** `@SM <VERB> [args] [*<CRC>]`
- **Reply or event:** `@SM <WORD> [args] *<CRC>`

`<CRC>` is the CRC32 (IEEE, the same as `zlib.crc32`) of everything before ` *`, as 8 hex
digits. SHADOW accepts upper or lower case and sends upper case.

- **CRC on the WCB port:** required. A request without it, or with a wrong one, gets
  `@SM ERR crc`. On the USB port the CRC is optional.
- **Lengths:** requests are at most 72 characters (the longest, a raw `SET`, is 65); replies at most 96
  (`Protocol::kMaxLine`), before the route. The longest is a `SLOT` with a 15-character raw command
  over a 15-character default on `F CIRCLE RIGHTL`: 93. A `PRESS` with a raw command and `from=` is 81.
  An event is sent only when its port has room for that line itself (not for `kMaxLine`), and never
  while a press waits for the TX queue.
- **Terminators:** lines end in `\r` or `\n`.
- **Tokens:** separated by single spaces.
- **Slots:** written as `<C> <M> <B>`, e.g. `F L1 UP`, `F L1 UP2` for its double tap, or `F L1 UPL` for its long press.
- **One write-type operation at a time.** SET, REVERT, DEFAULTS, PREFIX, SUFFIX, DUMP, SUM and MAC (its
  list and its changes) are refused with `ERR busy` while another one runs. HELLO, CFG, GET, EVENTS, BAT
  and `MAC <S>` are always answered.

### Requests

| Request                         | Reply                                                                                                                                                                                                                                 |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `HELLO [route]`                 | `READY v4 fw=<ver> slots=216 gen=<g> fmt=<0\|1> pads=<FD\|F\|D\|->`. `route` sets where replies go on the WCB port: `0` (the default) = `;S0`, `n` = `;W<n>;S0`. It is kept in RAM, so the Pi should send HELLO after SHADOW reboots. |
| `CFG`                           | `CFG pre=<hex> suf=<hex\|->`                                                                                                                                                                                                          |
| `GET <slot>`                    | `SLOT <slot> <key\|!hex\|-> <src> def=<key\|->` (`!hex`: a raw command), or for a reserved slot `SLOT <slot> - reserved res=<reason>`. `src` is `eeprom`, `default` or `reserved`.                                                    |
| `SET <slot> <key\|!hex\|->`     | `OK SET <slot> <key\|!HEX\|->` once written and verified. `-` unmaps the slot, as a custom value. `!hex` (either case) sets a raw command; the echo is upper case.                                                                    |
| `REVERT <slot>`                 | `OK REVERT <slot>`. The slot is back on its built-in default (`src=default`).                                                                                                                                                         |
| `DUMP`                          | 216 `SLOT` lines in slot order (single presses, double taps, then long presses), then `END DUMP 216`. About 12.5 s at 9600 baud.                                                                                                      |
| `SUM`                           | `SUM <8 hex> slots=216`: the CRC32 over, for each slot in order, its `SLOT` line text after `@SM SLOT ` plus `\n`. The Pi can compute the same over a cached DUMP and skip the DUMP when they match. About 0.4 s.                     |
| `DEFAULTS`                      | `OK DEFAULTS gen=<g>`. Every slot is back on its built-in default.                                                                                                                                                                    |
| `PREFIX <hex>`                  | `OK PREFIX <hex>`. The prefix must start with `;`, can't end with `;`, is at most 11 printable characters, and has no `^` and no `;T`/`;t`.                                                                                           |
| `SUFFIX <hex\|->`               | `OK SUFFIX <hex\|->`. At most 7 printable characters, no `^`, no `;T`/`;t`.                                                                                                                                                           |
| `EVENTS <0\|1>`                 | `OK EVENTS <0\|1>`. With 1, this port gets the events below.                                                                                                                                                                          |
| `BAT`                           | `BAT F=<lvl> D=<lvl> fs=<st> ds=<st> up=<s> hb=<s>`: the controllers' battery levels (see [Battery reports](#battery-reports)).                                                                                                       |
| `MAC`                           | Four lines, F, D, F2, D2, one a pass: `MAC <S> <address\|-> <eeprom\|default>`. Refused with `ERR busy` while another operation runs, like DUMP.                                                                                      |
| `MAC <S>`                       | `MAC <S> <address\|-> <eeprom\|default>`. Always answered, like GET. `S` is `F`, `D`, `F2` or `D2`.                                                                                                                                   |
| `MAC <S> <address\|-\|default>` | `OK MAC <S> <ADDRESS\|-\|default>` once written and verified: an address (`HH:HH:HH:HH:HH:HH`, either case; echoed upper case), `-` for no controller, `default` for the build's. A write: one at a time with SET and the others.     |

### Events

These are only sent with `EVENTS 1`, and are dropped rather than queued if the link is busy.

| Event                                                            | When                                                                                                                                                                                                                                                     |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `PRESS <slot> <key\|!hex\|-> <src> [from=<F\|D>] [res=<reason>]` | Any combo that reaches the mapping, mapped or not, when it fires: a double tap as `UP2`, a long press as `UPL` when it fires, a combo that waited for a second tap when its window closes. `from=` means the modifier was held on that other controller. |
| `CTL <F\|D> <0\|1>`                                              | A controller connected or dropped                                                                                                                                                                                                                        |
| `REFUSED <F\|D> <address>`                                       | A paired controller that isn't allowed tried to connect, as the foot or the dome. Also sent to the USB port without EVENTS.                                                                                                                              |

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

| Status byte   | Reported as                | Level  |
| ------------- | -------------------------- | ------ |
| `0x03`-`0x05` | `3`-`5`                    | `OK`   |
| `0x02`        | `2`                        | `LOW`  |
| `0x00`-`0x01` | `0`-`1`                    | `CRIT` |
| `0xEE` and up | `chg` (even) / `usb` (odd) | `OK`   |
| anything else | ignored                    |        |

A new level has to hold before SHADOW adopts it, so a reading flickering at a step boundary
doesn't flap: 2 s for the first reading of a connection, 3 s for a worse level, 30 s for a
better one. A controller that disconnects goes to `-` at once.

The status byte comes from `PS3BT::getBatteryStatus()`, added to the bundled USB Host Shield
library. It's saved only from input reports, so unlike `getStatus()` it never reads a byte of
some other packet. **Not yet measured on the droid:** how long a Navigation controller keeps
going after it first reports `LOW` (`2`) or `CRIT` (`1`).

### Errors

The format is `ERR <code> [<VERB> [<slot>]]`.

| Code                | Meaning                                                                                                                                 |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| `crc`               | The CRC was missing or wrong (the verb isn't echoed). Resend.                                                                           |
| `busy`              | Another operation is running. Retry shortly.                                                                                            |
| `args`              | Wrong number or shape of arguments                                                                                                      |
| `verb`              | Unknown verb                                                                                                                            |
| `slot`              | Not a valid `<C> <M> <B>`                                                                                                               |
| `reserved`          | The slot is reserved                                                                                                                    |
| `key`               | Not `[A-Za-z0-9_]{1,15}`, `-`, or `!` and the hex of 1-15 bytes 0x20-0x7E (odd length, a bad digit, `!`, `!-`, a NUL, 16 or more bytes) |
| `prefix` / `suffix` | The value breaks the rules above                                                                                                        |
| `eeprom`            | The write didn't verify after one retry                                                                                                 |
| `addr`              | Not a usable address (`HH:HH:HH:HH:HH:HH`, not all 0 or all FF)                                                                         |

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

These follow the owner's announce decisions of 2026-09-23 (SHADOW alone announces; it's alone on its WCB
port), with one change the owner made on 2026-09-24: SHADOW now also _reads_ Serial1, for `@SM`.

## DroidNet's export (the mapping on paper)

DroidNet's SHADOW tab (DroidNetSignalBooster `c6902014`, DroidNet 2.3.6) exports and imports the whole mapping as JSON.
That replaces the old control-mapping spreadsheet, which is retired. DroidNet owns this format; its handoff
(`docs/superpowers/HANDOFF_2026-09-24_shadow_button_map.md`) is the reference. Version 1 looks like this:

```json
{
  "format": "droidnet-shadow-map",
  "version": 1,
  "exported": "<ISO time>",
  "booster": "c2-body",
  "shadow": { "fw": "af4db5d", "gen": 1 },
  "prefix": ";W1;C",
  "suffix": "",
  "slots": {
    "F": {
      "L1": {
        "UP": { "key": "extremehappysou", "src": "default" },
        "PS": { "key": null, "src": "default" }
      }
    },
    "D": {}
  },
  "labels": { "extremehappysou": "HCR Extreme Happy" }
}
```

- Slots use the same `<C> <M> <B>` names as the protocol. Each is `{key: string|null, src: "default"|"eeprom"}`, so a
  custom value that happens to equal the default stays distinct.
- Reserved slots are left out.
- Version 2 (DroidNetSignalBooster `193851b`) adds the double taps under their wire names (`"UP2": {...}`) next to the
  single presses. DroidNet imports version 1 and 2 files.
- Version 3 adds the long presses (`"UPL": {...}`), and version 4 marks a raw command with
  `"raw": true` (`{"key": ";W2;S4PLAY:3", "src": "eeprom", "raw": true}`). DroidNet imports
  versions 1 to 4.
- `labels` are DroidNet's descriptions of the WCB stored commands, kept on the Pi. SHADOW never sees them.

## Firmware structure

| File                                                    | What                                                                                                                 |
| ------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| `lib/ShadowMap/src/sm_slots.*`                          | Slot layout, tokens, reserved slots                                                                                  |
| `lib/ShadowMap/src/sm_resolve.*`                        | Held buttons to slot (the lookup rule)                                                                               |
| `lib/ShadowMap/src/sm_store.*`                          | EEPROM store (headers, slots, the background writer)                                                                 |
| `lib/ShadowMap/src/sm_protocol.*`                       | The `@SM` protocol, `WcbPort` reply routing                                                                          |
| `lib/ShadowMap/src/sm_line.*`, `sm_tx.*`                | Non-blocking line reader, Serial1 TX queue that always keeps room for a button command                               |
| `lib/ShadowMap/src/sm_runtime.*`                        | What `loop()` does for the mapping: repeat timing, deferred presses, request pumping, the announce                   |
| `lib/ShadowMap/src/sm_crc.*`, `sm_eeprom.h`, `sm_pgm.h` | CRC16/CRC32, EEPROM access, flash-string shims for host builds                                                       |
| `src/default_mappings.cpp`                              | The built-in mappings (edit these; see CLAUDE.md)                                                                    |
| `src/main.cpp`                                          | Reads the controllers into `sm::PadState`, calls the runtime; `UsbReplyPort` sends USB replies with the debug output |

The library has no Arduino dependency, so it builds and tests on the host.

## Timing and memory

- **Loop time.** Worst-case time the mapping adds to one loop pass, measured on the emulated
  ATmega2560 at 16 MHz by the `test_avr_timing_*` suites (161 figures; Timer1, interrupts off,
  Serial1 idle so each drain moves 64 bytes, 15-character keys and defaults). Every figure is under
  1 ms, enforced by `test_avr_timing_*` (a figure at or over 1 ms fails the suite).

  **The pass rule** (`Runtime::afterButtons`, called from `loop()` after the motors and the buttons;
  `WIPDocs/TIMING_ROUND_2_DESIGN.md` section 3):
  - A pass where a press fired reads no request bytes and runs no background unit.
  - The first pass without a press after one that handled a request runs the background unit and
    reads no request bytes.
  - Any other pass pumps the ports (WCB first, then USB) until one request is handled or the bytes
    run out; if none was handled, it runs the background unit. Both ports share one 32-byte read
    budget a pass (`kMaxBytesPerPass`; USB gets what the WCB left). It was 64 until raw commands: with
    the 93-character SLOT line the worst read pass measured 988.5 us, 11.5 us under the limit. A long
    request now takes one more pass to read.
  - **The EEPROM writer's finishing step** (`MapStore::finishDue()`, the verify that ends a write) is
    put off one pass when the pass handled a request; its other steps run every pass. A full-budget
    request answered in the same pass as a finishing step measured 1022.5/1036.5 us without this
    (it existed in round 2 too, unmeasured). The put-off step runs in the next pass.

  **Waiting presses.** A press that can't go (EEPROM mid-write, no room in the TX queue, or already
  two fired this pass) waits in a FIFO of 8 and fires in order, at most 2 presses a pass
  (`kMaxFiresPerPass`, 3 until raw commands: a PRESS event is now checked against its own length, so
  more PRESS lines go out, and three presses plus a write's last step had reached 1090.5 us); one
  older than 250 ms or whose controller has gone is dropped. While any press waits, the TX queue is
  held for its command (the hold).

  **Worst figures:**

  |    us | Pass                                                                                                                 |
  | ----: | -------------------------------------------------------------------------------------------------------------------- |
  | 921.5 | three presses due + a write's last step: two fire, one waits (the worst press-only pass)                             |
  | 906.5 | full 32-byte budget ending a raw GET, the 93-character SLOT answered, + battery change + drain (the worst read pass) |
  | 904.0 | the same GET answered as a raw SET's (or DEFAULTS') finishing step comes due                                         |
  | 901.0 | GET pass: raw command on F CIRCLE RIGHTL, 93-character SLOT line                                                     |
  | 881.5 | three raw presses due + a raw SET's last step: two fire, one waits                                                   |
  | 811.0 | GET pass: F CIRCLE RIGHTL (15-char key over 15-char default)                                                         |
  | 713.5 | four presses due in one pass (two fire, two wait a pass)                                                             |
  | 708.5 | 32 junk bytes read + battery report + DEFAULTS' finishing step + drain (the worst junk pass)                         |

  Round 2 (`5443ae6`) had 944.5 for the 63-byte read, 892.0 for three presses + a write's last step and
  812.5 for four presses due (three fire).

  Before round 2 (Part A, `52df1b9`): 5 of 126 figures at or over 1 ms, worst 1.115 ms; at `0a8b73f`, 61.

  - The worst whole pass is the three-press pass with a write's last step, 921.5 us, 78.5 us under the
    limit; the worst read pass is 906.5 us. Round 2's 944.5 us read pass became 906.5 us with the
    32-byte budget and the longer v4 lines (`kMaxLine` 96).
  - While a WCB request can't be answered yet, the USB port isn't read. The request waits for room in
    the TX queue (the 9600-baud drain frees it, about 1 byte a ms) or for the EEPROM write to finish
    (a few ms a byte). This only matters on the bench, where the USB port is used.
  - A request refused for room still counts as the pass's request. Presses landing in most
    passes, with the queue full, can therefore keep delaying a request that can't be answered (the
    Task 6 sweeps put a press every 2-3 passes with a slow drain); real presses come about a second apart.

  Boot reads the 144 double-tap and long-press records once, in `setup()`.

  Motor updates run every 25 ms. Nothing in the mapping waits:
  - A lookup that would have to wait on an EEPROM write is deferred to the next pass. That
    press is dropped if its controller has gone, or if it's more than 250 ms old.
  - A request that can't be answered yet stays queued, and bytes arriving meanwhile are read
    and dropped so Serial1's 64-byte RX buffer can't overflow. It counts as its pass's request.
  - USB replies wait until the debug output is nearly empty, so `printOutput()` never writes
    more than about 128 bytes for them.

- **Round trips on SHADOW's side,** from the simulator, 9600 baud: GET about 75 ms, SET about
  120 ms (250 ms for the first write, which formats the EEPROM), SUM about 330 ms, DUMP about
  7.9 s for 144 slots (4.2 s for 72); expect about 12 s for 216. The inbound leg adds about 1 ms per character.
- **Round trips on the droid** (2026-09-24, WCB 1 S2, from DroidNet's listener API on the Pi to the reply in
  its serial history): HELLO 91-129 ms, GET 107-136 ms, SUM about 140 ms, SET 205 ms (303 ms for the first
  write), REVERT 159-175 ms, DUMP 4.41 s to `END DUMP 72`. Those were v1, before double taps; expect a
  DUMP of about 12.5 s (v3, 216 slots) and a SUM of about 0.3 s now (not yet measured on the droid).
- **Memory:**
  - Static SRAM is 2028 bytes (`pio run -e SHADOWC2B5`: "used 2028 bytes from 8192 bytes"), down
    from 3801 before the runtime mapping (the tables, descriptions and switch strings are gone).
    Double taps added 89 of them; timing Part A saved 21 (the hex digit table went to flash).
    Part A left it at 2895. Turning `SHADOW_VERBOSE` off in round 2 saved about 930 bytes (to 1969);
    the round's own growth was 49 bytes, inside the 64-byte budget (limit 1969 + 64 = 2033). Raw commands
    (2026-10-03) added 10: `opArg_` grew from 23 to 32 bytes (+9), the rest is the store's and the
    runtime's new state (not itemised). The line buffers grew by 16 bytes each, on the stack.
  - Flash is 55,306 of 253,952 bytes (54,404 at round 2; 56,264 after Part A, with `SHADOW_VERBOSE` on; 53,126 before
    Part A).

## Testing

```bash
pio test -e native                        # host: 212 tests (+43 AVR-only, skipped)
pio test -e simavr --without-uploading    # emulated ATmega2560: 254 (+1 host-only, skipped)
python3 -m unittest tools/shadowmap/test_shadowmap.py   # reference client vs. the firmware logic
python3 -m unittest tools/shadowmap/test_transcripts.py # what SHADOW sends, against golden/v4
```

- **What's covered:**
  - slot layout and tokens, and the resolver
  - old-vs-new equivalence for every button combination
  - the store: power cuts at every byte of slot, header and revert writes, header wraparound, a
    stuck EEPROM bit, stale-slot resurrection
  - the protocol: every verb and error, CRC handling, routing, DUMP/SUM consistency, flow control,
    button commands always fitting during a DUMP
  - runtime timing, and the real EEPROM under simavr
  - every mapping pass's worst time (`test_avr_timing_*`; fails at 1 ms)
  - waiting presses (`test_defer`): the FIFO, the hold, the two-a-pass limit, the 250 ms and
    controller-gone drops, commands that wait for TX room
  - the pass rule (`test_scheduler`): a press pass reads nothing, requests and the background unit
    both progress, a DUMP and a stream of GETs both finish, the fault path
  - the faster code against the code it replaced (`test_equiv_*`, `test_avr_crc_asm`,
    `test_avr_uart1`), and transcripts of scripted sessions against goldens in `tools/shadowmap/golden/v4`
    (`tools/shadowmap/test_transcripts.py`; recorded from `0a8b73f` with raw commands' Tasks 1-3
    applied; its docstring lists the 10 documented categories of difference, and there is a raw session)
  - double taps (`test_double_tap`): exclusive firing, the window, holds, taps after a double
    tap, both controllers, drops and faults, events
  - long presses (`test_long_press`, 24 cases): the threshold, releases before and after it, with
    and without a double tap, a double tap on a long-press combo, no repeat, another combo pressed
    while waiting, a modifier landing late, both controllers, drops and faults, the staleness rule,
    events
  - raw commands (`test_store_raw`, `test_protocol_raw`, `test_raw_press`, `test_avr_timing_v4`): the
    record and its flag (round trip, mid-write and queued reads, bad records, the downgrade rule, power
    cuts), `!<hex>` on every line, bad hex, the longest lines, SUM over raw slots, a raw press sent as
    typed with no prefix or suffix, its headroom, and a long command keeping its PRESS event
  - the Python client through a WCB emulator, including corrupted and dropped lines and a
    remote-WCB route
- **If a simavr run hangs:** the test program has probably run out of the Mega's 8 KB of SRAM. On
  AVR every expected-string literal lives in RAM, so split the suite, as `test_protocol` and
  `test_protocol_more` are. The fake EEPROM only backs the headers and single-press slots in
  full; double-tap records come from a pool of 6 (`FakeEeprom::kPoolRecords`).

## Not done yet

**On the droid.** C2-B5's firmware has all of these; nothing records them being checked there yet:

- One remap end to end: `SET F NONE L3 scanning`, press foot L3 (the probe-droid scan plays), then
  `REVERT F NONE L3`. And DroidNet's Find by pressing a combo.
- WCB 1's `alarm` (foot CIRCLE+LEFT: the Death Star klaxon and a 120 s red Flthy pulse) and `sadsound`
  (foot RIGHT) stored commands. Both were recreated on WCB 1 on 2026-09-24 and have never been fired.
- Driving while DroidNet reads the map (repeated `DUMP`s): driving must feel unchanged.
- A raw command fired from a controller.
- Long presses: whether 600 ms suits a Navigation controller.
- Double taps: whether 350 ms suits a Navigation controller's D-pad.

**Hardware questions for the owner.**

- SHADOW's 5 V TX1 drives WCB 1's RX, which on WCB V2.4 goes straight to an ESP32 GPIO with no level
  shifter (the stress is continuous, since a UART idles high). A divider on that one wire, for example
  1 kΩ in series and 2 kΩ to GND, would fix it.
- Is SHADOW fed on VIN (7 V or more) or on 5 V? The Mega ADK's power switch watches only VIN, so a board
  fed on 5 V (or on VIN below about 6.6 V) meets the USB host's 5 V when flashing over USB. With the droid's
  rail on and USB unplugged, about 5 V from VBUS to GND on the USB-B socket means use a USB power blocker.

**Firmware.**

- `criticalFaultDetect()` and `criticalFaultDetectDome()` in `src/main.cpp` still `delay(100)` when a
  controller's report has a bad status byte (`delay(15)` for the foot when no dome controller is
  connected). That blocks the whole loop for up to 100 ms a pass while the data stays bad. Found by the
  2025 safety review (`POTENTIALSAFETYISSUES.md`, removed 2026-10-16; in git history). Its other findings
  don't hold against today's code or fail safe: the foot reconnect flag clears as soon as reports arrive
  (`Usb.Task()` runs before the check every pass), `lastMsgTime` has no race (one thread), and the dome
  stopping on a foot fault, the 30 s first-message timer and the duplicate foot stop are all fail-safe.
- New WCB stored commands for volume and brightness, then map them (built-in defaults or DroidNet only:
  undecided).
- Maybe: a "learn" mode (report a press without firing it) and a lock notice. Neither is built.
