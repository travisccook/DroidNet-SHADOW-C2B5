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
// Times are shifted past the legacy repeat timer's first second (it starts at 0, so a first press
// inside the first 1000 ms would reset it on the next pass and count as a second press).
constexpr uint32_t kEpoch = 5000;

bool pass(uint32_t t, PadState foot, PadState dome) {
    t += kEpoch;
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

// A press that waited for the busy EEPROM sends its PRESS event when it fires.
static void test_a_deferred_press_sends_its_event() {
    mapSlot(kFUp, "fup");
    mapSlot(doubleOf(kFUp), "fup2");
    char line[48], req[64];
    withCrc("@SM EVENTS 1", req, sizeof(req));
    TEST_ASSERT_TRUE(rig->protocol.handleLine(req, rig->wcbPort));
    uint32_t t = 0;
    drainOut(t);
    rig->wcb.usb.count = 0;
    pass(1000, held(kHeldUp), kOff);
    pass(1002, kNone, kNone);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(rig->store.beginSet(kFDown, "busy")));
    rig->store.step();
    rig->eepromFrozen = true;
    pass(1000 + Runtime::kDoubleTapMs, kNone, kNone);
    TEST_ASSERT_EQUAL_UINT8(1, rig->runtime.deferredCount());
    rig->eepromFrozen = false;
    t = 1000 + Runtime::kDoubleTapMs;
    for (uint8_t i = 0; i < 20 && rig->runtime.deferredCount() > 0; ++i)
        pass(t += 2, kNone, kNone);
    drainOut(t);
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.commands.count);
    bool found = false;
    for (uint8_t i = 0; i < rig->wcb.usb.count; ++i) {
        const char *l = rig->wcb.usb.last(static_cast<uint8_t>(rig->wcb.usb.count - 1 - i));
        if (stripCrc(l, line, sizeof(line)) && strncmp(line, "@SM PRESS", 9) == 0 && strstr(line, "fup") != nullptr)
            found = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "no PRESS event for the deferred press");
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
    RUN_TEST(test_a_deferred_press_sends_its_event);
    return UNITY_END();
}
