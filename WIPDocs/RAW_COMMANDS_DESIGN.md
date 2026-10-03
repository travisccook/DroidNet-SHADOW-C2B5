# SHADOW Raw Commands - Design

> **Status:** design approved in the brainstorm of 2026-10-02; not built.
> **Scope:** a SHADOW button (any slot: single tap, double tap or long press) can send a **raw command**, free text
> typed by the owner, instead of a WCB stored command. Covers the firmware (`travisccook/DroidNet-SHADOW-C2B5`,
> checked out at `~/Repos/SHADOW-C2B5`, `@SM` protocol v4), DroidNet's SHADOW link, API and tab, and the rollout to
> c2-body.
> **Builds on:** [2026-09-24-shadow-button-map-design.md](2026-09-24-shadow-button-map-design.md),
> [2026-09-29-shadow-long-press-design.md](2026-09-29-shadow-long-press-design.md) and the firmware's
> `WIPDocs/COMMAND_MAPPING_DESIGN.md`, whose rules this follows wherever it doesn't say otherwise.

## 1. Goal

Most buttons run a WCB stored command, and that stays the default. But SHADOW shouldn't force one: a button can hold
a short line of text that SHADOW sends to its WCB exactly as typed, with no prefix and no suffix.

Out of scope: raw commands longer than 15 characters, raw built-in defaults (`src/default_mappings.cpp` stays
stored commands only), a per-button prefix or suffix, and rewriting raw text for a board's custom special characters.

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

## 3. What the owner sees

### 3.1 Inspector

- Above the stored-command picker, a two-way switch: **Stored command · Raw command**. It applies to the press type
  picked (Single tap / Double tap / Long press), like Change to, Clear and Test.
- **Raw command** replaces the picker with:
  - a monospace text field, `maxlength` 15, with a counter (`9 / 15`);
  - **Save** (Enter also saves) and **Test** (sends the line once through DroidNet, as Test does for stored
    commands);
  - an inline error when the text breaks a rule: "Raw commands are 1-15 printable ASCII characters.";
  - a hint chosen from the text's first character, as in section 2: "Runs as a WCB command.", "Runs as a settings
    command on SHADOW's WCB (WCB n).", or "Not a WCB command: the WCB broadcasts it to every serial port on every
    board." The command and function characters come from SHADOW's WCB when DroidNet knows them
    (`wcb_special_chars`), else the defaults `;` and `?`.
- A slot that holds a raw command opens on **Raw command** with its text filled in.
- Switching modes doesn't change SHADOW until Save or a stored command is picked.

### 3.2 Everywhere a slot's command shows

- **Combo cards, All combos, the cheat sheet, callouts, Last press and Find by pressing** show a raw command in
  monospace with a small **raw** tag. Long text is cut with an ellipsis and the full text is in the tooltip.
- **"Also on" badges** match raw commands by exact text, and never match a stored command with the same spelling.
- **Labels** belong to stored commands; a raw command has none.
- **The unassigned-only filter** (todo item 4) counts only stored-command slots as using a stored command.
- **Compare with saved copy** treats raw-ness as part of the value: `Happy` raw and `Happy` stored differ.

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

`Runtime::fire` queues a raw command as the text plus `\r`, without the prefix and suffix. 15 + 1 bytes is within
`kCommandHeadroom` (34), so a raw press can never be crowded out either.

### 4.3 Protocol v4

Wherever a slot's command appears on the wire, a raw command is written **`!` followed by its bytes in upper-case
hex** (`!3B57323B5334504C41593A33` is `;W2;S4PLAY:3`). Keys are `[A-Za-z0-9_]`, so a token starting with `!` can't be
a key. The WCB never sees raw text in an `@SM` line, so no `;`, `^` or `;T` from a raw command can be acted on in
transit.

- `READY v4 ...`. Nothing else in HELLO changes.
- `SET <ctl> <mod> <btn> !<hex>` sets a raw command. SHADOW accepts upper or lower case hex. Odd length, a bad digit,
  0 or more than 15 bytes, or a byte outside 0x20-0x7E is `ERR key`.
- `OK SET`, `SLOT` (DUMP and GET) and `PRESS` carry the same `!<hex>` token. `def=` never does (defaults are keys).
- `SUM` is unchanged in definition: it is computed over the `SLOT` bodies, `!<hex>` included, so DroidNet's copy
  matches byte for byte.
- **Line length:** the longest reply becomes a `SLOT` line with a 15-character raw command on `F CIRCLE RIGHTL` and a
  15-character default: 83 characters, plus 10 for the CRC, 93 in all. A `PRESS` with a raw command and `from=`
  reaches 81.
  `Protocol::kMaxLine` goes from 80 to **96**. Requests stay within 72 (`SET` with 15 raw bytes is 65 with its CRC).
  The WCB carries lines far longer than this (`structCommand[200]`).
- `opArg_`, which echoes the value in `OK SET`, grows to hold `!` plus 30 hex digits.
- RAM: the larger `Out` and body buffers add about 16 bytes to a few stack frames. The simavr runs confirm the 8 KB
  SRAM still fits.

### 4.4 Firmware docs

`WIPDocs/COMMAND_MAPPING_DESIGN.md`: raw records, v4, `!<hex>`, the new line limit, and a pointer to this spec. The
reference client `tools/shadowmap/shadowmap.py`, the host simulator `shadow_sim.cpp` and their contract tests speak v4.

## 5. DroidNet

### 5.1 Protocol and link

- `scripts/shadow_protocol.py`:
  - `PROTOCOL_VERSION = 4`, `PROTOCOL_VERSIONS = (1, 2, 3, 4)`.
  - `SlotState` and `Event` gain `raw: bool`. Parsing a `!<hex>` token sets it; `body` writes it back as `!<hex>`,
    so `map_sum` and the saved copy (stored as `SLOT` bodies) handle raw commands with no format change.
  - `RAW_RE` / `valid_raw()` for 1-15 printable ASCII characters, next to `KEY_RE`.
- `scripts/shadow_link.py`:
  - `set_slot`, `apply` and `test_fire` take `raw`. A raw value is checked with `valid_raw()`, a key with `KEY_RE`
    as now.
  - Raw is refused on a SHADOW older than v4: `{"error": "no_raw"}`. `apply` skips such slots with `why: "no_raw"`.
  - The status gains `raw_commands: bool` (v4), next to `double_taps` and `long_presses`.
  - `test_fire` with `raw` sends the text as-is, without SHADOW's prefix and suffix.
  - Restoring the saved copy carries raw-ness through.
- `web/handlers/shadow_handlers.py`: `POST /api/shadow/slot`, `/apply` items and `/test` accept `"raw": true`.
  New messages: `badraw` "Raw commands are 1-15 printable ASCII characters." and `no_raw` "Update SHADOW's firmware
  to send raw commands." `docs/API_REFERENCE.md` documents them.
- `scripts/shadow-dev-server.py` and the sim tests (`DROIDNET_SHADOW_DIR`) run against the v4 `shadow_sim.cpp`.
- `scripts/wcb_listener.py` has no line limit that a 93-character `@SM` line reaches; a link test sends the longest
  `SLOT` line through it.

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
2. **Flash SHADOW** with the v4 firmware after a rollback dump of the running `0a8b73f`. The layout is unchanged, so
   the owner's map (flourish double taps, long presses, Overload) survives; confirm with SUM against the saved copy.
3. **Owner tries a raw command** on a spare combo, then clears it or keeps it.

Both repos are committed straight to `main`.

## 7. Testing

**Firmware** (`pio test -e native`, `pio test -e simavr --without-uploading`, the reference client's unittest):

- Store: a raw record round-trips; the flag is invisible to callers; bad raw records (no NUL, a byte outside
  0x20-0x7E, empty) read as the default; a raw record fails the v3 rule `isValidKey` (the downgrade path); the
  existing power-cut and generation tests cover raw records.
- Protocol: `SET !<hex>` in both cases; bad hex, odd length, 16 bytes, a control byte and `!` alone are `ERR key`;
  `SLOT`, `GET`, `OK SET` and `PRESS` carry `!<hex>`; the longest `SLOT` and `PRESS` lines fit `kMaxLine`; `SUM` over a
  map with raw slots; `READY v4`.
- Runtime: a raw press queues exactly the text and `\r`, with no prefix or suffix, including text with `^`, `;T` and
  spaces; a raw press still gets its headroom with the TX queue busy.
- Memory and loop time: the existing simavr and 1 ms limits pass.

**DroidNet** (`pytest tests/`, `npx jest web/js`): `!<hex>` parsing and formatting, `valid_raw`, `map_sum` with raw
slots; the link against the v4 sim, including `no_raw` against v3, raw test fire, and a saved copy holding raw
slots; handler validation; export v4 and import of v1-v4; the model's `validRaw`, `usesOf` and drift. The tab through
the SHADOW dev server: switch, Save, Test, hints, the raw tag, and the disabled switch for a v3 SHADOW.
