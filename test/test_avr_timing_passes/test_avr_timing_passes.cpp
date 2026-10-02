// Whole loop() passes of the mapping, one heavy unit each (T1, see test/support/sm_avr_timing.h):
// every request taken in from the real Serial1, every line the protocol sends, every slot DUMP or
// SUM reads, one press fired (every kind, with and without its PRESS line, with the EEPROM writer
// idle or mid-write), 64 bytes read from each port, and requests that arrive as a press fires.
// Each figure is the worst pass of its scenario, from the request's arrival until the mapping
// has nothing left to do. AVR only (simavr).
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

uint32_t gNow = 1000000UL;

// `body` arrives whole in Serial1's RX buffer; passes run until the mapping is done.
void intake(PGM_P what, PGM_P body) {
    WorstPass w;
    injectLine(body);
    settle(gNow, &w);
    recordPass(what, w);
}

// A line up to 72 characters: the RX buffer holds 63 bytes, so its first 40 arrive a pass early.
void intakeRam(PGM_P what, const char *line) {
    WorstPass w;
    char first[41];
    const size_t n = strlen(line);
    const size_t head = n > 40 ? 40 : n;
    memcpy(first, line, head);
    first[head] = '\0';
    SerialPoke::inject(Serial1, first);
    gNow += 2;
    loopPass(gNow, kNone, kNone);
    w.add(gLast);
    SerialPoke::inject(Serial1, line + head);
    SerialPoke::inject(Serial1, "\r");
    settle(gNow, &w);
    recordPass(what, w);
}

}  // namespace

static void test_intakes() {
    timerBegin();
    Serial1.begin(9600);
    worstState();
    io.busyPasses = 2;  // an EEPROM byte write takes 3.4 ms: about two passes
    settle(gNow);

    intake(PSTR("intake pass: HELLO 20 (READY, 71 chars)"), PSTR("@SM HELLO 20"));
    intake(PSTR("intake pass: CFG (11-char prefix, 7-char suffix)"), PSTR("@SM CFG"));
    // BAT with both controllers CRIT and up= at its longest.
    {
        uint32_t t = 4294960000UL;
        for (uint8_t i = 0; i < 4; ++i, t += 1000)
            loopPass(t, kNone, kNone, 1, 1);
        WorstPass w;
        injectLine(PSTR("@SM BAT"));
        for (uint8_t i = 0; i < 8; ++i, t += 2) {
            loopPass(t, kNone, kNone, 1, 1);
            w.add(gLast);
        }
        recordPass(PSTR("intake pass: BAT (CRIT/CRIT, up=4294967)"), w);
        settle(gNow);
    }
    intake(PSTR("intake pass: EVENTS 1"), PSTR("@SM EVENTS 1"));
    intake(PSTR("GET pass: F CIRCLE RIGHTL (stale 15-char record, 15-char default)"), PSTR("@SM GET F CIRCLE RIGHTL"));
    eeSlot(kWide, kGen, kKey15);
    intake(PSTR("GET pass: F CIRCLE RIGHTL (15-char key over 15-char default)"), PSTR("@SM GET F CIRCLE RIGHTL"));
    eeSlot(kWide, kGen - 1, kKey15);
    intake(PSTR("GET pass: D CIRCLE RIGHT (stale record, 15-char default)"), PSTR("@SM GET D CIRCLE RIGHT"));
    intake(PSTR("intake pass: GET with a stray argument (ERR slot)"), PSTR("@SM GET F CIRCLE RIGHTL x"));

    char body[LineReader::kMaxLen + 1];
    memset(body, 'x', 62);
    memcpy(body, "@SM ", 4);
    memcpy(body + 62, " *00000000", 11);
    intakeRam(PSTR("intake pass: 72-char line, bad CRC"), body);
    memset(body, 'x', LineReader::kMaxLen);
    memcpy(body, "@SM ", 4);
    body[LineReader::kMaxLen] = '\0';
    intakeRam(PSTR("intake pass: 72-char line, no CRC on the WCB port"), body);
    strcpy_P(body, PSTR("@SM ABCDEFGHIJ abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg"));
    intakeRam(PSTR("intake pass: 72-char line, unknown verb, 7 tokens"), withCrc(body, gLine, sizeof(gLine)));
}

// The writes, every pass from the request until the reply has gone (with the paced EEPROM).
static void test_writes() {
    worstState();
    io.busyPasses = 2;
    settle(gNow);
    intake(PSTR("SET passes: F CIRCLE RIGHTL, 15 chars (long press: RAM bit reload)"),
           PSTR("@SM SET F CIRCLE RIGHTL zyxwvutsrqponml"));
    intake(PSTR("SET passes: D CIRCLE RIGHT, 15 chars"), PSTR("@SM SET D CIRCLE RIGHT zyxwvutsrqponml"));
    intake(PSTR("REVERT passes: F CIRCLE RIGHTL"), PSTR("@SM REVERT F CIRCLE RIGHTL"));
    intake(PSTR("PREFIX passes: 11 chars"), PSTR("@SM PREFIX 3B5731322C3B4361626364"));
    intake(PSTR("SUFFIX passes: 7 chars"), PSTR("@SM SUFFIX 61626364656667"));
    intake(PSTR("DEFAULTS passes (the last step reloads every RAM bit)"), PSTR("@SM DEFAULTS"));
    TEST_ASSERT_EQUAL_UINT16(65535, store.generation());

    // The first write ever: header A (the format), then the slot.
    eeErase();
    store.begin();
    TEST_ASSERT_FALSE(store.formatted());
    intake(PSTR("SET passes: first write (formats)"), PSTR("@SM SET F CIRCLE RIGHTL zyxwvutsrqponml"));
    TEST_ASSERT_TRUE(store.formatted());

    // A write that fails its verify twice (a worn cell): the ERR eeprom reply.
    worstState();
    io.busyPasses = 2;
    io.corrupt = true;
    intake(PSTR("SET passes: ERR eeprom (fails its verify twice)"), PSTR("@SM SET F CIRCLE RIGHTL zyxwvutsrqponml"));
    io.corrupt = false;
}

// DUMP and SUM, every pass; a SET refused during the DUMP; a GET refused for want of room.
static void test_dump_and_sum() {
    worstState();
    io.busyPasses = 2;
    settle(gNow);
    intake(PSTR("DUMP passes (stale F CIRCLE RIGHTL, 78-char line)"), PSTR("@SM DUMP"));
    eeSlot(kWide, kGen, kKey15);
    intake(PSTR("DUMP passes, 15-char key on F CIRCLE RIGHTL"), PSTR("@SM DUMP"));
    eeSlot(kWide, kGen - 1, kKey15);
    intake(PSTR("SUM passes (stale F CIRCLE RIGHTL)"), PSTR("@SM SUM"));

    // A SET arriving during a DUMP: ERR busy with the slot named.
    {
        WorstPass w;
        injectLine(PSTR("@SM DUMP"));
        for (uint8_t i = 0; i < 20; ++i) {
            gNow += 2;
            loopPass(gNow, kNone, kNone);
        }
        injectLine(PSTR("@SM SET F CIRCLE RIGHTL zyxwvutsrqponml"));
        for (uint8_t i = 0; i < 20; ++i) {
            gNow += 2;
            loopPass(gNow, kNone, kNone);
            w.add(gLast);
        }
        settle(gNow);
        recordPass(PSTR("intake pass: SET during a DUMP (ERR busy, slot named)"), w);
    }

    // A GET that can't be answered (the TX queue too full for a reply): it waits, and the bytes
    // behind it are read and dropped.
    {
        char fill[TxQueue::kCapacity];
        const uint8_t fillLen = 100;
        memset(fill, 'f', fillLen);
        fill[fillLen] = '\0';
        const char *parts[] = {";", fill};
        drainAll();
        TEST_ASSERT_TRUE(tx.pushLine(parts, 2));
        TEST_ASSERT_FALSE(port.canSend(Protocol::kMaxLine));
        injectLine(PSTR("@SM GET F CIRCLE RIGHTL"));
        SerialPoke::inject(Serial1, "abcdefghijklmnopqrstuvwxyz");
        WorstPass w;
        for (uint8_t i = 0; i < 3; ++i) {
            gNow += 2;
            loopPass(gNow, kNone, kNone);
            SerialPoke::inject(Serial1, "abcdefghij");
            w.add(gLast);
        }
        recordPass(PSTR("intake pass: GET refused for want of room, bytes behind it dropped"), w);
        SerialPoke::clearRx(Serial1);
        settle(gNow);
    }
}

// Lines the protocol sends on its own: a controller event, the announce, the battery report.
static void test_stages() {
    worstState();
    settle(gNow);
    WorstPass w;
    protocol.setPads(false, true);
    for (uint8_t i = 0; i < 4; ++i) {
        gNow += 2;
        loopPass(gNow, {false, 0}, kNone);
        w.add(gLast);
    }
    settle(gNow, &w);
    recordPass(PSTR("CTL pass: a controller dropped (EVENTS on)"), w);

    w = WorstPass();
    runtime.startAnnounce(gNow, 42);
    for (uint16_t i = 0; i < 2000; ++i) {
        gNow += 2;
        loopPass(gNow, kNone, kNone);
        w.add(gLast);
    }
    recordPass(PSTR("announce pass (11-char fw)"), w);

    w = WorstPass();
    uint32_t t = 4294950000UL;
    runtime.startBattery(port, t);
    for (uint16_t i = 0; i < 3000; ++i, t += 2) {
        loopPass(t, kNone, kNone, 1, 1);
        w.add(gLast);
    }
    recordPass(PSTR("battery report pass (CRIT/CRIT, 7-digit up=)"), w);
    settle(gNow);
}

// One press a pass, every kind, with and without the PRESS line, the EEPROM writer idle or mid-write.
static void test_one_press() {
    for (uint8_t events = 0; events < 2; ++events) {
        worstState(events != 0);
        settle(gNow);
        WorstPass single, longPress, doubleTap;
        // A single press: D CIRCLE RIGHT from=F, stale record, 15-char default.
        for (uint8_t i = 0; i < 3; ++i) {
            gNow += 1100;
            loopPass(gNow, kFootCircle, kDomeRight);
            TEST_ASSERT_TRUE(gAny);
            single.add(gLast);
            gNow += 2;
            loopPass(gNow, kNone, kNone);
            single.add(gLast);
        }
        settle(gNow, &single);
        // A long press: F CIRCLE RIGHTL from=D, stale record.
        gNow += 1100;
        loopPass(gNow, kFootRight, kDomeCircle);
        for (uint32_t ms = 2; ms <= Runtime::kLongPressMs + 4; ms += 2) {
            loopPass(gNow + ms, kFootRight, kDomeCircle);
            longPress.add(gLast);
        }
        gNow += Runtime::kLongPressMs + 6;
        loopPass(gNow, kNone, kNone);
        settle(gNow, &longPress);
        // A double tap: F CIRCLE RIGHT2 holds a valid 15-char record.
        eeSlot(doubleOf(slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT)), kGen, kKey15);
        store.begin();
        const PadState seq[] = {kFootRight, kNone, kFootRight, kNone};
        gNow += 1100;
        for (uint8_t i = 0; i < 4; ++i) {
            gNow += 50;
            loopPass(gNow, seq[i], kDomeCircle);
            doubleTap.add(gLast);
        }
        settle(gNow, &doubleTap);
        if (events != 0) {
            recordPass(PSTR("press pass: single, PRESS ... from=F (stale record)"), single);
            recordPass(PSTR("press pass: long press, PRESS ... from=D (stale record)"), longPress);
            recordPass(PSTR("press pass: double tap, PRESS (valid record)"), doubleTap);
        } else {
            recordPass(PSTR("press pass: single, EVENTS off"), single);
            recordPass(PSTR("press pass: long press, EVENTS off"), longPress);
            recordPass(PSTR("press pass: double tap, EVENTS off"), doubleTap);
        }
    }

    // Presses while a SET is mid-write (the EEPROM ready between its bytes, so the slot is read).
    worstState();
    io.busyPasses = 2;
    settle(gNow);
    ClassWorst w;
    injectLine(PSTR("@SM SET D L2 LEFTL zyxwvutsrqponml"));
    for (uint16_t i = 0; i < 160; ++i) {
        const bool press = (i % 3) == 0;
        gNow += press ? 1100 : 2;
        loopPass(gNow, press ? kFootCircle : kNone, press ? kDomeRight : kNone);
        w.add();
    }
    settle(gNow);
    recordPass(PSTR("press pass: single with PRESS, a SET mid-write"), w.t1);
    if (w.t2.passes > 0)
        recordPass(PSTR("press pass: single with PRESS as a SET finishes"), w.t2, kT2);
}

// 63 bytes (all the RX buffer holds) on each port in one pass.
static void test_rx() {
    worstState();
    settle(gNow);
    char junk[64];
    memset(junk, 'j', 62);
    junk[62] = '\r';
    junk[63] = '\0';
    WorstPass w;
    SerialPoke::inject(Serial1, junk);
    gNow += 2;
    loopPass(gNow, kNone, kNone);
    w.add(gLast);
    recordPass(PSTR("RX pass: 63 bytes of other traffic on Serial1"), w);
    w = WorstPass();
    SerialPoke::inject(Serial, junk);
    gNow += 2;
    loopPass(gNow, kNone, kNone);
    w.add(gLast);
    recordPass(PSTR("RX pass: 63 bytes of other traffic on USB"), w);
    w = WorstPass();
    SerialPoke::inject(Serial1, junk);
    SerialPoke::inject(Serial, junk);
    gNow += 2;
    loopPass(gNow, kNone, kNone);
    w.add(gLast);
    recordPass(PSTR("RX pass: 63 bytes on each port"), w);
    settle(gNow);
}

// A request whole in the RX buffer in the pass a press fires: every pass until both are done.
static void test_request_meets_press() {
    worstState();
    io.busyPasses = 2;
    settle(gNow);
    PGM_P const bodies[] = {PSTR("@SM HELLO 20"), PSTR("@SM BAT"), PSTR("@SM CFG"),
                            PSTR("@SM GET F CIRCLE RIGHTL"), PSTR("@SM SET D L2 LEFTL zyxwvutsrqponml")};
    PGM_P const names[] = {PSTR("press + HELLO 20 in one pass, worst pass"), PSTR("press + BAT in one pass, worst pass"),
                           PSTR("press + CFG in one pass, worst pass"), PSTR("press + worst GET in one pass, worst pass"),
                           PSTR("press + SET in one pass, worst pass")};
    for (uint8_t k = 0; k < 5; ++k) {
        WorstPass w;
        gNow += 1100;
        injectLine(bodies[k]);
        loopPass(gNow, kFootCircle, kDomeRight);
        TEST_ASSERT_TRUE(gAny);
        w.add(gLast);
        gNow += 2;
        loopPass(gNow, kNone, kNone);
        w.add(gLast);
        settle(gNow, &w);
        recordPass(names[k], w);
    }
}

static void test_every_pass_is_under_1_ms() { checkBudget(); }

#else

static void test_intakes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_writes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_dump_and_sum() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_stages() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_one_press() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_rx() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_request_meets_press() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_every_pass_is_under_1_ms() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_intakes);
    RUN_TEST(test_writes);
    RUN_TEST(test_dump_and_sum);
    RUN_TEST(test_stages);
    RUN_TEST(test_one_press);
    RUN_TEST(test_rx);
    RUN_TEST(test_request_meets_press);
    RUN_TEST(test_every_pass_is_under_1_ms);
    return UNITY_END();
}
