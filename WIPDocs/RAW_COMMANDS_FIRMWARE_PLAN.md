# Raw Commands (@SM v4): SHADOW firmware plan, on timing round 2

> Source: DroidNetSignalBooster `docs/superpowers/plans/2026-10-02-shadow-raw-commands.md` at `20e96847`
> (branch `shadow-raw`), Part A (firmware) only, copied here with Task 2b removed. Spec: `WIPDocs/RAW_COMMANDS_DESIGN.md`
> (a copy of DroidNetSignalBooster `docs/superpowers/specs/2026-10-02-shadow-raw-commands-design.md` at `20e96847`).
> Part B (DroidNet) is done by another session; Part C (rollout) waits for the owner.
>
> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox
> (`- [ ]`) syntax for tracking.

## Adaptation to timing round 2 (binding; these override the task text where they differ)

1. **Base.** Work on SHADOW `main` at `5443ae6` (tag `stage/timing-round-2`), not `0a8b73f`: the worktree is
   `/Users/travisccook/Repos/DroidNet-SHADOW-C2B5-worktrees/raw-v4`, branch `raw/v4`. The tasks' line numbers and
   "before" snippets are for `0a8b73f`: treat them as hints and find the current code. Since `0a8b73f`, `main` has:
   - timing Part A: `lib/ShadowMap/src/sm_out.h/.cpp` (the line builder `Out`, with `head()`, `hex()`, `hex32()`, `slot()`,
     `key()`), CRC byte planes and `crc32Block`/`crc16Block`, block EEPROM reads, `sm::Uart1`;
   - timing round 2 (`WIPDocs/TIMING_ROUND_2_DESIGN.md`): `Runtime::afterButtons()` (a pass that fires a press does no
     other mapping work; one request or the background unit a pass), `pump()` handles at most one request with one
     64-byte read budget shared by both ports, a FIFO of up to 8 waiting presses (at most `kMaxFiresPerPass` = 3 fire a
     pass; `fire()` returns false, and the press waits, when its command doesn't fit), `TxQueue::holdForCommand()` (the
     WCB port takes no replies or events while a press waits), `MapStore::kMaxReadsPerStep` = 4, `SHADOW_VERBOSE` off;
   - `tools/shadowmap/test_transcripts.py` compares each output stream with goldens recorded from `0a8b73f`
     (`golden/v3`) and names 7 categories of documented difference.
2. **Reference implementation.** Commit `637f946` (branch `timing/proto-v4`) implemented Tasks 1-4 on top of Part A
   plus a dropped design ("Part B": a SLOT reply read on one pass and sent on a later one, `Protocol::serviceLine`, a
   per-pass byte ledger). Read it with `git show 637f946 -- <path>`. Its store, slot, `Out` and test code fits `main`
   (a trial cherry-pick conflicts only in `sm_protocol.*`, `sm_runtime.*` and three tests). **Do not port Part B
   behaviour** (no SLOT reply in hand, no `serviceLine`, no byte ledger): keep round 2's rule. Where `637f946` and the
   task text differ, the task text and these rulings win; use `637f946` for how a change fits the Part A code.
3. **Task 2b is dropped.** Round 2 replaces it. Its goal stays: every mapping pass under 1 ms with v4's longest lines,
   enforced by `test_avr_timing_*` (which fail at 1 ms). New **Task 3b** measures it.
4. **If any pass reaches 1 ms** with v4: stop and report to the controller with the figure and its `parts:` line. Don't
   split replies across passes and don't raise the limit. (The first lever the controller will consider is a smaller
   `Runtime::kMaxBytesPerPass`, a one-pass deferral of RX bytes the owner's rule already allows.)
5. **PRESS room.** The plan's Review Focus 1 (a PRESS event checked against its own length, not `kMaxLine`) must keep
   round 2's hold: `notifyPress` still sends nothing while `TxQueue::heldForCommand()` is true.
6. **Transcripts.** Task 4 gains Step 6: v4 goldens.
7. **Commits and push.** Commit in the worktree with `git commit` (no PR). Do not push, tag or merge: the controller
   fast-forwards `main`, tags `stage/raw-v4` and pushes after the final review. Task 5 Step 3's rebase/push is the
   controller's. Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` only (the
   source plan's `Claude-Session` line names another session; leave it out).
8. **Markdown.** Edit `.md` files with python3 or Bash, as the source plan says.
9. **RAM.** Static SRAM is 2018 bytes on `main`; report the `pio run -e SHADOWC2B5` RAM line after Tasks 2, 3 and 5. The
   source plan expects about +40 bytes.
10. One `pio test` at a time per checkout; use the session scratchpad
    `/private/tmp/claude-501/-Users-travisccook-Repos-DroidNet-SHADOW-C2B5/6dcf2621-3c23-40df-87f3-321b09e66cc0/scratchpad`
    for logs, not `/tmp`. `include/controller_macs.h` is already in the worktree (gitignored; never commit it).

## Global Constraints

- Raw command: 1 to 15 characters, printable ASCII 0x20-0x7E, space included; never empty (empty means unmapped).
- `^` and `;T` are allowed in a raw command; it is sent exactly as typed: no prefix, no suffix, no respelling for
  `?CMDCHAR`/`?FUNCCHAR`/`?DELIM`.
- Built-in defaults stay stored-command keys: `src/default_mappings.cpp` doesn't change.
- EEPROM: no new layout; a slot record stays 16 bytes of text plus CRC16; the header stays layout 1.
- Raw flag: bit 7 of the record's first byte (`first char | 0x80`), then the rest of the text, then NULs; no caller sees
  the flag bit.
- `slotRecordValid` accepts a stored-command key (`isValidKey`, unchanged) or a raw record (flag set, 1-15 bytes, each
  0x20-0x7E with the flag cleared, NUL-terminated).
- Firing: a raw press queues the text plus `\r`; 15 + 1 bytes fits `kCommandHeadroom` (34).
- Wire: a raw command is `!` + its bytes in upper-case hex (`!3B57323B5334504C41593A33` is `;W2;S4PLAY:3`); SHADOW
  accepts either case; `-` stays unmapped.
- Protocol v4: `READY v4 ...`, nothing else in HELLO changes; `SET <ctl> <mod> <btn> !<hex>`; odd length, a bad digit, 0
  or more than 15 bytes, or a byte outside 0x20-0x7E is `ERR key`.
- `OK SET`, `SLOT` (DUMP and GET) and `PRESS` carry `!<hex>`; `def=` never does.
- `SUM` is unchanged in definition: CRC32 over the `SLOT` bodies, `!<hex>` included.
- `Protocol::kMaxLine` 80 -> 96: longest reply 93 (SLOT, raw on `F CIRCLE RIGHTL`, 15-character default), PRESS with raw
  and `from=` 81, requests stay within 72 (raw SET 65).
- `opArg_` grows to hold `!` plus 30 hex digits.
- DroidNet: `PROTOCOL_VERSION = 4`, `PROTOCOL_VERSIONS = (1, 2, 3, 4)`; status `shadow.raw_commands` (v4) next to
  `double_taps` and `long_presses`.
- New errors: `badraw` "Raw commands are 1-15 printable ASCII characters." and `no_raw` "Update SHADOW's firmware to
  send raw commands."
- Changes from the spec, found while planning (Task 14 Step 2 brings the spec in line):
  - Task 2b: `0a8b73f` already breaks the 1 ms pass budget (spec 7 assumed it held), so a SLOT reply (GET, each DUMP
    line) is read on one pass and sent on a later one, CRC16 runs a byte at a time and the TX queue copies whole parts.
  - A PRESS event is checked against its own length, not `kMaxLine` (Task 2; Review Focus 1).
  - Static SRAM rises by about 40 bytes, not only "about 16 bytes to a few stack frames" (spec 4.3; Task 3 Step 5).
  - A raw test fire is refused with `no_raw` before v4, like set and apply (Task 7).
  - `raw_space` (Task 7): DroidNet's WCB send path refuses a line that starts with whitespace. The WCB would have
    trimmed the space anyway (`WCB.ino:7010` at `66845b9`), so `raw_space` only stops DroidNet's Test, never SHADOW.
  - `@SM` lines reach the link as PortHandler `data` events, not through `scripts/wcb_listener.py` (spec 5.1; Task 7,
    `test_the_longest_slot_line_reaches_the_link`).
  - Find by pressing shows no command of its own (spec 3.2; Task 11): it names the combo and opens the inspector.
  - The hint reads the first character after any spaces, and text of spaces only gets its own hint (Hints, below).
  - The settings hint's "WCB n" is the prefix's WCB number (Hints, below).
- Export `droidnet-shadow-map` version 4; an entry may carry `"raw": true`; import reads 1 to 4; skipped note "Skipped,
  this SHADOW can't send raw commands (update its firmware): ...".
- UI copy: switch "Stored command · Raw command"; counter "9 / 15"; error "Raw commands are 1-15 printable ASCII
  characters."; disabled note "Update SHADOW's firmware to send raw commands."
- Hints: "Runs as a WCB command." / "Runs as a settings command on SHADOW's WCB (WCB n)." / "Not a WCB command: the WCB
  broadcasts it to every serial port on every board." / "Only spaces: the WCB trims them and ignores the empty line."
  The hint reads the first character after any spaces: the WCB trims each line (`WCB.ino:7010` at `66845b9`) and each
  `^` part (`:2370`) before it looks at it, and an empty line does nothing.
- n in the settings hint is the prefix's WCB number, as the tab's "SHADOW via WCB n" already uses (`state.wcbName`, from
  `prefixWcb(link.prefix)`): the status's route has no WCB number. On a droid where the prefix targets another board,
  n names the stored-command board, not SHADOW's, while the hint's characters come from the route's board
  (`_board_chars`). Accepted by the owner on 2026-10-02.
- Hint characters come from the status's existing `chars` (`{cmd, delim, func}`, from `wcb_special_chars`), else `;` and
  `?`. No new endpoint.
- The unassigned-only filter (todo item 4) is NOT part of this plan; only `usesOf` becomes raw-aware.
- Out of scope (owner, 2026-10-02): the HCR renumbering check (`scripts/hcr_numbering_check.py`) doesn't see raw
  commands. It scans `shadow-map.json`, where a raw command is saved as `!<hex>`, and `_may_trigger` keeps only text
  with `<` or `H,PLAY,`, so a raw `<CA0013>` or `;H,PLAY,A,13` is never flagged.
- Rollout: DroidNet with v4 first (it still speaks v3), then flash SHADOW after a rollback dump of `0a8b73f`, then the
  owner tries one raw command.
- No device writes (deploy, flash, mapping, stored commands) without the owner's explicit go-ahead, each time.
- Never open SHADOW's Mega USB port except in the owner-approved flash of Task 16; never send DEFAULTS to the droid's
  SHADOW (it holds the owner's map). DEFAULTS against the simulator is fine.
- Commits: in the worktrees; straight to `main` with `git push origin HEAD:main`; no PRs; `git commit -F - -- <paths>`
  with an explicit pathspec (`git add` new files first).
- Every commit message ends with the two trailer lines `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_01TpbbHNL9m8Gw9PAjSjiW3C`.
- Time limits: give these Bash calls `timeout: 600000` (the default is 120 s): every DroidNet `git commit` (the
  pre-commit `pytest-fast` hook runs the whole fast unit suite, 3-4 minutes), every full `pytest tests/unit`, every
  `pio test -e simavr` without `-f`, the first `pio run` or `pio test` in a fresh worktree, and every e2e file. Never
  commit with `--no-verify`. If a commit times out, check `git status` and `~/.cache/pre-commit/patch*` (pre-commit's
  stash of unstaged edits) before retrying.
- Markdown (both repos): a global PostToolUse hook runs prettier on `.md` files touched by Edit/Write, which breaks
  these repos' style. Edit `.md` files only through Bash/python3, then lint DroidNet docs with `markdownlint-cli2
  --no-globs <literal paths>`.
- No mapping pass may reach 1 ms on the emulated ATmega2560 (the existing `test_avr` limit), GET and DUMP of the longest
  SLOT line included (Task 2b; `0a8b73f` already takes 1.07 ms for a 15-character key over a 15-character default); the
  main loop must never block.

## Review Focus

1. **A press whose command is longer than 21 bytes loses its PRESS event once `kMaxLine` is 96.** `notifyPress` checks
   `canSend(kMaxLine)` after the command is already queued; 8 + 96 + 1 + 34 = 139 of the 160-byte queue leaves room only
   for commands of 21 bytes or less, so any suffix or a `;W12,;C` prefix silently kills Last press and Find by pressing.
   Test: Task 2, `test_a_long_command_keeps_its_press_event`.
2. **A raw slot read while its write is in flight, or while it waits behind the first format:** the flag bit must not
   reach a caller (it would turn `?` into `0xBF` and the hex into `BF...`). Tests: Task 1,
   `test_raw_reads_without_the_flag_mid_write_and_before_format`; Task 2,
   `test_get_during_a_raw_write_answers_the_text`.
3. **Raw text that looks like DroidNet's sentinels** (`*` means revert, `""` unmapped, `-` unmapped on the wire) or that
   has spaces at either end, through set, undo, restore of the saved copy and import: it must go out as `!2A`,
   `!202D20`, never as REVERT, never trimmed. Tests: Task 7, `test_raw_sentinels_are_sent_as_text` (set, apply and
   restore, `" - "` included); Task 10, `Undo after replacing a raw '*' puts the raw '*' back` and `Undo after replacing
   a raw command with spaces at either end puts it back untrimmed`; Task 12, `a raw "*" in a file is the text "*"`;
   Task 13, `test_restoring_a_saved_raw_star_sets_it_back_as_raw`.
4. **Raw text spelled exactly like a stored-command key** (`scream` raw vs `scream` stored): different in `usesOf`, the
   drift check, the picker's current mark, "Not on WCB 1" and import's no-change test. Tests: Task 7,
   `test_raw_and_stored_with_one_spelling_differ`; Task 9, `a raw command spelled like a stored key is a different
   thing`; Task 10, `a raw command spelled like a stored key isn't that key in the picker`.
5. **HTML-active raw text** (`<b>"x"</b>&`, typed or from an imported file) in cards, tooltips (`title`), the text field
   (`value`), Last press, the grid, the cheat sheet and the diff dialog. Tests: Task 11, `raw text is escaped everywhere
   it shows`; Task 12, `raw text from an imported file is escaped in the import dialog`.

---


## Part A: Firmware (`/Users/travisccook/Repos/SHADOW-C2B5-raw`)

`pio` is `/Users/travisccook/.platformio/penv/bin/pio` if it isn't on `PATH`. Always pass `--without-uploading` to
simavr runs. If a simavr suite hangs it has run out of the Mega's 8 KB of SRAM (every Unity string literal lives in
RAM on AVR): split it, as `test_protocol` and `test_protocol_more` are split.

### Setup A: Firmware worktree and baseline

- [ ] **Step 1: Create the worktree**

```bash
git -C /Users/travisccook/Repos/SHADOW-C2B5 fetch origin
git -C /Users/travisccook/Repos/SHADOW-C2B5 worktree add -b raw-commands /Users/travisccook/Repos/SHADOW-C2B5-raw origin/main
cp /Users/travisccook/Repos/SHADOW-C2B5/include/controller_macs.h /Users/travisccook/Repos/SHADOW-C2B5-raw/include/controller_macs.h
git -C /Users/travisccook/Repos/SHADOW-C2B5-raw log --oneline -1
```

`include/controller_macs.h` is gitignored (the controllers' MAC addresses) and `pio run` refuses to build without it;
never commit it. Expected log line: `0a8b73f Long presses: drop a stale single...`. If `origin/main` has moved past
`0a8b73f`, read the new commits before going on: this plan's line numbers are for `0a8b73f`.

- [ ] **Step 2: Record the baseline**

Run: `cd /Users/travisccook/Repos/SHADOW-C2B5-raw && pio run && pio test -e native && pio test -e simavr
--without-uploading && python3 -m unittest tools/shadowmap/test_shadowmap.py`
Expected: all pass. Write down the `RAM:` and `Flash:` lines `pio run` prints (for example
`RAM: [====      ]  36.4% (used 2979 bytes from 8192 bytes)`) and the native and simavr test counts; Tasks 3 and 5
compare against them.

### Task 1: Store: raw-command records

**Files:**

- Modify: `lib/ShadowMap/src/sm_store.h`
- Modify: `lib/ShadowMap/src/sm_store.cpp`
- Create: `test/test_store_raw/test_store_raw.cpp`

**Interfaces:**

- Produces: `constexpr uint8_t sm::kRawFlag = 0x80;` `bool sm::isValidRaw(const char *text);`
  `bool MapStore::keyForSlot(uint8_t slot, char out[kKeyBuf], Source *src = nullptr, bool *raw = nullptr);`
  (`out` never holds the flag; `*raw` is set whenever `true` is returned, and nothing is touched on `false`)
  `Result MapStore::beginSet(uint8_t slot, const char *key, bool raw = false);` (`BadKey` when `raw` and
  `!isValidRaw(key)`, or `!raw` and `!isValidKey(key)`).
- Existing callers compile unchanged (default arguments).

- [ ] **Step 1: Write the failing test suite**

Create `test/test_store_raw/test_store_raw.cpp`:

```cpp
// MapStore raw commands: the record (bit 7 of the first byte), its rules, and every way a slot
// is read back. A raw command is text SHADOW sends to its WCB exactly as typed.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#define SM_TEST_OWN_SETUP
#include <sm_test_main.h>
#include <stdio.h>

using namespace sm;

SM_DEFINE_TEST_DEFAULTS

namespace {

FakeEeprom *eeprom = nullptr;
// Power-cut snapshot: the headers and single-press slots, which is all these tests write there.
uint8_t baseMem[FakeEeprom::kFlat];

const char kRaw[] = ";W2;S4PLAY:3^;T";  // 15 characters, with the WCB's own '^' and ";T"

uint8_t result(MapStore::Result r) { return static_cast<uint8_t>(r); }

void snapshot() {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, eeprom->doubleRecordsUsed(), "snapshot() only covers the single-press slots");
    memcpy(baseMem, eeprom->mem, sizeof(baseMem));
}
void restore() {
    eeprom->reset();
    memcpy(eeprom->mem, baseMem, sizeof(baseMem));
}

void runUntilIdle(MapStore &store) {
    for (uint16_t passes = 0; store.busy() && passes < 5000; ++passes) {
        store.step();
        eeprom->tick();
    }
    TEST_ASSERT_FALSE_MESSAGE(store.busy(), "store never finished");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, eeprom->violations, "EEPROM touched while busy");
}

void expectValue(MapStore &store, uint8_t slot, const char *want, bool wantRaw, Source wantSrc) {
    char key[kKeyBuf];
    Source src = Source::Reserved;
    bool raw = !wantRaw;
    TEST_ASSERT_TRUE(store.keyForSlot(slot, key, &src, &raw));
    TEST_ASSERT_EQUAL_STRING(want, key);
    TEST_ASSERT_TRUE_MESSAGE(raw == wantRaw, want);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(wantSrc), static_cast<uint8_t>(src));
}

void set(MapStore &store, uint8_t slot, const char *text, bool raw) {
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok), result(store.beginSet(slot, text, raw)));
    runUntilIdle(store);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                            static_cast<uint8_t>(store.takeCompletion()));
}

uint16_t recordAddr(uint8_t slot) {
    return static_cast<uint16_t>(2 * MapStore::kHeaderSize + slot * MapStore::kSlotSize);
}

// Writes a single-press slot record by hand, with the CRC the store computes (slotCrc in
// sm_store.cpp: layout version, generation, slot index, then the 16 name bytes).
void writeRecord(uint8_t slot, uint16_t gen, const uint8_t name[kKeyBuf]) {
    uint16_t crc = kCrc16Init;
    crc = crc16Update(crc, MapStore::kLayoutVersion);
    crc = crc16Update(crc, static_cast<uint8_t>(gen & 0xFF));
    crc = crc16Update(crc, static_cast<uint8_t>(gen >> 8));
    crc = crc16Update(crc, slot);
    for (uint8_t i = 0; i < kKeyBuf; ++i)
        crc = crc16Update(crc, name[i]);
    uint8_t *rec = eeprom->mem + recordAddr(slot);
    memcpy(rec, name, kKeyBuf);
    rec[kKeyBuf] = static_cast<uint8_t>(crc & 0xFF);
    rec[kKeyBuf + 1] = static_cast<uint8_t>(crc >> 8);
}

}  // namespace

void tearDown() {}

void setUp() {
    static FakeEeprom instance;
    instance.reset();
    eeprom = &instance;
}

static void test_raw_text_rules() {
    TEST_ASSERT_TRUE(isValidRaw(kRaw));
    TEST_ASSERT_TRUE(isValidRaw(" "));
    TEST_ASSERT_TRUE(isValidRaw("*"));
    TEST_ASSERT_TRUE(isValidRaw("~~~~~~~~~~~~~~~"));    // 15
    TEST_ASSERT_FALSE(isValidRaw("~~~~~~~~~~~~~~~~"));  // 16
    TEST_ASSERT_FALSE(isValidRaw(""));
    TEST_ASSERT_FALSE(isValidRaw("a\nb"));
    TEST_ASSERT_FALSE(isValidRaw("\x1F"));
    TEST_ASSERT_FALSE(isValidRaw("\x7F"));
    TEST_ASSERT_FALSE(isValidRaw("caf\xC3\xA9"));
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::BadKey), result(store.beginSet(kSlotFootNoneDown, "", true)));
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::BadKey),
                            result(store.beginSet(kSlotFootNoneDown, "~~~~~~~~~~~~~~~~", true)));
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::BadKey),
                            result(store.beginSet(kSlotFootNoneDown, "a b", false)));  // keys keep their rule
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Reserved),
                            result(store.beginSet(slotIndex(Ctrl::F, Mod::L1, Btn::L3), "x", true)));
    TEST_ASSERT_FALSE(store.busy());
    TEST_ASSERT_EQUAL_UINT32(0, eeprom->writes);
}

static void test_raw_record_round_trips_with_the_flag_hidden() {
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, kSlotFootNoneDown, kRaw, true);
        expectValue(store, kSlotFootNoneDown, kRaw, true, Source::Eeprom);
        // In EEPROM: the first character with bit 7 set, the rest as typed, then NULs.
        const uint16_t at = recordAddr(kSlotFootNoneDown);
        TEST_ASSERT_EQUAL_HEX8(';' | kRawFlag, eeprom->peek(at));
        TEST_ASSERT_EQUAL_HEX8('W', eeprom->peek(static_cast<uint16_t>(at + 1)));
        TEST_ASSERT_EQUAL_HEX8('T', eeprom->peek(static_cast<uint16_t>(at + 14)));
        TEST_ASSERT_EQUAL_HEX8(0, eeprom->peek(static_cast<uint16_t>(at + 15)));
        set(store, kSlotDomeL2Right, " a ", true);  // spaces are kept, at both ends
        expectValue(store, kSlotDomeL2Right, " a ", true, Source::Eeprom);
    }
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    expectValue(rebooted, kSlotFootNoneDown, kRaw, true, Source::Eeprom);
    expectValue(rebooted, kSlotDomeL2Right, " a ", true, Source::Eeprom);
    expectValue(rebooted, kSlotFootL1Up, "extremehappysou", false, Source::Default);
    TEST_ASSERT_EQUAL_UINT8(2, rebooted.validEepromSlots());
    // Back to a key: the flag goes with the old value.
    set(rebooted, kSlotFootNoneDown, "scream", false);
    expectValue(rebooted, kSlotFootNoneDown, "scream", false, Source::Eeprom);
    TEST_ASSERT_EQUAL_HEX8('s', eeprom->peek(recordAddr(kSlotFootNoneDown)));
    // A caller that doesn't ask whether it's raw still gets the plain text.
    set(rebooted, kSlotFootNoneDown, "*", true);
    char key[kKeyBuf];
    TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key));
    TEST_ASSERT_EQUAL_STRING("*", key);
}

// Review Focus 2: the queued first write and the slot being written are answered from RAM, and
// that copy holds the flagged first byte. Neither may leak it.
static void test_raw_reads_without_the_flag_mid_write_and_before_format() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    // Unformatted: the first write waits behind the header; a lookup answers from the queue.
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok), result(store.beginSet(kSlotFootNoneDown, kRaw, true)));
    expectValue(store, kSlotFootNoneDown, kRaw, true, Source::Eeprom);
    runUntilIdle(store);
    store.takeCompletion();
    expectValue(store, kSlotFootNoneDown, kRaw, true, Source::Eeprom);
    // Mid-write: a lookup of the slot being written answers from the job.
    eeprom->writeTicks = 10;
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok),
                            result(store.beginSet(kSlotDomeCrossLeft, "?SEQ,LIST", true)));
    store.step();  // the first byte, the flagged one, is going in
    TEST_ASSERT_FALSE(eeprom->ready());
    expectValue(store, kSlotDomeCrossLeft, "?SEQ,LIST", true, Source::Eeprom);
    runUntilIdle(store);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                            static_cast<uint8_t>(store.takeCompletion()));
    expectValue(store, kSlotDomeCrossLeft, "?SEQ,LIST", true, Source::Eeprom);
    // A key going in over a raw command reads as that key, not raw.
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok), result(store.beginSet(kSlotFootNoneDown, "plain", false)));
    store.step();
    expectValue(store, kSlotFootNoneDown, "plain", false, Source::Eeprom);
    runUntilIdle(store);
    store.takeCompletion();
}

static void test_bad_raw_records_read_as_the_default() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotFootL1Up, "x", false);  // formats the EEPROM
    const uint16_t gen = store.generation();
    uint8_t name[kKeyBuf];
    // First a good hand-built record reads as raw: the helper's CRC is right, so the bad cases
    // below fail for their bytes, not their CRC.
    memset(name, 0, sizeof(name));
    memcpy(name, "hi there", 8);
    name[0] = static_cast<uint8_t>(name[0] | kRawFlag);
    writeRecord(kSlotFootNoneDown, gen, name);
    {
        MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
        rebooted.begin();
        expectValue(rebooted, kSlotFootNoneDown, "hi there", true, Source::Eeprom);
    }
    for (uint8_t which = 0; which < 6; ++which) {
        memset(name, 0, sizeof(name));
        memcpy(name, "hi there", 8);
        switch (which) {
            case 0: memset(name, 'a', sizeof(name)); break;  // no NUL
            case 1: name[2] = 0x01; break;                    // a control byte
            case 2: name[2] = 0x7F; break;                    // DEL
            case 3: name[2] = 0xC3; break;                    // not ASCII
            case 4: memset(name, 0, sizeof(name)); break;     // empty: the flag alone
            default: name[0] = 0x1F; break;                   // the first byte isn't printable
        }
        name[0] = static_cast<uint8_t>(name[0] | kRawFlag);
        writeRecord(kSlotFootNoneDown, gen, name);
        MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
        rebooted.begin();
        char msg[12];
        snprintf(msg, sizeof(msg), "case %u", static_cast<unsigned>(which));
        char key[kKeyBuf];
        Source src = Source::Eeprom;
        TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key, &src));
        TEST_ASSERT_EQUAL_STRING_MESSAGE("angrysound", key, msg);
        TEST_ASSERT_TRUE_MESSAGE(src == Source::Default, msg);
    }
}

// The downgrade path: v3 and older check a record with isValidKey() alone. A raw record fails it,
// so they read the slot's built-in default, exactly as for a record that failed its CRC.
static void test_a_raw_record_fails_the_v3_rule() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotFootNoneDown, "abc", true);
    char name[kKeyBuf];
    for (uint8_t i = 0; i < kKeyBuf; ++i)
        name[i] = static_cast<char>(eeprom->peek(static_cast<uint16_t>(recordAddr(kSlotFootNoneDown) + i)));
    TEST_ASSERT_TRUE((static_cast<uint8_t>(name[0]) & kRawFlag) != 0);
    TEST_ASSERT_FALSE(isValidKey(name));
}

// A power cut at every byte of a raw write leaves the old key, the new raw command or the
// default: never the text without its flag, never a key with one.
static void test_power_cut_during_a_raw_write() {
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, kSlotFootNoneDown, "happysound", false);
        set(store, kSlotDomeCrossLeft, "neighbour", false);
    }
    snapshot();
    uint32_t needed = 0;
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        const uint32_t before = eeprom->writes;
        set(store, kSlotFootNoneDown, kRaw, true);
        needed = eeprom->writes - before;
    }
    TEST_ASSERT_TRUE(needed > 5);
    for (uint32_t cut = 0; cut <= needed; ++cut) {
        for (uint8_t garbage = 0; garbage < 2; ++garbage) {
            restore();
            eeprom->budget = static_cast<int32_t>(cut);
            eeprom->cutGarbage = garbage != 0;
            {
                MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
                store.begin();
                store.beginSet(kSlotFootNoneDown, kRaw, true);
                runUntilIdle(store);  // after the cut nothing lands and verification fails
                store.takeCompletion();
            }
            eeprom->powerOn();
            MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
            rebooted.begin();
            char key[kKeyBuf];
            Source src = Source::Reserved;
            bool raw = false;
            TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key, &src, &raw));
            const bool oldValue = strcmp(key, "happysound") == 0 && !raw && src == Source::Eeprom;
            const bool newValue = strcmp(key, kRaw) == 0 && raw && src == Source::Eeprom;
            const bool fallback = strcmp(key, "angrysound") == 0 && !raw && src == Source::Default;
            TEST_ASSERT_TRUE_MESSAGE(oldValue || newValue || fallback, key);
            if (!garbage && cut >= needed)
                TEST_ASSERT_TRUE(newValue);
            expectValue(rebooted, kSlotDomeCrossLeft, "neighbour", false, Source::Eeprom);
        }
    }
}

static void test_raw_double_taps_and_long_presses_count_as_mapped() {
    const uint8_t dbl = slotIndex(Ctrl::F, Mod::L1, Btn::UP, Tap::DOUBLE);
    const uint8_t lng = slotIndex(Ctrl::F, Mod::L1, Btn::UP, Tap::LONG);
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, dbl, "?", true);
        set(store, lng, " ", true);
        TEST_ASSERT_TRUE(store.doubleTapMapped(kSlotFootL1Up));
        TEST_ASSERT_TRUE(store.longPressMapped(kSlotFootL1Up));
        expectValue(store, lng, " ", true, Source::Eeprom);
    }
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);  // the RAM copy is rebuilt from the records
    rebooted.begin();
    TEST_ASSERT_TRUE(rebooted.doubleTapMapped(kSlotFootL1Up));
    TEST_ASSERT_TRUE(rebooted.longPressMapped(kSlotFootL1Up));
    expectValue(rebooted, dbl, "?", true, Source::Eeprom);
}

static void test_revert_and_defaults_clear_raw_slots() {
    const uint8_t lng = slotIndex(Ctrl::D, Mod::CROSS, Btn::LEFT, Tap::LONG);
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotDomeCrossLeft, kRaw, true);
    set(store, lng, kRaw, true);
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok), result(store.beginRevert(kSlotDomeCrossLeft)));
    runUntilIdle(store);
    store.takeCompletion();
    expectValue(store, kSlotDomeCrossLeft, "scanning", false, Source::Default);
    expectValue(store, lng, kRaw, true, Source::Eeprom);
    set(store, kSlotFootNoneDown, kRaw, true);  // a single-press raw record for DEFAULTS to retire
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok), result(store.beginDefaults()));
    runUntilIdle(store);
    store.takeCompletion();
    expectValue(store, lng, "", false, Source::Default);
    expectValue(store, kSlotFootNoneDown, "angrysound", false, Source::Default);
    TEST_ASSERT_FALSE(store.longPressMapped(kSlotDomeCrossLeft));
    // After a reboot the old generation's raw records stay dead (a raw branch in slotRecordValid
    // that skipped the CRC would bring them back): each slot reads its default, and loadTap,
    // rebuilt under the new generation, finds no long press.
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    expectValue(rebooted, lng, "", false, Source::Default);
    expectValue(rebooted, kSlotFootNoneDown, "angrysound", false, Source::Default);
    expectValue(rebooted, kSlotDomeCrossLeft, "scanning", false, Source::Default);
    TEST_ASSERT_FALSE(rebooted.longPressMapped(kSlotDomeCrossLeft));
    set(rebooted, lng, " a ", true);  // the new generation takes raw records
    expectValue(rebooted, lng, " a ", true, Source::Eeprom);
    TEST_ASSERT_TRUE(rebooted.longPressMapped(kSlotDomeCrossLeft));
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_raw_text_rules);
    RUN_TEST(test_raw_record_round_trips_with_the_flag_hidden);
    RUN_TEST(test_raw_reads_without_the_flag_mid_write_and_before_format);
    RUN_TEST(test_bad_raw_records_read_as_the_default);
    RUN_TEST(test_a_raw_record_fails_the_v3_rule);
    RUN_TEST(test_power_cut_during_a_raw_write);
    RUN_TEST(test_raw_double_taps_and_long_presses_count_as_mapped);
    RUN_TEST(test_revert_and_defaults_clear_raw_slots);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/travisccook/Repos/SHADOW-C2B5-raw && pio test -e native -f test_store_raw`
Expected: compile errors, in clang's wording (native builds use `/usr/bin/g++`, which is Apple clang):
`use of undeclared identifier 'isValidRaw'`, the same for `kRawFlag`, and no matching `beginSet`/`keyForSlot` for the
extra argument.

- [ ] **Step 3: Implement**

`lib/ShadowMap/src/sm_store.h`. In the layout comment, replace the two slot lines with:

```cpp
//   216 slots x 18 B                   stored-command key (NUL-padded), or a raw command with
//                                      bit 7 of its first byte set, + CRC16 over (layout
//                                      version, generation, slot index, the 16 name bytes)
```

After `constexpr uint8_t kSuffixMax = 7;` add:

```cpp
// A raw command's record is its text with this bit set on the first byte. Printable text never
// sets bit 7, so the flag costs no characters, and firmware from before raw commands (@SM v3 and
// older) fails isValidKey() on the record and reads the slot's built-in default instead.
constexpr uint8_t kRawFlag = 0x80;
```

After the `isValidKey` declaration add:

```cpp
// 1-15 printable ASCII characters (0x20-0x7E), space included: a raw command, which SHADOW sends
// to the WCB exactly as typed, with no prefix or suffix. '^' and ";T" are allowed: they do on
// the WCB what they always do.
bool isValidRaw(const char *text);
```

Replace the `keyForSlot` comment and declaration:

```cpp
    // The key a slot currently maps to ("" = nothing), or with `*raw` true the raw command's
    // text; the raw flag itself never reaches `out`. Returns false, without touching `out`,
    // `*src` or `*raw`, only while an EEPROM write is in flight and the slot has to be read from
    // EEPROM; call again on a later loop pass. The writer pauses for one pass to let the read through.
    bool keyForSlot(uint8_t slot, char out[kKeyBuf], Source *src = nullptr, bool *raw = nullptr);
```

Replace the `beginSet` declaration:

```cpp
    Result beginSet(uint8_t slot, const char *key, bool raw = false);  // "" clears the slot; raw: a raw command
```

Replace `void startSlotJob(uint8_t slot, const char *key);` with
`void startSlotJob(uint8_t slot, const char *key, bool raw);`, and after `char pendingKey_[kKeyBuf];` add
`bool pendingRaw_ = false;  // the queued write is a raw command`.

`lib/ShadowMap/src/sm_store.cpp`. In the anonymous namespace, replace `slotRecordValid` with:

```cpp
// A raw record: the flag on the first byte, then 1-15 printable characters (sm_store.h).
bool rawRecordValid(const uint8_t *rec) {
    if ((rec[0] & kRawFlag) == 0)
        return false;
    char text[kKeyBuf];
    memcpy(text, rec, kKeyBuf);
    text[0] = static_cast<char>(rec[0] & 0x7F);
    text[kKeyMax] = '\0';
    return isValidRaw(text);
}

bool slotRecordValid(const uint8_t *rec, uint8_t slot, uint16_t gen) {
    if (!fieldTerminated(rec, kKeyBuf))
        return false;
    if (!isValidKey(reinterpret_cast<const char *>(rec)) && !rawRecordValid(rec))
        return false;
    return get16(rec + kKeyBuf) == slotCrc(slot, gen, rec);
}

// A record's (or a job's) name field as text, with the raw flag cleared and reported.
void readName(const uint8_t *name, char out[kKeyBuf], bool *raw) {
    memcpy(out, name, kKeyBuf);
    out[kKeyMax] = '\0';
    *raw = (name[0] & kRawFlag) != 0;
    out[0] = static_cast<char>(name[0] & 0x7F);
}
```

After `isValidKey` add:

```cpp
bool isValidRaw(const char *text) {
    size_t n = 0;
    for (; text[n] != '\0'; ++n) {
        const uint8_t c = static_cast<uint8_t>(text[n]);
        if (n >= kKeyMax || c < 0x20 || c > 0x7E)
            return false;
    }
    return n > 0;
}
```

Replace `MapStore::keyForSlot`:

```cpp
bool MapStore::keyForSlot(uint8_t slot, char out[kKeyBuf], Source *src, bool *raw) {
    Source source = Source::Default;
    bool isRaw = false;
    if (slot >= kSlotCount || slotReserved(slot) != Reserved::No) {
        out[0] = '\0';
        source = Source::Reserved;
    } else if (phase_ != Phase::Idle && kind_ == JobKind::Slot && jobIndex_ == slot) {
        // Mid-write: answer with the value being written, never a half-written one.
        readName(jobData_, out, &isRaw);
        source = Source::Eeprom;
    } else if (phase_ != Phase::Idle && kind_ == JobKind::SlotErase && jobIndex_ == slot) {
        defaultKey(slot, out);
    } else if (pendingSlot_ == slot) {
        memcpy(out, pendingKey_, kKeyBuf);
        isRaw = pendingRaw_;
        source = Source::Eeprom;
    } else if (!formatted()) {
        defaultKey(slot, out);
    } else {
        uint8_t rec[kSlotSize];
        if (!readSlotRecord(slot, rec))
            return false;
        if (slotRecordValid(rec, slot, gen_)) {
            readName(rec, out, &isRaw);
            source = Source::Eeprom;
        } else {
            defaultKey(slot, out);
        }
    }
    if (src != nullptr)
        *src = source;
    if (raw != nullptr)
        *raw = isRaw;
    return true;
}
```

Replace `MapStore::beginSet`:

```cpp
MapStore::Result MapStore::beginSet(uint8_t slot, const char *key, bool raw) {
    if (busy())
        return Result::Busy;
    if (slot >= kSlotCount)
        return Result::BadSlot;
    if (slotReserved(slot) != Reserved::No)
        return Result::Reserved;
    if (raw ? !isValidRaw(key) : !isValidKey(key))
        return Result::BadKey;
    completion_ = Completion::None;
    if (!formatted()) {
        pendingSlot_ = slot;
        strcpy(pendingKey_, key);
        pendingRaw_ = raw;
        startHeaderJob(0, 1, gen_, prefix_, suffix_);
    } else {
        startSlotJob(slot, key, raw);
    }
    return Result::Ok;
}
```

Replace `MapStore::startSlotJob`'s signature and its first two lines:

```cpp
void MapStore::startSlotJob(uint8_t slot, const char *key, bool raw) {
    memset(jobData_, 0, sizeof(jobData_));
    copyField(jobData_, key, kKeyBuf);
    if (raw)
        jobData_[0] = static_cast<uint8_t>(jobData_[0] | kRawFlag);  // the CRC below covers the flag
    put16(jobData_ + kKeyBuf, slotCrc(slot, gen_, jobData_));
```

In `finishJob`, `startSlotJob(slot, pendingKey_);` becomes `startSlotJob(slot, pendingKey_, pendingRaw_);`.
`loadTap` needs no change: a raw record's first byte is non-zero, so it counts as mapped.

- [ ] **Step 4: Run the store suites, native then simavr**

Run: `cd /Users/travisccook/Repos/SHADOW-C2B5-raw && pio test -e native -f test_store_raw -f test_store && pio test -e
simavr -f test_store_raw -f test_store --without-uploading`
Expected: PASS. If the simavr run of `test_store_raw` hangs, move `test_power_cut_during_a_raw_write` (with
`baseMem`, `snapshot` and `restore`) into `test/test_store_raw_cut/test_store_raw_cut.cpp` with the same includes,
helpers and `setUp`/`tearDown`. If you split, `git add` the new file and add `test/test_store_raw_cut` to Step 6's
commit pathspec.

- [ ] **Step 5: Run everything else, native**

Run: `cd /Users/travisccook/Repos/SHADOW-C2B5-raw && pio test -e native`
Expected: PASS (every caller still uses the old arguments).

- [ ] **Step 6: Commit**

```bash
cd /Users/travisccook/Repos/SHADOW-C2B5-raw && git add test/test_store_raw/test_store_raw.cpp && git commit -F - -- lib/ShadowMap/src/sm_store.h lib/ShadowMap/src/sm_store.cpp test/test_store_raw <<'EOF'
Store: raw-command records (bit 7 of the first byte)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TpbbHNL9m8Gw9PAjSjiW3C
EOF
```

### Task 2: Protocol v4: raw commands as `!<hex>`, 96-character lines

**Files:**

- Modify: `lib/ShadowMap/src/sm_protocol.h` (lines 1-7, 60, 74, 92, 103)
- Modify: `lib/ShadowMap/src/sm_protocol.cpp` (lines 13, 21-102 `Out`, 429-438 SET, 497-524 SUM, 593-635)
- Modify: `test/test_protocol/test_protocol.cpp`, `test/test_protocol_more/test_protocol_more.cpp`,
  `test/test_runtime/test_runtime.cpp` (`READY v3` -> `READY v4`)
- Create: `test/test_protocol_raw/test_protocol_raw.cpp`

**Interfaces:**

- Consumes: `MapStore::keyForSlot(..., bool *raw)`, `MapStore::beginSet(..., bool raw)`, `kRawFlag`, `isValidRaw`
  (Task 1).
- Produces: `@SM READY v4 ...`; `Protocol::kMaxLine == 96`;
  `void Protocol::notifyPress(uint8_t slot, const char *key, Source src, bool modFromOther, bool raw = false);`
  `SET <slot> !<hex>` (either case) answering `OK SET <slot> !<HEX>` (always upper case); `SLOT` and `PRESS` lines with
  `!<HEX>`; `ERR key` for `!`, `!-`, odd length, a bad digit, NUL, 16+ bytes or a byte outside 0x20-0x7E.
- Behaviour change: a PRESS event is sent when the event port has room for that line, not for `kMaxLine`.

- [ ] **Step 1: Update the existing expectations**

Run:

```bash
cd /Users/travisccook/Repos/SHADOW-C2B5-raw && sed -i '' 's/@SM READY v3 /@SM READY v4 /' test/test_protocol/test_protocol.cpp test/test_protocol_more/test_protocol_more.cpp test/test_runtime/test_runtime.cpp && grep -rn "READY v" test | cut -c1-110
```

Expected: seven lines, all `READY v4` (test_protocol.cpp 9, 41, 72; test_protocol_more.cpp 138, 149, 151;
test_runtime.cpp 251).

- [ ] **Step 2: Write the failing raw-command suite**

Create `test/test_protocol_raw/test_protocol_raw.cpp`:

```cpp
// The @SM protocol, part 3: raw commands (v4). A raw command travels as "!" and its bytes in
// upper-case hex, so the WCB never sees its own ';', '^' or ";T" in an @SM line (the simulated
// WCB counts any of them as a violation, and tearDown fails on one). The rig is in
// test/support/sm_protocol_rig.h.
#include <unity.h>

#include <sm_protocol_rig.h>

namespace {

// 15 characters with the WCB's own '^' and ";T", and its hex.
const char kRaw[] = ";W2;S4PLAY:3^;T";
const char kRawHex[] = "3B57323B5334504C41593A335E3B54";

// F CIRCLE RIGHTL with a 15-character default (no real firmware has one there), for the
// longest SLOT line.
const DefaultMapping kWideDefaults[] PROGMEM = {
    {slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT, Tap::LONG), "abcdefghijklmno"},
};

}  // namespace

static void test_set_raw_round_trips_as_upper_case_hex() {
    char req[64], want[96];
    snprintf(req, sizeof(req), "@SM SET F NONE DOWN !%s", kRawHex);
    snprintf(want, sizeof(want), "@SM OK SET F NONE DOWN !%s", kRawHex);
    TEST_ASSERT_EQUAL_STRING(want, ask(req));
    snprintf(want, sizeof(want), "@SM SLOT F NONE DOWN !%s eeprom def=angrysound", kRawHex);
    TEST_ASSERT_EQUAL_STRING(want, ask("@SM GET F NONE DOWN"));
    char key[kKeyBuf];
    bool raw = false;
    TEST_ASSERT_TRUE(rig->store.keyForSlot(kSlotFootNoneDown, key, nullptr, &raw));
    TEST_ASSERT_EQUAL_STRING(kRaw, key);
    TEST_ASSERT_TRUE(raw);
    // Lower case is accepted; the echo is always upper case.
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN !3B5732", ask("@SM SET F NONE DOWN !3b5732"));
    // "*" and " - " are text here: not DroidNet's "back to default", not the wire's "unmapped".
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN !2A", ask("@SM SET F NONE DOWN !2A"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE DOWN !2A eeprom def=angrysound", ask("@SM GET F NONE DOWN"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN !202D20", ask("@SM SET F NONE DOWN !202D20"));
    // Back to a key: not raw.
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN scream", ask("@SM SET F NONE DOWN scream"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE DOWN scream eeprom def=angrysound", ask("@SM GET F NONE DOWN"));
}

static void test_set_raw_refuses_bad_hex() {
    // "!" alone, "!-" (hex decoding reads "-" as nothing), odd length, a bad digit, NUL, a
    // control byte, DEL, non-ASCII, and 16 bytes.
    const char *const bad[] = {"!", "!-", "!3", "!3G", "!00", "!1F", "!7F", "!C3A9",
                               "!4142434445464748494A4B4C4D4E4F50"};
    char req[64];
    for (const char *arg : bad) {
        snprintf(req, sizeof(req), "@SM SET F NONE DOWN %s", arg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("@SM ERR key SET F NONE DOWN", ask(req), arg);
    }
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE DOWN angrysound default def=angrysound", ask("@SM GET F NONE DOWN"));
}

// Review Focus 2, on the wire: a GET of a raw slot while it waits behind the first format, and
// while its own bytes are going in, answers the text's hex, never the flagged byte's.
static void test_get_during_a_raw_write_answers_the_text() {
    rig->eeprom.writeTicks = 20;
    char req[64], want[96], ok[96];
    snprintf(req, sizeof(req), "@SM SET D L2 RIGHT !%s", kRawHex);
    snprintf(want, sizeof(want), "@SM SLOT D L2 RIGHT !%s eeprom def=-", kRawHex);
    snprintf(ok, sizeof(ok), "@SM OK SET D L2 RIGHT !%s", kRawHex);
    send(req);  // the first write: the header goes in first, the slot waits in RAM
    TEST_ASSERT_EQUAL_STRING(want, ask("@SM GET D L2 RIGHT"));
    TEST_ASSERT_EQUAL_STRING(ok, await());
    send("@SM SET D L2 RIGHT !3F");  // "?"
    for (int i = 0; i < 3; ++i)
        pass();
    TEST_ASSERT_TRUE(rig->store.busy());
    TEST_ASSERT_EQUAL_STRING("@SM SLOT D L2 RIGHT !3F eeprom def=-", ask("@SM GET D L2 RIGHT"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET D L2 RIGHT !3F", await());
}

static void test_dump_and_sum_carry_raw_slots() {
    char req[64], want[96];
    snprintf(req, sizeof(req), "@SM SET D CROSS LEFT !%s", kRawHex);
    ask(req);
    ask("@SM SET F L2 PS2 !2A");
    ask("@SM SET F L1 UPL !20");
    snprintf(want, sizeof(want), "@SM SLOT D CROSS LEFT !%s eeprom def=scanning", kRawHex);
    send("@SM DUMP");
    uint32_t crc = kCrc32Init;
    char body[96];
    bool sawRaw = false, sawStar = false, sawSpace = false;
    uint32_t seen = 0;
    for (int i = 0; i < 40000 && seen < sm::kSlotCount + 1; ++i) {
        const uint32_t before = rig->wcb.usb.count;
        pass();
        for (uint32_t k = rig->wcb.usb.count - before; k > 0; --k) {
            TEST_ASSERT_TRUE(stripCrc(rig->wcb.usb.last(static_cast<uint8_t>(k - 1)), body, sizeof(body)));
            if (++seen <= sm::kSlotCount) {
                sawRaw = sawRaw || strcmp(body, want) == 0;
                sawStar = sawStar || strcmp(body, "@SM SLOT F L2 PS2 !2A eeprom def=-") == 0;
                sawSpace = sawSpace || strcmp(body, "@SM SLOT F L1 UPL !20 eeprom def=-") == 0;
                for (const char *c = body + 9; *c != '\0'; ++c)  // after "@SM SLOT "
                    crc = crc32Update(crc, static_cast<uint8_t>(*c));
                crc = crc32Update(crc, '\n');
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(sm::kSlotCount + 1, seen);
    TEST_ASSERT_TRUE(sawRaw);
    TEST_ASSERT_TRUE(sawStar);
    TEST_ASSERT_TRUE(sawSpace);
    char sum[40];
    snprintf(sum, sizeof(sum), "@SM SUM %08lX slots=216", static_cast<unsigned long>(crc32Final(crc)));
    TEST_ASSERT_EQUAL_STRING(sum, ask("@SM SUM"));
}

static void test_press_carries_the_raw_token() {
    ask("@SM EVENTS 1");
    rig->protocol.notifyPress(kSlotFootNoneDown, kRaw, Source::Eeprom, false, true);
    char want[96];
    snprintf(want, sizeof(want), "@SM PRESS F NONE DOWN !%s eeprom", kRawHex);
    TEST_ASSERT_EQUAL_STRING(want, await());
    rig->protocol.notifyPress(kSlotFootNoneDown, "scream", Source::Eeprom, false);
    TEST_ASSERT_EQUAL_STRING("@SM PRESS F NONE DOWN scream eeprom", await());
}

// Review Focus 1: the press's own command is already queued when its event is built. Checked
// against kMaxLine (96), a 22-34 byte command leaves too little room (8 + 96 + 1 + 34 = 139 of
// 160) and every such event is dropped. Checked against its own length, it fits.
static void test_a_long_command_keeps_its_press_event() {
    ask("@SM EVENTS 1");
    TEST_ASSERT_EQUAL_UINT8(0, rig->tx.used());
    TEST_ASSERT_TRUE(pushCommand(rig->tx, ";SEQPREFIX1", "abcdefghijklmno", ",SUFFIX"));  // 34 bytes, the longest
    rig->protocol.notifyPress(kSlotFootNoneDown, "abcdefghijklmno", Source::Eeprom, false);
    TEST_ASSERT_EQUAL_STRING("@SM PRESS F NONE DOWN abcdefghijklmno eeprom", await());
    TEST_ASSERT_EQUAL_STRING(";SEQPREFIX1abcdefghijklmno,SUFFIX", rig->wcb.commands.last());
}

// Protocol::kMaxLine: the longest reply is a SLOT with a 15-character raw command and a
// 15-character default on F CIRCLE RIGHTL (93 with its CRC); a PRESS with a raw command and
// from= is 81; the longest request, a SET with 15 raw bytes, is 65 (LineReader takes 72).
static void test_the_longest_lines_fit() {
    const uint8_t slot = slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT, Tap::LONG);
    MapStore wide(rig->eeprom, kWideDefaults, 1);
    wide.begin();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(wide.beginSet(slot, kRaw, true)));
    while (wide.busy()) {
        wide.step();
        rig->eeprom.tick();
    }
    Protocol wideProtocol(wide, "test");
    TEST_ASSERT_TRUE(wideProtocol.handleLine("@SM GET F CIRCLE RIGHTL", rig->usb));
    char want[100], body[100];
    snprintf(want, sizeof(want), "@SM SLOT F CIRCLE RIGHTL !%s eeprom def=abcdefghijklmno", kRawHex);
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING(want, body);
    TEST_ASSERT_EQUAL_UINT32(93, strlen(rig->usb.log.last()));
    TEST_ASSERT_TRUE(strlen(rig->usb.log.last()) <= Protocol::kMaxLine);

    ask("@SM EVENTS 1");
    rig->protocol.notifyPress(slot, kRaw, Source::Eeprom, true, true);
    await();
    TEST_ASSERT_EQUAL_UINT32(81, strlen(rig->wcb.usb.last()));

    char req[80], line[96];
    snprintf(req, sizeof(req), "@SM SET F CIRCLE RIGHTL !%s", kRawHex);
    withCrc(req, line, sizeof(line));
    TEST_ASSERT_EQUAL_UINT32(65, strlen(line));
    TEST_ASSERT_TRUE(strlen(line) <= LineReader::kMaxLen);
    for (const char *p = line; *p != '\0'; ++p)
        rig->reader.feed(*p);
    TEST_ASSERT_TRUE(rig->reader.feed('\r'));
    TEST_ASSERT_TRUE(rig->protocol.handleLine(rig->reader.line(), rig->wcbPort));
    rig->reader.consume();
    char ok[96];
    snprintf(ok, sizeof(ok), "@SM OK SET F CIRCLE RIGHTL !%s", kRawHex);
    TEST_ASSERT_EQUAL_STRING(ok, await());  // all 30 digits: the echo fits opArg_
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_set_raw_round_trips_as_upper_case_hex);
    RUN_TEST(test_set_raw_refuses_bad_hex);
    RUN_TEST(test_get_during_a_raw_write_answers_the_text);
    RUN_TEST(test_dump_and_sum_carry_raw_slots);
    RUN_TEST(test_press_carries_the_raw_token);
    RUN_TEST(test_a_long_command_keeps_its_press_event);
    RUN_TEST(test_the_longest_lines_fit);
    return UNITY_END();
}
```

- [ ] **Step 3: Run to see it fail**

Run: `cd /Users/travisccook/Repos/SHADOW-C2B5-raw && pio test -e native -f test_protocol_raw -f test_protocol -f
test_protocol_more -f test_runtime`
Expected: `test_protocol_raw` fails to compile (`notifyPress` takes four arguments); after that the READY lines fail
with `Expected '@SM READY v4 ...' Was '@SM READY v3 ...'`.

- [ ] **Step 4: Implement**

`lib/ShadowMap/src/sm_protocol.h`. Replace the last sentence of the header comment (line 6-7):

```cpp
// The CRC is CRC32 (IEEE, as zlib) of everything before " *", as 8 hex digits. Nothing in a
// line ever contains '^' (the WCB splits commands on it): prefix, suffix and raw commands travel
// hex-encoded (a raw command as "!" and its bytes). See WIPDocs/COMMAND_MAPPING_DESIGN.md.
```

Replace `kMaxLine`:

```cpp
    // Longest reply, before port framing: a SLOT with a 15-character raw command ("!" and 30 hex
    // digits) and a 15-character default on F CIRCLE RIGHTL is 93 with its CRC.
    static constexpr uint8_t kMaxLine = 96;
```

Replace the `notifyPress` comment and declaration:

```cpp
    // A combo reached the mapping (mapped or not). Sent to whichever port asked for EVENTS, if it
    // has room for this line; `modFromOther` says the modifier was held on the other controller,
    // `raw` that `key` is a raw command (sent as "!<hex>").
    void notifyPress(uint8_t slot, const char *key, Source src, bool modFromOther, bool raw = false);
```

Replace the `formatSlotBody` declaration with
`uint8_t formatSlotBody(char *dst, uint8_t cap, uint8_t slot, const char *key, Source src, bool raw) const;`.
Replace the `opArg_` line with:

```cpp
    // The OK reply's echo: a key, a raw command as "!" + 30 hex digits, or PREFIX/SUFFIX's hex.
    static constexpr uint8_t kOpArgBuf = 2 + 2 * kKeyMax;
    static_assert(kOpArgBuf >= 2 * kPrefixMax + 1, "PREFIX's hex echo must fit opArg_");
    char opArg_[kOpArgBuf] = {};
```

`lib/ShadowMap/src/sm_protocol.cpp`:

- Line 13: `constexpr uint8_t kProtocolVersion = 4;  // 2: double-tap slots; 3: long-press slots; 4: raw commands`.
- In `class Out`, after `Out &key(const char *k) { ... }` add:

```cpp
    // A slot's command: a key, "-" for none, or a raw command as "!" and its bytes in hex.
    Out &value(const char *k, bool raw) { return raw && *k != '\0' ? c('!').hex(k) : key(k); }
```

- After `decodeHex` add:

```cpp
// "!" and the text's bytes in upper-case hex, NUL-terminated: how a raw command travels.
void rawToken(const char *text, char *out, uint8_t cap) {
    uint8_t n = 0;
    out[n++] = '!';
    for (; *text != '\0' && n + 2 < cap; ++text) {
        const uint8_t b = static_cast<uint8_t>(*text);
        out[n++] = kHexDigits[b >> 4];
        out[n++] = kHexDigits[b & 0x0F];
    }
    out[n] = '\0';
}
```

- Replace the `if (isSet) { ... }` block in `handleLine`:

```cpp
    if (isSet) {
        // A raw command travels as "!" and its bytes in hex, so its own ';', '^' or ";T" never
        // appear in a line the WCB carries. Anything that isn't 1-15 decodable bytes is "key";
        // decodeHex reads "-" as nothing, so "!-" is refused here.
        const char *arg = argv[4];
        const bool raw = arg[0] == '!';
        char text[kKeyBuf];
        const char *key = arg;
        if (raw) {
            if (!decodeHex(arg + 1, text, kKeyMax) || text[0] == '\0') {
                sendError(port, PSTR("key"), verb, slot);
                return true;
            }
            key = text;
        } else if (strcmp(arg, "-") == 0) {
            key = "";
        }
        const MapStore::Result r = store_.beginSet(slot, key, raw);
        if (r == MapStore::Result::Ok) {
            if (raw) {
                rawToken(key, opArg_, sizeof(opArg_));  // upper case, however the request spelled it
            } else {
                strncpy(opArg_, key, sizeof(opArg_) - 1);
                opArg_[sizeof(opArg_) - 1] = '\0';
            }
        }
        startOp(Op::Set, port, r, verb, slot);
        return true;
    }
```

- In `service()`'s SUM branch, replace the slot read and the body line:

```cpp
            char key[kKeyBuf];
            Source src = Source::Default;
            bool raw = false;
            if (!store_.keyForSlot(dumpNext_, key, &src, &raw))
                return;  // EEPROM busy: next pass
            char body[Protocol::kMaxLine + 1];
            const uint8_t n = formatSlotBody(body, sizeof(body), dumpNext_, key, src, raw);
```

- Replace `sendSlotLine`, `formatSlotBody` and `notifyPress`:

```cpp
bool Protocol::sendSlotLine(uint8_t slot, ReplyPort &port) {
    char key[kKeyBuf];
    Source src = Source::Default;
    bool raw = false;
    if (!store_.keyForSlot(slot, key, &src, &raw))
        return false;
    char body[kMaxLine + 1];
    formatSlotBody(body, sizeof(body), slot, key, src, raw);
    Out out;
    out.p(PSTR("@SM SLOT ")).s(body);
    emit(out, port);
    return true;
}

// "<slot> <key|!hex|-> <source> def=<default|->", or "<slot> - reserved res=<reason>".
uint8_t Protocol::formatSlotBody(char *dst, uint8_t cap, uint8_t slot, const char *key, Source src,
                                 bool raw) const {
    Out out;
    out.slot(slot).c(' ').value(key, raw).c(' ').p(sourceToken(src));
    const Reserved why = slotReserved(slot);
    if (why != Reserved::No) {
        out.p(PSTR(" res=")).p(reservedToken(why));
    } else {
        char def[kKeyBuf];
        store_.defaultKey(slot, def);
        out.p(PSTR(" def=")).key(def);
    }
    const uint8_t n = out.len() < cap ? out.len() : static_cast<uint8_t>(cap - 1);
    memcpy(dst, out.str(), n);
    dst[n] = '\0';
    return n;
}

void Protocol::notifyPress(uint8_t slot, const char *key, Source src, bool modFromOther, bool raw) {
    if (eventPort_ == nullptr)
        return;
    Out out;
    out.p(PSTR("@SM PRESS ")).slot(slot).c(' ').value(key, raw).c(' ').p(sourceToken(src));
    if (modFromOther)
        out.p(PSTR(" from=")).c(slotCtrl(slot) == Ctrl::F ? 'D' : 'F');
    const Reserved why = slotReserved(slot);
    if (why != Reserved::No)
        out.p(PSTR(" res=")).p(reservedToken(why));
    // Room for this line, not for kMaxLine: the press's command is already queued, and with a
    // 96-character kMaxLine any command over 21 bytes would drop the event every time.
    if (out.finish() && eventPort_->canSend(out.len()))
        eventPort_->send(out.str(), out.len());
}
```

- [ ] **Step 5: Run every suite, native and simavr**

Run: `cd /Users/travisccook/Repos/SHADOW-C2B5-raw && pio test -e native && pio test -e simavr --without-uploading`
Expected: PASS everywhere, including the unchanged `test_button_commands_always_fit_during_a_dump` and
`test_refuses_requests_when_it_cannot_reply`. If the simavr run of `test_protocol_raw` hangs (SRAM), move
`test_dump_and_sum_carry_raw_slots` and `test_the_longest_lines_fit` into
`test/test_protocol_raw2/test_protocol_raw2.cpp`
with the same includes and the `kRaw`/`kRawHex`/`kWideDefaults` constants. If you split, `git add` the new file and
add `test/test_protocol_raw2` to Step 6's commit pathspec; Task 2b then edits `test_the_longest_lines_fit` there.

- [ ] **Step 6: Commit**

```bash
cd /Users/travisccook/Repos/SHADOW-C2B5-raw && git add test/test_protocol_raw/test_protocol_raw.cpp && git commit -F - -- lib/ShadowMap/src/sm_protocol.h lib/ShadowMap/src/sm_protocol.cpp test/test_protocol test/test_protocol_more test/test_runtime test/test_protocol_raw <<'EOF'
@SM protocol v4: raw commands as !<hex>, 96-character lines

A PRESS event is now checked against its own length: with kMaxLine at 96, checking
kMaxLine dropped the event of any command longer than 21 bytes.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TpbbHNL9m8Gw9PAjSjiW3C
EOF
```

### Task 3: Runtime: a raw press sends its text as typed

**Files:**

- Modify: `lib/ShadowMap/src/sm_runtime.h` (`FiredPress`, lines 18-24)
- Modify: `lib/ShadowMap/src/sm_runtime.cpp` (`Runtime::fire`, lines 158-175)
- Modify: `lib/ShadowMap/src/sm_tx.h`, `lib/ShadowMap/src/sm_tx.cpp` (`pushRaw`)
- Modify: `test/support/sm_fakes.h` (`WcbSim::rawOk`)
- Modify: `src/main.cpp` (lines 263-275, the verbose log)
- Modify: `test/test_line_tx/test_line_tx.cpp`, `test/test_avr/test_avr.cpp`
- Create: `test/test_raw_press/test_raw_press.cpp`

**Interfaces:**

- Consumes: Tasks 1-2b.
- Produces: `bool sm::pushRaw(TxQueue &tx, const char *text);` (text + `\r`, ignoring the headroom);
  `FiredPress::raw`; `WcbSim::rawOk` (test support: lines that aren't replies or announces go to `commands` as they
  are, unchecked).

- [ ] **Step 1: Teach the simulated WCB about raw commands**

`test/support/sm_fakes.h`, in `class WcbSim`, after `int room = 63;  // HardwareSerial's TX buffer when empty` add:

```cpp
    // Set by tests that fire raw commands: a line that isn't a reply or an announce then goes to
    // `commands` exactly as it is, bare or with '^' or ";T", because SHADOW sends raw commands as
    // typed. Replies are still checked.
    bool rawOk = false;
```

In its `private:` section, before `void line(...)`, add:

```cpp
    static bool isReply(const char *s) {
        if (strncmp(s, ";S0", 3) == 0)
            return true;
        if (s[0] != ';' || s[1] != 'W')
            return false;
        const char *p = s + 2;
        while (*p >= '0' && *p <= '9')
            ++p;
        if (*p == ',')
            ++p;
        return strncmp(p, ";S0", 3) == 0;
    }
```

and make these the first lines of `line()`:

```cpp
        if (rawOk && strncmp(s, "@WDP", 4) != 0 && !isReply(s)) {
            commands.add(s, n);
            return;
        }
```

- [ ] **Step 2: Write the failing tests**

Create `test/test_raw_press/test_raw_press.cpp`:

```cpp
// sm::Runtime raw commands: a raw press sends exactly its text and '\r', with no prefix or
// suffix, whatever it contains, and still gets the command headroom. Real built-in defaults
// (F NONE LEFT is "scaredsound" and fires at once), a fake EEPROM and a simulated WCB that
// takes raw lines (WcbSim::rawOk).
#include <unity.h>

#include <ShadowMap.h>
#include <default_mappings.h>
#include <sm_fakes.h>
#define SM_TEST_OWN_SETUP
#include <sm_test_main.h>
#include <new>
#include <stdio.h>

using namespace sm;

namespace {

struct NoInput {
    int available() { return 0; }
    int read() { return -1; }
};

struct Rig {
    FakeEeprom eeprom;
    MapStore store{eeprom, defaultButtonMappings, defaultButtonMappingsCount};
    TxQueue tx;
    WcbPort wcbPort{tx};
    Protocol protocol{store, "2.0.0"};
    Runtime runtime{store, protocol, tx, "2.0.0"};
    WcbSim wcb;
    LineReader reader;
    NoInput in;
    FiredPress fired = {};
};

Rig *rig = nullptr;

const PadState kOff = {false, 0};
const uint8_t kLeft = slotIndex(Ctrl::F, Mod::NONE, Btn::LEFT);
const uint8_t kUp2 = slotIndex(Ctrl::F, Mod::NONE, Btn::UP, Tap::DOUBLE);
const uint8_t kUpL = slotIndex(Ctrl::F, Mod::NONE, Btn::UP, Tap::LONG);
const char kRaw[] = ";W2;S4PLAY:3^;T";

bool pass(uint32_t t, PadState foot, PadState dome = kOff) {
    rig->runtime.pump(rig->in, rig->reader, rig->wcbPort);
    const bool fired = rig->runtime.buttons(t, foot, dome, &rig->fired);
    rig->runtime.service(t, rig->wcb);
    rig->eeprom.tick();
    rig->wcb.refill();
    return fired;
}

PadState held(uint16_t bits) { return {true, bits}; }

void hold(uint32_t from, uint32_t to, PadState foot) {
    for (uint32_t t = from; t <= to; ++t)
        pass(t, foot);
}

void run() {
    while (rig->store.busy()) {
        rig->store.step();
        rig->eeprom.tick();
    }
    rig->store.takeCompletion();
}

void mapSlot(uint8_t slot, const char *text, bool raw) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(rig->store.beginSet(slot, text, raw)));
    run();
}

uint32_t commands() { return rig->wcb.commands.count; }
const char *lastCommand() { return rig->wcb.commands.last(); }

bool piSawRecently(const char *want) {
    char body[96];
    for (uint8_t i = 0; i < LineLog::kLines; ++i) {
        if (stripCrc(rig->wcb.usb.last(i), body, sizeof(body)) && strcmp(body, want) == 0)
            return true;
    }
    return false;
}

}  // namespace

void setUp() {
    static Rig instance;
    instance.~Rig();
    new (&instance) Rig();
    rig = &instance;
    rig->store.begin();
    rig->wcb.rawOk = true;
}

void tearDown() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.violations, "SHADOW broke a WCB rule");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->eeprom.violations, "EEPROM touched while busy");
}

static void test_a_raw_press_sends_exactly_its_text() {
    mapSlot(kLeft, kRaw, true);
    TEST_ASSERT_TRUE(pass(0, held(kHeldLeft)));
    TEST_ASSERT_EQUAL_STRING(kRaw, lastCommand());  // one line: the WCB splits it on '^' itself
    TEST_ASSERT_EQUAL_UINT32(1, commands());
    TEST_ASSERT_EQUAL_STRING(kRaw, rig->fired.key);
    TEST_ASSERT_TRUE(rig->fired.raw);
    TEST_ASSERT_TRUE(rig->fired.queued);
    TEST_ASSERT_EQUAL_UINT8(kLeft, rig->fired.slot);
}

static void test_no_prefix_or_suffix_on_a_raw_press() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(rig->store.beginPrefix(";W2;C")));
    run();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(rig->store.beginSuffix(",L")));
    run();
    mapSlot(kLeft, "hello", true);
    TEST_ASSERT_TRUE(pass(0, held(kHeldLeft)));
    TEST_ASSERT_EQUAL_STRING("hello", lastCommand());
    TEST_ASSERT_TRUE(pass(2000, held(kHeldDown)));  // F NONE DOWN, a stored command: prefix and suffix as always
    TEST_ASSERT_EQUAL_STRING(";W2;Cangrysound,L", lastCommand());
    TEST_ASSERT_FALSE(rig->fired.raw);
}

static void test_spaces_are_kept() {
    mapSlot(kLeft, " a b ", true);
    TEST_ASSERT_TRUE(pass(0, held(kHeldLeft)));
    TEST_ASSERT_EQUAL_STRING(" a b ", lastCommand());
}

static void test_raw_double_taps_and_long_presses() {
    mapSlot(kUp2, "?SEQ,LIST", true);
    mapSlot(kUpL, "held!", true);
    hold(0, 40, held(kHeldUp));
    hold(41, 100, held(0));
    TEST_ASSERT_TRUE(pass(101, held(kHeldUp)));  // the second tap
    TEST_ASSERT_EQUAL_STRING("?SEQ,LIST", lastCommand());
    hold(102, 2000, held(0));
    const uint32_t t = 3000;
    hold(t, t + Runtime::kLongPressMs - 1, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(t + Runtime::kLongPressMs, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING("held!", lastCommand());
    TEST_ASSERT_TRUE(rig->fired.raw);
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_a_raw_press_gets_the_headroom() {
    mapSlot(kLeft, "abcdefghijklmno", true);
    // Leave exactly the command headroom free, and don't drain.
    char fill[TxQueue::kCapacity];
    const uint8_t fillLen = TxQueue::kCapacity - kCommandHeadroom - 1;
    memset(fill, 'f', fillLen);
    fill[fillLen] = '\0';
    const char *parts[] = {fill};
    TEST_ASSERT_TRUE(rig->tx.pushLine(parts, 1));
    TEST_ASSERT_EQUAL_UINT8(kCommandHeadroom, rig->tx.freeSpace());
    TEST_ASSERT_TRUE(rig->runtime.buttons(0, held(kHeldLeft), kOff, &rig->fired));
    TEST_ASSERT_TRUE(rig->fired.queued);
    TEST_ASSERT_EQUAL_UINT8(kCommandHeadroom - 16, rig->tx.freeSpace());
}

static void test_the_press_event_names_the_raw_token() {
    char req[64];
    withCrc("@SM EVENTS 1", req, sizeof(req));
    TEST_ASSERT_TRUE(rig->protocol.handleLine(req, rig->wcbPort));
    mapSlot(kLeft, "*", true);
    pass(0, held(kHeldLeft));
    pass(1, held(0));
    pass(2, held(0));
    TEST_ASSERT_TRUE(piSawRecently("@SM PRESS F NONE LEFT !2A eeprom"));
    TEST_ASSERT_EQUAL_STRING("*", lastCommand());
}

static void test_raw_presses_fit_during_a_dump() {
    char req[64];
    withCrc("@SM DUMP", req, sizeof(req));
    TEST_ASSERT_TRUE(rig->protocol.handleLine(req, rig->wcbPort));
    int sent = 0;
    for (int i = 0; i < 20000 && rig->protocol.opInProgress(); ++i) {
        if (i % 7 == 0) {
            TEST_ASSERT_TRUE_MESSAGE(pushRaw(rig->tx, "abcdefghijklmno"), "raw command dropped");
            ++sent;
        }
        rig->protocol.service();
        rig->store.step();
        rig->eeprom.tick();
        rig->tx.drain(rig->wcb);
        rig->wcb.refill(3);  // about what 9600 baud drains between passes
    }
    for (int i = 0; i < 200; ++i) {
        rig->tx.drain(rig->wcb);
        rig->wcb.refill();
    }
    TEST_ASSERT_TRUE(sent > 20);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(sent), commands());
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.overlong);
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_a_raw_press_sends_exactly_its_text);
    RUN_TEST(test_no_prefix_or_suffix_on_a_raw_press);
    RUN_TEST(test_spaces_are_kept);
    RUN_TEST(test_raw_double_taps_and_long_presses);
    RUN_TEST(test_a_raw_press_gets_the_headroom);
    RUN_TEST(test_the_press_event_names_the_raw_token);
    RUN_TEST(test_raw_presses_fit_during_a_dump);
    return UNITY_END();
}
```

In `test/test_line_tx/test_line_tx.cpp`, after `test_command_headroom_fits_the_longest_command` add (and register
it in `runAllTests()` after that test):

```cpp
static void test_command_headroom_fits_the_longest_raw_command() {
    TxQueue q;
    char fill[TxQueue::kCapacity];
    const uint8_t fillLen = TxQueue::kCapacity - kCommandHeadroom - 1;
    memset(fill, 'f', fillLen);
    fill[fillLen] = '\0';
    const char *parts[] = {fill};
    TEST_ASSERT_TRUE(q.pushLine(parts, 1));
    TEST_ASSERT_TRUE(pushRaw(q, "~~~~~~~~~~~~~~~"));  // 15 characters and '\r'
    TEST_ASSERT_EQUAL_UINT8(kCommandHeadroom - 16, q.freeSpace());
}
```

In `test/test_avr/test_avr.cpp`, inside `#if defined(__AVR__)`:

- In `test_set_reboot_defaults_on_real_eeprom`, after the `realeeprom` write's `takeCompletion` assertion add:

```cpp
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                                static_cast<uint8_t>(store.beginSet(kSlotDomeCrossLeft, "a ;T^b", true)));
        runUntilIdle(store);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                                static_cast<uint8_t>(store.takeCompletion()));
```

  and after the rebooted `realeeprom` assertions, replace `TEST_ASSERT_EQUAL_UINT8(1, rebooted.validEepromSlots());`
  with:

```cpp
    bool raw = false;
    TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotDomeCrossLeft, key, &src, &raw));
    TEST_ASSERT_EQUAL_STRING("a ;T^b", key);  // the flag is bit 7 on the real EEPROM too
    TEST_ASSERT_TRUE(raw && src == Source::Eeprom);
    TEST_ASSERT_EQUAL_UINT8(2, rebooted.validEepromSlots());
```

- In `test_per_pass_work_is_bounded`, after `TEST_ASSERT_TRUE(store.doubleTapMapped(slotIndex(Ctrl::D, Mod::CIRCLE,
  Btn::RIGHT)));`
  add:

```cpp
    // The longest request now: a SET with 15 raw bytes, hex-decoded on the way in.
    withCrc("@SM SET D CIRCLE UP !3B57323B5334504C41593A335E3B54", line, sizeof(line));
    t = micros();
    TEST_ASSERT_TRUE(protocol.handleLine(line, port));
    report("handleLine(SET raw)", micros() - t);
    worst = 0;
    for (uint32_t i = 0; i < 200000UL && (store.busy() || protocol.opInProgress()); ++i) {
        t = micros();
        protocol.service();
        store.step();
        tx.drain(wcb);
        const unsigned long dt = micros() - t;
        if (dt > worst)
            worst = dt;
    }
    report("pass during SET raw", worst);
```

- After the existing `report("button press", micros() - t);` add:

```cpp
    // A raw press: read the slot (raw), queue the text.
    t = micros();
    char rawText[kKeyBuf];
    bool isRaw = false;
    TEST_ASSERT_TRUE(store.keyForSlot(slotIndex(Ctrl::D, Mod::CIRCLE, Btn::UP), rawText, nullptr, &isRaw));
    TEST_ASSERT_TRUE(isRaw);
    TEST_ASSERT_TRUE(pushRaw(tx, rawText));
    tx.drain(wcb);
    report("button press (raw)", micros() - t);
```

- [ ] **Step 3: Run to see them fail**

Run: `cd /Users/travisccook/Repos/SHADOW-C2B5-raw && pio test -e native -f test_raw_press -f test_line_tx`
Expected: compile errors, in clang's wording: `no member named 'raw' in 'sm::FiredPress'` and
`use of undeclared identifier 'pushRaw'`.

- [ ] **Step 4: Implement**

`lib/ShadowMap/src/sm_tx.h`, after the `pushCommand` declaration:

```cpp
// Queues a raw command: the text exactly as typed, then '\r', with no prefix or suffix. Ignores
// the headroom, like pushCommand: a raw command (16 bytes at most) always fits in it.
bool pushRaw(TxQueue &tx, const char *text);
static_assert(kKeyMax + 1 <= kCommandHeadroom, "a raw command must fit the command headroom");
```

`lib/ShadowMap/src/sm_tx.cpp`, after `pushCommand`:

```cpp
bool pushRaw(TxQueue &tx, const char *text) {
    const char *parts[] = {text};
    return tx.pushLine(parts, 1);
}
```

`lib/ShadowMap/src/sm_runtime.h`, in `FiredPress` after `bool modFromOther;  ...`:

```cpp
    bool raw;           // key is a raw command, sent as typed
```

`lib/ShadowMap/src/sm_runtime.cpp`, replace `Runtime::fire`:

```cpp
bool Runtime::fire(uint8_t slot, bool modFromOther, FiredPress *fired) {
    char key[kKeyBuf];
    Source src = Source::Default;
    bool raw = false;
    if (!store_.keyForSlot(slot, key, &src, &raw))
        return false;
    bool queued = false;
    if (key[0] != '\0')
        queued = raw ? pushRaw(tx_, key) : pushCommand(tx_, store_.prefix(), key, store_.suffix());
    protocol_.notifyPress(slot, key, src, modFromOther, raw);
    if (fired != nullptr) {
        fired->slot = slot;
        memcpy(fired->key, key, kKeyBuf);
        fired->src = src;
        fired->modFromOther = modFromOther;
        fired->raw = raw;
        fired->queued = queued;
    }
    return true;
}
```

`src/main.cpp`, in `mappedButtons()`, after `output += fired.key[0] != '\0' ? fired.key : "(unmapped)";` add:

```cpp
    if (fired.raw)
        output += " (raw)";
```

- [ ] **Step 5: Run every suite and the firmware build**

Run: `cd /Users/travisccook/Repos/SHADOW-C2B5-raw && pio test -e native && pio test -e simavr --without-uploading && pio
run`
Expected: all PASS, including `test_avr` (every reported time under 1 ms; note `handleLine(SET raw)`,
`pass during SET raw` and `button press (raw)`), `test_double_tap`, `test_long_press` and
`test_legacy_equivalence`. Compare `pio run`'s `RAM:` line with Setup A: static SRAM should be about 40 bytes higher
(`opArg_` 23 -> 32, `pendingRaw_`, Task 2b's 22-byte reply buffer; 2966 -> 3006 when this plan was drafted). If it
rose by more than 64 bytes, find out why before going on. Write both new numbers down for Task 5, with the
`test_avr` times.

- [ ] **Step 6: Commit**

```bash
cd /Users/travisccook/Repos/SHADOW-C2B5-raw && git add test/test_raw_press/test_raw_press.cpp && git commit -F - -- lib/ShadowMap/src/sm_runtime.h lib/ShadowMap/src/sm_runtime.cpp lib/ShadowMap/src/sm_tx.h lib/ShadowMap/src/sm_tx.cpp test/support/sm_fakes.h src/main.cpp test/test_line_tx/test_line_tx.cpp test/test_avr/test_avr.cpp test/test_raw_press <<'EOF'
Runtime: a raw press sends its text as typed

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TpbbHNL9m8Gw9PAjSjiW3C
EOF
```

### Task 3b: Every mapping pass under 1 ms with v4's longest lines

**Files:**

- Create: `test/test_avr_timing_v4/test_avr_timing_v4.cpp`
- Modify (if the suite needs raw records): `test/support/sm_avr_timing.h` (`eeSlot`/`eeWorst` helpers)

**Interfaces:**

- Consumes: Tasks 1-3 (raw records, `!<hex>` lines, `kMaxLine` 96, raw presses); `test/support/sm_avr_loop.h`
  (`loopPass()` in round 2's order, `recordPass()`, `settle()`, `injectLine()`, `worstState()`), `checkBudget()`.

- [ ] **Step 1: Write the suite**

Start from `git show 637f946:test/test_avr_timing_v4/test_avr_timing_v4.cpp` and fit it to `main`'s harness
(`loopPass()` calls `buttons()`, `batteries()`, then `afterButtons()`; there is no SLOT reply in hand). Record, through
`recordPass()`/`record()` so `checkBudget()` enforces them:

- a GET of a raw slot whose SLOT line is the longest (93 characters: raw on `F CIRCLE RIGHTL` over a 15-character
  default), the whole pass with the drain;
- **the worst read with v4:** 63 bytes in Serial1's RX buffer ending a CRC'd GET of that slot, answered in that pass,
  plus the drain (round 2's worst figure was 942.0 us with a 77-character reply);
- a DUMP's worst pass over raw records; SUM over raw records;
- a SET of 15 raw bytes, and the first write (format);
- a raw press with the 81-character PRESS line (EVENTS on, `from=`), and three raw presses in one pass;
- a raw SET arriving in the pass a raw press fires (under round 2 they are separate passes: label it truthfully).

Labels must say what the measured worst pass contains (read the `parts:` line).

- [ ] **Step 2: Run it**

Run: `pio test -e simavr -f test_avr_timing_v4 --without-uploading -v`, then `-f "test_avr_timing*"`.
Expected: PASS, every `figures,` line `0 at or over 1 ms`. If a figure reaches 1 ms, stop and report (Adaptation 4).

- [ ] **Step 3: Commit**

```bash
git add test/test_avr_timing_v4 test/support
git commit -m "Timing: v4's longest lines, every pass under 1 ms

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 4: Reference client, simulator and contract tests speak v4

**Files:**

- Modify: `tools/shadowmap/shadowmap.py`
- Modify: `tools/shadowmap/test_shadowmap.py`
- Modify: `tools/shadowmap/shadow_sim.cpp` (header comment only)

**Interfaces:**

- Produces (Python, `tools/shadowmap/shadowmap.py`): `PROTOCOL_VERSION = 4`; `KEY_RE` anchored with `\Z`;
  `RAW_RE`; `valid_raw(text) -> bool`; `raw_token(text) -> str`; `parse_value(token) -> Tuple[str, bool]` (raises
  `ProtocolError("parse")` on a bad `!` token); `SlotState.raw: bool = False` and `SlotState.token`;
  `Event.raw: bool = False`; `Client.set(slot, key, raw=False)`; CLI `set --raw`.
- Produces (test support): `WcbEmulator.raw_ok`.

- [ ] **Step 1: Write the failing contract tests**

In `tools/shadowmap/test_shadowmap.py`:

In `WcbEmulator.__init__`, after `self.corrupt_next_reply = False` add:

```python
        self.raw_ok = False  # raw commands go out as typed: bare, '^' and ';T' included
```

Make these the first lines of `WcbEmulator._from_shadow`:

```python
        if self.raw_ok and not line.startswith("@WDP") and not re.match(r"^(;W\d{1,2},?)?;S0", line):
            self.commands.append(line)  # a raw command, sent as typed: the WCB rules don't apply to it
            return
```

In `ContractTest.test_hello` change `self.assertEqual(3, self.ready.version)` to `self.assertEqual(4,
self.ready.version)`.
Add to `ContractTest`:

```python
    RAW = ";W2;S4PLAY:3^;T"

    def test_raw_command_round_trip_press_and_sum(self):
        self.wcb.raw_ok = True
        self.client.set(self.slot("F NONE LEFT"), self.RAW, raw=True)
        s = self.client.get(self.slot("F NONE LEFT"))
        self.assertEqual((self.RAW, True, "eeprom"), (s.key, s.raw, s.source))
        self.wcb.press(foot=LEFT)
        self.assertEqual(self.RAW, self.wcb.commands[-1])  # as typed: no prefix, no suffix
        dump = self.client.dump()
        self.assertTrue({x.slot: x for x in dump}[self.slot("F NONE LEFT")].raw)
        self.assertEqual(smc.map_sum(dump), self.client.sum())  # SUM covers the !<hex> bodies
        self.client.set(self.slot("F NONE LEFT"), "*", raw=True)  # the text "*", not a revert
        s = self.client.get(self.slot("F NONE LEFT"))
        self.assertEqual(("*", True, "eeprom"), (s.key, s.raw, s.source))

    def test_raw_lower_case_and_bad_hex(self):
        self.assertEqual(
            ["@SM OK SET F NONE LEFT !3B5732"],
            self.client.request("@SM SET F NONE LEFT !3b5732", lambda r: r.startswith("@SM OK SET")),
        )
        for bad in ("!", "!-", "!3", "!00", "!1F", "!" + "41" * 16):
            with self.assertRaises(smc.ProtocolError) as e:
                self.client.request(f"@SM SET F NONE LEFT {bad}", lambda r: False)
            self.assertEqual("key", e.exception.code)
        with self.assertRaises(ValueError):
            self.client.set(self.slot("F NONE LEFT"), "ab\n", raw=True)
        with self.assertRaises(ValueError):
            self.client.set(self.slot("F NONE LEFT"), "", raw=True)

    def test_raw_event(self):
        self.wcb.raw_ok = True
        self.client.set(self.slot("F NONE LEFT"), "?SEQ,LIST", raw=True)
        events = []
        self.client.on_event = events.append
        self.client.events(True)
        self.wcb.press(foot=LEFT)
        self.client.listen(0.01)
        press = [e for e in events if e.kind == "PRESS"]
        self.assertEqual([("?SEQ,LIST", True)], [(e.key, e.raw) for e in press])

    def test_power_cut_mid_raw_write(self):
        self.client.set(self.slot("F NONE LEFT"), "firstvalue")
        for cut in range(0, 20):
            self.wcb.cut_after(cut)
            try:
                self.client.set(self.slot("F NONE LEFT"), self.RAW, raw=True)
            except (smc.ProtocolError, smc.Timeout):
                pass  # verify failed after the cut: expected
            self.wcb.reboot()
            self.client.hello()
            s = self.client.get(self.slot("F NONE LEFT"))
            self.assertIn(
                (s.key, s.raw, s.source),
                [("firstvalue", False, "eeprom"), (self.RAW, True, "eeprom"), ("scaredsound", False, "default")],
            )
            self.assertEqual(smc.map_sum(self.client.dump()), self.client.sum())
            self.client.set(self.slot("F NONE LEFT"), "firstvalue")
```

Add to `FramingTest`:

```python
    def test_raw_tokens(self):
        self.assertEqual("!3B57323B5334504C41593A33", smc.raw_token(";W2;S4PLAY:3"))
        self.assertEqual((";W2", True), smc.parse_value("!3b5732"))
        self.assertEqual(("", False), smc.parse_value("-"))
        self.assertEqual(("scream", False), smc.parse_value("scream"))
        for bad in ("!", "!-", "!3", "!0A", "!C3A9"):
            with self.assertRaises(smc.ProtocolError):
                smc.parse_value(bad)
        self.assertTrue(smc.valid_raw(" ") and smc.valid_raw("~" * 15))
        self.assertFalse(smc.valid_raw("ab\n") or smc.valid_raw("") or smc.valid_raw("x" * 16))
        self.assertFalse(smc.KEY_RE.match("scream\n"))  # "$" would let the newline through
        s = smc.parse_slot_state("@SM SLOT F NONE UP !2A eeprom def=happysound")
        self.assertEqual(("*", True), (s.key, s.raw))
        self.assertEqual("F NONE UP !2A eeprom def=happysound", s.body)
```

- [ ] **Step 2: Run to see them fail**

Run: `cd /Users/travisccook/Repos/SHADOW-C2B5-raw && python3 -m unittest tools/shadowmap/test_shadowmap.py`
Expected: FAIL: `version: SHADOW speaks v4; this client speaks v3` in every contract test's `setUp`, and
`AttributeError: module 'shadowmap' has no attribute 'raw_token'`.

- [ ] **Step 3: Implement**

`tools/shadowmap/shadowmap.py`:

- In the docstring's command-line list, after the `UPL` line add
  `shadowmap.py --wcb /dev/ttyUSB0 --shadow-port 3 set --raw F L1 UP ';W2;S4PLAY:3'   (a raw command, sent as
  typed)`.
- Replace the constants block from `PROTOCOL_VERSION` to `FORBIDDEN`:

```python
PROTOCOL_VERSION = 4  # v2 added the double-tap slots, v3 the long presses, v4 raw commands
SINGLE_SLOTS = 72  # 0-71 single presses; 72-143 tapped twice; 144-215 held
TAPS = (1, 2, 3)  # single, double tap, long press
TAP_MARK = {1: "", 2: "2", 3: "L"}
SLOT_COUNT = len(TAPS) * SINGLE_SLOTS
KEY_RE = re.compile(r"^[A-Za-z0-9_]{1,15}\Z")  # \Z, not $: "$" also matches before a trailing "\n"
RAW_RE = re.compile(r"[\x20-\x7e]{1,15}")  # a raw command; use with fullmatch
FORBIDDEN = ("^",)  # the WCB splits commands on it
```

- After `class Timeout`, add:

```python
def valid_raw(text) -> bool:
    """A raw command: 1-15 printable ASCII characters, space included, sent as typed."""
    return isinstance(text, str) and RAW_RE.fullmatch(text) is not None


def raw_token(text: str) -> str:
    """How a raw command travels in an @SM line: '!' and its bytes in upper-case hex."""
    return "!" + text.encode("ascii").hex().upper()


def parse_value(token: str) -> Tuple[str, bool]:
    """A slot's command token -> (text, raw): '-' is unmapped, '!<hex>' a raw command, else a key."""
    if token == "-":
        return "", False
    if token.startswith("!"):
        try:
            text = bytes.fromhex(token[1:]).decode("ascii")
        except ValueError:  # odd length, a bad digit, or a byte over 0x7F
            raise ProtocolError("parse", token) from None
        if not valid_raw(text):
            raise ProtocolError("parse", token)
        return text, True
    return token, False
```

- Replace `SlotState`:

```python
@dataclass
class SlotState:
    slot: Slot
    key: str  # "" = unmapped; with raw, a raw command's text
    source: str  # eeprom | default | reserved
    default: Optional[str] = None  # built-in key ("" = none); None for reserved slots
    reserved: Optional[str] = None  # psgate | overspeed | stick
    raw: bool = False  # key is a raw command (v4): SHADOW sends it to the WCB as typed

    @property
    def token(self) -> str:
        """The command as SHADOW writes it: a key, '-' (unmapped) or '!<hex>' (raw)."""
        if self.raw and self.key:
            return raw_token(self.key)
        return self.key or "-"

    @property
    def body(self) -> str:
        """The SLOT line's text after '@SM SLOT ', exactly as SHADOW sends it."""
        text = f"{self.slot} {self.token} {self.source}"
        if self.reserved is not None:
            return f"{text} res={self.reserved}"
        return f"{text} def={self.default or '-'}"
```

- In `parse_slot_state`, replace `key = "" if t[5] == "-" else t[5]` with `key, raw = parse_value(t[5])` and the
  return with `return SlotState(Slot.from_tokens(t[2], t[3], t[4]), key, t[6], default, kv.get("res"), raw)`.
- In `Event`, after `battery: Optional[Battery] = None` add `raw: bool = False  # PRESS: key is a raw command`.
- In `parse_event`, replace the PRESS branch:

```python
    if len(t) >= 7 and t[1] == "PRESS":
        kv = _kv(t[7:])
        key, raw = parse_value(t[5])
        return Event("PRESS", Slot.from_tokens(t[2], t[3], t[4]), key, t[6], kv.get("from"), kv.get("res"), raw=raw)
```

- Replace `Client.set`:

```python
    def set(self, slot: Slot, key: str, raw: bool = False) -> None:
        """Maps `slot` to stored command `key` ("" to unmap), or with `raw` to a raw command that
        SHADOW sends exactly as typed. Returns once SHADOW has written and verified it."""
        if raw:
            if not valid_raw(key):
                raise ValueError(f"a raw command is 1-15 printable ASCII characters: {key!r}")
            token = raw_token(key)
        else:
            if key and not KEY_RE.match(key):
                raise ValueError(f"key must be 1-15 of [A-Za-z0-9_]: {key!r}")
            token = key or "-"
        want = f"@SM OK SET {slot} {token}"
        self.request(f"@SM SET {slot} {token}", lambda r: r == want)
```

- In `main()`, after `ap.add_argument("args", nargs="*")` add
  `ap.add_argument("--raw", action="store_true", help="set: the value is a raw command, sent exactly as typed")`,
  and in the `set` branch call `client.set(Slot.parse(" ".join(a.args[:3])), a.args[3] if len(a.args) > 3 else "",
  raw=a.raw)`.

`tools/shadowmap/shadow_sim.cpp`, in the header comment after the `stdout carries what SHADOW would write to Serial1.`
sentence add: `A raw command comes out exactly as typed (it may be a bare line, or hold '^' or ";T").`

- [ ] **Step 4: Run the contract tests**

Run: `cd /Users/travisccook/Repos/SHADOW-C2B5-raw && python3 -m unittest tools/shadowmap/test_shadowmap.py`
Expected: PASS (the whole file: the raw tests and every existing one).

- [ ] **Step 5: Commit**

```bash
cd /Users/travisccook/Repos/SHADOW-C2B5-raw && git commit -F - -- tools/shadowmap/shadowmap.py tools/shadowmap/test_shadowmap.py tools/shadowmap/shadow_sim.cpp <<'EOF'
Reference client and simulator: @SM v4 raw commands

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TpbbHNL9m8Gw9PAjSjiW3C
EOF
```

DroidNet's `shadow_protocol.PORTED_FROM` (Task 6) names this commit, but Task 5 rebases it: take its short hash only
after Task 5's push (Task 5 Step 3 prints it).

- [ ] **Step 6 (added for round 2): v4 transcripts**

`tools/shadowmap/test_transcripts.py` compares against `golden/v3`, recorded from `0a8b73f` (v3), with round 2's 7
documented categories. Under v4 every READY says `v4` and a raw session exists. Bring in the v4 goldens recorded from
`0a8b73f` with this plan's Tasks 1-3 applied verbatim, and the raw session:

```bash
git checkout 637f946 -- tools/shadowmap/golden/v4
git show 637f946:tools/shadowmap/transcripts.py   # read its `s_raw` session and SESSIONS changes
```

Add `s_raw` (and its `SESSIONS`/`ONE_ACTIVITY` entries) to `tools/shadowmap/transcripts.py` exactly as `637f946` has
it, so `test_the_scripts_are_the_recorded_ones` matches `golden/v4/raw.in`; point `test_transcripts.py` at
`golden/v4`. Run `python3 -m unittest tools/shadowmap/test_transcripts.py -v`. Every difference must be one of the
existing 7 categories (keyed patches, as now) or the `golden/v4` recording's own documented difference; any other is a
bug: stop and report. EEPROM images must match `golden/v4/eeprom.json` exactly. Commit:

```bash
git add tools/shadowmap/golden/v4 tools/shadowmap/transcripts.py tools/shadowmap/test_transcripts.py
git commit -m "Transcripts: v4 goldens and the raw session

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 5: Firmware docs, final check, push

**Files:**

- Modify: `WIPDocs/COMMAND_MAPPING_DESIGN.md`
- Modify: `CLAUDE.md` (firmware repo)

- [ ] **Step 1: Update the design doc (python3, not Edit/Write)**

Run the two scripts below from `/Users/travisccook/Repos/SHADOW-C2B5-raw`. Before running the second, replace
`<SRAM>`, `<FLASH>`, `<RAWSET>`, `<SENDPASS>`, `<NATIVE>`, `<AVR>` and `<AVR-ONLY>` in it with the numbers from
Task 3 Step 5 and its simavr run (static SRAM bytes, flash bytes, the `handleLine(SET raw)` time and the worst of the
"longest SLOT line" times in ms, the native test count, the simavr test count, the AVR-only test count). Every `edit()`
fails loudly if its anchor isn't found exactly once.

```bash
cd /Users/travisccook/Repos/SHADOW-C2B5-raw && python3 - <<'PY'
from pathlib import Path

p = Path("WIPDocs/COMMAND_MAPPING_DESIGN.md")
t = p.read_text()


def edit(old, new):
    global t
    assert t.count(old) == 1, old[:60]
    t = t.replace(old, new)


edit(
    "  command (see [Long presses](#long-presses)).\n",
    "  command (see [Long presses](#long-presses)).\n"
    "- **Any slot can hold a raw command instead:** up to 15 characters that SHADOW sends to the WCB\n"
    "  exactly as typed, with no prefix and no suffix (see [Raw commands](#raw-commands)).\n",
)
edit(
    "5. **The link runs at 9600 baud in a noisy droid.** Every line in both directions carries a\n   CRC32.\n",
    "5. **The link runs at 9600 baud in a noisy droid.** Every line in both directions carries a\n   CRC32.\n\n"
    "The one deliberate exception to rules 1 and 2 is a [raw command](#raw-commands): the owner typed it,\n"
    "so SHADOW sends it as typed, bare or with `^` or `;T`. It never appears unencoded in an `@SM` line.\n",
)
edit(
    "**Reserved slots** are hardcoded in `main.cpp` or never reach the mapping.",
    "### Raw commands\n\n"
    "Added 2026-10-02. Design: DroidNet's\n"
    "[spec](https://github.com/travisccook/DroidNetSignalBooster/blob/main/docs/superpowers/specs/2026-10-02-shadow-raw-commands-design.md).\n"
    "Any slot (single tap, double tap or long press) can hold a raw command instead of a stored-command\n"
    "key: 1-15 printable ASCII characters (0x20-0x7E, space included), never empty.\n\n"
    "- **Sent exactly as typed:** the text and `\\r`, with no prefix, no suffix and no respelling for a\n"
    "  board's `?CMDCHAR`/`?FUNCCHAR`/`?DELIM`. 15 + 1 bytes is within `kCommandHeadroom` (34), so a raw\n"
    "  press can't be crowded out either (`pushRaw`).\n"
    "- **What the WCB does with it** depends on the first character (`WCB.ino:4869-4880`): the command\n"
    "  character (`;`) makes it a WCB command, the function character (`?`) a settings command on\n"
    "  SHADOW's WCB, anything else is broadcast to every serial port on every board\n"
    "  (`processBroadcastCommand`, `WCB.ino:6853`), unless broadcasts from SHADOW's port are blocked or\n"
    "  routed. `^` and `;T` inside it do what they always do on a WCB.\n"
    "- **Stored as a record with bit 7 of its first byte set** (`kRawFlag`): printable text never sets it,\n"
    "  so the flag costs no characters, and no caller of `MapStore` ever sees it (`keyForSlot` reports\n"
    "  it separately). See EEPROM layout.\n"
    "- **On the wire** a raw command is `!` and its bytes in upper-case hex (see Protocol reference).\n"
    "- **Built-in defaults stay stored-command keys.**\n\n"
    "**Reserved slots** are hardcoded in `main.cpp` or never reach the mapping.",
)
edit(
    "  which is unmapped, until DroidNet sets one. A DEFAULTS from any firmware clears all three banks.\n",
    "  which is unmapped, until DroidNet sets one. A DEFAULTS from any firmware clears all three banks.\n\n"
    "- **Raw commands came later, without a new layout.** A raw record is the text with bit 7 of its first\n"
    "  byte set, then NULs, under the same CRC. `slotRecordValid` accepts a key (`isValidKey`) or a raw\n"
    "  record (flag set, 1-15 bytes, each 0x20-0x7E with the flag cleared, NUL-terminated). Firmware from\n"
    "  before raw commands fails `isValidKey` on a raw record and reads that slot's built-in default, as for\n"
    "  a record that failed its CRC, so going back to older firmware is safe.\n",
)
edit("## Protocol reference (v3)", "## Protocol reference (v4)")
edit(
    "changed. A v1 or v2 client refuses a v3 SHADOW at HELLO. DroidNet speaks all three (it only shows\n"
    "double taps to a v2 SHADOW and long presses to a v3 one); the reference client (`tools/shadowmap/`)\n"
    "speaks v3 only.\n",
    "changed. v4 adds raw commands: wherever a slot's command appears (`OK SET`, `SLOT` in GET and DUMP,\n"
    "`PRESS`), a raw command is `!` and its bytes in upper-case hex (`!3B57323B5334504C41593A33` is\n"
    "`;W2;S4PLAY:3`), and `SET` takes one; keys are `[A-Za-z0-9_]`, so a `!` token is never a key, and the\n"
    "WCB never sees a raw command's `;`, `^` or `;T` in an `@SM` line. `def=` never carries one. `SUM` is\n"
    "unchanged: it covers the `SLOT` bodies, `!<hex>` included. A client from before v4 refuses a v4 SHADOW\n"
    "at HELLO. DroidNet speaks v1 to v4 (raw commands only to a v4 SHADOW); the reference client\n"
    "(`tools/shadowmap/`) speaks v4 only.\n",
)
edit(
    "- **Lengths:** requests are at most 72 characters; replies at most 80, before the route.",
    "- **Lengths:** requests are at most 72 characters (the longest, a raw `SET`, is 65); replies at most 96,\n"
    "  before the route (the longest, a `SLOT` with a 15-character raw command and a 15-character default on\n"
    "  `F CIRCLE RIGHTL`, is 93; a `PRESS` with a raw command and `from=` is 81). Events are sent only when\n"
    "  the port has room for that line.",
)
edit("| `HELLO [route]` | `READY v3 fw=", "| `HELLO [route]` | `READY v4 fw=")
edit(
    "| `GET <slot>` | `SLOT <slot> <key\\|-> <src> def=<key\\|->`,",
    "| `GET <slot>` | `SLOT <slot> <key\\|!hex\\|-> <src> def=<key\\|->` (`!hex`: a raw command),",
)
edit(
    "| `SET <slot> <key\\|->` | `OK SET <slot> <key\\|->` once written and verified. `-` unmaps the slot, as a custom value. |",
    "| `SET <slot> <key\\|!hex\\|->` | `OK SET <slot> <key\\|!HEX\\|->` once written and verified. `-` unmaps the slot, as a custom value. `!hex` (either case) sets a raw command; the echo is upper case. |",
)
edit(
    "| `PRESS <slot> <key\\|-> <src> [from=<F\\|D>] [res=<reason>]` |",
    "| `PRESS <slot> <key\\|!hex\\|-> <src> [from=<F\\|D>] [res=<reason>]` |",
)
edit(
    "| `key` | Not `[A-Za-z0-9_]{1,15}` or `-` |",
    "| `key` | Not `[A-Za-z0-9_]{1,15}`, `-`, or `!` and the hex of 1-15 bytes 0x20-0x7E (odd length, a bad digit, `!`, `!-`, NUL, 16+ bytes) |",
)
edit(
    "  single presses. DroidNet imports version 1 and 2 files.\n",
    "  single presses. DroidNet imports version 1 and 2 files.\n"
    "- Version 3 adds the long presses (`\"UPL\": {...}`), and version 4 marks a raw command with\n"
    "  `\"raw\": true` (`{\"key\": \";W2;S4PLAY:3\", \"src\": \"eeprom\", \"raw\": true}`). DroidNet imports\n"
    "  versions 1 to 4.\n",
)
p.write_text(t)
PY
grep -n "Double taps added 89 of them\|Flash is 53,156\|host: 137 tests\|emulated ATmega2560: all 139\|On the droid: long presses\|handling a request: 0.61\|worst pass of a double tap" WIPDocs/COMMAND_MAPPING_DESIGN.md
```

Expected: the script prints nothing; the grep prints the seven lines the second script replaces. Then:

```bash
cd /Users/travisccook/Repos/SHADOW-C2B5-raw && python3 - <<'PY'
from pathlib import Path

p = Path("WIPDocs/COMMAND_MAPPING_DESIGN.md")
t = p.read_text()


def edit(old, new):
    global t
    assert t.count(old) == 1, old[:60]
    t = t.replace(old, new)


edit(
    "descriptions and switch strings are gone). Double taps added 89 of them.",
    "descriptions and switch strings are gone). Double taps added 89 of them. Raw commands (2026-10-02)\n"
    "    bring it to <SRAM> bytes: `opArg_` grows from 23 to 32 bytes, the store keeps one more flag, and the\n"
    "    protocol keeps one SLOT reply in hand (22 bytes); the line buffers grow by 16 bytes each on the stack.",
)
edit("  - Flash is 53,156 of 253,952 bytes.", "  - Flash is <FLASH> of 253,952 bytes.")
edit("  - handling a request: 0.61 ms", "  - handling a request: 0.61 ms (a raw SET, hex-decoded on the way in: <RAWSET> ms)")
edit(
    "  - a button press: 0.21 ms; the worst pass of a double tap: 0.43 ms\n",
    "  - a button press: 0.21 ms; the worst pass of a double tap: 0.43 ms\n"
    "  - a SLOT reply (GET, and each DUMP line) takes two passes since 2026-10-02: one reads the slot, the next\n"
    "    formats and sends the line, so the longest line (93 characters) costs at most <SENDPASS> ms a pass.\n"
    "    Before, a 15-character key over a 15-character default already took 1.07 ms in one pass (unmeasured\n"
    "    then). CRC16 is computed a byte at a time and the TX queue copies whole parts, for the same reason.\n",
)
edit("host: 137 tests (+2 AVR-only, skipped)", "host: <NATIVE> tests (+<AVR-ONLY> AVR-only, skipped)")
edit("emulated ATmega2560: all 139", "emulated ATmega2560: all <AVR>")
edit(
    "  - the Python client through a WCB emulator,",
    "  - raw commands (`test_store_raw`, `test_protocol_raw`, `test_raw_press`): the record and its flag\n"
    "    (round trip, mid-write and queued reads, bad records, the downgrade rule, power cuts), `!<hex>` on\n"
    "    every line, bad hex, the longest lines, SUM over raw slots, a raw press sent as typed with no prefix\n"
    "    or suffix, its headroom, and a long command keeping its PRESS event\n"
    "  - the Python client through a WCB emulator,",
)
edit(
    "- On the droid: long presses.",
    "- On the droid: raw commands (2026-10-02). Worth checking: one raw command fired from a controller, and\n"
    "  that the owner's map survives the flash (SUM against DroidNet's saved copy).\n"
    "- On the droid: long presses.",
)
p.write_text(t)
PY
```

Read the changed sections back with `sed -n` and check the wording and wrapping (about 100 columns, like the rest of
the file).

- [ ] **Step 2: Update the firmware CLAUDE.md (python3)**

```bash
cd /Users/travisccook/Repos/SHADOW-C2B5-raw && python3 - <<'PY'
from pathlib import Path

p = Path("CLAUDE.md")
t = p.read_text()
old1 = "Every mapped button sends `<prefix><key><suffix>\\r` (default prefix `;W1;C`)"
new1 = (
    "Every mapped button sends `<prefix><key><suffix>\\r` (default prefix `;W1;C`), or, for a raw command,"
    " exactly the text the owner typed and `\\r`"
)
old2 = "Nothing SHADOW sends to the WCB may be a bare line (it gets broadcast to every board) or contain `^` or `;T`."
new2 = (
    old2
    + " The one exception is a raw command: the owner typed it, so it goes out as typed (bare, or with `^` or `;T`),"
    " and it never appears unencoded in an `@SM` line (`!<hex>`)."
)
for old, new in ((old1, new1), (old2, new2)):
    assert t.count(old) == 1, old[:50]
    t = t.replace(old, new)
p.write_text(t)
PY
```

- [ ] **Step 3: Commit, final check, rebase and push**

Commit the docs first: `git rebase` refuses a tree with uncommitted changes.

```bash
cd /Users/travisccook/Repos/SHADOW-C2B5-raw && git commit -F - -- WIPDocs/COMMAND_MAPPING_DESIGN.md CLAUDE.md <<'EOF'
Docs: raw commands (@SM v4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TpbbHNL9m8Gw9PAjSjiW3C
EOF
```

Then rebase and run everything:

```bash
cd /Users/travisccook/Repos/SHADOW-C2B5-raw && git status --short && git fetch origin && git rebase origin/main && pio test -e native && pio test -e simavr --without-uploading && python3 -m unittest tools/shadowmap/test_shadowmap.py && pio run
```

Expected: `git status --short` prints nothing (`include/controller_macs.h` is ignored), and all PASS. Then push, and
take Task 4's short hash as pushed (Task 6's `PORTED_FROM`; Task 14 names it too):

```bash
git -C /Users/travisccook/Repos/SHADOW-C2B5-raw push origin HEAD:main
git -C /Users/travisccook/Repos/SHADOW-C2B5-raw log -1 --format=%h origin/main -- tools/shadowmap/shadowmap.py
```

Pushing is fine; flashing is not (Task 16). Don't pull into the shared checkout `/Users/travisccook/Repos/SHADOW-C2B5`
yourself: other sessions may be using it (Part B's v3 runs use their own scratch worktree at `0a8b73f`). Keep the
worktree: Part B points `DROIDNET_SHADOW_DIR` at it, and Task 16 builds the flash image there.

---

