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

// Starts a slow EEPROM write and makes F NONE RIGHT need an EEPROM read, so a press on it
// gets deferred. Returns the time to carry on from.
uint32_t deferAPress(uint32_t t) {
    rig->store.beginSet(slotIndex(Ctrl::F, Mod::NONE, Btn::RIGHT), "custom");
    while (rig->store.busy())
        pass(t++, held(0));
    rig->eeprom.writeTicks = 200;  // a very slow EEPROM
    rig->store.beginSet(slotIndex(Ctrl::D, Mod::L2, Btn::UP), "longkeyvalue123");
    pass(t++, held(0));
    t += 2000;
    TEST_ASSERT_FALSE(pass(t++, held(kHeldRight)));
    return t;
}

static void test_deferred_press_is_dropped_if_the_controller_drops() {
    uint32_t t = deferAPress(0);
    pass(t++, kOff);  // foot controller gone
    rig->eeprom.writeTicks = 1;
    for (int i = 0; i < 400; ++i)
        pass(t++, kOff);
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.commands.count);
}

static void test_deferred_press_is_dropped_when_stale() {
    uint32_t t = deferAPress(0);
    // The EEPROM stays busy (a fault holding up passes would look the same) past the limit.
    t += Runtime::kDeferMaxMs + 1;
    rig->eeprom.busy = 0;
    rig->eeprom.writeTicks = 1;
    for (int i = 0; i < 400; ++i)
        pass(t++, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.commands.count);
}

static void test_pump_keeps_the_uart_drained_while_a_reply_waits() {
    // Leave no room for replies, then send a request followed by a flood of noise.
    const char *filler[] = {";filler filler filler filler filler filler filler filler filler filler"};
    rig->wcb.room = 0;
    while (rig->tx.pushLine(filler, 1)) {
    }
    char req[64];
    withCrc("@SM GET D CROSS LEFT", req, sizeof(req));
    char stream[200];
    snprintf(stream, sizeof(stream), "%s\r%s", req, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    rig->in.load(stream);
    rig->runtime.pump(rig->in, rig->reader, rig->wcbPort);
    TEST_ASSERT_TRUE(rig->reader.ready());  // held: no room to reply
    rig->runtime.pump(rig->in, rig->reader, rig->wcbPort);
    TEST_ASSERT_EQUAL_UINT32(rig->in.len, rig->in.pos);  // and the noise was read, not left to overflow
    // Once there's room, the held request is answered.
    for (uint32_t t = 1; t < 400 && rig->wcb.usb.count == 0; ++t)
        pass(t, held(0));
    TEST_ASSERT_TRUE(piSawRecently("@SM SLOT D CROSS LEFT scanning default def=scanning"));
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
    TEST_ASSERT_EQUAL_STRING("@SM READY v3 fw=2.0.0 slots=216 gen=0 fmt=0 pads=F", body);
}

static void test_pump_work_per_pass_is_bounded() {
    char noise[200];
    memset(noise, 'x', sizeof(noise) - 1);
    noise[sizeof(noise) - 1] = '\0';
    rig->in.load(noise);
    rig->runtime.pump(rig->in, rig->reader, rig->wcbPort);
    TEST_ASSERT_EQUAL_UINT32(Runtime::kMaxBytesPerPass, rig->in.pos);
}

// Runs passes every `step` ms from `from` until an announce goes out; returns its time.
static uint32_t nextAnnounceAt(uint32_t from, uint32_t step) {
    const uint32_t count = rig->wcb.announces.count;
    uint32_t t = from;
    for (; t < from + 40000 && rig->wcb.announces.count == count; t += step)
        pass(t, held(0));
    TEST_ASSERT_EQUAL_UINT32(count + 1, rig->wcb.announces.count);
    return t - step;
}

static void test_announce_timing_and_format() {
    for (uint32_t t = 0; t < 60000; t += 500)
        pass(t, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.announces.count);  // not until startAnnounce()

    rig->runtime.startAnnounce(60000, 12345);
    const uint32_t first = nextAnnounceAt(60000, 10);
    TEST_ASSERT_UINT32_WITHIN(1250, 60000 + 1750, first);  // 0.5-3 s after the start
    TEST_ASSERT_EQUAL_STRING("@WDP1 {\"type\":\"Shadow\",\"fw\":\"2.0.0\"}", rig->wcb.announces.last());
    TEST_ASSERT_EQUAL_UINT32(Runtime::kAnnounceOverhead, strlen(rig->wcb.announces.last()) + 1 - strlen("2.0.0"));

    uint32_t at = first;
    uint32_t lo = 0xFFFFFFFFUL, hi = 0;
    for (int i = 0; i < 8; ++i) {
        const uint32_t next = nextAnnounceAt(at + 50, 50);
        const uint32_t gap = next - at;
        lo = gap < lo ? gap : lo;
        hi = gap > hi ? gap : hi;
        at = next;
    }
    TEST_ASSERT_UINT32_WITHIN(2550, 27500, lo);  // every gap 25-30 s (to the 50 ms step)
    TEST_ASSERT_UINT32_WITHIN(2550, 27500, hi);
    TEST_ASSERT_TRUE(hi - lo >= 500);  // and they vary
}

static void test_announce_start_depends_on_seed() {
    Runtime a(rig->store, rig->protocol, rig->tx, "2.0.0");
    Runtime b(rig->store, rig->protocol, rig->tx, "2.0.0");
    Runtime zero(rig->store, rig->protocol, rig->tx, "2.0.0");
    a.startAnnounce(0, 1);
    b.startAnnounce(0, 2);
    zero.startAnnounce(0, 0);  // a zero seed must still work
    Runtime *runtimes[] = {&a, &b, &zero};
    uint32_t first[] = {0, 0, 0};
    for (uint32_t t = 0; t <= 3000; t += 10) {
        for (int i = 0; i < 3; ++i) {
            rig->wcb.refill();
            const uint32_t before = rig->wcb.announces.count;
            runtimes[i]->service(t, rig->wcb);
            if (rig->wcb.announces.count != before && first[i] == 0)
                first[i] = t;
        }
    }
    const uint32_t firstA = first[0], firstB = first[1], firstZero = first[2];
    TEST_ASSERT_TRUE(firstA >= 500 && firstB >= 500 && firstZero >= 500);
    TEST_ASSERT_TRUE(firstA != firstB);
}

static void test_announce_waits_for_room_and_can_be_off() {
    rig->runtime.startAnnounce(0, 99);
    const char *filler[] = {";filler filler filler filler filler filler filler filler filler filler"};
    rig->wcb.room = 0;
    while (rig->tx.pushLine(filler, 1)) {
    }
    rig->runtime.service(3000, rig->wcb);  // due, but no room: skipped, not squeezed in
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.announces.count);
    rig->wcb.refill();
    for (uint32_t t = 3001; t < 3100 && rig->wcb.announces.count == 0; ++t)
        pass(t, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.announces.count);

    Runtime quiet(rig->store, rig->protocol, rig->tx, nullptr);
    quiet.startAnnounce(0, 99);
    for (uint32_t t = 0; t < 100000; t += 1000)
        quiet.service(t, rig->wcb);
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.announces.count);
}

static void test_no_announce_during_a_fault() {
    rig->runtime.startAnnounce(0, 5);
    for (uint32_t t = 0; t < 120000; t += 100) {
        rig->runtime.service(t, rig->wcb, false);  // readUSB() reported a fault
        rig->wcb.refill();
    }
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.announces.count);
    pass(120000, held(0));  // fault cleared: the overdue announce goes straight out
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
    RUN_TEST(test_deferred_press_is_dropped_if_the_controller_drops);
    RUN_TEST(test_deferred_press_is_dropped_when_stale);
    RUN_TEST(test_pump_keeps_the_uart_drained_while_a_reply_waits);
    RUN_TEST(test_pump_reads_requests_through_noise);
    RUN_TEST(test_pump_work_per_pass_is_bounded);
    RUN_TEST(test_announce_timing_and_format);
    RUN_TEST(test_announce_start_depends_on_seed);
    RUN_TEST(test_announce_waits_for_room_and_can_be_off);
    RUN_TEST(test_no_announce_during_a_fault);
    RUN_TEST(test_millis_wraparound);
    RUN_TEST(test_events_follow_presses_and_controllers);
    return UNITY_END();
}
