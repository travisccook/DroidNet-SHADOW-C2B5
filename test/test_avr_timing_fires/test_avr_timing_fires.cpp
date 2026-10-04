// Passes where more than one heavy thing can't be moved to another pass (see
// test/support/sm_avr_timing.h for the classes): a press fired as the EEPROM writer finishes a
// job (T2), two presses in one pass (T2), and three or four due in one pass (T3: two fire, the rest
// wait a pass). Presses can't wait for a later pass (their timing rules,
// WIPDocs/COMMAND_MAPPING_DESIGN.md, are fixed) except that at most Runtime::kMaxFiresPerPass (2) fire
// in one pass and a press that finds no room in the TX queue waits: those wait one pass, in order
// (WIPDocs/TIMING_ROUND_2_DESIGN.md, section 4). AVR only.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#include <sm_test_main.h>
#include <sm_avr_timing.h>
#include <sm_avr_loop.h>

#if defined(__AVR__)

using namespace sm;
using namespace smt;

namespace {

uint32_t gNow = 2000000UL;

constexpr uint8_t kDomeCircleUp = slotIndex(Ctrl::D, Mod::CIRCLE, Btn::UP);
constexpr uint8_t kDomeCircleDown = slotIndex(Ctrl::D, Mod::CIRCLE, Btn::DOWN);
constexpr PadState kDomeCircleRight = {true, kHeldCircle | kHeldRight};
constexpr PadState kDomeCircleUpHeld = {true, kHeldCircle | kHeldUp};
constexpr PadState kDomeCircleDownHeld = {true, kHeldCircle | kHeldDown};

// `body` arrives, and every pass of its write runs with nothing held but the one where the writer
// is due to finish its job: there a single press fires (D CIRCLE RIGHT from=F, stale record).
WorstPass pressAsItFinishes(PGM_P body, uint8_t *finishes) {
    WorstPass w;
    *finishes = 0;
    injectLine(body);
    for (uint16_t i = 0; i < 600; ++i) {
        if (!wcbLines.ready() && Serial1.available() == 0 && !store.busy() && !protocol.opInProgress() &&
            tx.used() == 0)
            break;
        if (store.finishDue() && io.ready()) {
            gNow += 1100;  // past the shared one-second repeat timer
            loopPass(gNow, kFootCircle, kDomeRight);
            TEST_ASSERT_TRUE(gAny);
            TEST_ASSERT_TRUE(gFinishing);
            w.add(gLast);
            ++*finishes;
        } else {
            gNow += 2;
            loopPass(gNow, kNone, kNone);
        }
    }
    settle(gNow);
    return w;
}

}  // namespace

static void test_press_as_a_write_finishes() {
    timerBegin();
    Serial1.begin(9600);
    worstState();
    io.busyPasses = 2;
    settle(gNow);
    uint8_t finishes = 0;
    WorstPass w = pressAsItFinishes(PSTR("@SM SET D L2 LEFTL zyxwvutsrqponml"), &finishes);
    TEST_ASSERT_EQUAL_UINT8(1, finishes);
    recordPass(PSTR("press + SET's last step (long-press slot: RAM bit reload)"), w, kT2);

    w = pressAsItFinishes(PSTR("@SM DEFAULTS"), &finishes);
    TEST_ASSERT_EQUAL_UINT8(1, finishes);
    TEST_ASSERT_EQUAL_UINT16(65535, store.generation());
    recordPass(PSTR("press + DEFAULTS' last step (every RAM bit reloaded)"), w, kT2);

    eeErase();
    store.begin();
    w = pressAsItFinishes(PSTR("@SM SET D L2 LEFTL zyxwvutsrqponml"), &finishes);
    TEST_ASSERT_EQUAL_UINT8(2, finishes);  // the header, then the slot
    recordPass(PSTR("press + a first write's last steps (format, then the slot)"), w, kT2);
}

static void test_two_presses() {
    // A press deferred while the EEPROM was busy fires with a long press due in the same pass.
    worstState();
    settle(gNow);
    gNow += 1100;
    const uint32_t start = gNow;
    loopPass(gNow, kFootRight, kDomeCircle);  // the long press starts waiting
    for (gNow += 2; gNow < start + Runtime::kLongPressMs - 2; gNow += 2)
        loopPass(gNow, kFootRight, kDomeCircle);
    const PadState footBoth = {true, kHeldRight | kHeldCircle};
    io.holdBusy(2);  // busy through the next pass: the dome's press is deferred
    loopPass(gNow, footBoth, kDomeRight);
    TEST_ASSERT_FALSE(gAny);
    gNow = start + Runtime::kLongPressMs;
    WorstPass w;
    loopPass(gNow, footBoth, kDomeRight);
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);
    w.add(gLast);
    settle(gNow);
    recordPass(PSTR("two presses: a deferred press + a long press"), w, kT2);

    // The dome's single press and the foot's long press in one pass.
    worstState();
    settle(gNow);
    gNow += 1100;
    const uint32_t start2 = gNow;
    loopPass(gNow, kFootRight, kDomeCircle);
    for (gNow += 2; gNow < start2 + Runtime::kLongPressMs; gNow += 2)
        loopPass(gNow, kFootRight, kDomeCircle);
    w = WorstPass();
    gNow = start2 + Runtime::kLongPressMs;
    loopPass(gNow, kFootRight, kDomeCircleRight);  // D CIRCLE RIGHT fires; F CIRCLE RIGHTL comes due
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);
    w.add(gLast);
    settle(gNow);
    recordPass(PSTR("two presses: the dome's single + the foot's long press"), w, kT2);
}

namespace {

// Three or four presses due in one pass P (Runtime::kMaxFiresPerPass, two, fire in P; the rest wait
// for P + 2, in order, and the figure is the worse of the two passes):
//   - D CIRCLE RIGHT, its double tap mapped, tapped once: its window runs out in the pass before P,
//     with the EEPROM busy, so it's deferred and fires in P;
//   - D CIRCLE UP, its double tap mapped, pressed in the pass before P and turned into D CIRCLE
//     DOWN in P: another combo, so D CIRCLE UP fires as a single press in P;
//   - D CIRCLE DOWN: with its double tap mapped it waits; without, it fires at once;
//   - F CIRCLE RIGHT (CIRCLE from the dome), held since P - 600 ms: its long press fires in P.
// With `finishing`, a SET's verify-and-finish step is due in P too, and there is no deferred press:
// a deferred press always skips the writer's next step (MapStore::readWanted_), so the two can't
// meet. Three presses is then the most that can come due in one pass with the writer finishing: the
// shared repeat timer lets one combo fire at once a second, and a controller fires at most one press
// besides that.
WorstPass manyPresses(uint8_t fires, bool finishing) {
    const bool deferred = !finishing;
    const bool downFires = fires == 4 || finishing;
    io.busyPasses = 0;
    io.corrupt = false;
    eeWorst();
    eeSlot(doubleOf(kWideSingle), kGen, kKey15);
    eeSlot(doubleOf(kDomeCircleUp), kGen, kKey15);
    eeSlot(kDomeCircleUp, kGen - 1, kKey15);
    eeSlot(kDomeCircleDown, kGen - 1, kKey15);
    if (!downFires)
        eeSlot(doubleOf(kDomeCircleDown), kGen, kKey15);
    store.begin();
    freshRuntime();
    request(PSTR("@SM HELLO 20"));
    request(PSTR("@SM EVENTS 1"));
    SerialPoke::clearRx(Serial1);
    settle(gNow);
    TEST_ASSERT_TRUE(store.doubleTapMapped(kWideSingle));
    TEST_ASSERT_TRUE(store.doubleTapMapped(kDomeCircleUp));
    TEST_ASSERT_TRUE(store.doubleTapMapped(kDomeCircleDown) != downFires);

    // With `finishing` (the worst figure), both controllers' battery levels also change in P: CRIT is
    // first seen BatteryWatch::kFirstMs before P, so the settled level changes in P and not earlier.
    const uint8_t batt = finishing ? 1 : 0;
    if (finishing) {
        loopPass(gNow += 2, kNone, kNone, batt, batt);
        gNow += BatteryWatch::kFirstMs - Runtime::kLongPressMs;
        TEST_ASSERT_TRUE(runtime.footBattery().level() != BattLevel::Crit);
    } else {
        gNow += 1100;
    }
    const uint32_t a = gNow;
    const uint32_t p = a + Runtime::kLongPressMs;
    const uint32_t tap = p - 2 - Runtime::kDoubleTapMs;  // D CIRCLE RIGHT's window runs out at p - 2
    loopPass(a, kFootRight, kDomeCircle, batt, batt);                 // F CIRCLE RIGHT starts waiting for its long press
    for (gNow = a + 2; gNow < p - 2; gNow += 2) {
        if (deferred && gNow == tap)
            loopPass(gNow, kFootRight, kDomeCircleRight, batt, batt);  // the tap
        else
            loopPass(gNow, kFootRight, kDomeCircle, batt, batt);
        TEST_ASSERT_FALSE(gAny);
    }
    if (deferred)
        io.holdBusy(2);  // busy through the pass before P
    loopPass(p - 2, kFootRight, kDomeCircleUpHeld, batt, batt);  // D CIRCLE RIGHT deferred; D CIRCLE UP waits
    TEST_ASSERT_FALSE(gAny);
    if (finishing) {
        // A SET brought to its verify-and-finish step for P.
        TEST_ASSERT_TRUE(store.beginSet(slotIndex(Ctrl::D, Mod::L2, Btn::LEFT, Tap::LONG), kKey15) ==
                         MapStore::Result::Ok);
        for (uint8_t i = 0; i < 40 && !store.finishDue(); ++i)
            store.step();
        TEST_ASSERT_TRUE(store.finishDue());
    }
    WorstPass w;
    loopPass(p, kFootRight, kDomeCircleDownHeld, batt, batt);
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_TRUE(gFinishing == finishing);
    if (finishing) {
        TEST_ASSERT_TRUE(runtime.footBattery().level() == BattLevel::Crit);  // the change landed in P
        TEST_ASSERT_TRUE(runtime.domeBattery().level() == BattLevel::Crit);
    }
    TEST_ASSERT_FALSE(store.finishDue());
    w.add(gLast);
    if (fires > Runtime::kMaxFiresPerPass) {
        // Two fire; the rest (the long press last) wait one pass, in order, and all fire in it.
        const uint8_t rest = static_cast<uint8_t>(fires - Runtime::kMaxFiresPerPass);
        TEST_ASSERT_TRUE(rest <= Runtime::kMaxFiresPerPass);
        TEST_ASSERT_EQUAL_UINT8(Runtime::kMaxFiresPerPass, runtime.firesLastPass());
        TEST_ASSERT_EQUAL_UINT8(rest, runtime.deferredCount());
        loopPass(p + 2, kFootRight, kDomeCircleDownHeld, batt, batt);
        TEST_ASSERT_TRUE(gAny);
        TEST_ASSERT_EQUAL_UINT8(rest, runtime.firesLastPass());
        TEST_ASSERT_EQUAL_UINT8(0, runtime.deferredCount());
        w.add(gLast);
    } else {
        TEST_ASSERT_EQUAL_UINT8(fires, runtime.firesLastPass());
    }
    TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);  // the last of them
    gNow = p + 2;
    loopPass(gNow += 2, kNone, kNone);
    settle(gNow);
    return w;
}

}  // namespace

static void test_three_and_four_presses() {
    recordPass(PSTR("three presses due in one pass (one deferred): two fire, one waits a pass"), manyPresses(3, false),
               kT3);
    recordPass(PSTR("four presses due in one pass: two fire, two wait a pass"), manyPresses(4, false), kT3);
    recordPass(PSTR("three presses due + a write's last step: two fire, one waits a pass"), manyPresses(3, true), kT3);
}

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

static void test_every_pass_is_under_1_ms() { checkBudget(); }

#else

static void test_press_as_a_write_finishes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_two_presses() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_three_and_four_presses() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_presses_waiting_for_room() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_every_pass_is_under_1_ms() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_press_as_a_write_finishes);
    RUN_TEST(test_two_presses);
    RUN_TEST(test_three_and_four_presses);
    RUN_TEST(test_presses_waiting_for_room);
    RUN_TEST(test_every_pass_is_under_1_ms);
    return UNITY_END();
}
