// Worst-case time of the runtime's per-pass work: button presses (the command and the PRESS
// event), the announce, the battery report, reading request bytes from the real Serial1, the
// drain into the real Serial1, and whole loop() passes of the mapping (pump, pump, buttons,
// batteries, service) with requests, presses and EEPROM writes in flight. AVR only (simavr);
// see test/support/sm_avr_timing.h for the method.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#include <sm_test_main.h>
#include <sm_avr_timing.h>
#include <sm_avr_loop.h>

#if defined(__AVR__)

using namespace sm;
using namespace smt;

static void test_button_presses() {
    timerBegin();
    worstState();
    uint16_t t;

    // A long press, the longest PRESS line: F CIRCLE RIGHTL from=D, stale record, 15-char
    // default, 11-char prefix and 7-char suffix on the command.
    const uint32_t t0 = 10000;
    t = ticksOf([&] { gAny = runtime.buttons(t0, kFootRight, kDomeCircle, &gFired); });
    TEST_ASSERT_FALSE(gAny);
    TEST_ASSERT_TRUE(runtime.tapPending());
    record(PSTR("buttons(): a long-press combo pressed (starts waiting)"), t);
    for (uint32_t ms = 100; ms < Runtime::kLongPressMs; ms += 100)
        TEST_ASSERT_FALSE(runtime.buttons(t0 + ms, kFootRight, kDomeCircle, &gFired));
    drainAll();
    null.longest = 0;
    t = ticksOf([&] { gAny = runtime.buttons(t0 + Runtime::kLongPressMs, kFootRight, kDomeCircle, &gFired); });
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);
    TEST_ASSERT_EQUAL_STRING(kDef15, gFired.key);
    TEST_ASSERT_TRUE(gFired.modFromOther);
    TEST_ASSERT_TRUE(gFired.queued);
    drainAll();
    TEST_ASSERT_EQUAL_UINT8(8 + 66, null.longest);
    record(PSTR("buttons(): long press fires (command + PRESS F CIRCLE RIGHTL ... from=D)"), t, kT1,
           static_cast<uint8_t>(null.longest - 8));
    runtime.buttons(t0 + 700, kNone, kNone, &gFired);

    // A single press fires at once: D CIRCLE RIGHT from=F, stale record, 15-char default.
    null.longest = 0;
    t = ticksOf([&] { gAny = runtime.buttons(20000, kFootCircle, kDomeRight, &gFired); });
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_EQUAL_UINT8(kWideSingle, gFired.slot);
    TEST_ASSERT_TRUE(gFired.modFromOther);
    drainAll();
    record(PSTR("buttons(): single press fires (command + PRESS D CIRCLE RIGHT ... from=F)"), t, kT1,
           static_cast<uint8_t>(null.longest - 8));
    runtime.buttons(20100, kNone, kNone, &gFired);

    // The same with EVENTS off: the command only.
    request(PSTR("@SM EVENTS 0"));
    t = ticksOf([&] { gAny = runtime.buttons(21200, kFootCircle, kDomeRight, &gFired); });
    TEST_ASSERT_TRUE(gAny);
    drainAll();
    record(PSTR("buttons(): single press fires, EVENTS off (command only)"), t);
    runtime.buttons(21300, kNone, kNone, &gFired);
    request(PSTR("@SM EVENTS 1"));

    // A double tap: F CIRCLE RIGHT2 holds a valid 15-char record.
    eeSlot(doubleOf(slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT)), kGen, kKey15);
    store.begin();
    freshRuntime();
    request(PSTR("@SM HELLO 20"));
    request(PSTR("@SM EVENTS 1"));
    Worst w;
    const PadState seq[] = {kFootRight, kNone, kFootRight, kNone};
    for (uint8_t i = 0; i < 4; ++i) {
        w.add(ticksOf([&] { runtime.buttons(30000UL + i * 50UL, seq[i], kDomeCircle, &gFired); }));
        drainAll();
    }
    TEST_ASSERT_EQUAL_UINT8(doubleOf(slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT)), gFired.slot);
    record(PSTR("buttons(): worst pass of a double tap (second press fires F CIRCLE RIGHT2)"), w.ticks);

    // Two presses in one pass: a press deferred while an EEPROM write was in flight fires with
    // a long press that comes due in the same pass.
    worstState();
    io.busyPasses = 3;
    TEST_ASSERT_TRUE(runtime.buttons(40000, kNone, kNone, &gFired) == false);
    runtime.buttons(40000, kFootRight, kDomeCircle, &gFired);  // the long press starts waiting
    TEST_ASSERT_TRUE(store.beginSet(slotIndex(Ctrl::D, Mod::L2, Btn::LEFT), kKey15) == MapStore::Result::Ok);
    store.step();  // the first byte goes out; the EEPROM is busy for three ticks
    TEST_ASSERT_FALSE(io.ready());
    // The dome presses RIGHT while the foot also holds CIRCLE: D CIRCLE RIGHT (from=F), whose
    // record can't be read while the EEPROM is busy, so it's deferred. The foot's combo is still
    // F CIRCLE RIGHT, its long press still waiting.
    const PadState footBoth = {true, kHeldRight | kHeldCircle};
    TEST_ASSERT_FALSE(runtime.buttons(40590, footBoth, kDomeRight, &gFired));
    drainAll();
    TEST_ASSERT_EQUAL_UINT8(0, tx.used());  // nothing went out: deferred
    io.tick();
    io.tick();
    io.tick();
    TEST_ASSERT_TRUE(io.ready());
    const uint16_t linesBefore = null.lines;
    t = ticksOf([&] { gAny = runtime.buttons(40600, footBoth, kDomeRight, &gFired); });
    TEST_ASSERT_TRUE(gAny);
    TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);
    drainAll();
    // Both commands, and the first press's PRESS line (the second's waits for room: dropped).
    TEST_ASSERT_EQUAL_UINT16(3, null.lines - linesBefore);
    record(PSTR("buttons(): a deferred press and a long press in one pass"), t, kT2);
    io.busyPasses = 0;
    for (uint16_t i = 0; i < 200 && store.busy(); ++i)
        store.step();
    store.takeCompletion();
}

static void test_announce_and_battery() {
    worstState();
    uint16_t t;

    // The WCB device announce (11-char firmware version).
    runtime.startAnnounce(0, 42);
    drainAll();
    t = ticksOf([&] { runtime.service(Runtime::kFirstAnnounceMaxMs, null); });
    null.note();
    TEST_ASSERT_EQUAL_UINT8(42, null.last);
    record(PSTR("service(): pass with the announce (NullUart)"), t, kT1, null.last);
    drainAll();

    // The battery report: both CRIT, up= at its longest (7 digits).
    const uint32_t base = 4294960000UL;
    runtime.startBattery(port, base);
    runtime.batteries(base, true, 1, true, 1);
    t = ticksOf([&] { runtime.batteries(base + 2001, true, 1, true, 1); });
    record(PSTR("batteries(): both levels settle"), t);
    t = ticksOf([&] { runtime.service(base + 2001, null); });
    null.note();
    record(PSTR("service(): pass with the battery report (NullUart)"), t, kT1, static_cast<uint8_t>(null.last - 8));
    drainAll();

    // The same two passes draining into the real Serial1 (UART idle).
    Serial1.begin(9600);
    runtime.startAnnounce(0, 42);
    SerialPoke::dropTx1();
    t = ticksOf([&] { runtime.service(Runtime::kFirstAnnounceMaxMs, SMT_WCB_UART); });
    record(PSTR("service(): pass with the announce, drain into Serial1"), t);
    SerialPoke::dropTx1();
    drainAll();
    runtime.startBattery(port, base);
    runtime.batteries(base + 4000, false, 0, false, 0);
    runtime.batteries(base + 4001, true, 1, true, 1);
    runtime.batteries(base + 6002, true, 1, true, 1);
    SerialPoke::dropTx1();
    t = ticksOf([&] { runtime.service(base + 6002, SMT_WCB_UART); });
    TEST_ASSERT_TRUE(tx.used() > 0);
    record(PSTR("service(): pass with the battery report, drain into Serial1"), t);
    SerialPoke::dropTx1();
    drainAll();
}

static void test_line_input() {
    worstState();
    Serial1.begin(9600);
    uint16_t t;

    // LineReader on its own: a 72-char "@SM " line and its CR, byte by byte.
    char body[LineReader::kMaxLen + 2];
    memset(body, 'x', LineReader::kMaxLen);
    memcpy(body, "@SM ", 4);
    body[LineReader::kMaxLen] = '\r';
    body[LineReader::kMaxLen + 1] = '\0';
    LineReader reader;
    bool ready = false;
    t = ticksOf([&] {
        for (uint8_t i = 0; body[i] != '\0'; ++i)
            ready = reader.feed(body[i]);
    });
    TEST_ASSERT_TRUE(ready);
    record(PSTR("LineReader: a 72-char line and its CR (73 feeds)"), t, kPart);

    // pump() from the real Serial1: 63 bytes (all its RX buffer holds) of other traffic.
    char junk[64];
    memset(junk, 'j', 62);
    junk[62] = '\r';
    junk[63] = '\0';
    SerialPoke::inject(Serial1, junk);
    t = ticksOf([] { runtime.pump(SMT_WCB_UART, wcbLines, port); });
    TEST_ASSERT_EQUAL_INT(0, Serial1.available());
    record(PSTR("pump(): 63 bytes of other traffic"), t);

    // 63 bytes: the end of other traffic, then the worst GET, all in the RX buffer at once.
    crcLine(PSTR("@SM GET F CIRCLE RIGHTL"));
    const uint8_t getLen = static_cast<uint8_t>(strlen(gLine) + 1);
    memset(junk, 'j', 63 - getLen);
    junk[63 - getLen - 1] = '\r';
    junk[63 - getLen] = '\0';
    SerialPoke::inject(Serial1, junk);
    SerialPoke::inject(Serial1, gLine);
    SerialPoke::inject(Serial1, "\r");
    drainAll();
    t = ticksOf([] { runtime.pump(SMT_WCB_UART, wcbLines, port); });
    record(PSTR("pump(): 63 bytes ending the worst GET"), t);
    uint32_t now = 1000;
    settle(now);

    // Two requests in the RX buffer at once (a second sender, or a resend landing behind a
    // request): EVENTS 1 and the worst GET.
    crcLine(PSTR("@SM EVENTS 1"));
    SerialPoke::inject(Serial1, gLine);
    SerialPoke::inject(Serial1, "\r");
    injectLine(PSTR("@SM GET F CIRCLE RIGHTL"));
    t = ticksOf([] { runtime.pump(SMT_WCB_UART, wcbLines, port); });
    record(PSTR("pump(): two requests in the RX buffer (EVENTS 1 + the worst GET)"), t);
    settle(now);

    // A 72-char line split over two passes; the second ends it (bad CRC).
    memset(body, 'x', 62);
    memcpy(body, "@SM ", 4);
    memcpy(body + 62, " *00000000\r", 12);
    char first[64];
    memcpy(first, body, 63);
    first[63] = '\0';
    SerialPoke::inject(Serial1, first);
    Worst w;
    w.add(ticksOf([] { runtime.pump(SMT_WCB_UART, wcbLines, port); }));
    SerialPoke::inject(Serial1, body + 63);
    w.add(ticksOf([] { runtime.pump(SMT_WCB_UART, wcbLines, port); }));
    record(PSTR("pump(): worst pass of a 72-char line over two passes (bad CRC)"), w.ticks);
    settle(now);
}

static void test_uart_drain() {
    worstState();
    Serial1.begin(9600);
    char line[96];
    memset(line, 'a', 86);
    line[86] = '\0';
    const char *parts[] = {line};
    TEST_ASSERT_TRUE(tx.pushLine(parts, 1));
    SerialPoke::dropTx1();
    TEST_ASSERT_EQUAL_INT(63, Serial1.availableForWrite());
    const uint16_t t = ticksOf([] { tx.drain(SMT_WCB_UART); });
    // 64: an idle UART takes the first byte straight into UDR, which leaves the ring's 63.
    TEST_ASSERT_EQUAL_UINT8(87 - 64, tx.used());
    record(PSTR("drain(): 64 bytes into Serial1 (UART idle)"), t);
    SerialPoke::inject(Serial1, "012345678901234567890123456789012345678901234567890123456789012");
    TEST_ASSERT_EQUAL_INT(63, SMT_WCB_UART.available());
    const uint16_t rx = ticksOf([] {
        while (SMT_WCB_UART.available() > 0)
            gSink8 = static_cast<uint8_t>(SMT_WCB_UART.read());
    });
    part(PSTR("63 x Serial1 available() + read()"), rx);
    SerialPoke::dropTx1();
    tx.drain(SMT_WCB_UART);
    SerialPoke::dropTx1();
}

// Whole loop() passes of the mapping (pump, pump, buttons, batteries, service into Serial1).
static void test_loop_passes() {
    Serial1.begin(9600);

    // The worst GET's last 63 bytes arrive in the pass a long press comes due.
    worstState();
    const uint32_t t0 = 50000;
    loopPass(t0, kFootRight, kDomeCircle);
    for (uint32_t ms = 100; ms < Runtime::kLongPressMs; ms += 100)
        loopPass(t0 + ms, kFootRight, kDomeCircle);
    drainAll();
    char junk[40];
    crcLine(PSTR("@SM GET F CIRCLE RIGHTL"));
    const uint8_t getLen = static_cast<uint8_t>(strlen(gLine) + 1);
    memset(junk, 'j', 63 - getLen);
    junk[63 - getLen - 1] = '\r';
    junk[63 - getLen] = '\0';
    SerialPoke::inject(Serial1, junk);
    SerialPoke::inject(Serial1, gLine);
    SerialPoke::inject(Serial1, "\r");
    WorstPass get;
    bool fired = false;
    uint32_t now = t0 + Runtime::kLongPressMs;
    for (uint8_t i = 0; i < 6; ++i, now += 2) {
        loopPass(now, kFootRight, kDomeCircle);
        if (gAny) {
            TEST_ASSERT_EQUAL_UINT8(kWide, gFired.slot);
            fired = true;
        }
        get.add(gLast);
    }
    TEST_ASSERT_TRUE(fired);
    settle(now, &get);
    recordPass(PSTR("loop pass: worst GET + long press fires + drain into Serial1"), get);

    // A SUM in progress (no output until its end) with a single press, and its PRESS line, on
    // every other pass; run twice so every slot's pass meets a press.
    WorstPass sum;
    for (uint8_t phase = 0; phase < 2; ++phase) {
        drainAll();
        request(PSTR("@SM SUM"));
        now = 100000UL + phase * 100000UL;
        for (uint16_t i = 0; i < 900 && protocol.opInProgress(); ++i) {
            const bool press = (i & 1) == phase && i < 2 * kSlotCount + 8;  // then let the reply out
            now += press ? 1100 : 10;  // past the shared one-second repeat timer
            loopPass(now, press ? kFootCircle : kNone, press ? kDomeRight : kNone);
            sum.add(gLast);
        }
        TEST_ASSERT_FALSE(protocol.opInProgress());
        settle(now);
    }
    recordPass(PSTR("loop pass: SUM slot pass + single press with PRESS line + drain"), sum);

    // A DUMP with a battery report and the announce due all along (they wait for room).
    drainAll();
    runtime.startAnnounce(0, 42);
    runtime.startBattery(port, 0);
    request(PSTR("@SM DUMP"));
    WorstPass w;
    now = 300000UL;
    for (uint16_t i = 0; i < 2400 && protocol.opInProgress(); ++i) {
        now += 10;
        loopPass(now, kNone, kNone, 1, 1);
        w.add(gLast);
    }
    TEST_ASSERT_FALSE(protocol.opInProgress());
    for (uint8_t i = 0; i < 10; ++i) {
        now += 10;
        loopPass(now, kNone, kNone, 1, 1);
        w.add(gLast);
    }
    recordPass(PSTR("loop pass: DUMP (worst slot) + battery and announce due + drain"), w);

    // A SET of F CIRCLE LEFTL (an erased long-press slot: all 18 bytes written) arriving over
    // Serial1 while F CIRCLE RIGHT is held for its long press. The EEPROM stays busy for two
    // passes after each byte (3.4 ms on the Mega), so the long press can come due mid-write and
    // be deferred to the next pass. Swept over where the press falls; a pass where it fires as the
    // write finishes (the RAM bit's reload) is T2.
    ClassWorst sweep;
    for (uint8_t offset = 0; offset < 12; ++offset) {
        worstState();
        io.busyPasses = 2;
        uint32_t at = 60000UL;
        loopPass(at, kFootRight, kDomeCircle);  // the long press starts waiting
        at += Runtime::kLongPressMs - 2UL * offset;
        injectLine(PSTR("@SM SET F CIRCLE LEFTL zyxwvutsrqponml"));
        for (uint16_t i = 0; i < 120; ++i) {
            loopPass(at, kFootRight, kDomeCircle);
            sweep.add();
            at += 2;
        }
        TEST_ASSERT_FALSE(store.busy());
        TEST_ASSERT_FALSE(protocol.opInProgress());
    }
    io.busyPasses = 0;
    recordPass(PSTR("loop pass: SET in flight (paced EEPROM) + long press, swept"), sweep.t1);
    if (sweep.t2.passes > 0)
        recordPass(PSTR("loop pass: SET finishing + long press fires, swept"), sweep.t2, kT2);

    // DEFAULTS in flight, with presses.
    worstState();
    io.busyPasses = 2;
    injectLine(PSTR("@SM DEFAULTS"));
    ClassWorst defaults;
    now = 400000UL;
    for (uint16_t i = 0; i < 400 && (store.busy() || protocol.opInProgress() || i < 4); ++i) {
        const bool press = (i & 1) == 0;
        now += press ? 1100 : 10;
        loopPass(now, press ? kFootCircle : kNone, press ? kDomeRight : kNone);
        defaults.add();
    }
    io.busyPasses = 0;
    TEST_ASSERT_FALSE(store.busy());
    TEST_ASSERT_EQUAL_UINT16(65535, store.generation());
    recordPass(PSTR("loop pass: DEFAULTS in flight (paced EEPROM) + presses"), defaults.t1);
    if (defaults.t2.passes > 0)
        recordPass(PSTR("loop pass: DEFAULTS finishing + press fires"), defaults.t2, kT2);

    // A write that fails its verify twice (a worn cell): the ERR eeprom reply.
    worstState();
    io.corrupt = true;
    request(PSTR("@SM SET F CIRCLE RIGHTL zyxwvutsrqponml"));
    Worst err;
    for (uint16_t i = 0; i < 400 && (store.busy() || protocol.opInProgress()); ++i) {
        err.add(ticksOf([] { protocol.service(); }));
        store.step();
        drainAll();
    }
    io.corrupt = false;
    TEST_ASSERT_FALSE(protocol.opInProgress());
    record(PSTR("service(): ERR eeprom reply (a write failed its verify)"), err.ticks, kT1,
           static_cast<uint8_t>(null.last - 8));
}

static void test_every_pass_is_under_1_ms() { checkBudget(); }

#else

static void test_button_presses() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_announce_and_battery() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_line_input() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_uart_drain() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_loop_passes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_every_pass_is_under_1_ms() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_button_presses);
    RUN_TEST(test_announce_and_battery);
    RUN_TEST(test_line_input);
    RUN_TEST(test_uart_drain);
    RUN_TEST(test_loop_passes);
    RUN_TEST(test_every_pass_is_under_1_ms);
    return UNITY_END();
}
