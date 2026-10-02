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
