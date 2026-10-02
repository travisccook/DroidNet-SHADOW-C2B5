// The per-pass rule (sm::Runtime::pump/service, sm::Protocol::serviceLine/serviceSum): a request
// is taken in only in a pass with nothing heavy in it, one background unit runs in a light pass,
// a SLOT reply is read on one pass and sent on a later one, and the EEPROM writer and the drain run
// every pass. The real built-in defaults, a fake EEPROM, a simulated WCB and a USB port.
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

// Serial input: what arrives, `budget` bytes of it at most a pass.
struct FakeSerialIn {
    char data[400];
    size_t len = 0;
    size_t pos = 0;
    size_t arrivedTo = 0;  // bytes in the UART's RX buffer: data[pos, arrivedTo)
    void add(const char *s) {
        const size_t n = strlen(s);
        memcpy(data + len, s, n);
        len += n;
        arrivedTo = len;
    }
    void addLine(const char *body) {
        char line[96];
        withCrc(body, line, sizeof(line));
        add(line);
        add("\r");
    }
    int available() { return static_cast<int>(arrivedTo - pos); }
    int read() { return pos < arrivedTo ? static_cast<unsigned char>(data[pos++]) : -1; }
    size_t unread() const { return arrivedTo - pos; }
};

// A WCB UART that counts the drains that reach it.
struct CountingWcb {
    WcbSim sim;
    uint32_t drains = 0;  // passes in which drain() asked for room
    bool asked = false;
    int availableForWrite() {
        asked = true;
        return sim.availableForWrite();
    }
    size_t write(const uint8_t *d, size_t n) { return sim.write(d, n); }
};

struct Rig {
    FakeEeprom eeprom;
    MapStore store{eeprom, defaultButtonMappings, defaultButtonMappingsCount};
    TxQueue tx;
    WcbPort wcbPort{tx};
    Protocol protocol{store, "2.0.0"};
    Runtime runtime{store, protocol, tx, "2.0.0"};
    CountingWcb wcb;
    LineReader wcbLines;
    LineReader usbLines;
    FakeSerialIn wcbIn;
    FakeSerialIn usbIn;
    CapturePort usb;
    FiredPress fired = {};
    uint32_t now = 1000;
    int refill = 63;  // bytes the UART sends between passes
};

Rig *rig = nullptr;

const PadState kNone = {true, 0};

// One loop() pass in main.cpp's order. Returns whether a press fired.
bool pass(PadState foot = kNone, PadState dome = kNone, uint32_t dt = 2) {
    rig->now += dt;
    const bool fired = rig->runtime.buttons(rig->now, foot, dome, &rig->fired);
    rig->runtime.pump(rig->wcbIn, rig->wcbLines, rig->wcbPort);
    rig->runtime.pump(rig->usbIn, rig->usbLines, rig->usb);
    rig->runtime.batteries(rig->now, foot.connected, 0, dome.connected, 0);
    rig->wcb.asked = false;
    rig->runtime.service(rig->now, rig->wcb);
    if (rig->wcb.asked)
        ++rig->wcb.drains;
    rig->eeprom.tick();
    rig->wcb.sim.refill(rig->refill);
    return fired;
}

const char *piLast(uint8_t back = 0) {
    static char body[96];
    if (!stripCrc(rig->wcb.sim.usb.last(back), body, sizeof(body)))
        return "";
    return body;
}

uint32_t piCount() { return rig->wcb.sim.usb.count; }

// Passes until the Pi has `n` more lines (or the limit); returns how many passes that took.
uint16_t until(uint32_t n, uint16_t limit = 400) {
    const uint32_t target = piCount() + n;
    uint16_t i = 0;
    for (; i < limit && piCount() < target; ++i)
        pass();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(target, piCount(), "no reply");
    return i;
}

void settle() {
    for (uint16_t i = 0; i < 4000; ++i) {
        if (rig->store.busy() || rig->protocol.opInProgress() || rig->tx.used() > 0 || rig->wcbIn.unread() > 0 ||
            rig->wcbLines.ready() || rig->usbIn.unread() > 0 || rig->usbLines.ready())
            pass();
        else
            return;
    }
    TEST_FAIL_MESSAGE("never settled");
}

}  // namespace

void setUp() {
    static Rig instance;
    instance.~Rig();
    new (&instance) Rig();
    rig = &instance;
    rig->store.begin();
    for (uint8_t i = 0; i < 4; ++i)
        pass();
}

void tearDown() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.sim.violations, "SHADOW broke a WCB rule");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.sim.overruns, "wrote past the UART buffer");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->eeprom.violations, "EEPROM touched while busy");
}

// A request whose bytes come in a pass that fires a press: that pass reads only 16 bytes (both
// ports together), takes nothing in and sends nothing but the press; the rest waits in the UART.
static void test_a_press_pass_takes_no_request_and_reads_little() {
    rig->wcbIn.addLine("@SM CFG");
    rig->usbIn.add("@SM HELLO\r");
    const uint32_t before = piCount();
    TEST_ASSERT_TRUE(pass({true, kHeldUp}));  // F NONE UP: fires at once
    TEST_ASSERT_EQUAL_UINT32(16, rig->wcbIn.pos + rig->usbIn.pos);
    TEST_ASSERT_TRUE(rig->wcbIn.unread() > 0);
    TEST_ASSERT_EQUAL_UINT32(0, rig->usb.log.count);
    TEST_ASSERT_EQUAL_UINT32(before, piCount());  // the command went out, no reply
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", rig->wcb.sim.commands.last());
    // The line, once complete, is taken in on a light pass; nothing is read after it.
    until(1);
    TEST_ASSERT_EQUAL_STRING("@SM CFG pre=3B57313B43 suf=-", piLast());
    settle();
    TEST_ASSERT_EQUAL_UINT32(1, rig->usb.log.count);  // then the USB request
}

// A pass that reads 16 bytes or more takes no request in; a pass where another port already
// took one in waits; a pass where the EEPROM writer finishes a job waits.
static void test_intake_waits_for_a_light_pass() {
    rig->wcbIn.addLine("@SM CFG");  // 18 bytes: the line arrives whole in one pass
    pass();
    TEST_ASSERT_TRUE(rig->wcbLines.ready());  // read, not taken in
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcbIn.unread());
    pass();
    TEST_ASSERT_FALSE(rig->wcbLines.ready());  // taken in
    settle();

    // Both ports: the WCB's line is taken in; the USB one waits a pass.
    rig->wcbIn.addLine("@SM CFG");
    rig->usbIn.add("@SM CFG\r");
    pass();  // both arrive (heavy)
    pass();
    TEST_ASSERT_FALSE(rig->wcbLines.ready());
    TEST_ASSERT_TRUE(rig->usbLines.ready());
    pass();
    TEST_ASSERT_FALSE(rig->usbLines.ready());
    TEST_ASSERT_EQUAL_UINT32(1, rig->usb.log.count);
    settle();

    // A write's finishing step: the request waits for the next pass.
    rig->eeprom.writeTicks = 1;
    TEST_ASSERT_TRUE(rig->store.beginSet(slotIndex(Ctrl::F, Mod::L1, Btn::UP), "abc") == MapStore::Result::Ok);
    for (uint16_t i = 0; i < 200 && !rig->store.finishDue(); ++i)
        pass();
    TEST_ASSERT_TRUE(rig->store.finishDue());
    rig->usbIn.add("@SM CFG\r");  // 8 bytes: light
    pass();
    TEST_ASSERT_TRUE(rig->usbLines.ready());  // the finishing pass took nothing in
    TEST_ASSERT_FALSE(rig->store.finishDue());  // (the format; the slot's write follows)
    pass();
    TEST_ASSERT_FALSE(rig->usbLines.ready());
    settle();
}

// Nothing more is read in a pass that took a request in.
static void test_nothing_is_read_after_an_intake() {
    rig->usbIn.add("@SM CFG\r");
    pass();  // 8 bytes, taken in at once
    TEST_ASSERT_EQUAL_UINT32(1, rig->usb.log.count);
    rig->usbIn.add("@SM CFG\r@SM CFG\r");
    pass();
    TEST_ASSERT_EQUAL_UINT32(2, rig->usb.log.count);
    TEST_ASSERT_EQUAL_UINT32(8, rig->usbIn.unread());  // the second line is still in the UART
    pass();
    TEST_ASSERT_EQUAL_UINT32(3, rig->usb.log.count);
}

// A request that can't be answered (no room) is held and the bytes behind it are read and
// dropped, as before; once there's room it's answered.
static void test_a_refused_intake_keeps_the_discard_rule() {
    rig->usb.open = false;
    rig->usbIn.add("@SM CFG\r");
    char noise[100];
    memset(noise, 'x', 90);
    noise[90] = '\0';
    rig->usbIn.add(noise);
    for (uint8_t i = 0; i < 4; ++i)
        pass();
    TEST_ASSERT_TRUE(rig->usbLines.ready());
    TEST_ASSERT_EQUAL_UINT32(0, rig->usbIn.unread());
    rig->usb.open = true;
    pass();
    TEST_ASSERT_EQUAL_UINT32(1, rig->usb.log.count);
}

// A GET's line goes out on a later pass than the GET; one SLOT reply is in hand at a time, and a
// port's replies keep their requests' order.
static void test_a_get_replies_on_a_later_pass_in_order() {
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM GET F L1 UP", rig->usb));
    TEST_ASSERT_EQUAL_UINT32(0, rig->usb.log.count);
    TEST_ASSERT_TRUE(rig->protocol.opInProgress());
    TEST_ASSERT_FALSE(rig->protocol.handleLine("@SM CFG", rig->usb));       // same port: after it
    char line[64];
    withCrc("@SM GET D L1 UP", line, sizeof(line));
    TEST_ASSERT_FALSE(rig->protocol.handleLine(line, rig->wcbPort));  // a GET: one at a time
    withCrc("@SM CFG", line, sizeof(line));
    TEST_ASSERT_TRUE(rig->protocol.handleLine(line, rig->wcbPort));  // another port's other request
    TEST_ASSERT_TRUE(rig->protocol.service());
    char body[96];
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default def=extremehappysou", body);
    TEST_ASSERT_FALSE(rig->protocol.opInProgress());
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM CFG", rig->usb));
    settle();

    // Through the runtime: the reply in hand doesn't block the request behind it (no deadlock).
    rig->usbIn.add("@SM GET F L1 UP\r@SM CFG\r@SM GET D L1 UP\r");
    const uint32_t before = rig->usb.log.count;
    for (uint8_t i = 0; i < 20 && rig->usb.log.count < before + 3; ++i)
        pass();
    TEST_ASSERT_EQUAL_UINT32(before + 3, rig->usb.log.count);
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(2), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default def=extremehappysou", body);
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(1), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("@SM CFG pre=3B57313B43 suf=-", body);
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(0), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT D L1 UP frontholoup default def=frontholoup", body);
}

// A GET during a DUMP goes out between two DUMP lines, long before the DUMP reaches its slot.
static void test_a_get_during_a_dump_goes_between_its_lines() {
    rig->wcbIn.addLine("@SM DUMP");
    uint32_t slots = 0, getAt = 0, ends = 0;
    for (uint16_t i = 0; i < 30000 && ends == 0; ++i) {
        if (i == 30)
            rig->wcbIn.addLine("@SM GET D CROSS LEFT");
        const uint32_t before = piCount();
        pass();
        for (uint32_t k = piCount() - before; k > 0; --k) {
            const char *b = piLast(static_cast<uint8_t>(k - 1));
            if (strncmp(b, "@SM SLOT ", 9) == 0)
                ++slots;
            if (strcmp(b, "@SM SLOT D CROSS LEFT scanning default def=scanning") == 0 && getAt == 0)
                getAt = slots;
            if (strcmp(b, "@SM END DUMP 216") == 0)
                ++ends;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(1, ends);
    TEST_ASSERT_EQUAL_UINT32(kSlotCount + 1, slots);
    TEST_ASSERT_TRUE(getAt > 0 && getAt < slotIndex(Ctrl::D, Mod::CROSS, Btn::LEFT));
}

// One background unit a light pass, first come first: a controller event, then the reply in hand,
// then the operation's step. A unit that can't run (no room) doesn't use up the pass.
static void test_one_unit_a_light_pass_in_order() {
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM EVENTS 1", rig->usb));
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM GET F L1 UP", rig->usb));
    rig->protocol.setPads(false, true);  // the foot drops: a CTL event is due too
    TEST_ASSERT_TRUE(rig->protocol.serviceLine());
    TEST_ASSERT_EQUAL_STRING_LEN("@SM CTL F 0", rig->usb.log.last(), 11);
    TEST_ASSERT_TRUE(rig->protocol.serviceLine());
    TEST_ASSERT_EQUAL_STRING_LEN("@SM SLOT F L1 UP", rig->usb.log.last(), 16);
    TEST_ASSERT_FALSE(rig->protocol.serviceLine());

    // The USB reply waits for room; a SUM on the WCB carries on beside it, step and reply.
    rig->protocol.setPads(true, true);
    rig->usb.open = false;
    TEST_ASSERT_TRUE(rig->protocol.serviceLine() == false);  // the CTL event waits for room too
    rig->usb.open = true;
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM GET F L2 UP", rig->usb));
    rig->usb.open = false;
    char line[64];
    withCrc("@SM SUM", line, sizeof(line));
    TEST_ASSERT_TRUE(rig->protocol.handleLine(line, rig->wcbPort));
    uint16_t steps = 0;
    for (; steps < 300 && rig->protocol.serviceSum(); ++steps)
        rig->eeprom.tick();
    TEST_ASSERT_EQUAL_UINT16(kSlotCount, steps);
    TEST_ASSERT_TRUE(rig->protocol.serviceLine());  // the SUM reply, the USB reply still waiting
    rig->tx.drain(rig->wcb);
    TEST_ASSERT_EQUAL_STRING_LEN("@SM SUM ", piLast(), 8);
    TEST_ASSERT_TRUE(rig->protocol.opInProgress());  // the GET's reply
    rig->usb.open = true;
    TEST_ASSERT_TRUE(rig->protocol.serviceLine());  // the CTL event first, then the reply
    TEST_ASSERT_TRUE(rig->protocol.serviceLine());
    TEST_ASSERT_EQUAL_STRING_LEN("@SM SLOT F L2 UP", rig->usb.log.last(), 16);
    TEST_ASSERT_FALSE(rig->protocol.opInProgress());
}

// The battery report waits out a DUMP (as before: the DUMP's lines take the room first); the
// announce, needing less room, goes out during it, and during a SUM within a few passes.
static void test_background_lines_during_dump_and_sum() {
    rig->runtime.startBattery(rig->wcbPort, rig->now);
    rig->runtime.startAnnounce(rig->now, 7);
    rig->wcbIn.addLine("@SM DUMP");
    uint32_t batBeforeEnd = 0, ends = 0, announces = rig->wcb.sim.announces.count;
    const uint32_t start = rig->now;
    for (uint16_t i = 0; i < 30000 && ends == 0; ++i) {
        const uint32_t before = piCount();
        pass(kNone, kNone, 20);  // slow passes: the report comes due during the DUMP
        for (uint32_t k = piCount() - before; k > 0; --k) {
            const char *b = piLast(static_cast<uint8_t>(k - 1));
            if (strncmp(b, "@SM BAT ", 8) == 0 && ends == 0)
                ++batBeforeEnd;
            if (strncmp(b, "@SM END DUMP", 12) == 0)
                ++ends;
        }
    }
    TEST_ASSERT_TRUE(rig->now - start > kBatteryFirstMs + 1000);  // the report came due during it
    TEST_ASSERT_EQUAL_UINT32(0, batBeforeEnd);
    TEST_ASSERT_TRUE(rig->wcb.sim.announces.count > announces);  // the first announce went out
    until(1);
    TEST_ASSERT_EQUAL_STRING_LEN("@SM BAT ", piLast(), 8);
    settle();

    // During a SUM (no lines until its end), a due announce goes out within a few passes.
    rig->runtime.startAnnounce(rig->now, 9);
    rig->now += Runtime::kFirstAnnounceMaxMs;
    rig->wcbIn.addLine("@SM SUM");
    pass();
    pass();  // taken in
    TEST_ASSERT_TRUE(rig->protocol.opInProgress());
    announces = rig->wcb.sim.announces.count;
    uint8_t passes = 0;
    while (rig->wcb.sim.announces.count == announces && passes < 10) {
        pass();
        ++passes;
    }
    TEST_ASSERT_TRUE(passes < 4);
    settle();
}

// The EEPROM writer and the drain run every pass, a press's pass included.
static void test_step_and_drain_run_every_pass() {
    rig->eeprom.writeTicks = 1;  // busy for one tick: a byte a pass
    rig->wcbIn.addLine("@SM SET F NONE DOWN abcdefghijklmno");
    pass();
    pass();  // taken in: the write starts
    TEST_ASSERT_TRUE(rig->store.busy());
    const uint32_t writes = rig->eeprom.writes;
    const uint32_t drains = rig->wcb.drains;
    // Something always queued (the UART takes a byte a pass), so every drain asks for room.
    const char *filler[] = {";filler filler filler filler"};
    TEST_ASSERT_TRUE(rig->tx.pushLine(filler, 1));
    rig->wcb.sim.room = 0;
    rig->refill = 1;
    for (uint8_t i = 0; i < 8; ++i)
        pass(i % 2 == 0 ? PadState{true, static_cast<uint16_t>(i % 4 == 0 ? kHeldUp : kHeldLeft)} : kNone, kNone, 1100);
    rig->refill = 63;
    TEST_ASSERT_EQUAL_UINT32(writes + 8, rig->eeprom.writes);  // a byte every pass, presses or not
    TEST_ASSERT_EQUAL_UINT32(drains + 8, rig->wcb.drains);
    settle();
}

// The writer's verify-and-finish step waits one pass when a press fires in its pass (once), and
// runs on the next, press or not.
static void test_a_finishing_step_waits_a_press_pass() {
    rig->eeprom.writeTicks = 1;
    TEST_ASSERT_TRUE(rig->store.beginSet(slotIndex(Ctrl::F, Mod::L1, Btn::UP), "abc") == MapStore::Result::Ok);
    settle();  // formats, then writes the slot
    TEST_ASSERT_TRUE(rig->store.beginSet(slotIndex(Ctrl::F, Mod::L1, Btn::UP, Tap::LONG), "xyz") == MapStore::Result::Ok);
    for (uint16_t i = 0; i < 200 && !rig->store.finishDue(); ++i)
        pass();
    TEST_ASSERT_TRUE(rig->store.finishDue());
    TEST_ASSERT_TRUE(pass({true, kHeldUp}, kNone, 1100));  // a press: the step waits
    TEST_ASSERT_TRUE(rig->store.finishDue());
    TEST_ASSERT_TRUE(pass({true, kHeldLeft}, kNone, 1100));  // another: it doesn't wait twice
    TEST_ASSERT_FALSE(rig->store.finishDue());
    TEST_ASSERT_FALSE(rig->store.busy());
    TEST_ASSERT_TRUE(rig->store.longPressMapped(slotIndex(Ctrl::F, Mod::L1, Btn::UP)));
    // Without a press it runs in its own pass.
    TEST_ASSERT_TRUE(rig->store.beginSet(slotIndex(Ctrl::F, Mod::L1, Btn::UP, Tap::LONG), "") == MapStore::Result::Ok);
    for (uint16_t i = 0; i < 200 && !rig->store.finishDue(); ++i)
        pass();
    pass();
    TEST_ASSERT_FALSE(rig->store.busy());
    settle();
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_a_press_pass_takes_no_request_and_reads_little);
    RUN_TEST(test_intake_waits_for_a_light_pass);
    RUN_TEST(test_nothing_is_read_after_an_intake);
    RUN_TEST(test_a_refused_intake_keeps_the_discard_rule);
    RUN_TEST(test_a_get_replies_on_a_later_pass_in_order);
    RUN_TEST(test_a_get_during_a_dump_goes_between_its_lines);
    RUN_TEST(test_one_unit_a_light_pass_in_order);
    RUN_TEST(test_background_lines_during_dump_and_sum);
    RUN_TEST(test_step_and_drain_run_every_pass);
    RUN_TEST(test_a_finishing_step_waits_a_press_pass);
    return UNITY_END();
}
