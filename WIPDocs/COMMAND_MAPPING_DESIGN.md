# Runtime Button Mapping: Design

**Status:** proposal, not implemented. **Branch:** `claude/arduino-command-mapping-config-x49d73`

## Goal

Change which WCB stored command each controller button combo sends without reflashing SHADOW
and without disconnecting the PS3 controllers. DroidNet on the Pi is the editor. SHADOW keeps
the mapping in its own EEPROM and runs without the Pi.

## Decisions so far

- **Volume and brightness are removed.** Their SHADOW-side state (`vol`, `brightness`, the
  `2P`/`<PVV...>`/holo `R00xx` strings) goes away and they become ordinary WCB stored commands.
  After that, every mapped button just sends one WCB stored command.
- **Approach:** the Pi edits and SHADOW stores the mapping in EEPROM. The fallback approach,
  keeping the whole mapping on the WCB as stored commands named after each combo, stays on the
  shelf.
- **Transport: the WCB link, not the Mega's USB port.** When Linux opens the Mega's USB port
  it asserts DTR, which resets the Mega and drops the controllers. DroidNet also toggles DTR on
  purpose (`web/port_handler.py:351-362` in DroidNet). The WCB path avoids the reset entirely. USB stays for
  flashing and bench testing.

## How messages travel

```
DroidNet (Pi) --USB--> Pi's WCB --(ESP-NOW if a different board)--> SHADOW's WCB --S<x> 9600--> SHADOW Serial1
DroidNet (Pi) <--USB-- Pi's WCB <--(ESP-NOW if a different board)-- SHADOW's WCB <--S<x> 9600-- SHADOW Serial1
```

- **Pi to SHADOW:** the Pi writes `;S<x>@SM ...` to its WCB, or `;W<n>;S<x>@SM ...` if SHADOW is
  on WCB `n`. The WCB strips the route and writes `@SM ...\r` to SHADOW
  (`processSerialMessage`, WCB `Code/WCB/WCB.ino:6444`).
- **SHADOW to Pi:** SHADOW writes `;S0@SM ...` (or `;W<n>;S0@SM ...`) to its WCB. Port 0 is USB,
  so the WCB prints `@SM ...` to the Pi (`WCB.ino:6468`). Commands that arrive over ESP-NOW go
  through the same parser (`WCB.ino:4694`), so the remote form works too. That comes from
  reading the source; it has not been tested on hardware.
- DroidNet's always-on WCB listener already receives these lines. A passive matcher, like the
  Victron responder and the stream trigger, picks out the `@SM` replies.

WCB line numbers refer to `travisccook/Wireless_Communication_Board-WCB` `main` at `66845b9`
(6.2.1).

### Rules the WCB imposes

1. **SHADOW never sends a bare line on Serial1.** A line that doesn't start with `;` or `?` is
   broadcast to every S-port on every WCB (`processBroadcastCommand`, `WCB.ino:6853`). Every
   reply is wrapped in `;S0`. The one exception is `@WDP1 ...`, which the WCB captures before
   parsing (`WCB.ino:7015`).
2. **No `^` in any payload.** The WCB splits commands on it. No `;T`/`;t` either, because the WCB
   treats those as timer commands. The line checksum is therefore `*<crc>`, not `^?CHK`. Any
   argument that could contain special characters, such as the command prefix, is sent
   hex-encoded.
3. **SHADOW's port hears every broadcast on the mesh.** The parser ignores any line that doesn't
   start with `@SM `. Recommend `?BCAST,OUT,S<x>,OFF` on SHADOW's port, since SHADOW has never
   needed broadcast input.
4. **With WCB debug on**, the Pi also sees lines like `Sent to USB: @SM ...` and
   `Processing input from Serial3: ;S0@SM ...`. The Pi only acts on lines that *start* with `@SM `.
5. **The link runs at 9600 baud in a noisy droid.** Every line in both directions carries a
   CRC32.

## Slots

The mapping is a fixed grid, so a lookup is arithmetic rather than a table scan:

- Controllers: `F` (foot), `D` (dome)
- Buttons: `UP RIGHT DOWN LEFT L3 PS`
- Modifiers: `NONE L1 L2 PS CIRCLE CROSS`
- `index = controller*36 + modifier*6 + button`, which gives 72 slots

**Lookup order is unchanged from today:** when a button is pressed, the held modifiers are
checked in the order `L1, L2, PS, CIRCLE, CROSS`, then `NONE`. The first slot with a mapping
wins. `CROSS`, `CIRCLE` and `PS` modifiers can still come from the other controller.

**Reserved slots.** These are hardcoded in `main.cpp` or never reach the mapping, and the Pi
greys them out:

| Slot | Why |
| --- | --- |
| `PS` + `NONE` / `PS` / `CIRCLE` / `CROSS`, both controllers | PS only reaches the mapping with L1/L2 held. PS+CROSS and PS+CIRCLE disable and enable the drive stick. |
| Foot `L3` + `L1` | Over-speed toggle |

That leaves 31 usable foot slots and 32 dome slots.

**Known bug:** `ps3ToggleSettings()` doesn't check which controller it is handling, so dome
L3+L1 toggles foot over-speed *and* fires Front Holo Center. The fix limits over-speed to the
foot controller, which frees dome L3+L1.

## EEPROM layout (4096 bytes; nothing uses EEPROM today)

```
0x000  header, 32 B   magic "SM", layout version, generation, prefix (default ";W1;C"),
                      suffix (default ""), header CRC16
0x020  slots, 72 x 18 B
         name[16]     stored-command key, up to 15 chars, NUL-padded ("" = unmapped)
         crc16        CRC over (generation, slot index, name)
0x530  free, ~2.7 KB  room for speeds and deadzones later
```

- **Per-slot CRC, no commit step.** A slot is live as soon as it's written. A slot whose CRC
  fails, for example after a power cut mid-write, uses its built-in default. The other slots
  are unaffected.
- **Generation byte.** `DEFAULTS` increments it. Every existing slot CRC then fails, so all slots
  read their defaults immediately, after a one-byte write.
- **Prefix and suffix live in the header,** so a WCB syntax change (`;SEQ`, or adding `,L` to stop
  6.2+ fanning `;C` out to the whole mesh) is a Pi-side edit.
- **Wrong header** (bad magic, layout version or CRC): SHADOW uses the built-in defaults for
  everything.
- **Flashing preserves the mapping.** Bootloader uploads don't touch EEPROM, whether from
  PlatformIO or DroidNet's `avrdude -D`.

## SHADOW firmware changes

- **Remove** the `marcDuinoButtonPush` switch, the volume/brightness state, `SimpleButtonMapping`
  and its SRAM descriptions. Sending becomes `prefix + key + suffix + "\r"`.
- **Built-in defaults:** today's mapping as a PROGMEM table of 72 names (1152 B of flash, 0 B of
  SRAM).
- **Non-blocking line reader** on Serial1 and Serial: a 64 B buffer, `\r` or `\n` ends a line, and
  a line that is too long is dropped. It never uses `readStringUntil`, which blocks for up to 1 s.
  The `while (Serial1.available()) Serial1.read();` flushes in `simpleMarcDuino*()` go away,
  because they would eat protocol bytes.
- **Non-blocking EEPROM writer:** one byte per loop pass, only when `eeprom_is_ready()`, skipping
  bytes that are unchanged. About 60 ms per slot at worst. One SET is in flight at a time.
- **No half-written names:** a button press on the slot being written sends the new name from the
  write buffer.
- **Non-blocking replies:** a reply is only sent when `Serial1.availableForWrite()` has room, so
  it never blocks the loop. `DUMP` sends one line per loop pass.
- **Optional device announce:** every 30 s, `@WDP1 {"type":"Shadow","fw":"<ver>","caps":["shadow.map"]}`.
  "Shadow" is already in the WCB's device-type list, and the WCB mesh then knows which board and
  port SHADOW is on.
- **SRAM:** frees the 440 B of mapping tables, their descriptions and most of the switch's string
  literals. The new code adds about 160 B of buffers. The current build uses 3801 of 8192 B.

## Protocol

Every line is `@SM <verb> [args] *<CRC32 of everything before " *", 8 hex digits>`. A line with a
bad CRC is dropped, and the Pi retries after a timeout. SET is idempotent, so retries are safe.

| Pi to SHADOW | SHADOW reply |
| --- | --- |
| `@SM HELLO <route>` where route `0` = the Pi is on SHADOW's WCB, `n` = the Pi's WCB is `n` | `@SM READY v1 gen=<g> slots=72 lock=<0\|1> fw=<ver>` |
| `@SM GET F L1 UP` | `@SM SLOT F L1 UP scream src=eeprom` (or `src=default`) |
| `@SM DUMP` | 72 `SLOT` lines, then `@SM END` |
| `@SM SET F L1 UP scream` (`-` = unmapped) | `@SM OK F L1 UP` once the bytes are written, or `@SM ERR <locked\|reserved\|badkey\|busy> F L1 UP` |
| `@SM DEFAULTS` | `@SM OK DEFAULTS` |
| `@SM PREFIX <hex>` / `@SM SUFFIX <hex>` | `@SM OK PREFIX` |

- Keys match `[A-Za-z0-9_]{1,15}`, the same limit the WCB uses for stored-command keys.
- HELLO sets the reply route (`;S0` or `;W<n>;S0`), which is held in RAM. SHADOW never replies
  on Serial1 before a HELLO.
- Requests that arrive on USB get bare `@SM` replies on USB, for bench testing from a laptop or
  DroidNet's serial monitor.
- Round trip is about 100-200 ms. Remapping a few buttons takes well under a second, and a full
  push of 72 slots takes about 10 s.

## Pi side (DroidNet)

- **Backend:** `web/handlers/shadow_handlers.py` plus `scripts/shadow_mapping.py`, following the
  CHiRP tab's pattern.
  - Sends through the existing WCB listener (`send_wcb_command`, `expect_reply=False`).
  - Correlates `@SM` replies with a passive matcher on the listener's `data` events, like
    `scripts/victron_request_responder.py`. It has timeouts and retries.
  - Never opens the Mega's USB port for this.
- **Config:** `/opt/droidnet/config/shadow-mapping.json` holds the route (the Pi's WCB, SHADOW's
  WCB and S-port) and descriptions. SHADOW itself stores only the keys.
- **Stored-command picker:** from `GET /api/comlink/stored-commands`.
- **UI:** a graphical controller mapping tab. That's a separate design pass.
- `Reference/ControlMapping.xlsx` and `WIPDocs/scripts/update_control_mapping.py` currently parse
  `config.cpp`. They should read the Pi's mapping, or SHADOW's `DUMP`, instead.

## Open questions

1. **Wiring:** is SHADOW's Serial1 on the WCB the Pi's USB is plugged into? Which S-port, and what
   are the WCB numbers?
2. **Edit lock:** require PS+CROSS on the foot controller (drive stick disabled) before SHADOW
   accepts writes, or allow them any time? Writes never touch motor control. The lock only stops
   a mapping changing mid-drive.
3. **Replacement stored commands:** what are the keys for volume and brightness, so the defaults
   table can use them?
