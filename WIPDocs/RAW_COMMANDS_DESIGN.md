# SHADOW Raw Commands - Design

> **Status:** built: SHADOW `f098694` (tag `stage/raw-v4`, on SHADOW `main`); on DroidNet `main`
> (`8d97d76c..1a4385cc`). Not yet deployed or flashed; rollout follows the plan's Part C, each step with the owner's go-ahead.
> **Scope:** a SHADOW button (any slot: single tap, double tap or long press) can send a **raw command**, free text
> typed by the owner, instead of a WCB stored command. Covers the firmware (`travisccook/DroidNet-SHADOW-C2B5`,
> checked out at `~/Repos/DroidNet-SHADOW-C2B5`, `@SM` protocol v4), DroidNet's SHADOW link, API and tab, and the
> rollout to c2-body.
> **Builds on:** [2026-09-24-shadow-button-map-design.md](2026-09-24-shadow-button-map-design.md),
> [2026-09-29-shadow-long-press-design.md](2026-09-29-shadow-long-press-design.md) and the firmware's
> `WIPDocs/COMMAND_MAPPING_DESIGN.md`, whose rules this follows wherever it doesn't say otherwise.

## 1. Goal

Most buttons run a WCB stored command, and that stays the default. But SHADOW shouldn't force one: a button can hold
a short line of text that SHADOW sends to its WCB exactly as typed, with no prefix and no suffix.

Out of scope: raw commands longer than 15 characters, raw built-in defaults (`src/default_mappings.cpp` stays
stored commands only), a per-button prefix or suffix, and rewriting raw text for a board's custom special characters.
The HCR renumbering check doesn't see raw commands: it scans the saved copy, which holds them as `!<hex>` (owner,
2026-10-02).

## 2. Decisions made in the brainstorm

- **Free text.** No required first character and no banned characters. The only limits are physical:
  - **1 to 15 characters**, the size of an EEPROM slot (16 bytes with the terminating NUL).
  - **Printable ASCII, space included** (0x20-0x7E). A line break would end the command early; anything outside
    ASCII means nothing to the WCB and would take several of the 15 bytes.
  - **Not empty**: empty already means "unmapped".
- **`^` and `;T` are allowed in a raw command.** The firmware's rule against them protects SHADOW's own `@SM` replies,
  and raw text never appears in a reply unencoded (section 4.3). In a fired command they do what they always do on
  a WCB: `^` splits the line into several commands, `;T` makes it a timer command.
- **Sent exactly as typed.** No prefix, no suffix, no respelling for a board's `?CMDCHAR`/`?FUNCCHAR`/`?DELIM`.
- **What the WCB does with it** is the WCB's business, and the tab explains it rather than blocking it (checked in
  the WCB source at DroidNet's pinned commit `66845b9`, `Code/WCB/WCB.ino:4869-4880`):
  - starts with the command character (`;` by default): a normal WCB command;
  - starts with the function character (`?` by default): a settings command on SHADOW's own WCB;
  - anything else: broadcast to every serial port on every board (`processBroadcastCommand`, `WCB.ino:6853`), unless
    broadcasts from SHADOW's port are blocked or routed by a serial mapping, in which case it goes where that setting
    sends it, or nowhere.
  - The WCB trims spaces from each line (`WCB.ino:7010`) and each `^` part (`WCB.ino:2370`) first, so the first
    character after any spaces decides, and a line of spaces only does nothing.

## 3. What the owner sees

### 3.1 Inspector

- Above the stored-command picker, a two-way switch: **Stored command · Raw command**. It applies to the press type
  picked (Single tap / Double tap / Long press), like Change to, Clear and Test.
- **Raw command** replaces the picker with:
  - a monospace text field, `maxlength` 15, with a counter (`9 / 15`);
  - **Save** (Enter also saves) and **Test** (sends the line once through DroidNet, as Test does for stored
    commands; off for text that starts with a space, which DroidNet's WCB send path refuses: `raw_space`. Any other
    text, trailing spaces included, is sent exactly as typed);
  - an inline error when the text breaks a rule: "Raw commands are 1-15 printable ASCII characters.";
  - a hint chosen from the text's first character after any spaces, as in section 2: "Runs as a WCB command.",
    "Runs as a settings command on SHADOW's WCB (WCB n).", "Not a WCB command: the WCB broadcasts it to every serial
    port on every board.", or, for spaces only, "Only spaces: the WCB trims them and ignores the empty line." The
    command and function characters come from SHADOW's WCB when DroidNet knows them (`wcb_special_chars`), else the
    defaults `;` and `?`. n is the prefix's WCB number, as the tab's "SHADOW via WCB n" already uses (the route
    carries no WCB number).
- A slot that holds a raw command opens on **Raw command** with its text filled in. The field follows what SHADOW
  has for that press (Undo, Clear, Use default, another window) unless the owner is typing in it (focused and
  changed from what it was filled with); typing keeps the editor on **Raw command** even if a live event changes
  the press's kind.
- Switching modes doesn't change SHADOW until Save or a stored command is picked.

### 3.2 Everywhere a slot's command shows

- **Combo cards, All combos, the cheat sheet, callouts and Last press** show a raw command in monospace with a small
  **raw** tag. Long text is cut with an ellipsis and the full text is in the tooltip. **Find by pressing** shows no
  command of its own: it names the combo and opens the inspector, which opens on Raw command for a raw slot.
- **"Also on" badges** match raw commands by exact text, and never match a stored command with the same spelling.
- **Labels** belong to stored commands; a raw command has none.
- **The unassigned-only filter** (todo item 4) counts only stored-command slots as using a stored command.
- **Compare with saved copy** treats raw-ness as part of the value: `Happy` raw and `Happy` stored differ.
  Applying the saved copy names the combos it skipped (for example "this SHADOW can't send raw commands (update its
  firmware)") and the ones whose write failed, with the reason, instead of reporting success.

### 3.3 Older firmware

Against a SHADOW speaking v1-v3 the **Raw command** side of the switch is disabled with "Update SHADOW's firmware
to send raw commands." Everything else works as today.

## 4. Firmware

### 4.1 Storage

- **No new EEPROM layout.** A slot record stays 16 bytes of text plus CRC16; the header stays layout 1.
- **Raw flag:** bit 7 of the record's first byte. Printable text never sets it, so the flag costs no characters. A
  raw record is `first char | 0x80`, then the rest of the text, then NULs.
- **Validation on load** (`slotRecordValid`) accepts either a stored-command key (`isValidKey`, unchanged) or a raw
  record: flag set, 1-15 bytes, every byte (first with the flag cleared) in 0x20-0x7E, NUL-terminated.
- **Downgrade is safe:** v3 and older firmware fail `isValidKey` on a raw record and fall back to that slot's
  built-in default, exactly as for a record that failed its CRC.
- The store hands callers the text and whether it is raw (no caller sees the flag bit). `beginSet` takes both.
- Built-in defaults stay keys.

### 4.2 Firing

`Runtime::fire` queues a raw command with `pushRaw`: the text plus `\r`, without the prefix and suffix. 15 + 1 bytes is
within `kCommandHeadroom` (34), so a raw press can never be crowded out either.

### 4.3 Protocol v4

Wherever a slot's command appears on the wire, a raw command is written **`!` followed by its bytes in upper-case
hex** (`!3B57323B5334504C41593A33` is `;W2;S4PLAY:3`). Keys are `[A-Za-z0-9_]`, so a token starting with `!` can't be
a key. The WCB never sees raw text in an `@SM` line, so no `;`, `^` or `;T` from a raw command can be acted on in
transit.

- `READY v4 ...`. Nothing else in HELLO changes.
- `SET <ctl> <mod> <btn> !<hex>` sets a raw command. SHADOW accepts upper or lower case hex. Odd length, a bad digit,
  0 or more than 15 bytes, or a byte outside 0x20-0x7E is `ERR key`. The order isn't specified: hex that doesn't
  decode (odd length, a bad digit, a NUL byte, `!` alone, `!-`, 16 or more bytes) answers `ERR key` before the slot's
  reserved check, while hex that decodes to a byte outside 0x20-0x7E is checked after it, so on a reserved slot it
  answers `ERR reserved`. As for every write, `ERR slot` and `ERR busy` come first.
- `OK SET`, `SLOT` (DUMP and GET) and `PRESS` carry the same `!<hex>` token. `def=` never does (defaults are keys).
- `SUM` is unchanged in definition: it is computed over the `SLOT` bodies, `!<hex>` included, so DroidNet's copy
  matches byte for byte.
- **Line length:** the longest reply becomes a `SLOT` line with a 15-character raw command on `F CIRCLE RIGHTL` and a
  15-character default: 83 characters, plus 10 for the CRC, 93 in all. A `PRESS` with a raw command and `from=`
  reaches 81.
  `Protocol::kMaxLine` goes from 80 to **96**. Requests stay within 72 (`SET` with 15 raw bytes is 65 with its CRC).
  The WCB carries lines far longer than this (`structCommand[200]`).
- `opArg_`, which echoes the value in `OK SET`, grows to hold `!` plus 30 hex digits.
- A `PRESS` event is sent when the event port has room for that line (`kPressLineMin`, 36, first, then the line's own
  length), not for `kMaxLine`: with `kMaxLine` at 96 the old check dropped the event of any command over 21 bytes.
- RAM: static SRAM rises by 10 bytes, 2018 to 2028 of 8192 (`opArg_` 23 -> 32 is 9 of them), and the line buffers
  grow by 16 bytes each, on the stack. Flash is 55,306 bytes (54,404 at timing round 2). The simavr runs confirm the
  8 KB SRAM still fits.
- Loop time. SHADOW's timing rounds (Part A, then round 2 at `5443ae6`) replaced the plan's fix for `0a8b73f`'s
  1.07 ms GET pass (a `SLOT` reply read on one pass and sent on a later one, CRC16 a byte at a time, whole-part TX
  copies); v4 adapts round 2's pass rule instead (`WIPDocs/COMMAND_MAPPING_DESIGN.md`, Timing and memory):
  - No `SLOT` reply is held over to a later pass: a `GET` is answered in the pass that reads it, and the background
    unit builds and sends one `DUMP` line a pass when the port has room for `kMaxLine`.
  - Both ports share one **32-byte read budget** a pass (`kMaxBytesPerPass`, 64 in round 2), so a request over 32
    bytes, such as a raw `SET`, takes one more pass to read.
  - At most **2 presses fire a pass** (`kMaxFiresPerPass`, 3 in round 2); the others wait in an 8-entry FIFO and fire
    in order, and one older than 250 ms or whose controller has gone is dropped.
  - The EEPROM writer's finishing step is put off one pass when that pass handled a request, so its `OK` comes one
    pass later, and a write-type request arriving in between gets `ERR busy`, which DroidNet resends.
  - Measured on the emulated ATmega2560 (`test_avr_timing_*`, 161 figures, each must be under 1 ms): the worst pass is
    921.5 us (three presses due and a write's last step: two fire, one waits), the worst read pass 906.5 us (a full
    32-byte budget ending a raw `GET`, the 93-character `SLOT` answered, a battery change and a drain), and a `GET`
    of a raw command on `F CIRCLE RIGHTL` 901.0 us.
- A `DUMP` takes about 12.5 s at 9600 baud (12.75 s in the simulator), inside DroidNet's 20 s `dump_timeout`.

### 4.4 Firmware docs

`WIPDocs/COMMAND_MAPPING_DESIGN.md`: raw records, v4, `!<hex>`, the new line limit, the v4 timing figures, and a
pointer to this spec (copied as `WIPDocs/RAW_COMMANDS_DESIGN.md`; the firmware plan is
`WIPDocs/RAW_COMMANDS_FIRMWARE_PLAN.md`). The reference client `tools/shadowmap/shadowmap.py` (v4 only), the host
simulator `shadow_sim.cpp` and their contract tests speak v4, and the transcript goldens moved to
`tools/shadowmap/golden/v4`, with a raw session.

## 5. DroidNet

### 5.1 Protocol and link

- `scripts/shadow_protocol.py`:
  - `PROTOCOL_VERSION = 4`, `PROTOCOL_VERSIONS = (1, 2, 3, 4)`, `PORTED_FROM = "f098694"`.
  - `SlotState` and `Event` gain `raw: bool`. Parsing a `!<hex>` token sets it; `body` writes it back as `!<hex>`,
    so `map_sum` and the saved copy (stored as `SLOT` bodies) handle raw commands with no format change.
  - `RAW_RE` / `valid_raw()` for 1-15 printable ASCII characters, next to `KEY_RE`.
  - Hex is parsed strictly: a `!<hex>` token (`parse_value`) and `hex_decode` take hex digits only, as SHADOW's
    `ERR key` rule does, so whitespace inside the hex (which `bytes.fromhex` would skip) is a parse error.
- `scripts/shadow_link.py`:
  - `set_slot`, `apply` and `test_fire` take `raw`. A raw value is checked with `valid_raw()`, a key with `KEY_RE`
    as now.
  - Raw is refused on a SHADOW older than v4: `{"error": "no_raw"}`. `apply` skips such slots with `why: "no_raw"`.
  - The status gains `raw_commands: bool` (v4), next to `double_taps` and `long_presses`.
  - `test_fire` with `raw` sends the text as-is, trailing spaces included, without SHADOW's prefix and suffix or
    respelling for the board's special characters (`send_as_typed`). Like set and apply it is refused with `no_raw`
    before v4, with `offline` before SHADOW has answered HELLO (an unheard SHADOW isn't old firmware), and with
    `raw_space` for text that starts with a space (DroidNet's WCB send path refuses it; the WCB would trim it anyway).
  - Restoring the saved copy carries raw-ness through (a raw `*` stays text, never REVERT). `apply` returns the slots
    it skipped (`why`: `no_raw`, `reserved`, `no_double`, `no_long`, `badraw`, `badkey`) and the op lists the ones
    whose write failed; the tab reports both.
  - One request at a time: a single worker sends a request, waits for its reply (1 s, 20 s for a `DUMP`) and resends
    on silence, `ERR crc` and `ERR busy` (3 resends, then HELLO and one more round), as v4's pass timing needs.
- `web/handlers/shadow_handlers.py`: `POST /api/shadow/slot`, `/apply` items and `/test` accept `"raw": true`.
  New messages: `badraw` "Raw commands are 1-15 printable ASCII characters.", `no_raw` "Update SHADOW's firmware
  to send raw commands." and `raw_space` "DroidNet can't send a line that starts with a space, so it can't test this
  raw command. SHADOW sends it as typed." `docs/API_REFERENCE.md` documents them.
- `scripts/shadow-dev-server.py` and the sim tests (`DROIDNET_SHADOW_DIR`) run against the v4 `shadow_sim.cpp`
  (`f098694`); the v3 sim tests run against `0a8b73f`.
- `@SM` lines reach the link as PortHandler `data` events (`web/server.py` `_subscribe_passive_matchers`), not
  through `scripts/wcb_listener.py`; a link test sends the longest (93-character) `SLOT` line through `on_data`, and
  another through PortHandler's own line split.

### 5.2 SHADOW tab (`web/js/shadow-model.js`, `web/js/shadow.js`)

- The model's slot gains `raw`; `validRaw()` mirrors `valid_raw()`. `usesOf` and the drift check compare
  `(raw, key)` pairs.
- The inspector, cards, cheat sheet and events as in section 3.

### 5.3 Export and import

- Export **version 4**: an entry may carry `"raw": true` (`{"key": ";W2;S4PLAY:3", "src": "eeprom", "raw": true}`).
  Entries without it are stored commands, so versions 1-3 read the same.
- Import reads versions 1 to 4. A raw entry is checked with `validRaw()`. Onto an older SHADOW it is skipped and
  listed: "Skipped, this SHADOW can't send raw commands (update its firmware): ...".

### 5.4 Docs

`docs/SHADOW_BUTTON_MAP_GUIDE.md`: a **Raw commands** section (what they are, the 15-character limit, what the WCB
does with each kind of first character, the firmware requirement).

## 6. Rollout

Every device step waits for the owner's go-ahead, after checking that no other session is using c2-body or SHADOW's
port.

1. **DroidNet with v4** is released and deployed to c2-body. It still speaks v3 to the SHADOW running now. (A v3
   DroidNet refuses a v4 SHADOW at HELLO, so DroidNet must go first.)
2. **Flash SHADOW** with the v4 firmware (`f098694`) after a rollback dump of the running `0a8b73f`. The layout is
   unchanged, so the owner's map (flourish double taps, long presses, Overload) survives; confirm with SUM against the
   saved copy.
3. **Owner tries a raw command** on a spare combo, then clears it or keeps it.

Both repos are committed straight to `main`.

## 7. Testing

**Firmware** (`pio test -e native`, `pio test -e simavr --without-uploading`, the reference client's unittest):

- Store: a raw record round-trips; the flag is invisible to callers; bad raw records (no NUL, a byte outside
  0x20-0x7E, empty) read as the default; a raw record fails the v3 rule `isValidKey` (the downgrade path); a power
  cut during a raw write, and REVERT and DEFAULTS, cover raw records (`test_store_raw`).
- Protocol: `SET !<hex>` in both cases; bad hex, odd length, 16 bytes, a control byte and `!` alone are `ERR key`;
  `SLOT`, `GET`, `OK SET` and `PRESS` carry `!<hex>`; the longest `SLOT` and `PRESS` lines fit `kMaxLine`; `SUM` over a
  map with raw slots; a long command keeps its `PRESS` event; `READY v4` (`test_protocol_raw`).
- Runtime: a raw press queues exactly the text and `\r`, with no prefix or suffix, including text with `^`, `;T` and
  spaces; a raw press still gets its headroom with the TX queue busy (`test_raw_press`).
- Memory and loop time: the simavr runs fit the 8 KB SRAM, and `test_avr_timing_*` keeps every pass under 1 ms, now
  including `GET` and `DUMP` of the longest `SLOT` lines and raw presses (`test_avr_timing_v4`; section 4.3).
- At `f098694`: `pio test -e native` 212 tests (43 AVR-only skipped), `pio test -e simavr` 254, plus the reference
  client's unittest and the v4 transcript goldens.

**DroidNet** (`pytest tests/`, `npx jest web/js`): `!<hex>` parsing and formatting, `valid_raw`, `map_sum` with raw
slots; the link against the v4 sim, including `no_raw` against v3, raw test fire, and a saved copy holding raw
slots; handler validation; export v4 and import of v1-v4; the model's `validRaw`, `usesOf` and drift. The tab through
the SHADOW dev server: switch, Save, Test, hints, the raw tag, and the disabled switch for a v3 SHADOW. The sim
tests run against both the v4 (`f098694`) and the v3 (`0a8b73f`) simulator, and an e2e test at 320 and 390 px keeps
the ×2 and Long lines in All combos readable on a phone.
