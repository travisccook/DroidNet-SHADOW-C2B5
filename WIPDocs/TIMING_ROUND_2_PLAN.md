# Timing Round 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every mapping loop pass under 1 ms on the emulated ATmega2560, motors first, with work that must give way
waiting one pass and no button command ever dropped.

**Architecture:** `sm::Runtime` gains the pass rule (`afterButtons()`: a press pass does no other mapping work, a
request pass is followed by a background pass, otherwise one request or the background unit), a FIFO of waiting
presses (a press that can't fire for want of room, an EEPROM write or the three-a-pass limit waits in order), and a
hold that keeps replies out of the TX queue while a command waits. `MapStore::step()` reads at most 4 bytes a step.
`main.cpp` moves request handling after the motors and turns `SHADOW_VERBOSE` off.

**Tech Stack:** C++17 (avr-gcc for the firmware, host g++ for `native`), PlatformIO, Unity, simavr, Python 3
(`tools/shadowmap`).

**Spec:** `WIPDocs/TIMING_ROUND_2_DESIGN.md` (read it first; this plan argues from it).

## Global Constraints

- Motor control is never compromised: `readUSB()`, `footMotorDrive()` and `domeDrive()` stay first in `loop()`;
  nothing in the mapping waits, touches Serial2 or motor state, or adds interrupt-disabled sections.
- Every figure the `test_avr_timing_*` suites record (except `T-` building blocks) is under 1 ms;
  `SM_TIMING_REPORT_ONLY` is removed from `platformio.ini`.
- Work that gives way waits one pass; every line's bytes are unchanged; each port's lines keep their order.
- No button command is dropped except by the existing rules (older than `kDeferMaxMs` = 250 ms, or its controller
  has gone).
- At most `kMaxFiresPerPass` = 3 presses fire in one pass; the FIFO holds `kMaxDeferred` = 8.
- `MapStore::kMaxReadsPerStep` = 4.
- Static SRAM grows by at most 64 bytes over Part A's 2895 (the `SHADOWC2B5` build's RAM line).
- Never `Serial1.print` in `loop()`; everything to the WCB goes through `sm::TxQueue` and `sm::Uart1`.
- Native builds use `-Wall -Wextra -Werror`: no unused parameters or variables.
- On AVR every Unity string literal lives in RAM: if a simavr suite hangs, split it.
- One `pio test` at a time per checkout (two at once corrupt each other's builds).
- Don't change anything under `Reference/`. New `.md` files go in `WIPDocs/`.
- Never commit `include/controller_macs.h` or any MAC address.
- Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Commit to `main` directly (sole
  developer, no PRs). Don't flash.

Commands (from the repo root; `pio` is `~/.platformio/penv/bin/pio` if not on PATH):

```bash
pio run -e SHADOWC2B5                                   # firmware; prints RAM and Flash
pio test -e native                                      # host suites
pio test -e simavr --without-uploading                  # emulated ATmega2560
pio test -e simavr -f "test_avr_timing*" --without-uploading -v   # timing figures
python3 -m unittest tools/shadowmap/test_shadowmap.py tools/shadowmap/test_transcripts.py
```

`pio test` takes `-f <suite>` to run one suite (for example `-f test_defer`).

## Review Focus

1. **A controller fault while presses wait.** Waiting presses must not fire late after the fault clears (the 250 ms
   rule), and the reply hold must not stay on through the fault. Test: Task 3, `test_a_fault_clears_the_hold_and_old_presses_drop`.
2. **A request that arrives while a command waits for room.** It must be answered once the command has gone, not
   starve. Test: Task 4, `test_a_request_waits_out_a_waiting_command`.
3. **A waiting single followed by its double tap.** The single (fired late from the FIFO) must go out before the
   double tap. Test: Task 3, `test_a_waiting_single_goes_before_the_double_tap_after_it`.
4. **An unmapped press while the queue is full.** It sends no command, so it fires at once (its PRESS event waits for
   room as before) unless presses are already waiting. Test: Task 3, `test_an_unmapped_press_needs_no_room`.
5. **RAM growth.** Fourteen new bytes of state plus the 48-byte FIFO; the static SRAM line must stay within 64 bytes
   of 2895. Check: Task 7 Step 6.

---

## File structure

| File | Change |
| --- | --- |
| `include/config.h` | `SHADOW_VERBOSE` commented out |
| `lib/ShadowMap/src/sm_store.h/.cpp` | `kMaxReadsPerStep`, bounded write-phase step |
| `lib/ShadowMap/src/sm_tx.h` | `holdForCommand()` / `heldForCommand()` |
| `lib/ShadowMap/src/sm_protocol.cpp` | `WcbPort::canSend()` refuses while held |
| `lib/ShadowMap/src/sm_runtime.h/.cpp` | waiting-press FIFO, fire limit, `pump()` handles one, `background()`, `finishPass()`, `afterButtons()`, `Link`, `Work`, diagnostics |
| `src/main.cpp` | loop order, `mappedButtons()` returns whether a press fired, `serviceMapping(fired, faulted)`, verbose block without `queued` |
| `tools/shadowmap/shadow_sim.cpp` | the simulator's pass follows `loop()` |
| `tools/shadowmap/test_transcripts.py` | per-stream comparison with the documented differences |
| `test/support/sm_fakes.h` | `NoSerial`, `NullReplyPort` |
| `test/support/sm_avr_loop.h` | `loopPass()` in the new order |
| `test/test_store/test_store.cpp` | bounded step test |
| `test/test_line_tx/test_line_tx.cpp` | hold test |
| `test/test_defer/test_defer.cpp` (new) | waiting presses |
| `test/test_scheduler/test_scheduler.cpp` (new) | the pass rule |
| `test/test_runtime`, `test_double_tap`, `test_long_press` | rigs use `afterButtons()` |
| `test/test_avr_timing_fires/…` | four presses due at once; presses waiting for room |
| `platformio.ini` | `SM_TIMING_REPORT_ONLY` removed |
| `WIPDocs/COMMAND_MAPPING_DESIGN.md`, `CLAUDE.md`, `WIPDocs/HANDOFF_BUTTON_MAP.md`, the spec | docs |

---

### Task 1: Turn `SHADOW_VERBOSE` off

**Files:**
- Modify: `include/config.h:20`
- Modify: `WIPDocs/HANDOFF_BUTTON_MAP.md:274-275`

**Interfaces:** none.

- [ ] **Step 1: Comment the define out**

In `include/config.h` replace

```c
#define SHADOW_VERBOSE // uncomment this for console VERBOSE output
```

with

```c
//#define SHADOW_VERBOSE // uncomment this for console VERBOSE output (bench only: printOutput() blocks the loop
                         // while it sends, several ms a pass when driving; see WIPDocs/TIMING_ROUND_2_DESIGN.md)
```

- [ ] **Step 2: Build with it off, then with it on**

Run: `pio run -e SHADOWC2B5`
Expected: SUCCESS, no warnings from `src/main.cpp` (look for `warning:`).
Run: `PLATFORMIO_BUILD_FLAGS=-DSHADOW_VERBOSE pio run -e SHADOWC2B5`
Expected: SUCCESS (the verbose code still compiles for bench use).
Then run `pio run -e SHADOWC2B5` again so `build/` holds the normal firmware.

- [ ] **Step 3: Update the bench step**

In `WIPDocs/HANDOFF_BUTTON_MAP.md` replace

```markdown
6. **Every mapped combo still sends what it used to.** The verbose log prints
   `ACTIVATED: F L1 UP -> extremehappysou`. PS + direction prints `(unmapped)`.
```

with

```markdown
6. **Every mapped combo still sends what it used to.** `SHADOW_VERBOSE` is off (it blocks the loop); watch the
   PRESS events instead: `python3 tools/shadowmap/shadowmap.py --usb <port> events` (or send `@SM EVENTS 1` on the
   USB port) shows `@SM PRESS F L1 UP extremehappysou default`; PS + direction shows `- reserved`. For the old
   `ACTIVATED:` log, build with `PLATFORMIO_BUILD_FLAGS=-DSHADOW_VERBOSE` for that bench session.
```

(Check `tools/shadowmap/shadowmap.py --help` for the exact subcommand that turns events on; if it has none, keep
only the `@SM EVENTS 1` wording.)

- [ ] **Step 4: Commit**

```bash
git add include/config.h WIPDocs/HANDOFF_BUTTON_MAP.md
git commit -m "Turn SHADOW_VERBOSE off: printOutput() blocked the loop for ms a pass

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The EEPROM writer reads at most 4 bytes a step

**Files:**
- Modify: `lib/ShadowMap/src/sm_store.h` (public constants), `lib/ShadowMap/src/sm_store.cpp` (`MapStore::step`)
- Test: `test/test_store/test_store.cpp`

**Interfaces:**
- Produces: `static constexpr uint8_t MapStore::kMaxReadsPerStep = 4;`

- [ ] **Step 1: Write the failing test**

Add to `test/test_store/test_store.cpp` (before `runAllTests`), and `RUN_TEST(test_a_step_reads_at_most_four_bytes);`
at the end of `runAllTests`'s list:

```cpp
// A rewrite of the same key: every byte already matches, so the write phase only reads. Each step
// reads at most kMaxReadsPerStep bytes (the verify step reads the record in one block), and the
// result is the same as before: no writes, verified, Done.
static void test_a_step_reads_at_most_four_bytes() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotFootL1Up, "samekey");
    const uint32_t writesBefore = eeprom->writes;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(store.beginSet(kSlotFootL1Up, "samekey")));
    uint8_t shortSteps = 0;
    while (store.busy()) {
        const uint32_t before = eeprom->reads;
        store.step();
        const uint32_t n = eeprom->reads - before;
        if (n != MapStore::kSlotSize) {  // the verify step reads the whole record
            TEST_ASSERT_LESS_OR_EQUAL_UINT32(MapStore::kMaxReadsPerStep, n);
            ++shortSteps;
        }
        eeprom->tick();
    }
    TEST_ASSERT_EQUAL_UINT8((MapStore::kSlotSize + MapStore::kMaxReadsPerStep - 1) / MapStore::kMaxReadsPerStep,
                            shortSteps);
    TEST_ASSERT_EQUAL_UINT32(writesBefore, eeprom->writes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                            static_cast<uint8_t>(store.takeCompletion()));
    expectKey(store, kSlotFootL1Up, "samekey", Source::Eeprom);
}
```

(`setUp()` in this file resets `*eeprom`; check that it does before relying on it, and call `eeprom->reset()` at
the top of the test if it doesn't.)

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e native -f test_store`
Expected: compile error, `kMaxReadsPerStep` is not a member of `MapStore`.

- [ ] **Step 3: Implement**

In `sm_store.h`, after `static constexpr uint16_t kFootprint = ...;` add:

```cpp
    // Bytes the write phase reads in one step() before it returns: a step over bytes that already
    // hold the right value stays short (about 5 us a byte on the Mega), and the next step carries on.
    static constexpr uint8_t kMaxReadsPerStep = 4;
```

In `sm_store.cpp`, `MapStore::step()`, replace the write-phase block

```cpp
    if (phase_ == Phase::Write) {
        while (jobPos_ < jobLen_) {
            const uint16_t addr = static_cast<uint16_t>(jobAddr_ + jobPos_);
            const uint8_t want = jobData_[jobPos_++];
            if (io_.read(addr) != want) {
                io_.write(addr, want);
                ++writes_;
                return;
            }
        }
        phase_ = Phase::Verify;
        return;
    }
```

with

```cpp
    if (phase_ == Phase::Write) {
        for (uint8_t reads = 0; jobPos_ < jobLen_; ++reads) {
            if (reads == kMaxReadsPerStep)
                return;  // the next step carries on
            const uint16_t addr = static_cast<uint16_t>(jobAddr_ + jobPos_);
            const uint8_t want = jobData_[jobPos_++];
            if (io_.read(addr) != want) {
                io_.write(addr, want);
                ++writes_;
                return;
            }
        }
        phase_ = Phase::Verify;
        return;
    }
```

Also update the class comment at the top of `sm_store.h` ("All writes happen in step(), one byte per call ...") to
add: "and at most kMaxReadsPerStep bytes are read per call".

- [ ] **Step 4: Run the store suites**

Run: `pio test -e native -f test_store -f test_equiv_store -f test_store_raw`
(`test_store_raw` doesn't exist yet on this branch; drop it if pio complains.)
Expected: all PASS. The power-cut loops in `test_store` count passes, not reads, so they still pass; if one asserts
an exact pass count, raise it by the extra steps and say so in the commit message.

- [ ] **Step 5: Commit**

```bash
git add lib/ShadowMap/src/sm_store.h lib/ShadowMap/src/sm_store.cpp test/test_store/test_store.cpp
git commit -m "Store: the write phase reads at most 4 bytes a step

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Waiting presses (FIFO, room, three a pass) and the reply hold

**Files:**
- Modify: `lib/ShadowMap/src/sm_tx.h`, `lib/ShadowMap/src/sm_protocol.cpp` (`WcbPort::canSend`),
  `lib/ShadowMap/src/sm_runtime.h`, `lib/ShadowMap/src/sm_runtime.cpp`, `src/main.cpp` (verbose block in
  `mappedButtons()`)
- Test: `test/test_line_tx/test_line_tx.cpp`, `test/test_defer/test_defer.cpp` (new)

**Interfaces:**
- Produces:
  - `void TxQueue::holdForCommand(bool on)`, `bool TxQueue::heldForCommand() const`
  - `static constexpr uint8_t Runtime::kMaxDeferred = 8`, `Runtime::kMaxFiresPerPass = 3`
  - `uint8_t Runtime::deferredCount() const`, `uint8_t Runtime::firesLastPass() const`,
    `uint16_t Runtime::droppedPresses() const`
  - `FiredPress` loses its `queued` field (a command that doesn't fit now waits instead).

- [ ] **Step 1: Write the hold test**

Add to `test/test_line_tx/test_line_tx.cpp` (and its `RUN_TEST`):

```cpp
// While a button command waits for room, the WCB port takes no replies; the command itself still
// goes in (pushCommand ignores the hold, as it ignores the headroom).
static void test_hold_keeps_replies_out() {
    sm::TxQueue tx;
    sm::WcbPort port(tx);
    TEST_ASSERT_TRUE(port.canSend(10));
    tx.holdForCommand(true);
    TEST_ASSERT_TRUE(tx.heldForCommand());
    TEST_ASSERT_FALSE(port.canSend(10));
    TEST_ASSERT_TRUE(sm::pushCommand(tx, ";W1;C", "key", ""));
    tx.holdForCommand(false);
    TEST_ASSERT_TRUE(port.canSend(10));
}
```

(Check the includes at the top of `test_line_tx.cpp`; `WcbPort` is in `sm_protocol.h`, which `ShadowMap.h`
includes.)

- [ ] **Step 2: Write the waiting-press suite**

Create `test/test_defer/test_defer.cpp`:

```cpp
// sm::Runtime's waiting presses (WIPDocs/TIMING_ROUND_2_DESIGN.md, section 4): a press that can't fire
// yet (no room in the TX queue, the EEPROM mid-write, or the pass's limit of three) waits in order and
// fires on a later pass; none is dropped but by the old rules (over 250 ms old, or its controller gone).
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

struct Rig {
    FakeEeprom eeprom;
    MapStore store{eeprom, defaultButtonMappings, defaultButtonMappingsCount};
    TxQueue tx;
    WcbPort wcbPort{tx};
    Protocol protocol{store, "2.0.0"};
    Runtime runtime{store, protocol, tx, "2.0.0"};
    WcbSim wcb;
    bool stalled = false;        // the UART sends nothing between passes
    bool eepromFrozen = false;   // the EEPROM stays busy
};

Rig *rig = nullptr;

const PadState kNone = {true, 0};
const PadState kOff = {false, 0};
const uint8_t kFUp = slotIndex(Ctrl::F, Mod::NONE, Btn::UP);
const uint8_t kFDown = slotIndex(Ctrl::F, Mod::NONE, Btn::DOWN);
const uint8_t kDUp = slotIndex(Ctrl::D, Mod::NONE, Btn::UP);
const uint8_t kDDown = slotIndex(Ctrl::D, Mod::NONE, Btn::DOWN);

PadState held(uint16_t bits) { return {true, bits}; }

// One pass: the buttons, then the rest of the mapping's work (unscheduled: this suite is about
// buttons()), then the EEPROM and UART clocks.
bool pass(uint32_t t, PadState foot, PadState dome) {
    FiredPress fired;
    const bool any = rig->runtime.buttons(t, foot, dome, &fired);
    rig->runtime.service(t, rig->wcb);
    if (!rig->eepromFrozen)
        rig->eeprom.tick();
    if (!rig->stalled)
        rig->wcb.refill();
    return any;
}

void mapSlot(uint8_t slot, const char *key) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(rig->store.beginSet(slot, key)));
    while (rig->store.busy()) {
        rig->store.step();
        rig->eeprom.tick();
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                            static_cast<uint8_t>(rig->store.takeCompletion()));
}

// Fills the TX queue so no command fits (8 bytes free; the shortest command here is 9) and stops
// the UART.
void stallQueue() {
    rig->stalled = true;
    rig->wcb.room = 0;
    char fill[152];
    memcpy(fill, ";S0", 3);
    memset(fill + 3, 'x', sizeof(fill) - 3);
    TEST_ASSERT_TRUE(rig->tx.pushLine2(fill, sizeof(fill) - 1, nullptr, 0));  // 151 + '\r' = 152 queued
    TEST_ASSERT_EQUAL_UINT8(8, rig->tx.freeSpace());
}

void unstall() {
    rig->stalled = false;
    rig->wcb.refill();
}

// Passes with nothing held until everything queued has reached the WCB.
void drainOut(uint32_t &t) {
    for (uint16_t i = 0; i < 50 && rig->tx.used() > 0; ++i)
        pass(t += 2, kNone, kNone);
    TEST_ASSERT_EQUAL_UINT8(0, rig->tx.used());
}

}  // namespace

void setUp() {
    static Rig instance;
    instance.~Rig();
    new (&instance) Rig();
    rig = &instance;
    rig->store.begin();
}

void tearDown() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.violations, "SHADOW broke a WCB rule");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->eeprom.violations, "EEPROM touched while busy");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, rig->runtime.droppedPresses(), "the waiting list overflowed");
}

static void test_a_command_waits_for_room() {
    mapSlot(kFUp, "fup");
    TEST_ASSERT_FALSE(rig->store.doubleTapMapped(kFUp));  // fires at once when there's room
    stallQueue();
    TEST_ASSERT_FALSE(pass(1000, held(kHeldUp), kOff));
    TEST_ASSERT_EQUAL_UINT8(1, rig->runtime.deferredCount());
    TEST_ASSERT_TRUE(rig->tx.heldForCommand());
    TEST_ASSERT_FALSE(rig->wcbPort.canSend(10));  // replies wait for the command
    TEST_ASSERT_FALSE(pass(1002, held(kHeldUp), kOff));
    unstall();
    bool any = false;
    uint32_t t = 1004;
    for (; t < 1100 && !any; t += 2)
        any = pass(t, held(kHeldUp), kOff);
    TEST_ASSERT_TRUE(any);
    TEST_ASSERT_EQUAL_UINT8(0, rig->runtime.deferredCount());
    TEST_ASSERT_FALSE(rig->tx.heldForCommand());
    drainOut(t);
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.commands.count);
    TEST_ASSERT_EQUAL_STRING(";W1;Cfup", rig->wcb.commands.last());
}

// Four singles wait for room (each combo's double tap is mapped, so another combo ends its wait and
// fires it); when room comes, three fire in one pass and the fourth on the next, in the order pressed.
static void test_waiting_presses_keep_their_order_three_a_pass() {
    mapSlot(kFUp, "fup");
    mapSlot(doubleOf(kFUp), "fup2");
    mapSlot(kFDown, "fdn");
    mapSlot(doubleOf(kFDown), "fdn2");
    mapSlot(kDUp, "dup");
    mapSlot(doubleOf(kDUp), "dup2");
    mapSlot(kDDown, "ddn");
    mapSlot(doubleOf(kDDown), "ddn2");
    stallQueue();
    pass(1000, held(kHeldUp), held(kHeldUp));      // both wait for a second tap
    pass(1010, held(kHeldDown), held(kHeldDown));  // another combo: the UPs fire, no room: they wait
    TEST_ASSERT_EQUAL_UINT8(2, rig->runtime.deferredCount());
    pass(1020, held(kHeldUp), held(kHeldUp));      // the DOWNs fire, no room: they wait too
    TEST_ASSERT_EQUAL_UINT8(4, rig->runtime.deferredCount());
    pass(1022, kNone, kNone);
    unstall();
    pass(1024, kNone, kNone);  // the filler starts to drain; still no room at the start of this pass
    TEST_ASSERT_EQUAL_UINT8(0, rig->runtime.firesLastPass());
    pass(1026, kNone, kNone);
    TEST_ASSERT_EQUAL_UINT8(Runtime::kMaxFiresPerPass, rig->runtime.firesLastPass());
    TEST_ASSERT_EQUAL_UINT8(1, rig->runtime.deferredCount());
    pass(1028, kNone, kNone);
    TEST_ASSERT_EQUAL_UINT8(1, rig->runtime.firesLastPass());
    TEST_ASSERT_EQUAL_UINT8(0, rig->runtime.deferredCount());
    uint32_t t = 1030;
    drainOut(t);  // well before 1370, when the second UP tap's window would close
    TEST_ASSERT_EQUAL_UINT32(4, rig->wcb.commands.count);
    TEST_ASSERT_EQUAL_STRING(";W1;Cdup", rig->wcb.commands.last(3));  // the dome is read first, as always
    TEST_ASSERT_EQUAL_STRING(";W1;Cfup", rig->wcb.commands.last(2));
    TEST_ASSERT_EQUAL_STRING(";W1;Cddn", rig->wcb.commands.last(1));
    TEST_ASSERT_EQUAL_STRING(";W1;Cfdn", rig->wcb.commands.last(0));
}

static void test_a_waiting_press_is_dropped_after_250_ms() {
    mapSlot(kFUp, "fup");
    stallQueue();
    pass(1000, held(kHeldUp), kOff);
    TEST_ASSERT_EQUAL_UINT8(1, rig->runtime.deferredCount());
    uint32_t t = 1000;
    for (; t <= 1000 + Runtime::kDeferMaxMs; t += 10)
        pass(t, held(kHeldUp), kOff);
    TEST_ASSERT_EQUAL_UINT8(1, rig->runtime.deferredCount());  // 250 ms old: still waiting
    pass(t, held(kHeldUp), kOff);
    TEST_ASSERT_EQUAL_UINT8(0, rig->runtime.deferredCount());  // past it: dropped, as before
    TEST_ASSERT_FALSE(rig->tx.heldForCommand());
    unstall();
    drainOut(t);
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.commands.count);
}

static void test_a_waiting_press_is_dropped_when_its_controller_goes() {
    mapSlot(kFUp, "fup");
    stallQueue();
    pass(1000, held(kHeldUp), kOff);
    TEST_ASSERT_EQUAL_UINT8(1, rig->runtime.deferredCount());
    pass(1002, kOff, kOff);
    TEST_ASSERT_EQUAL_UINT8(0, rig->runtime.deferredCount());
    unstall();
    uint32_t t = 1004;
    drainOut(t);
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.commands.count);
}

// Two singles come due in one pass while the EEPROM is mid-write: both wait (the old single slot kept
// only the second) and both fire, in order, once it's free.
static void test_two_presses_held_by_the_eeprom_both_fire() {
    mapSlot(kFUp, "fup");
    mapSlot(doubleOf(kFUp), "fup2");
    mapSlot(kDUp, "dup");
    mapSlot(doubleOf(kDUp), "dup2");
    pass(1000, held(kHeldUp), held(kHeldUp));
    pass(1002, kNone, kNone);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(rig->store.beginSet(kFDown, "busy")));
    rig->store.step();  // writes the first byte: the EEPROM is busy
    rig->eepromFrozen = true;
    pass(1000 + Runtime::kDoubleTapMs, kNone, kNone);  // both windows close: both wait for the EEPROM
    TEST_ASSERT_EQUAL_UINT8(2, rig->runtime.deferredCount());
    rig->eepromFrozen = false;
    uint32_t t = 1000 + Runtime::kDoubleTapMs;
    for (uint8_t i = 0; i < 20 && rig->runtime.deferredCount() > 0; ++i)
        pass(t += 2, kNone, kNone);
    TEST_ASSERT_EQUAL_UINT8(0, rig->runtime.deferredCount());
    drainOut(t);
    TEST_ASSERT_EQUAL_UINT32(2, rig->wcb.commands.count);
    TEST_ASSERT_EQUAL_STRING(";W1;Cdup", rig->wcb.commands.last(1));
    TEST_ASSERT_EQUAL_STRING(";W1;Cfup", rig->wcb.commands.last(0));
}

// Review focus 3: a single waiting for room, then its own double tap: the single goes first.
static void test_a_waiting_single_goes_before_the_double_tap_after_it() {
    mapSlot(kFUp, "fup");
    mapSlot(doubleOf(kFUp), "fup2");
    mapSlot(kFDown, "fdn");
    stallQueue();
    pass(1000, held(kHeldUp), kOff);
    pass(1010, held(kHeldDown), kOff);  // ends UP's wait: fup waits for room
    pass(1020, kNone, kOff);
    TEST_ASSERT_EQUAL_UINT8(2, rig->runtime.deferredCount());  // fup, then fdn (DOWN has no double tap)
    pass(1030, held(kHeldUp), kOff);  // UP again: a new first tap
    pass(1040, kNone, kOff);
    pass(1050, held(kHeldUp), kOff);  // its second tap: the double tap queues behind
    TEST_ASSERT_EQUAL_UINT8(3, rig->runtime.deferredCount());
    unstall();
    uint32_t t = 1050;
    for (uint8_t i = 0; i < 20 && rig->runtime.deferredCount() > 0; ++i)
        pass(t += 2, kNone, kOff);
    drainOut(t);
    TEST_ASSERT_EQUAL_UINT32(3, rig->wcb.commands.count);
    TEST_ASSERT_EQUAL_STRING(";W1;Cfup", rig->wcb.commands.last(2));
    TEST_ASSERT_EQUAL_STRING(";W1;Cfdn", rig->wcb.commands.last(1));
    TEST_ASSERT_EQUAL_STRING(";W1;Cfup2", rig->wcb.commands.last(0));
}

// Review focus 4: an unmapped combo sends no command, so a full queue doesn't hold it up.
static void test_an_unmapped_press_needs_no_room() {
    mapSlot(kFUp, "");  // unmapped
    stallQueue();
    TEST_ASSERT_TRUE(pass(1000, held(kHeldUp), kOff));
    TEST_ASSERT_EQUAL_UINT8(0, rig->runtime.deferredCount());
    TEST_ASSERT_FALSE(rig->tx.heldForCommand());
}

// Review focus 1: a fault while a press waits. The fault's passes don't call buttons(); the hold
// comes off (no press can fire in a fault), and when the fault clears the old press is dropped.
static void test_a_fault_clears_the_hold_and_old_presses_drop() {
    mapSlot(kFUp, "fup");
    stallQueue();
    pass(1000, held(kHeldUp), kOff);
    TEST_ASSERT_TRUE(rig->tx.heldForCommand());
    rig->runtime.faultPass();  // what afterButtons(…, faulted = true, …) does first (Task 4 uses it there)
    TEST_ASSERT_FALSE(rig->tx.heldForCommand());
    unstall();
    pass(1300, held(kHeldUp), kOff);  // the fault is over, 300 ms later
    TEST_ASSERT_EQUAL_UINT8(0, rig->runtime.deferredCount());
    uint32_t t = 1300;
    drainOut(t);
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.commands.count);
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_a_command_waits_for_room);
    RUN_TEST(test_waiting_presses_keep_their_order_three_a_pass);
    RUN_TEST(test_a_waiting_press_is_dropped_after_250_ms);
    RUN_TEST(test_a_waiting_press_is_dropped_when_its_controller_goes);
    RUN_TEST(test_two_presses_held_by_the_eeprom_both_fire);
    RUN_TEST(test_a_waiting_single_goes_before_the_double_tap_after_it);
    RUN_TEST(test_an_unmapped_press_needs_no_room);
    RUN_TEST(test_a_fault_clears_the_hold_and_old_presses_drop);
    return UNITY_END();
}
```

Notes for the implementer:
- `test_a_fault_clears_the_hold_and_old_presses_drop` calls `Runtime::faultPass()`, which this task adds
  (Step 4); Task 4's `afterButtons()` calls it on a faulted pass.
- In `test_a_waiting_single_goes_before_the_double_tap_after_it`, check `kFDown` has no double tap or long press
  in the built-in defaults (`TEST_ASSERT_FALSE(rig->store.doubleTapMapped(kFDown))`); if it does, map its double tap
  to `""` first.
- `FakeEeprom::kPoolRecords` is 6: the order test maps 4 double taps, which fits.

- [ ] **Step 3: Run them to see them fail**

Run: `pio test -e native -f test_line_tx -f test_defer`
Expected: compile errors (`holdForCommand`, `deferredCount`, `firesLastPass`, `droppedPresses`, `faultPass` and
`kMaxFiresPerPass` don't exist).

- [ ] **Step 4: Implement**

`sm_tx.h`, in `class TxQueue` public section, after `freeSpace()`:

```cpp
    // While a button command waits for room (sm::Runtime), replies, events and the announce stay
    // out of the queue so the drain frees room for it. pushCommand() ignores the hold.
    void holdForCommand(bool on) { held_ = on; }
    bool heldForCommand() const { return held_; }
```

and in its private section, after `uint8_t used_ = 0;`:

```cpp
    bool held_ = false;
```

`sm_protocol.cpp`, `WcbPort::canSend`:

```cpp
bool WcbPort::canSend(uint8_t len) {
    if (tx_.heldForCommand())
        return false;  // a button command is waiting for room
    // Sized for the longest route so a HELLO that lengthens it can't break the headroom.
    const uint16_t framed = static_cast<uint16_t>(kMaxRouteLen + len + 1);
    return tx_.freeSpace() >= framed + kCommandHeadroom;
}
```

`sm_runtime.h`:
- Remove `bool queued;` from `struct FiredPress` and its comment.
- In `class Runtime` public constants, after `kLongPressMs`:

```cpp
    static constexpr uint8_t kMaxDeferred = 8;      // presses waiting to fire (see buttons())
    static constexpr uint8_t kMaxFiresPerPass = 3;  // presses fired in one pass; more wait a pass, in order
```

- Replace the doc comment of `buttons()` with:

```cpp
    // Once per pass, after the controllers have been read: presses that waited, oldest first, then the
    // dome controller, then the foot, as the old code did. A combo fires when pressed and again every
    // second while held; the repeat timer is shared by both controllers. A combo whose double tap or long
    // press is mapped waits instead (see WIPDocs/COMMAND_MAPPING_DESIGN.md, Double taps and Long presses).
    // A press that can't fire yet (the EEPROM mid-write, no room in the TX queue for its command, or
    // kMaxFiresPerPass already fired this pass) waits, in order, and fires on a later pass; it is dropped
    // only if its controller has gone or it is more than kDeferMaxMs old. While one waits, the TX queue
    // takes no replies (TxQueue::holdForCommand). Returns true, filling `fired` with the last, when a
    // press fired this pass.
```

- After `tapPending()`, add:

```cpp
    // A pass in which readUSB() reported a fault (buttons() isn't called): no press can fire, so the
    // TX queue isn't held for one. Presses still waiting are dropped by buttons() once they are old.
    void faultPass() { tx_.holdForCommand(false); }

    // Diagnostics, for tests and the timing harness.
    uint8_t deferredCount() const { return deferredCount_; }
    uint8_t firesLastPass() const { return firesThisPass_; }
    uint16_t droppedPresses() const { return droppedPresses_; }  // the waiting list was full (never expected)
```

- In the private section replace

```cpp
    uint8_t deferredSlot_ = kNoSlot;  // pressed while the EEPROM was mid-write
    bool deferredFromOther_ = false;
    uint32_t deferredAt_ = 0;
```

with

```cpp
    struct Deferred {
        uint8_t slot;
        bool fromOther;
        uint32_t at;  // when it first tried to fire
    };
    Deferred deferred_[kMaxDeferred];
    uint8_t deferredCount_ = 0;
    uint8_t firesThisPass_ = 0;
    uint16_t droppedPresses_ = 0;
```

and add `void defer(uint8_t slot, bool modFromOther, uint32_t nowMs);` next to the other private methods.

`sm_runtime.cpp`, `Runtime::buttons()`:

```cpp
bool Runtime::buttons(uint32_t nowMs, const PadState &foot, const PadState &dome, FiredPress *fired) {
    protocol_.setPads(foot.connected, dome.connected);
    firesThisPass_ = 0;
    bool any = false;
    // Presses that waited, oldest first. One that still can't go keeps its place, and so does
    // everything behind it. Only while still fresh and its controller is still there (a fault can
    // hold up the passes in between).
    uint8_t kept = 0;
    for (uint8_t i = 0; i < deferredCount_; ++i) {
        const Deferred d = deferred_[i];
        const PadState &pad = slotCtrl(d.slot) == Ctrl::F ? foot : dome;
        if (!pad.connected || nowMs - d.at > kDeferMaxMs)
            continue;  // dropped
        if (kept == 0 && fire(d.slot, d.fromOther, fired)) {
            any = true;
            continue;
        }
        deferred_[kept++] = d;
    }
    deferredCount_ = kept;
    any = handlePad(Ctrl::D, dome, foot, nowMs, fired) || any;
    any = handlePad(Ctrl::F, foot, dome, nowMs, fired) || any;
    tx_.holdForCommand(deferredCount_ > 0);
    return any;
}
```

`Runtime::fireOrDefer()` and a new `Runtime::defer()`:

```cpp
bool Runtime::fireOrDefer(uint8_t slot, bool modFromOther, uint32_t nowMs, FiredPress *fired) {
    if (deferredCount_ == 0 && fire(slot, modFromOther, fired))
        return true;
    defer(slot, modFromOther, nowMs);  // behind any press already waiting
    return false;
}

void Runtime::defer(uint8_t slot, bool modFromOther, uint32_t nowMs) {
    if (deferredCount_ == kMaxDeferred) {
        ++droppedPresses_;  // eight presses already waiting: more than two controllers can produce
        return;
    }
    deferred_[deferredCount_++] = {slot, modFromOther, nowMs};
}
```

`Runtime::fire()`:

```cpp
// Sends a press: its command (if its slot maps to one) and its PRESS event. False, sending nothing,
// when it can't go this pass: kMaxFiresPerPass already fired, the EEPROM is mid-write and the slot has
// to be read, or the command doesn't fit in the TX queue.
bool Runtime::fire(uint8_t slot, bool modFromOther, FiredPress *fired) {
    if (firesThisPass_ >= kMaxFiresPerPass)
        return false;
    char key[kKeyBuf];
    Source src = Source::Default;
    if (!store_.keyForSlot(slot, key, &src))
        return false;
    if (key[0] != '\0' && !pushCommand(tx_, store_.prefix(), key, store_.suffix()))
        return false;
    ++firesThisPass_;
    protocol_.notifyPress(slot, key, src, modFromOther);
    if (fired != nullptr) {
        fired->slot = slot;
        memcpy(fired->key, key, kKeyBuf);
        fired->src = src;
        fired->modFromOther = modFromOther;
    }
    return true;
}
```

`Runtime::announce()`: change the room check to

```cpp
    if (tx_.heldForCommand() || tx_.freeSpace() < len + 1 + kCommandHeadroom)
        return;  // busy right now; try again next pass
```

`src/main.cpp`, `mappedButtons()` verbose block: delete the two lines

```cpp
    if (fired.key[0] != '\0' && !fired.queued)
        output += " [WCB queue full, dropped]";
```

- [ ] **Step 5: Run the new tests**

Run: `pio test -e native -f test_line_tx -f test_defer`
Expected: PASS. If `test_waiting_presses_keep_their_order_three_a_pass` sees the three fire one pass later than
written, check how many bytes the first refill drains (`WcbSim::refill` gives 63): the pass numbers in the test
follow from 152 queued bytes and 63 a pass; fix the test's pass count, not the code, and only if the order and the
three-then-one split still hold.

- [ ] **Step 6: Run everything that drives presses**

Run: `pio test -e native`
Expected: all PASS. `grep -rn "queued" test/` first: any test reading `FiredPress::queued` must drop that check
(the field is gone; a full queue now means the press waits).
Run: `PLATFORMIO_BUILD_FLAGS=-DSHADOW_VERBOSE pio run -e SHADOWC2B5` then `pio run -e SHADOWC2B5`
Expected: both SUCCESS.

- [ ] **Step 7: Run the suites on simavr**

Run: `pio test -e simavr --without-uploading -f test_defer -f test_line_tx -f test_double_tap -f test_long_press -f test_runtime`
Expected: PASS (if `test_defer` hangs, it ran out of SRAM: split it in two suites).

- [ ] **Step 8: Commit**

```bash
git add lib/ShadowMap/src/sm_tx.h lib/ShadowMap/src/sm_protocol.cpp lib/ShadowMap/src/sm_runtime.h \
  lib/ShadowMap/src/sm_runtime.cpp src/main.cpp test/test_line_tx/test_line_tx.cpp test/test_defer/test_defer.cpp
git commit -m "Runtime: presses that can't fire wait in order; three a pass; replies hold for a waiting command

A command that didn't fit in the TX queue was dropped, and a second press deferred while the
EEPROM was busy overwrote the first. Both now wait in a FIFO of 8 and fire on a later pass.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The pass rule in `sm::Runtime`

**Files:**
- Modify: `lib/ShadowMap/src/sm_runtime.h`, `lib/ShadowMap/src/sm_runtime.cpp`, `test/support/sm_fakes.h`
- Modify (rigs): `test/test_runtime/test_runtime.cpp`, `test/test_double_tap/test_double_tap.cpp`,
  `test/test_long_press/test_long_press.cpp`
- Test: `test/test_scheduler/test_scheduler.cpp` (new)

**Interfaces:**
- Consumes: `Runtime::faultPass()`, `TxQueue::heldForCommand()` (Task 3).
- Produces:
  - `template <class Serial> struct Runtime::Link { Serial &port; LineReader &lines; ReplyPort &replies; };`
  - `template <class Serial> bool Runtime::pump(Serial &, LineReader &, ReplyPort &)`: at most one request; true
    when it handled one.
  - `void Runtime::background(uint32_t nowMs, bool mayAnnounce)`
  - `template <class Out> void Runtime::finishPass(Out &wcb)`
  - `template <class Out> void Runtime::service(uint32_t nowMs, Out &wcb, bool mayAnnounce = true)` (kept,
    unscheduled: `background()` + `finishPass()`)
  - `enum class Runtime::Work : uint8_t { Presses, Request, Background }`, `Work Runtime::lastWork() const`
  - `template <class WcbIn, class UsbIn, class Out> void Runtime::afterButtons(uint32_t nowMs, bool fired,
    bool faulted, Link<WcbIn> wcb, Link<UsbIn> usb, Out &wcbOut)`
  - `sm_fakes.h`: `struct NoSerial` (nothing to read), `class NullReplyPort` (never used for replies).

- [ ] **Step 1: Write the scheduler suite**

Create `test/test_scheduler/test_scheduler.cpp`:

```cpp
// The pass rule (WIPDocs/TIMING_ROUND_2_DESIGN.md, section 3; sm::Runtime::afterButtons): a pass that
// fired a press reads no request and runs no background unit; the first pass without a press after a
// handled request runs the background unit only; any other pass handles at most one request (WCB first)
// or, with none, runs the background unit. The real built-in defaults, a fake EEPROM, a simulated WCB.
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

// Serial input: everything added is already in the UART's RX buffer.
struct FakeSerialIn {
    char data[320];
    size_t len = 0;
    size_t pos = 0;
    void add(const char *s) {
        const size_t n = strlen(s);
        TEST_ASSERT_TRUE(len + n <= sizeof(data));
        memcpy(data + len, s, n);
        len += n;
    }
    void addLine(const char *body) {
        char line[96];
        withCrc(body, line, sizeof(line));
        add(line);
        add("\r");
    }
    int available() { return static_cast<int>(len - pos); }
    int read() { return pos < len ? static_cast<unsigned char>(data[pos++]) : -1; }
    size_t unread() const { return len - pos; }
};

struct Rig {
    FakeEeprom eeprom;
    MapStore store{eeprom, defaultButtonMappings, defaultButtonMappingsCount};
    TxQueue tx;
    WcbPort wcbPort{tx};
    Protocol protocol{store, "2.0.0"};
    Runtime runtime{store, protocol, tx, "2.0.0"};
    WcbSim wcb;
    LineReader wcbLines;
    LineReader usbLines;
    FakeSerialIn wcbIn;
    FakeSerialIn usbIn;
    CapturePort usb;
    uint32_t now = 1000;
};

Rig *rig = nullptr;

const PadState kNone = {true, 0};
const uint8_t kFUp = slotIndex(Ctrl::F, Mod::NONE, Btn::UP);

PadState held(uint16_t bits) { return {true, bits}; }

// One loop() pass in main.cpp's order; returns what the mapping did besides presses.
Runtime::Work pass(PadState foot = kNone, PadState dome = kNone, bool faulted = false) {
    rig->now += 2;
    bool any = false;
    if (!faulted) {
        FiredPress fired;
        any = rig->runtime.buttons(rig->now, foot, dome, &fired);
    }
    rig->runtime.afterButtons(rig->now, any, faulted, Runtime::Link<FakeSerialIn>{rig->wcbIn, rig->wcbLines, rig->wcbPort},
                              Runtime::Link<FakeSerialIn>{rig->usbIn, rig->usbLines, rig->usb}, rig->wcb);
    rig->eeprom.tick();
    rig->wcb.refill();
    return rig->runtime.lastWork();
}

void expectWork(Runtime::Work want, Runtime::Work got) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(want), static_cast<uint8_t>(got));
}

void mapSlot(uint8_t slot, const char *key) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(rig->store.beginSet(slot, key)));
    while (rig->store.busy()) {
        rig->store.step();
        rig->eeprom.tick();
    }
    rig->store.takeCompletion();
}

// True if one of the last few lines the Pi received starts with `prefix` (CRC checked).
bool piSawRecently(const char *prefix) {
    char body[96];
    for (uint8_t i = 0; i < LineLog::kLines; ++i) {
        if (stripCrc(rig->wcb.usb.last(i), body, sizeof(body)) && strncmp(body, prefix, strlen(prefix)) == 0)
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
    mapSlot(kFUp, "fup");
}

void tearDown() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.violations, "SHADOW broke a WCB rule");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->eeprom.violations, "EEPROM touched while busy");
}

static void test_a_press_pass_reads_no_request() {
    TEST_ASSERT_FALSE(rig->store.doubleTapMapped(kFUp));
    rig->wcbIn.addLine("@SM HELLO");
    const size_t waiting = rig->wcbIn.unread();
    expectWork(Runtime::Work::Presses, pass(held(kHeldUp)));
    TEST_ASSERT_EQUAL_UINT32(waiting, rig->wcbIn.unread());  // not a byte read
    expectWork(Runtime::Work::Request, pass(held(kHeldUp)));  // held: the shared repeat timer fires it once a second
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcbIn.unread());
    for (uint8_t i = 0; i < 5; ++i)
        pass();
    TEST_ASSERT_TRUE(piSawRecently("@SM READY "));
    TEST_ASSERT_EQUAL_STRING(";W1;Cfup", rig->wcb.commands.last());
}

static void test_the_background_unit_follows_each_request() {
    rig->wcbIn.addLine("@SM HELLO");
    rig->wcbIn.addLine("@SM CFG");
    rig->wcbIn.addLine("@SM BAT");
    const Runtime::Work want[] = {Runtime::Work::Request,    Runtime::Work::Background, Runtime::Work::Request,
                                  Runtime::Work::Background, Runtime::Work::Request,    Runtime::Work::Background,
                                  Runtime::Work::Background};
    for (const Runtime::Work w : want)
        expectWork(w, pass());
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcbIn.unread());
}

static void test_a_press_doesnt_cancel_the_background_unit_it_owes() {
    rig->wcbIn.addLine("@SM HELLO");
    rig->wcbIn.addLine("@SM CFG");
    expectWork(Runtime::Work::Request, pass());
    expectWork(Runtime::Work::Presses, pass(held(kHeldUp)));
    expectWork(Runtime::Work::Background, pass());
    expectWork(Runtime::Work::Request, pass());
}

static void test_one_request_a_pass_over_both_ports() {
    rig->wcbIn.addLine("@SM HELLO");
    rig->usbIn.addLine("@SM HELLO");
    expectWork(Runtime::Work::Request, pass());
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcbIn.unread());
    TEST_ASSERT_TRUE(rig->usbIn.unread() > 0);  // the WCB went first
    expectWork(Runtime::Work::Background, pass());
    expectWork(Runtime::Work::Request, pass());
    TEST_ASSERT_EQUAL_UINT32(0, rig->usbIn.unread());
    TEST_ASSERT_EQUAL_UINT32(1, rig->usb.log.count);
}

// A DUMP keeps the queue busy while GETs come in one at a time (the Pi waits for each reply): both
// finish, and every GET is answered.
static void test_a_dump_and_gets_both_finish() {
    rig->wcbIn.addLine("@SM DUMP");
    uint8_t getsSent = 0;
    uint16_t passes = 0;
    for (; passes < 20000; ++passes) {
        pass();
        const bool getDone = rig->wcbIn.unread() == 0 && !rig->wcbLines.ready();
        if (getDone && getsSent < 5) {
            rig->wcbIn.addLine("@SM GET F NONE UP");
            ++getsSent;
        }
        if (getsSent == 5 && getDone && !rig->protocol.opInProgress() && rig->tx.used() == 0)
            break;
    }
    TEST_ASSERT_TRUE(passes < 20000);
    TEST_ASSERT_EQUAL_UINT32(kSlotCount + 1 + 5, rig->wcb.usb.count);  // 216 SLOT lines, END DUMP, 5 GETs
}

// The battery report waits for room behind a DUMP, as before, and goes out when it ends.
static void test_the_battery_report_still_goes_out_after_a_dump() {
    rig->runtime.startBattery(rig->wcbPort, rig->now);
    rig->wcbIn.addLine("@SM DUMP");
    pass();
    rig->runtime.batteries(rig->now, true, 3, true, 3);  // a change: report due
    uint16_t i = 0;
    for (; i < 20000 && rig->protocol.opInProgress(); ++i)
        pass();
    TEST_ASSERT_FALSE(rig->protocol.opInProgress());
    bool seen = false;
    for (uint8_t j = 0; j < 10 && !seen; ++j) {
        pass();
        seen = piSawRecently("@SM BAT ");
    }
    TEST_ASSERT_TRUE(seen);
}

static void test_a_faulted_pass_follows_the_rule() {
    rig->wcbIn.addLine("@SM HELLO");
    rig->wcbIn.addLine("@SM CFG");
    rig->tx.holdForCommand(true);  // left over from before the fault
    expectWork(Runtime::Work::Request, pass(kNone, kNone, true));
    TEST_ASSERT_FALSE(rig->tx.heldForCommand());
    expectWork(Runtime::Work::Background, pass(kNone, kNone, true));
    expectWork(Runtime::Work::Request, pass(kNone, kNone, true));
}

// A request that can't be answered yet (no room) still counts as the pass's request.
static void test_a_request_that_cant_be_answered_counts() {
    rig->wcb.room = 0;
    char fill[150];
    memcpy(fill, ";S0", 3);
    memset(fill + 3, 'x', sizeof(fill) - 3);
    TEST_ASSERT_TRUE(rig->tx.pushLine2(fill, sizeof(fill) - 1, nullptr, 0));
    rig->wcbIn.addLine("@SM GET F NONE UP");
    rig->now += 2;
    rig->runtime.afterButtons(rig->now, false, false,
                              Runtime::Link<FakeSerialIn>{rig->wcbIn, rig->wcbLines, rig->wcbPort},
                              Runtime::Link<FakeSerialIn>{rig->usbIn, rig->usbLines, rig->usb}, rig->wcb);
    expectWork(Runtime::Work::Request, rig->runtime.lastWork());
    TEST_ASSERT_TRUE(rig->wcbLines.ready());  // kept for a later pass
}

// Review focus 2: a GET arriving while a command waits for room is answered once it has gone.
static void test_a_request_waits_out_a_waiting_command() {
    rig->wcb.room = 0;
    char fill[152];
    memcpy(fill, ";S0", 3);
    memset(fill + 3, 'x', sizeof(fill) - 3);
    TEST_ASSERT_TRUE(rig->tx.pushLine2(fill, sizeof(fill) - 1, nullptr, 0));
    expectWork(Runtime::Work::Background, pass(held(kHeldUp)));  // fup can't fit: it waits, nothing fired
    TEST_ASSERT_TRUE(rig->tx.heldForCommand());
    rig->wcbIn.addLine("@SM GET F NONE UP");
    bool answered = false;
    for (uint16_t i = 0; i < 400 && !answered; ++i) {
        pass();
        answered = piSawRecently("@SM SLOT F NONE UP ");
    }
    TEST_ASSERT_TRUE(answered);
    TEST_ASSERT_EQUAL_STRING(";W1;Cfup", rig->wcb.commands.last());
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_a_press_pass_reads_no_request);
    RUN_TEST(test_the_background_unit_follows_each_request);
    RUN_TEST(test_a_press_doesnt_cancel_the_background_unit_it_owes);
    RUN_TEST(test_one_request_a_pass_over_both_ports);
    RUN_TEST(test_a_dump_and_gets_both_finish);
    RUN_TEST(test_the_battery_report_still_goes_out_after_a_dump);
    RUN_TEST(test_a_faulted_pass_follows_the_rule);
    RUN_TEST(test_a_request_that_cant_be_answered_counts);
    RUN_TEST(test_a_request_waits_out_a_waiting_command);
    return UNITY_END();
}
```

Notes for the implementer:
- `test_a_request_waits_out_a_waiting_command` relies on `WcbSim::refill()` (63 a pass) draining the filler after
  the first pass: `pass()` refills every pass, and `rig->wcb.room = 0` only stops the first one.
- In `test_a_press_pass_reads_no_request` the second `pass(held(kHeldUp))` must not fire again: the shared repeat
  timer allows one fire a second. If it fires, the expected work there is `Presses`; re-check `Runtime::legacy()`.
- `CapturePort::canSend` takes `kMaxLine`; a USB HELLO's READY fits.

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e native -f test_scheduler`
Expected: compile errors (`Runtime::Link`, `Runtime::Work`, `afterButtons`, `lastWork`).

- [ ] **Step 3: Implement the rule**

`sm_fakes.h`, before `class CapturePort`:

```cpp
// A serial port with nothing to read (the USB link in suites that only drive the WCB).
struct NoSerial {
    int available() { return 0; }
    int read() { return -1; }
};

// A reply port nothing is sent to (paired with NoSerial).
class NullReplyPort final : public sm::ReplyPort {
  public:
    bool canSend(uint8_t) override { return false; }
    void send(const char *, uint8_t) override {}
    bool requiresCrc() const override { return false; }
};
```

`sm_runtime.h`, in `class Runtime` public section, replace the existing `pump()` template and `service()` template
(and their comments) with:

```cpp
    // A serial port's input, its line reader and where its replies go, for afterButtons().
    template <class Serial>
    struct Link {
        Serial &port;
        LineReader &lines;
        ReplyPort &replies;
    };

    // What the last afterButtons() did besides presses.
    enum class Work : uint8_t { Presses, Request, Background };
    Work lastWork() const { return lastWork_; }

    // Reads @SM requests from `port` without blocking, at most kMaxBytesPerPass bytes, and handles at
    // most one. Returns true when it handled one: answered it, or found it can't be answered yet (reply
    // queue full, EEPROM busy), in which case it stays in `reader` for a later pass and bytes arriving
    // meanwhile are read and dropped so the UART's RX buffer can't overflow. The Pi sends one request at
    // a time and resends after a timeout.
    template <class Serial>
    bool pump(Serial &port, LineReader &reader, ReplyPort &replies) {
        uint8_t budget = kMaxBytesPerPass;
        while (true) {
            if (reader.ready()) {
                if (protocol_.handleLine(reader.line(), replies)) {
                    reader.consume();
                    return true;
                }
                while (budget > 0) {
                    uint8_t n = batch(port.available(), budget);
                    if (n == 0)
                        break;
                    budget = static_cast<uint8_t>(budget - n);
                    for (; n > 0; --n)
                        reader.discard(static_cast<char>(port.read()));
                }
                return true;
            }
            // As many bytes as are waiting (up to the budget), with one available() call: a line
            // that completes stops the batch so it's handled before the next byte is read.
            uint8_t n = batch(port.available(), budget);
            if (n == 0)
                return false;
            for (; n > 0; --n) {
                --budget;
                if (reader.feed(static_cast<char>(port.read())))
                    break;
            }
        }
    }

    // The background unit: protocol work (a DUMP line, a SUM step, a write's reply, a CTL event), the
    // announce (not while `mayAnnounce` is false: a controller fault; a due announce then waits for the
    // fault to clear), and the battery report (which carries on through a fault). Each waits for room.
    void background(uint32_t nowMs, bool mayAnnounce);

    // Every pass, faults included: the EEPROM writer's step, then queued bytes to the WCB's UART.
    template <class Out>
    void finishPass(Out &wcb) {
        store_.step();
        tx_.drain(wcb);
    }

    // background() and finishPass() in one call, with no pass rule: for tests and tools that drive one
    // thing at a time. loop() uses afterButtons().
    template <class Out>
    void service(uint32_t nowMs, Out &wcb, bool mayAnnounce = true) {
        background(nowMs, mayAnnounce);
        finishPass(wcb);
    }

    // The rest of the mapping's pass, once per loop() pass after buttons() (faults included, with
    // `fired` false: buttons() isn't called then). WIPDocs/TIMING_ROUND_2_DESIGN.md, section 3:
    //   - a pass that fired a press reads no request and runs no background unit;
    //   - the first pass without a press after one that handled a request runs the background unit;
    //   - any other pass handles at most one request (WCB first, then USB) and, if it handled none,
    //     runs the background unit;
    // then finishPass() into `wcbOut`, every pass. No unit waits more than one pass for another.
    template <class WcbIn, class UsbIn, class Out>
    void afterButtons(uint32_t nowMs, bool fired, bool faulted, Link<WcbIn> wcb, Link<UsbIn> usb, Out &wcbOut) {
        if (faulted)
            faultPass();
        if (fired) {
            lastWork_ = Work::Presses;
        } else if (backgroundOwed_) {
            background(nowMs, !faulted);
            backgroundOwed_ = false;
            lastWork_ = Work::Background;
        } else if (pump(wcb.port, wcb.lines, wcb.replies) || pump(usb.port, usb.lines, usb.replies)) {
            backgroundOwed_ = true;
            lastWork_ = Work::Request;
        } else {
            background(nowMs, !faulted);
            lastWork_ = Work::Background;
        }
        finishPass(wcbOut);
    }
```

and in the private section, after `uint16_t droppedPresses_ = 0;`:

```cpp
    bool backgroundOwed_ = false;  // a request was handled; the next pass without a press is the background unit's
    Work lastWork_ = Work::Background;
```

`sm_runtime.cpp`, after `Runtime::fire()`:

```cpp
void Runtime::background(uint32_t nowMs, bool mayAnnounce) {
    protocol_.service();
    if (mayAnnounce)
        announce(nowMs);
    reportBattery(nowMs);
}
```

- [ ] **Step 4: Run the scheduler suite**

Run: `pio test -e native -f test_scheduler`
Expected: PASS.

- [ ] **Step 5: Move the test rigs to the rule**

In `test/test_runtime/test_runtime.cpp`, replace `pass()` with:

```cpp
// One loop() pass at time `t` in main.cpp's order: buttons, then the rest of the mapping's pass.
bool pass(uint32_t t, PadState foot, PadState dome = kOff) {
    rig->now = t;
    const bool fired = rig->runtime.buttons(t, foot, dome, &rig->fired);
    rig->runtime.afterButtons(t, fired, false, Runtime::Link<FakeSerialIn>{rig->in, rig->reader, rig->wcbPort},
                              Runtime::Link<NoSerial>{noUsb, noUsbLines, noUsbReplies}, rig->wcb);
    rig->eeprom.tick();
    rig->wcb.refill();
    return fired;
}
```

with, above it in the anonymous namespace:

```cpp
NoSerial noUsb;
LineReader noUsbLines;
NullReplyPort noUsbReplies;
```

Do the same in `test/test_double_tap/test_double_tap.cpp` and `test/test_long_press/test_long_press.cpp`: their
`NoInput` struct is `NoSerial` now (delete it and use `NoSerial`; `rig->in` becomes a `NoSerial`), and their
`pass()` becomes:

```cpp
bool pass(uint32_t t, PadState foot, PadState dome = kOff) {
    const bool fired = rig->runtime.buttons(t, foot, dome, &rig->fired);
    rig->runtime.afterButtons(t, fired, false, Runtime::Link<NoSerial>{rig->in, rig->reader, rig->wcbPort},
                              Runtime::Link<NoSerial>{noUsb, noUsbLines, noUsbReplies}, rig->wcb);
    rig->eeprom.tick();
    rig->wcb.refill();
    return fired;
}
```

Leave `test_battery`, `test_avr` and the timing suites on `service()` (unscheduled) for now; Task 6 moves the
timing harness.

- [ ] **Step 6: Run everything and fix one-pass shifts only**

Run: `pio test -e native`
Expected: all PASS, or failures where a reply or event now arrives one pass later (a request after a press, a
reply after a request). For each failing assertion:
1. Confirm from the test's timeline that the difference is one of the spec's section 3 cases (a request in a press
   pass, a second request, a background line after a request, READY's `pads=`).
2. Fix the test by giving it the extra pass (a loop bound, a `pass()` call), never by changing what it expects
   to be sent.
3. Note each such change in the commit message.
Any other failure is a bug in Steps 3-5: fix the code.

- [ ] **Step 7: The same on simavr**

Run: `pio test -e simavr --without-uploading -f test_scheduler -f test_runtime -f test_double_tap -f test_long_press -f test_defer`
Expected: PASS (if `test_scheduler` hangs, it ran out of SRAM: move the DUMP and battery tests to
`test/test_scheduler_dump/`).

- [ ] **Step 8: Commit**

```bash
git add lib/ShadowMap/src/sm_runtime.h lib/ShadowMap/src/sm_runtime.cpp test/support/sm_fakes.h \
  test/test_scheduler test/test_runtime test/test_double_tap test/test_long_press
git commit -m "Runtime: the pass rule (afterButtons): a press pass does no other mapping work

A pass that fired a press reads no request and runs no background unit; a request pass is
followed by a background pass; otherwise one request or the background unit. pump() handles
at most one request.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `loop()`, the simulator and the transcripts

**Files:**
- Modify: `src/main.cpp` (`mappedButtons`, `serviceMapping`, `loop`), `tools/shadowmap/shadow_sim.cpp` (`pass`),
  `tools/shadowmap/test_transcripts.py`

**Interfaces:**
- Consumes: `Runtime::afterButtons`, `Runtime::Link` (Task 4).

- [ ] **Step 1: `main.cpp`**

After `UsbReplyPort usbReplies;` add:

```cpp
// The two ports @SM requests come in on, for mapRuntime.afterButtons().
sm::Runtime::Link<sm::Uart1> wcbLink{wcbUart, wcbLines, wcbReplies};
sm::Runtime::Link<HardwareSerial> usbLink{Serial, usbLines, usbReplies};
```

`mappedButtons()` returns whether a press fired:

```cpp
// Button combos to WCB stored commands (see sm::Runtime for the repeat and double-tap timing).
// Returns true when a press fired this pass.
bool mappedButtons() {
    sm::FiredPress fired;
    if (!mapRuntime.buttons(millis(), padState(PS3NavFoot), padState(PS3NavDome), &fired))
        return false;

#ifdef SHADOW_VERBOSE
    ... (unchanged, minus the two lines Task 3 removed) ...
#endif
    return true;
}
```

Keep its forward declaration (if any) in step: `grep -n "mappedButtons" src/main.cpp include/*.h` and change
`void` to `bool` everywhere it is declared.

`serviceMapping()`:

```cpp
// Everything the mapping does besides button presses, once per pass after the motors (faults
// included): batteries, then one request or the background unit (sm::Runtime::afterButtons), the
// EEPROM step and the drain. SHADOW doesn't announce itself to the WCB while a controller fault is
// active.
void serviceMapping(bool fired, bool faulted) {
    watchBatteries();
    mapRuntime.afterButtons(millis(), fired, faulted, wcbLink, usbLink, wcbUart);
}
```

`loop()`: delete

```cpp
    // Mapping edits from DroidNet (via the WCB) or the USB port. Never blocks.
    mapRuntime.pump(wcbUart, wcbLines, wcbReplies);
    mapRuntime.pump(Serial, usbLines, usbReplies);
```

and change the rest to:

```cpp
    if (!readUSB()) {
        // We have a fault condition that we want to ensure that we do NOT process any controller data!!!
        serviceMapping(false, true);
        printOutput();
        return;
    }

    footMotorDrive();
    domeDrive();
    const bool fired = mappedButtons();
    toggleSettings();
    // Mapping edits from DroidNet (via the WCB) or the USB port, after the motors. Never blocks.
    serviceMapping(fired, false);
    printOutput();
```

- [ ] **Step 2: Build**

Run: `pio run -e SHADOWC2B5`
Expected: SUCCESS. Note the RAM line for Task 7.
Run: `PLATFORMIO_BUILD_FLAGS=-DSHADOW_VERBOSE pio run -e SHADOWC2B5` then `pio run -e SHADOWC2B5`
Expected: both SUCCESS.

- [ ] **Step 3: The simulator follows `loop()`**

In `tools/shadowmap/shadow_sim.cpp`, above `struct Shadow`, add:

```cpp
// The simulator has no USB port: a link with nothing to read.
struct NoInput {
    int available() { return 0; }
    int read() { return -1; }
};
class NullReplies final : public sm::ReplyPort {
  public:
    bool canSend(uint8_t) override { return false; }
    void send(const char *, uint8_t) override {}
    bool requiresCrc() const override { return false; }
};
NoInput noUsb;
sm::LineReader noUsbLines;
NullReplies noUsbReplies;
```

and replace `pass()` with:

```cpp
void pass(Shadow &s, FileEeprom &eeprom, StdoutUart &uart, InputQueue &in, sm::PadState foot, sm::PadState dome) {
    in.arrive();
    sm::FiredPress fired;
    const bool any = s.runtime.buttons(now, foot, dome, &fired);  // main.cpp's order: presses, then serviceMapping()
    s.runtime.batteries(now, foot.connected, footBattery, dome.connected, domeBattery);
    s.runtime.afterButtons(now, any, false, sm::Runtime::Link<InputQueue>{in, s.reader, s.port},
                           sm::Runtime::Link<NoInput>{noUsb, noUsbLines, noUsbReplies}, uart);
    eeprom.tick();
    uart.refill();
    now += kPassMs;
}
```

- [ ] **Step 4: Run the transcripts and look at every difference**

Run: `python3 -m unittest tools/shadowmap/test_transcripts.py`
Expected: FAIL in `test_byte_identical` (the old byte-for-byte test can't hold under the rule).

Replace `tools/shadowmap/test_transcripts.py` with Part B's per-stream version as the starting point:

```bash
git show origin/timing/proto:tools/shadowmap/test_transcripts.py > tools/shadowmap/test_transcripts.py
```

Then edit it for this design:
1. The module docstring: replace the per-pass rule description with this round's (WIPDocs/TIMING_ROUND_2_DESIGN.md
   section 3: a press pass reads no request and runs no background unit; a request pass is followed by a background
   pass; work that gives way waits one pass). Keep the list of what is checked.
2. Delete the `COINCIDE` patches (they are Part B's) and set `COINCIDE = {}`.
3. In `test_round_trips`, keep "a GET's round trip is at most two passes longer" and "a SUM stays under 0.5 s".

Run it again: `python3 -m unittest tools/shadowmap/test_transcripts.py -v`
For each failing subtest, print the two streams (add a temporary `print` of `g` and `n`, or run
`python3 -c "import sys; sys.path.insert(0,'tools/shadowmap'); import test_transcripts as t; ..."`) and classify
every difference. Each one must be one of:
- a press's command or PRESS event now before a reply, where a request landed in the press's pass (`coincide`);
- a SET in a press's pass giving that press the old key (`coincide`);
- a PRESS event that `0a8b73f` dropped for want of room now sent, or the reverse, where a request met a press
  (`coincide`);
- READY's `pads=` in the first pass after a boot (already handled by `normalize`).
Encode each as a `COINCIDE` patch with a comment naming the script line it comes from, as Part B's did. **Any other
difference is a bug: stop and fix the code** (likely Task 4's rule or Task 3's waiting list), don't patch it.

- [ ] **Step 5: Run the Python suites**

Run: `python3 -m unittest tools/shadowmap/test_shadowmap.py tools/shadowmap/test_transcripts.py`
Expected: OK.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp tools/shadowmap/shadow_sim.cpp tools/shadowmap/test_transcripts.py
git commit -m "loop(): requests after the motors, through the pass rule; transcripts per stream

The two pump() calls at the top of loop() ran before readUSB() and the motors; the mapping's
whole pass now runs after them (serviceMapping). The simulator follows. test_transcripts
compares each stream with 0a8b73f and names every difference (all where a request met a press).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Timing harness in the new order; every pass under 1 ms

**Files:**
- Modify: `test/support/sm_avr_loop.h`, `test/test_avr_timing_fires/test_avr_timing_fires.cpp`, the other
  `test/test_avr_timing_*` suites where they name `PassParts` fields or assume the old order, `platformio.ini`

**Interfaces:**
- Consumes: `Runtime::afterButtons`, `Runtime::Link`, `Runtime::deferredCount`, `Runtime::firesLastPass`.

- [ ] **Step 1: `loopPass()` in the new order**

In `test/support/sm_avr_loop.h` replace `struct PassParts` and `loopPass()` with:

```cpp
struct PassParts {
    uint16_t buttons, batteries, after;
    uint16_t total() const { return static_cast<uint16_t>(buttons + batteries + after); }
};
```

```cpp
uint16_t loopPass(uint32_t now, const PadState &foot, const PadState &dome, uint8_t footBatt = 0, uint8_t domeBatt = 0,
                  bool mayAnnounce = true) {
    SerialPoke::dropTx1();
    io.tick();
    gFinishing = store.finishDue();
    gLast.buttons = ticksOf([&] { gAny = runtime.buttons(now, foot, dome, &gFired); });
    gLast.batteries = ticksOf([&] { runtime.batteries(now, foot.connected, footBatt, dome.connected, domeBatt); });
    gLast.after = ticksOf([&] {
        runtime.afterButtons(now, gAny, !mayAnnounce, Runtime::Link<Uart1>{SMT_WCB_UART, wcbLines, port},
                             Runtime::Link<HardwareSerial>{Serial, usbLines, usbPort}, SMT_WCB_UART);
    });
    return gLast.total();
}
```

and in `recordPass()`:

```cpp
    say(PSTR("  parts: buttons %u.%u, batteries %u.%u, after %u.%u us (%u passes)"), p.buttons / 2,
        (p.buttons & 1) * 5, p.batteries / 2, (p.batteries & 1) * 5, p.after / 2, (p.after & 1) * 5, w.passes);
```

Update the comment above `PassParts` ("One loop() pass of the mapping, in main.cpp's order ...") to name the new
order. Then `grep -n "wcbPump\|usbPump\|\.service\b\|parts\." test/test_avr_timing_*/*.cpp` and update each use.

- [ ] **Step 2: Four presses due at once**

In `test/test_avr_timing_fires/test_avr_timing_fires.cpp`, in `manyPresses()`, replace

```cpp
    WorstPass w;
    loopPass(p, kFootRight, kDomeCircleDownHeld);
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);  // the last of them
    TEST_ASSERT_TRUE(gFinishing == finishing);
    TEST_ASSERT_FALSE(store.finishDue());
    w.add(gLast);
```

with

```cpp
    WorstPass w;
    loopPass(p, kFootRight, kDomeCircleDownHeld);
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_TRUE(gFinishing == finishing);
    TEST_ASSERT_FALSE(store.finishDue());
    w.add(gLast);
    if (fires > Runtime::kMaxFiresPerPass) {
        // Three fire; the long press (the last of them) waits one pass in order.
        TEST_ASSERT_EQUAL_UINT8(Runtime::kMaxFiresPerPass, runtime.firesLastPass());
        TEST_ASSERT_EQUAL_UINT8(1, runtime.deferredCount());
        loopPass(p + 2, kFootRight, kDomeCircleDownHeld);
        TEST_ASSERT_TRUE(gAny);
        TEST_ASSERT_EQUAL_UINT8(0, runtime.deferredCount());
        w.add(gLast);
    } else {
        TEST_ASSERT_EQUAL_UINT8(fires, runtime.firesLastPass());
    }
    TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);  // the last of them
```

and adjust the following `gNow = p; loopPass(gNow += 2, ...)` to start at `p + 2`. Rename the four-press figure
`"four presses due in one pass (three fire, one waits a pass)"`.

- [ ] **Step 3: Presses waiting for room**

Add to `test_avr_timing_fires.cpp` (and its `RUN_TEST`, plus the `TEST_IGNORE` stub in the `#else` branch):

```cpp
// Presses that find the TX queue full wait in order and go as the drain frees room: every pass from
// the one they come due in until the last has fired.
static void test_presses_waiting_for_room() {
    worstState();
    settle(gNow);
    char fill[140];
    memcpy(fill, ";S0", 3);
    memset(fill + 3, 'x', sizeof(fill) - 3);
    TEST_ASSERT_TRUE(tx.pushLine2(fill, sizeof(fill) - 1, nullptr, 0));  // 20 bytes free: no command fits
    gNow += 1100;
    WorstPass w;
    loopPass(gNow, kFootCircle, kDomeRight);  // D CIRCLE RIGHT (from=F): no room, it waits
    TEST_ASSERT_FALSE(gAny);
    TEST_ASSERT_EQUAL_UINT8(1, runtime.deferredCount());
    w.add(gLast);
    for (uint8_t i = 0; i < 10 && runtime.deferredCount() > 0; ++i) {
        loopPass(gNow += 2, kFootCircle, kDomeRight);
        w.add(gLast);
    }
    TEST_ASSERT_EQUAL_UINT8(0, runtime.deferredCount());
    settle(gNow);
    recordPass(PSTR("a press waiting for room, every pass until it fires"), w);
}
```

- [ ] **Step 4: Turn the 1 ms limit on**

In `platformio.ini`, `[env:simavr]`, delete the line `    -D SM_TIMING_REPORT_ONLY` and change the comment
above `build_flags` to:

```ini
; The timing suites (test_avr_timing_*) fail when a mapping pass reaches 1 ms
; (WIPDocs/TIMING_ROUND_2_DESIGN.md); with SM_TIMING_TARGET also when a pass misses its
; T1/T2 target (test/support/sm_avr_timing.h). SM_TIMING_REPORT_ONLY only prints the figures.
```

- [ ] **Step 5: Run the timing suites**

Run: `pio test -e simavr -f "test_avr_timing*" --without-uploading -v > /tmp/timing.log 2>&1; grep -E "OVER 1 ms|figures, |test cases" /tmp/timing.log`
(use the session scratchpad instead of `/tmp` if one is set)
Expected: every suite PASS; each `figures,` line says `0 at or over 1 ms`.

If a figure reaches 1 ms:
- If it is a press-only pass (no request, no background unit: its `after` part is only the EEPROM step and drain),
  do Task 6a (the spec's section 6) and come back.
- Otherwise read its `parts:` line: a pass that fired a press must show `after` as only the EEPROM step and the
  drain (under about 120 us). If it shows a request or a background unit, the rule has a hole: fix Task 4's code.

- [ ] **Step 6: Run the whole simavr suite**

Run: `pio test -e simavr --without-uploading`
Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add test/support/sm_avr_loop.h test/test_avr_timing_* platformio.ini
git commit -m "Timing: the harness follows loop(); every pass under 1 ms, enforced

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6a (only if Task 6 Step 5 finds a press-only pass at 1 ms): PRESS events leave the press pass

**Files:**
- Modify: `lib/ShadowMap/src/sm_protocol.h/.cpp` (`notifyPress` records; `service()` sends one recorded event first),
  `lib/ShadowMap/src/sm_runtime.cpp` (none expected)
- Test: `test/test_scheduler/test_scheduler.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// With EVENTS on, a press pass queues only the command; its PRESS event goes out in a later
// background pass, in order.
static void test_press_events_leave_the_press_pass() {
    rig->wcbIn.addLine("@SM EVENTS 1");
    for (uint8_t i = 0; i < 4; ++i)
        pass();
    const uint8_t before = rig->tx.used();
    pass(held(kHeldUp));
    TEST_ASSERT_EQUAL_UINT8(before + strlen(";W1;Cfup") + 1, rig->tx.used());  // the command only
    bool seen = false;
    for (uint8_t i = 0; i < 10 && !seen; ++i) {
        pass();
        seen = piSawRecently("@SM PRESS F NONE UP fup eeprom");
    }
    TEST_ASSERT_TRUE(seen);
}
```

(`before` must be 0 for the arithmetic: if a background line is still queued after the four passes, run passes
until `rig->tx.used() == 0` first.)

- [ ] **Step 2: Implement**

In `Protocol`, add a 4-entry ring of recorded events (`struct PendingPress { uint8_t slot; char key[kKeyBuf]; Source
src; bool modFromOther; }`, 19 bytes each, 76 bytes; check the RAM budget in Task 7 and say so if it goes over 64
bytes in all). `notifyPress()` appends (dropping the oldest when full, as it drops today when there's no room) and
returns. `service()` first sends the oldest recorded event if the event port has room (the code `notifyPress()` has
today, moved into a `sendPress()`), and returns after it; otherwise it carries on as now.

- [ ] **Step 3: Run Task 4's and Task 6's suites again, then the transcripts**

Run: `pio test -e native` and `pio test -e simavr --without-uploading`, then
`python3 -m unittest tools/shadowmap/test_transcripts.py`. New transcript differences must be PRESS events one
pass later (same order within the events stream): if the streams compare equal, nothing to add.

- [ ] **Step 4: Commit**

```bash
git commit -am "Protocol: PRESS events go out from the background unit, one a pass

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Docs, RAM check, tag

**Files:**
- Modify: `WIPDocs/COMMAND_MAPPING_DESIGN.md` (Timing and memory, Testing), `CLAUDE.md`,
  `WIPDocs/TIMING_ROUND_2_DESIGN.md` (status)

- [ ] **Step 1: Collect the figures**

Run: `pio test -e simavr -f "test_avr_timing*" --without-uploading -v > <scratch>/timing.log 2>&1`, then
`grep -o 'TIMING .*' <scratch>/timing.log | sort -k2 -n -r | head -15` and the `figures,` lines.

- [ ] **Step 2: `COMMAND_MAPPING_DESIGN.md`, Timing and memory**

Replace the **Loop time** bullet's Part A text (from "Worst-case time the mapping adds" through "remove it once
every pass is under 1 ms.") with:
- the pass rule in four lines (as the spec's section 3), naming `Runtime::afterButtons`;
- the waiting presses (FIFO of 8, three a pass, the hold) in two lines;
- the worst figures from Step 1 (the top five, and the worst press-only pass), and "every figure under 1 ms,
  enforced by `test_avr_timing_*`";
- one line: "Before round 2 (Part A, `52df1b9`): 5 of 126 figures at or over 1 ms, worst 1.115 ms; at `0a8b73f`, 61."

Replace "A request that can't be answered yet stays queued, and bytes arriving meanwhile are read and dropped so
Serial1's 64-byte RX buffer can't overflow." with the same sentence plus "It counts as its pass's request."

Update **Memory** with the `pio run -e SHADOWC2B5` RAM and Flash lines, and the growth over 2895.

Update **Testing**: the counts from `pio test -e native` and `pio test -e simavr --without-uploading` summaries, and
add `test_defer` and `test_scheduler` to "What's covered".

- [ ] **Step 3: `CLAUDE.md`**

In **Architecture**, `src/main.cpp` bullet: add "requests are handled after the motors, through
`sm::Runtime::afterButtons()` (one request or one background unit a pass, none in a pass that fired a press)".
In the paragraph about simavr (under the commands), replace "The timing suites (`test_avr_timing_*`) only report for
now" with "The timing suites (`test_avr_timing_*`) fail if a mapping pass reaches 1 ms". In
**Critical Development Notes**, add: "5. **`SHADOW_VERBOSE` stays off** on the droid: `printOutput()` blocks the loop
while the USB port sends (several ms a pass when driving). Turn it on only for a bench session."

- [ ] **Step 4: Spec status**

In `WIPDocs/TIMING_ROUND_2_DESIGN.md`, change the **Status** line to
"implemented on `main` (tag `stage/timing-round-2`), 2026-10-02 or the actual date; not flashed."

- [ ] **Step 5: Check RAM (review focus 5)**

Run: `pio run -e SHADOWC2B5 | grep RAM`
Expected: used bytes ≤ 2895 + 64 = 2959. If over, report it in the summary with the figure (don't trim silently).

- [ ] **Step 6: Full verification**

Run, one at a time:

```bash
pio run -e SHADOWC2B5
pio test -e native
pio test -e simavr --without-uploading
python3 -m unittest tools/shadowmap/test_shadowmap.py tools/shadowmap/test_transcripts.py
```

Expected: all SUCCESS / PASS / OK.

- [ ] **Step 7: Commit, tag, push**

```bash
git add WIPDocs/COMMAND_MAPPING_DESIGN.md CLAUDE.md WIPDocs/TIMING_ROUND_2_DESIGN.md
git commit -m "Docs: timing round 2 (the pass rule, waiting presses, figures, memory)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag -a stage/timing-round-2 -m "Timing round 2: every mapping pass under 1 ms, motors first, no dropped commands"
git push origin main stage/timing-round-2
```
