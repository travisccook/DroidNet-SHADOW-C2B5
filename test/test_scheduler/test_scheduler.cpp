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
    rig->runtime.batteries(rig->now, true, 3, true, 3);
    rig->now += BatteryWatch::kFirstMs;  // the levels settle: a change, so a report is due
    rig->runtime.batteries(rig->now, true, 3, true, 3);
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
