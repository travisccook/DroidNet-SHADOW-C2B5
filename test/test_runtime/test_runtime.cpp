// sm::Runtime: what loop() does for the mapping, driven pass by pass with the real built-in
// defaults, a fake EEPROM and a simulated WCB.
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

// Serial input: `budget` bytes become available per pass.
struct FakeSerialIn {
    char data[200];
    size_t len = 0;
    size_t pos = 0;
    size_t budget = 1000;

    void load(const char *s) {
        len = strlen(s);
        memcpy(data, s, len);
        pos = 0;
    }
    int available() {
        const size_t left = len - pos;
        return static_cast<int>(left < budget ? left : budget);
    }
    int read() {
        if (pos >= len || budget == 0)
            return -1;
        --budget;
        return static_cast<unsigned char>(data[pos++]);
    }
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
    FakeSerialIn in;
    FiredPress fired = {};
    uint32_t now = 0;
};

Rig *rig = nullptr;

const PadState kOff = {false, 0};

// One loop() pass at time `t`: requests in, buttons, service out.
bool pass(uint32_t t, PadState foot, PadState dome = kOff) {
    rig->now = t;
    rig->runtime.pump(rig->in, rig->reader, rig->wcbPort);
    const bool fired = rig->runtime.buttons(t, foot, dome, &rig->fired);
    rig->runtime.service(t, rig->wcb);
    rig->eeprom.tick();
    rig->wcb.refill();
    return fired;
}

PadState held(uint16_t bits) { return {true, bits}; }

// True if one of the last few lines the Pi received is `want` (CRC checked).
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
}

void tearDown() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.violations, "SHADOW broke a WCB rule");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->eeprom.violations, "EEPROM touched while busy");
}

static void test_press_fires_once_then_every_second_while_held() {
    TEST_ASSERT_TRUE(pass(0, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", rig->wcb.commands.last());
    TEST_ASSERT_EQUAL_STRING("happysound", rig->fired.key);
    for (uint32_t t = 10; t <= 1000; t += 10)
        TEST_ASSERT_FALSE(pass(t, held(kHeldUp)));
    TEST_ASSERT_TRUE(pass(1001, held(kHeldUp)));  // held past a second: repeats
    TEST_ASSERT_EQUAL_UINT32(2, rig->wcb.commands.count);
}

// The old code never reset the counter on release, so taps inside the same second are
// ignored. Kept as it was.
static void test_retap_within_the_second_is_ignored() {
    TEST_ASSERT_TRUE(pass(0, held(kHeldLeft)));
    TEST_ASSERT_FALSE(pass(100, held(0)));
    TEST_ASSERT_FALSE(pass(200, held(kHeldLeft)));
    TEST_ASSERT_FALSE(pass(300, held(0)));
    TEST_ASSERT_TRUE(pass(1200, held(kHeldLeft)));
    TEST_ASSERT_EQUAL_UINT32(2, rig->wcb.commands.count);
    TEST_ASSERT_EQUAL_STRING(";W1;Cscaredsound", rig->wcb.commands.last());
}

static void test_counter_is_shared_by_both_controllers() {
    TEST_ASSERT_TRUE(pass(0, held(0), held(kHeldDown)));  // dome first
    TEST_ASSERT_EQUAL_STRING(";W1;Cresetsilent", rig->wcb.commands.last());
    TEST_ASSERT_FALSE(pass(100, held(kHeldDown), held(0)));  // foot inside the same second
    TEST_ASSERT_TRUE(pass(1101, held(kHeldDown), held(0)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cangrysound", rig->wcb.commands.last());
    // Both at once: the dome is handled first and wins the pass.
    TEST_ASSERT_TRUE(pass(3000, held(kHeldUp), held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Csinglemuse", rig->wcb.commands.last());
    TEST_ASSERT_EQUAL_UINT32(3, rig->wcb.commands.count);
}

static void test_gate_and_connection() {
    TEST_ASSERT_FALSE(pass(0, held(kHeldL1)));  // a modifier alone doesn't count
    TEST_ASSERT_FALSE(pass(1, held(kHeldPs | kHeldCross)));  // drive stick toggle, not a mapping
    TEST_ASSERT_FALSE(pass(2, {false, kHeldUp}));
    TEST_ASSERT_TRUE(pass(3, held(kHeldUp | kHeldL1)));  // and none of that used up the press
    TEST_ASSERT_EQUAL_STRING(";W1;Cextremehappysou", rig->wcb.commands.last());
}

static void test_unmapped_and_reserved_send_nothing() {
    TEST_ASSERT_TRUE(pass(0, held(kHeldUp | kHeldPs)));  // was brightness up: now unmapped
    TEST_ASSERT_EQUAL_STRING("", rig->fired.key);
    TEST_ASSERT_TRUE(pass(2000, held(kHeldL3 | kHeldL1)));  // foot over-speed toggle
    TEST_ASSERT_TRUE(rig->fired.src == Source::Reserved);
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.commands.count);
    // The dome's L3+L1 is Front Holo Center, not over-speed.
    TEST_ASSERT_TRUE(pass(4000, held(0), held(kHeldL3 | kHeldL1)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cfrontholocenter", rig->wcb.commands.last());
}

static void test_modifier_from_the_other_controller() {
    TEST_ASSERT_TRUE(pass(0, held(kHeldUp), held(kHeldCross)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cleia", rig->wcb.commands.last());
    TEST_ASSERT_TRUE(rig->fired.modFromOther);
    TEST_ASSERT_TRUE(pass(2000, held(kHeldUp | kHeldCross), held(0)));
    TEST_ASSERT_FALSE(rig->fired.modFromOther);
}

static void test_press_waits_while_eeprom_is_mid_write() {
    // Store a custom key so the lookup has to read EEPROM, then start a slow write elsewhere.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(rig->store.beginSet(slotIndex(Ctrl::F, Mod::NONE, Btn::RIGHT), "custom")));
    uint32_t t = 0;
    while (rig->store.busy())
        pass(t++, held(0));
    rig->eeprom.writeTicks = 12;
    rig->store.beginSet(slotIndex(Ctrl::D, Mod::L2, Btn::UP), "longkeyvalue123");
    pass(t++, held(0));  // starts the first byte: the EEPROM is busy now

    t += 2000;
    TEST_ASSERT_FALSE(pass(t++, held(kHeldRight)));  // deferred, not dropped
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.commands.count);
    bool fired = false;
    for (int i = 0; i < 40 && !fired; ++i)
        fired = pass(t++, held(kHeldRight));
    TEST_ASSERT_TRUE(fired);
    TEST_ASSERT_EQUAL_STRING(";W1;Ccustom", rig->wcb.commands.last());
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.commands.count);
    while (rig->store.busy())
        pass(t++, held(0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done), static_cast<uint8_t>(rig->store.takeCompletion()));
}

static void test_pump_reads_requests_through_noise() {
    char req[64];
    withCrc("@SM HELLO", req, sizeof(req));
    char stream[200];
    snprintf(stream, sizeof(stream), "{\"rc\":1}\r@Atrigger\r%s\r", req);
    rig->in.load(stream);
    uint32_t t = 0;
    for (int i = 0; i < 100 && rig->wcb.usb.count == 0; ++i) {
        rig->in.budget = 3;  // a few bytes per pass, like 9600 baud
        pass(t++, held(0));
    }
    char body[96];
    TEST_ASSERT_TRUE(stripCrc(rig->wcb.usb.last(), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("@SM READY v1 fw=2.0.0 slots=72 gen=0 fmt=0 pads=F", body);
}

static void test_pump_work_per_pass_is_bounded() {
    char noise[200];
    memset(noise, 'x', sizeof(noise) - 1);
    noise[sizeof(noise) - 1] = '\0';
    rig->in.load(noise);
    rig->runtime.pump(rig->in, rig->reader, rig->wcbPort);
    TEST_ASSERT_EQUAL_UINT32(Runtime::kMaxBytesPerPass, rig->in.pos);
}

static void test_announce_timing_and_format() {
    for (uint32_t t = 0; t < 5000; t += 50)
        pass(t, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.announces.count);
    pass(5000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.announces.count);
    TEST_ASSERT_EQUAL_STRING("@WDP1 {\"type\":\"Shadow\",\"fw\":\"2.0.0\",\"caps\":[\"shadow.map\"]}", rig->wcb.announces.last());
    for (uint32_t t = 5050; t < 35000; t += 500)
        pass(t, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.announces.count);
    pass(35000, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, rig->wcb.announces.count);
}

static void test_announce_waits_for_room_and_can_be_off() {
    const char *filler[] = {";filler filler filler filler filler filler filler filler filler filler"};
    rig->wcb.room = 0;
    while (rig->tx.pushLine(filler, 1)) {
    }
    rig->runtime.service(6000, rig->wcb);  // due, but no room: skipped, not squeezed in
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.announces.count);
    rig->wcb.refill();
    for (uint32_t t = 6001; t < 6100 && rig->wcb.announces.count == 0; ++t)
        pass(t, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.announces.count);

    Runtime quiet(rig->store, rig->protocol, rig->tx, nullptr);
    for (uint32_t t = 0; t < 100000; t += 1000)
        quiet.service(t, rig->wcb);
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.announces.count);
}

static void test_millis_wraparound() {
    const uint32_t nearWrap = 0xFFFFFF00UL;
    TEST_ASSERT_TRUE(pass(nearWrap, held(kHeldUp)));  // start - 0 is huge: resets and fires
    TEST_ASSERT_FALSE(pass(nearWrap + 500, held(kHeldUp)));
    TEST_ASSERT_FALSE(pass(0x00000100UL, held(kHeldUp)));   // 512 ms later, across the wrap
    TEST_ASSERT_TRUE(pass(0x00000400UL, held(kHeldUp)));    // 1280 ms later
    TEST_ASSERT_EQUAL_UINT32(2, rig->wcb.commands.count);
}

static void test_events_follow_presses_and_controllers() {
    char req[64];
    withCrc("@SM EVENTS 1", req, sizeof(req));
    rig->in.load(req);
    rig->in.data[rig->in.len++] = '\r';
    pass(0, held(0));
    pass(1, held(0));
    TEST_ASSERT_TRUE(piSawRecently("@SM OK EVENTS 1"));
    TEST_ASSERT_TRUE(piSawRecently("@SM CTL F 1"));  // the foot showed up after EVENTS

    pass(10, held(kHeldUp), held(kHeldCross));
    pass(11, held(0), held(0));
    TEST_ASSERT_TRUE(piSawRecently("@SM PRESS F CROSS UP leia default from=D"));
    TEST_ASSERT_TRUE(piSawRecently("@SM CTL D 1"));

    pass(2000, held(0), kOff);  // dome dropped
    pass(2001, held(0), kOff);
    TEST_ASSERT_TRUE(piSawRecently("@SM CTL D 0"));
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_press_fires_once_then_every_second_while_held);
    RUN_TEST(test_retap_within_the_second_is_ignored);
    RUN_TEST(test_counter_is_shared_by_both_controllers);
    RUN_TEST(test_gate_and_connection);
    RUN_TEST(test_unmapped_and_reserved_send_nothing);
    RUN_TEST(test_modifier_from_the_other_controller);
    RUN_TEST(test_press_waits_while_eeprom_is_mid_write);
    RUN_TEST(test_pump_reads_requests_through_noise);
    RUN_TEST(test_pump_work_per_pass_is_bounded);
    RUN_TEST(test_announce_timing_and_format);
    RUN_TEST(test_announce_waits_for_room_and_can_be_off);
    RUN_TEST(test_millis_wraparound);
    RUN_TEST(test_events_follow_presses_and_controllers);
    return UNITY_END();
}
