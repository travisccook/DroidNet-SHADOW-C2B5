// Raw commands (@SM v4) at their longest, whole loop passes (see test/support/sm_avr_timing.h for
// the method and the classes): a GET of the 93-character SLOT line (15 raw characters over a
// 15-character default on F CIRCLE RIGHTL), the worst read with it (a full read budget ending the
// GET, answered in that pass), DUMP and SUM over raw records, a SET of 15 raw bytes (and the first
// write), raw presses through Runtime with the 80- and 81-character PRESS lines (one, two, three
// due in a pass, with a write's last step), and a raw SET arriving as a raw press fires.
// AVR only (simavr).
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

uint32_t gNow = 3000000UL;

constexpr uint8_t kDomeCircleDown = slotIndex(Ctrl::D, Mod::CIRCLE, Btn::DOWN);
constexpr uint8_t kL2LeftLong = slotIndex(Ctrl::D, Mod::L2, Btn::LEFT, Tap::LONG);
// D CIRCLE DOWN with CIRCLE held on the foot controller ("from=F").
constexpr PadState kDomeDown = {true, kHeldDown};
// The foot holds RIGHT and its own CIRCLE: still F CIRCLE RIGHT, and CIRCLE for the dome's presses.
constexpr PadState kFootBoth = {true, kHeldRight | kHeldCircle};

// The route (HELLO 20) in front of every line SHADOW sends to the WCB.
constexpr uint8_t kRoute = 8;

// `body` arrives whole in Serial1's RX buffer; passes run until the mapping is done.
void intake(PGM_P what, PGM_P body) {
    WorstPass w;
    injectLine(body);
    settle(gNow, &w);
    recordPass(what, w);
}

// A line longer than the 63 bytes Serial1's RX buffer holds: its first 40 arrive a pass early.
void injectLong(const char *line, WorstPass &w) {
    char first[41];
    memcpy(first, line, 40);
    first[40] = '\0';
    SerialPoke::inject(Serial1, first);
    gNow += 2;
    loopPass(gNow, kNone, kNone);
    w.add(gLast);
    SerialPoke::inject(Serial1, line + 40);
    SerialPoke::inject(Serial1, "\r");
}

// The worst state with 15-character raw commands on F CIRCLE RIGHTL and D CIRCLE RIGHT, valid
// records (or stale ones, which leave the 15-character defaults in force).
void rawState(bool stale) {
    worstState();
    eeSlot(kWide, stale ? kGen - 1 : kGen, kRaw15, true);
    eeSlot(kWideSingle, stale ? kGen - 1 : kGen, kRaw15, true);
    store.begin();
    io.busyPasses = 0;
    settle(gNow);
    drainAll();
}

// "@SM SET <slot> !<kRaw15Hex>" with its CRC, in gLine.
const char *rawSet(PGM_P head) {
    char body[LineReader::kMaxLen + 1];
    strcpy_P(body, head);
    strcat(body, kRaw15Hex);
    return withCrc(body, gLine, sizeof(gLine));
}

// Bytes a pass queued, those the drain moved into Serial1 included.
int16_t queuedBy(uint8_t before) {
    const int room = Serial1.availableForWrite();
    const int moved = room < 63 ? 64 - room : 0;
    return static_cast<int16_t>(tx.used() + moved - before);
}

}  // namespace

static void test_raw_slot_lines() {
    timerBegin();
    Serial1.begin(9600);
    rawState(false);
    char key[kKeyBuf];
    bool raw = false;
    TEST_ASSERT_TRUE(store.keyForSlot(kWide, key, nullptr, &raw));
    TEST_ASSERT_TRUE(raw);
    TEST_ASSERT_EQUAL_STRING(kRaw15, key);

    // The reply is the 93-character SLOT line.
    drainAll();
    null.longest = 0;
    TEST_ASSERT_TRUE(protocol.handleLine(crcLine(PSTR("@SM GET F CIRCLE RIGHTL")), port));
    drainAll();
    TEST_ASSERT_EQUAL_UINT8(kRoute + 93, null.longest);

    intake(PSTR("v4 GET passes: raw on F CIRCLE RIGHTL, 93-char SLOT line"), PSTR("@SM GET F CIRCLE RIGHTL"));

    // The worst read with v4: a full read budget whose last byte ends the GET (its first bytes were
    // read in an earlier pass), taken and answered in one pass, with the drain; in the same pass
    // both controllers' battery levels change (CRIT, first seen kFirstMs earlier).
    settle(gNow);
    loopPass(gNow += 2, kNone, kNone, 1, 1);
    settle(gNow);
    gNow += BatteryWatch::kFirstMs;
    BudgetLine get;
    budgetLine(PSTR("@SM GET F CIRCLE RIGHTL"), get);
    SerialPoke::inject(Serial1, get.lead);
    loopPass(gNow += 2, kNone, kNone);
    TEST_ASSERT_TRUE(runtime.lastWork() == Runtime::Work::Background);  // no request yet
    SerialPoke::inject(Serial1, get.budget);
    TEST_ASSERT_EQUAL_INT(Runtime::kMaxBytesPerPass, Serial1.available());
    WorstPass w;
    const uint8_t before = tx.used();
    loopPass(gNow += 2, kNone, kNone, 1, 1);
    TEST_ASSERT_TRUE(runtime.lastWork() == Runtime::Work::Request);
    TEST_ASSERT_TRUE(runtime.footBattery().level() == BattLevel::Crit);
    TEST_ASSERT_TRUE(runtime.domeBattery().level() == BattLevel::Crit);
    TEST_ASSERT_FALSE(wcbLines.ready());
    TEST_ASSERT_EQUAL_INT(0, Serial1.available());
    TEST_ASSERT_EQUAL_INT16(kRoute + 93 + 1, queuedBy(before));  // the 93-char SLOT line
    w.add(gLast);
    settle(gNow);
    recordPass(
        PSTR("v4 loop pass: full budget (32 bytes) ending a raw GET, 93-char SLOT answered, + battery change + drain"),
        w);

    // pump() alone with the same bytes (the read and the answer), then drain() alone (the reply's
    // first 64 bytes). Calling them here also keeps this program's code the slower shape: called
    // from more than one place, Runtime::pump<Uart1> and TxQueue::drain<Uart1> are emitted out of
    // line, and the loop pass above costs about 50 us more than with both inlined into
    // afterButtons() (the firmware's shape today, and this program's without these calls).
    // test_avr_timing_runtime has the slower shape too, and so does the figure above.
    SerialPoke::inject(Serial1, get.lead);
    TEST_ASSERT_FALSE(runtime.pump(SMT_WCB_UART, wcbLines, port));
    SerialPoke::inject(Serial1, get.budget);
    drainAll();
    bool took = false;
    uint16_t t = ticksOf([&] { took = runtime.pump(SMT_WCB_UART, wcbLines, port); });
    TEST_ASSERT_TRUE(took);
    TEST_ASSERT_EQUAL_INT(0, Serial1.available());
    TEST_ASSERT_EQUAL_UINT8(kRoute + 93 + 1, tx.used());
    record(PSTR("v4 pump(): full budget (32 bytes) ending a raw GET, its 93-char SLOT queued"), t);
    SerialPoke::dropTx1();
    t = ticksOf([] { tx.drain(SMT_WCB_UART); });
    TEST_ASSERT_EQUAL_UINT8(kRoute + 93 + 1 - 64, tx.used());
    record(PSTR("v4 drain(): the SLOT line's first 64 bytes into Serial1 (UART idle)"), t, kPart);
    settle(gNow);

    intake(PSTR("v4 DUMP passes: raw on F CIRCLE RIGHTL and D CIRCLE RIGHT (93-, 92-char lines)"),
           PSTR("@SM DUMP"));
    intake(PSTR("v4 SUM passes over raw records"), PSTR("@SM SUM"));
    rawState(true);
    intake(PSTR("v4 SUM passes over stale raw records"), PSTR("@SM SUM"));
}

static void test_raw_writes() {
    rawState(false);
    io.busyPasses = 2;  // an EEPROM byte write takes 3.4 ms: about two passes
    WorstPass w;
    rawSet(PSTR("@SM SET F CIRCLE RIGHTL !"));
    TEST_ASSERT_EQUAL_UINT(65, strlen(gLine));
    injectLong(gLine, w);
    settle(gNow, &w);
    char key[kKeyBuf];
    bool raw = false;
    TEST_ASSERT_TRUE(store.keyForSlot(kWide, key, nullptr, &raw));
    TEST_ASSERT_TRUE(raw);
    TEST_ASSERT_EQUAL_STRING(kRaw15, key);
    recordPass(PSTR("v4 SET passes: 15 raw bytes (65-char line) on F CIRCLE RIGHTL"), w);

    eeErase();
    store.begin();
    TEST_ASSERT_FALSE(store.formatted());
    w = WorstPass();
    rawSet(PSTR("@SM SET F CIRCLE RIGHTL !"));
    injectLong(gLine, w);
    settle(gNow, &w);
    TEST_ASSERT_TRUE(store.formatted());
    TEST_ASSERT_TRUE(store.keyForSlot(kWide, key, nullptr, &raw));
    TEST_ASSERT_TRUE(raw);
    recordPass(PSTR("v4 SET passes: 15 raw bytes, first write (formats)"), w);
    io.busyPasses = 0;
}

static void test_one_raw_press() {
    // buttons() alone: the raw long press fires, its text and the 81-char PRESS line queued.
    rawState(false);
    const uint32_t t0 = gNow + 1100;
    runtime.buttons(t0, kFootRight, kDomeCircle, &gFired);
    for (uint32_t ms = 100; ms < Runtime::kLongPressMs; ms += 100)
        TEST_ASSERT_FALSE(runtime.buttons(t0 + ms, kFootRight, kDomeCircle, &gFired));
    drainAll();
    null.longest = 0;
    const uint32_t due = t0 + Runtime::kLongPressMs;
    const uint16_t t = ticksOf([&] { gAny = runtime.buttons(due, kFootRight, kDomeCircle, &gFired); });
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);
    TEST_ASSERT_TRUE(gFired.raw);
    TEST_ASSERT_TRUE(gFired.modFromOther);
    drainAll();
    TEST_ASSERT_EQUAL_UINT8(kRoute + 81, null.longest);
    record(PSTR("v4 buttons(): raw long press fires (text + PRESS F CIRCLE RIGHTL ... from=D)"), t, kT1, 81);
    gNow = t0 + 700;
    runtime.buttons(gNow, kNone, kNone, &gFired);
    settle(gNow);

    for (uint8_t stale = 0; stale < 2; ++stale) {
        rawState(stale != 0);
        WorstPass single, longPress;
        // A single press: D CIRCLE RIGHT from=F (raw: the 80-char PRESS line).
        gNow += 1100;
        loopPass(gNow, kFootCircle, kDomeRight);
        TEST_ASSERT_TRUE(gAny);
        TEST_ASSERT_TRUE(gFired.raw == (stale == 0));
        single.add(gLast);
        settle(gNow, &single);
        // A long press: F CIRCLE RIGHTL from=D (raw: the 81-char PRESS line).
        gNow += 1100;
        loopPass(gNow, kFootRight, kDomeCircle);
        for (uint32_t ms = 2; ms <= Runtime::kLongPressMs + 4; ms += 2) {
            loopPass(gNow + ms, kFootRight, kDomeCircle);
            longPress.add(gLast);
        }
        TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);
        TEST_ASSERT_TRUE(gFired.raw == (stale == 0));
        gNow += Runtime::kLongPressMs + 6;
        loopPass(gNow, kNone, kNone);
        settle(gNow, &longPress);
        if (stale == 0) {
            recordPass(PSTR("v4 press passes: single raw press, 80-char PRESS ... from=F"), single);
            recordPass(PSTR("v4 press passes: long raw press, 81-char PRESS ... from=D"), longPress);
        } else {
            recordPass(PSTR("v4 press passes: single, stale raw record (default sent)"), single);
            recordPass(PSTR("v4 press passes: long press, stale raw record (default sent)"), longPress);
        }
    }
}

namespace {

// Raw presses due together in pass P, every one with from= and a raw command:
//   - three: D CIRCLE RIGHT from=F (its double tap mapped, pressed in P - 2) turns into D CIRCLE
//     DOWN from=F in P, so D CIRCLE RIGHT fires as a single press (80-char PRESS) and D CIRCLE
//     DOWN at once (79); F CIRCLE RIGHTL from=D (81) comes due in P and, the third, waits a pass;
//   - two: D CIRCLE RIGHT from=F pressed in P (80) and F CIRCLE RIGHTL from=D due in P (81).
// With `finishing`, a raw SET's verify-and-finish step (a long-press slot: RAM bit reload) is due
// in P too. The figure is the worse of P and, with three, P + 2.
WorstPass rawPresses(uint8_t due, bool finishing) {
    rawState(false);
    eeSlot(kDomeCircleDown, kGen, kRaw15, true);
    if (due == 3)
        eeSlot(doubleOf(kWideSingle), kGen, kRaw15, true);
    store.begin();
    settle(gNow);
    TEST_ASSERT_TRUE(store.doubleTapMapped(kWideSingle) == (due == 3));

    gNow += 1100;
    const uint32_t a = gNow;
    const uint32_t p = a + Runtime::kLongPressMs;
    loopPass(a, kFootRight, kDomeCircle);  // F CIRCLE RIGHT (from=D) starts waiting for its long press
    for (gNow = a + 2; gNow < p - 2; gNow += 2) {
        loopPass(gNow, kFootBoth, kDomeCircle);
        TEST_ASSERT_FALSE(gAny);
    }
    loopPass(p - 2, kFootBoth, due == 3 ? kDomeRight : kDomeCircle);  // three: D CIRCLE RIGHT waits
    TEST_ASSERT_FALSE(gAny);
    if (finishing) {
        TEST_ASSERT_TRUE(store.beginSet(kL2LeftLong, kRaw15, true) == MapStore::Result::Ok);
        for (uint8_t i = 0; i < 40 && !store.finishDue(); ++i)
            store.step();
        TEST_ASSERT_TRUE(store.finishDue());
    }
    WorstPass w;
    loopPass(p, kFootBoth, due == 3 ? kDomeDown : kDomeRight);
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_TRUE(gFinishing == finishing);
    TEST_ASSERT_FALSE(store.finishDue());
    TEST_ASSERT_TRUE(gFired.raw);
    TEST_ASSERT_TRUE(gFired.modFromOther);
    TEST_ASSERT_EQUAL_UINT8(Runtime::kMaxFiresPerPass, runtime.firesLastPass());
    w.add(gLast);
    if (due == 3) {
        TEST_ASSERT_EQUAL_UINT8(kDomeCircleDown, gFired.slot);
        TEST_ASSERT_EQUAL_UINT8(1, runtime.deferredCount());
        loopPass(p + 2, kFootBoth, kDomeDown);
        TEST_ASSERT_TRUE(gAny);
        TEST_ASSERT_EQUAL_UINT8(1, runtime.firesLastPass());
        TEST_ASSERT_EQUAL_UINT8(0, runtime.deferredCount());
        w.add(gLast);
    }
    TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);  // the long press, last
    TEST_ASSERT_TRUE(gFired.raw);
    TEST_ASSERT_TRUE(gFired.modFromOther);
    gNow = p + 4;
    loopPass(gNow, kNone, kNone);
    settle(gNow);
    if (finishing) {
        char key[kKeyBuf];
        bool raw = false;
        TEST_ASSERT_TRUE(store.keyForSlot(kL2LeftLong, key, nullptr, &raw));
        TEST_ASSERT_TRUE(raw);
    }
    return w;
}

}  // namespace

static void test_raw_presses_together() {
    recordPass(PSTR("v4 two raw presses (80- and 81-char PRESS, from=)"), rawPresses(2, false), kT2);
    recordPass(PSTR("v4 two raw presses (80- and 81-char PRESS, from=) + a raw SET's last step"), rawPresses(2, true),
               kT2);
    recordPass(PSTR("v4 three raw presses due: two fire (80, 79-char PRESS), the 81 waits a pass"),
               rawPresses(3, false), kT3);
    recordPass(PSTR("v4 three raw presses due + a raw SET's last step: two fire, one waits a pass"),
               rawPresses(3, true), kT3);
}

// A raw SET whole in the RX buffer in the pass a raw press fires: every pass until both are done.
static void test_raw_set_meets_raw_press() {
    rawState(false);
    io.busyPasses = 2;
    WorstPass w;
    rawSet(PSTR("@SM SET D L2 LEFTL !"));
    injectLong(gLine, w);
    gNow += 1100;
    loopPass(gNow, kFootCircle, kDomeRight);
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_TRUE(gFired.raw);
    TEST_ASSERT_TRUE(wcbLines.ready() || Serial1.available() > 0);  // the press pass took no request
    w.add(gLast);
    gNow += 2;
    loopPass(gNow, kNone, kNone);
    w.add(gLast);
    settle(gNow, &w);
    char key[kKeyBuf];
    bool raw = false;
    TEST_ASSERT_TRUE(store.keyForSlot(kL2LeftLong, key, nullptr, &raw));
    TEST_ASSERT_TRUE(raw);
    recordPass(PSTR("v4 raw press + raw SET in the RX buffer (separate passes), worst pass"), w);
    io.busyPasses = 0;
}

static void test_every_pass_is_under_1_ms() { checkBudget(); }

#else

static void test_raw_slot_lines() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_raw_writes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_one_raw_press() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_raw_presses_together() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_raw_set_meets_raw_press() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_every_pass_is_under_1_ms() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_raw_slot_lines);
    RUN_TEST(test_raw_writes);
    RUN_TEST(test_one_raw_press);
    RUN_TEST(test_raw_presses_together);
    RUN_TEST(test_raw_set_meets_raw_press);
    RUN_TEST(test_every_pass_is_under_1_ms);
    return UNITY_END();
}
