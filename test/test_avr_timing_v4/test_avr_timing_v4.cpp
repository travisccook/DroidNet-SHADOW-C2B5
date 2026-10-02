// Raw commands (@SM v4) at their longest, whole loop passes: a GET and a DUMP of the 93-character
// SLOT line (15 raw characters over a 15-character default on F CIRCLE RIGHTL), SUM over raw
// records, a SET of 15 raw bytes (and the first write), presses that send a raw command with the
// 81-character PRESS line, and a raw SET arriving as a press fires. AVR only (simavr); see
// test/support/sm_avr_timing.h for the method.
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

void intake(PGM_P what, PGM_P body) {
    WorstPass w;
    injectLine(body);
    settle(gNow, &w);
    recordPass(what, w);
}

// A line of up to 72 characters: the RX buffer holds 63 bytes, so its first 40 arrive a pass early.
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

// The worst state with 15-character raw commands on F CIRCLE RIGHTL and D CIRCLE RIGHT.
void rawState(bool stale) {
    worstState();
    eeSlot(kWide, stale ? kGen - 1 : kGen, kRaw15, true);
    eeSlot(kWideSingle, stale ? kGen - 1 : kGen, kRaw15, true);
    store.begin();
    io.busyPasses = 2;
    for (uint8_t i = 0; i < 4; ++i) {  // the controller events EVENTS 1 left due
        gNow += 2;
        loopPass(gNow, kNone, kNone);
    }
    settle(gNow);
    drainAll();
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

    // The GET: its request pass reads the slot, a later one sends the 93-character line.
    drainAll();
    null.longest = 0;
    const char *get = crcLine(PSTR("@SM GET F CIRCLE RIGHTL"));
    bool ok = false;
    uint16_t t = ticksOf([&] { ok = protocol.handleLine(get, port); });
    TEST_ASSERT_TRUE(ok);
    record(PSTR("v4 handleLine GET F CIRCLE RIGHTL (15 raw chars): slot read"), t);
    t = ticksOf([&] { ok = protocol.service(); });
    TEST_ASSERT_TRUE(ok);
    drainAll();
    TEST_ASSERT_EQUAL_UINT8(8 + 93, null.longest);
    record(PSTR("v4 service() GET: the 93-char SLOT line sent"), t, kT1, 93);
    intake(PSTR("v4 GET passes: F CIRCLE RIGHTL, 93-char SLOT line"), PSTR("@SM GET F CIRCLE RIGHTL"));
    intake(PSTR("v4 DUMP passes: raw on F CIRCLE RIGHTL and D CIRCLE RIGHT (93-char lines)"), PSTR("@SM DUMP"));
    intake(PSTR("v4 SUM passes over raw records"), PSTR("@SM SUM"));
    rawState(true);
    intake(PSTR("v4 SUM passes over stale raw records"), PSTR("@SM SUM"));
}

static void test_raw_writes() {
    rawState(false);
    char body[LineReader::kMaxLen + 1];
    strcpy_P(body, PSTR("@SM SET F CIRCLE RIGHTL !"));
    strcat(body, kRaw15Hex);
    WorstPass w;
    withCrc(body, gLine, sizeof(gLine));
    TEST_ASSERT_EQUAL_UINT(65, strlen(gLine));
    injectLong(gLine, w);
    settle(gNow, &w);
    char key[kKeyBuf];
    bool raw = false;
    TEST_ASSERT_TRUE(store.keyForSlot(kWide, key, nullptr, &raw));
    TEST_ASSERT_TRUE(raw);
    recordPass(PSTR("v4 SET passes: 15 raw bytes (hex decoded) on F CIRCLE RIGHTL"), w);

    eeErase();
    store.begin();
    TEST_ASSERT_FALSE(store.formatted());
    w = WorstPass();
    withCrc(body, gLine, sizeof(gLine));
    injectLong(gLine, w);
    settle(gNow, &w);
    TEST_ASSERT_TRUE(store.formatted());
    recordPass(PSTR("v4 SET passes: 15 raw bytes, first write (formats)"), w);
}

static void test_raw_presses() {
    for (uint8_t stale = 0; stale < 2; ++stale) {
        rawState(stale != 0);
        io.busyPasses = 0;
        WorstPass single, longPress;
        // A single press: D CIRCLE RIGHT from=F, a raw command (stale: its key default instead).
        gNow += 1100;
        null.longest = 0;
        loopPass(gNow, kFootCircle, kDomeRight);
        TEST_ASSERT_TRUE(gAny);
        TEST_ASSERT_TRUE(gFired.raw == (stale == 0));
        single.add(gLast);
        settle(gNow, &single);
        // A long press: F CIRCLE RIGHTL from=D, the 81-character PRESS line.
        gNow += 1100;
        loopPass(gNow, kFootRight, kDomeCircle);
        for (uint32_t ms = 2; ms <= Runtime::kLongPressMs + 4; ms += 2) {
            loopPass(gNow + ms, kFootRight, kDomeCircle);
            longPress.add(gLast);
        }
        gNow += Runtime::kLongPressMs + 6;
        loopPass(gNow, kNone, kNone);
        settle(gNow, &longPress);
        if (stale == 0) {
            recordPass(PSTR("v4 press pass: single raw command, PRESS ... from=F"), single);
            recordPass(PSTR("v4 press pass: long press raw command, 81-char PRESS from=D"), longPress);
        } else {
            recordPass(PSTR("v4 press pass: single, stale raw record (default sent)"), single);
            recordPass(PSTR("v4 press pass: long press, stale raw record"), longPress);
        }
    }

    // A raw SET whole in the RX buffer in the pass a raw press fires.
    rawState(false);
    char body[LineReader::kMaxLen + 1];
    strcpy_P(body, PSTR("@SM SET D L2 LEFTL !"));
    strcat(body, kRaw15Hex);
    withCrc(body, gLine, sizeof(gLine));
    WorstPass w;
    injectLong(gLine, w);
    gNow += 1100;
    loopPass(gNow, kFootCircle, kDomeRight);
    TEST_ASSERT_TRUE(gAny);
    w.add(gLast);
    settle(gNow, &w);
    recordPass(PSTR("v4 raw press + raw SET in one pass, worst pass"), w);
}

static void test_every_pass_is_under_1_ms() { checkBudget(); }

#else

static void test_raw_slot_lines() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_raw_writes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_raw_presses() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_every_pass_is_under_1_ms() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_raw_slot_lines);
    RUN_TEST(test_raw_writes);
    RUN_TEST(test_raw_presses);
    RUN_TEST(test_every_pass_is_under_1_ms);
    return UNITY_END();
}
