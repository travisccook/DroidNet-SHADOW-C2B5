// Controller battery levels: the status byte to a level, settling, and the "@SM BAT" line
// (asked for, on a change, and every 30 s) as the Pi sees it through the WCB.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#define SM_TEST_OWN_SETUP
#include <sm_test_main.h>
#include <new>
#include <stdio.h>

using namespace sm;

SM_DEFINE_TEST_DEFAULTS

namespace {

struct Rig {
    FakeEeprom eeprom;
    MapStore store{eeprom, kTestDefaults, kTestDefaultCount};
    TxQueue tx;
    WcbPort wcbPort{tx};
    Protocol protocol{store, "test"};
    Runtime runtime{store, protocol, tx, nullptr};
    WcbSim wcb;
    CapturePort usb;
};

Rig *rig = nullptr;

// What main.cpp's controllers look like this pass.
struct Pads {
    bool footOn;
    uint8_t foot;
    bool domeOn;
    uint8_t dome;
};

Pads pads = {false, 0, false, 0};

// One loop() pass at time `t`, as serviceMapping() runs it.
void pass(uint32_t t, bool mayAnnounce = true) {
    rig->runtime.batteries(t, pads.footOn, pads.foot, pads.domeOn, pads.dome);
    rig->runtime.service(t, rig->wcb, mayAnnounce);
    rig->eeprom.tick();
    rig->wcb.refill();
}

// Passes every 10 ms over [from, to).
void run(uint32_t from, uint32_t to, bool mayAnnounce = true) {
    for (uint32_t t = from; t < to; t += 10)
        pass(t, mayAnnounce);
}

// The Pi's last line, CRC checked and stripped.
const char *piLast() {
    static char body[96];
    TEST_ASSERT_TRUE_MESSAGE(stripCrc(rig->wcb.usb.last(), body, sizeof(body)), rig->wcb.usb.last());
    return body;
}

BattLevel levelFor(uint8_t status) {
    BattLevel level = BattLevel::None;
    TEST_ASSERT_TRUE(battLevelFor(status, &level));
    return level;
}

}  // namespace

void setUp() {
    static Rig instance;
    instance.~Rig();
    new (&instance) Rig();
    rig = &instance;
    rig->store.begin();
    pads = {false, 0, false, 0};
}

void tearDown() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.violations, "SHADOW broke a WCB rule");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.overruns, "wrote past the UART buffer");
}

static void test_status_byte_to_level() {
    TEST_ASSERT_EQUAL(BattLevel::Ok, levelFor(0x05));
    TEST_ASSERT_EQUAL(BattLevel::Ok, levelFor(0x04));
    TEST_ASSERT_EQUAL(BattLevel::Ok, levelFor(0x03));
    TEST_ASSERT_EQUAL(BattLevel::Low, levelFor(0x02));
    TEST_ASSERT_EQUAL(BattLevel::Crit, levelFor(0x01));
    TEST_ASSERT_EQUAL(BattLevel::Crit, levelFor(0x00));
    TEST_ASSERT_EQUAL(BattLevel::Ok, levelFor(0xEE));  // charging
    TEST_ASSERT_EQUAL(BattLevel::Ok, levelFor(0xEF));  // charged, on the cable
    TEST_ASSERT_EQUAL(BattLevel::Ok, levelFor(0xF1));
    BattLevel level = BattLevel::Low;
    TEST_ASSERT_FALSE(battLevelFor(0x06, &level));
    TEST_ASSERT_FALSE(battLevelFor(0xED, &level));
    TEST_ASSERT_EQUAL(BattLevel::Low, level);  // untouched
}

static void test_first_reading_settles_after_two_seconds() {
    BatteryWatch w;
    TEST_ASSERT_FALSE(w.update(0, true, 0));  // connected, no report yet
    TEST_ASSERT_FALSE(w.update(100, true, 0x04));
    TEST_ASSERT_EQUAL(BattLevel::None, w.level());
    TEST_ASSERT_EQUAL_UINT8(0x04, w.status());  // the detail is live straight away
    TEST_ASSERT_FALSE(w.update(2099, true, 0x04));
    TEST_ASSERT_TRUE(w.update(2100, true, 0x04));
    TEST_ASSERT_EQUAL(BattLevel::Ok, w.level());
    TEST_ASSERT_FALSE(w.update(2110, true, 0x04));
}

static void test_worse_takes_three_seconds_better_takes_thirty() {
    BatteryWatch w;
    w.update(0, true, 0x03);
    w.update(BatteryWatch::kFirstMs, true, 0x03);
    TEST_ASSERT_EQUAL(BattLevel::Ok, w.level());

    const uint32_t t = 10000;
    TEST_ASSERT_FALSE(w.update(t, true, 0x02));
    TEST_ASSERT_FALSE(w.update(t + BatteryWatch::kWorseMs - 1, true, 0x02));
    TEST_ASSERT_TRUE(w.update(t + BatteryWatch::kWorseMs, true, 0x02));
    TEST_ASSERT_EQUAL(BattLevel::Low, w.level());

    const uint32_t u = 20000;
    TEST_ASSERT_FALSE(w.update(u, true, 0x03));
    TEST_ASSERT_FALSE(w.update(u + BatteryWatch::kBetterMs - 1, true, 0x03));
    TEST_ASSERT_TRUE(w.update(u + BatteryWatch::kBetterMs, true, 0x03));
    TEST_ASSERT_EQUAL(BattLevel::Ok, w.level());
}

static void test_flicker_at_a_boundary_does_not_flap() {
    BatteryWatch w;
    w.update(0, true, 0x03);
    w.update(BatteryWatch::kFirstMs, true, 0x03);
    for (uint32_t t = 3000; t < 60000; t += 1000)
        TEST_ASSERT_FALSE(w.update(t, true, (t / 1000) % 2 != 0 ? 0x02 : 0x03));
    TEST_ASSERT_EQUAL(BattLevel::Ok, w.level());
    const uint8_t last = w.status();
    TEST_ASSERT_FALSE(w.update(60000, true, 0x77));  // not a battery status: ignored
    TEST_ASSERT_EQUAL_UINT8(last, w.status());
}

static void test_disconnect_forgets_at_once() {
    BatteryWatch w;
    w.update(0, true, 0x01);
    TEST_ASSERT_TRUE(w.update(BatteryWatch::kFirstMs, true, 0x01));
    TEST_ASSERT_EQUAL(BattLevel::Crit, w.level());
    TEST_ASSERT_TRUE(w.update(5000, false, 0x01));
    TEST_ASSERT_EQUAL(BattLevel::None, w.level());
    TEST_ASSERT_EQUAL_UINT8(0, w.status());
    TEST_ASSERT_FALSE(w.update(5010, false, 0));
    // Reconnects: a fresh first reading.
    TEST_ASSERT_FALSE(w.update(6000, true, 0x05));
    TEST_ASSERT_TRUE(w.update(6000 + BatteryWatch::kFirstMs, true, 0x05));
    TEST_ASSERT_EQUAL(BattLevel::Ok, w.level());
}

static void test_bat_request() {
    char line[96];
    rig->runtime.batteries(7000, true, 0x02, false, 0);  // unsettled: level still unknown
    TEST_ASSERT_TRUE(rig->protocol.handleLine(withCrc("@SM BAT", line, sizeof(line)), rig->usb));
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("@SM BAT F=- D=- fs=2 ds=- up=7 hb=30", line);

    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM BAT 1", rig->usb));
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args BAT", line);
}

static void test_reports_start_after_five_seconds_then_every_thirty() {
    run(0, 60000);
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.usb.count);  // not until startBattery()

    rig->runtime.startBattery(rig->wcbPort, 60000);
    run(60000, 65000);
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.usb.count);
    run(65000, 65020);
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.usb.count);
    TEST_ASSERT_EQUAL_STRING("@SM BAT F=- D=- fs=- ds=- up=65 hb=30", piLast());
    run(65020, 95000);
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.usb.count);
    run(95000, 95020);
    TEST_ASSERT_EQUAL_UINT32(2, rig->wcb.usb.count);
    // No EVENTS subscription needed, and a controller fault doesn't hold them up.
    run(95020, 125020, false);
    TEST_ASSERT_EQUAL_UINT32(3, rig->wcb.usb.count);
}

static void test_a_change_is_reported_at_once() {
    rig->runtime.startBattery(rig->wcbPort, 0);
    run(0, 5010);
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.usb.count);

    pads = {true, 0x04, true, 0xEE};
    run(5010, 7000);
    TEST_ASSERT_EQUAL_UINT32(1, rig->wcb.usb.count);  // still settling
    run(7000, 7100);
    TEST_ASSERT_EQUAL_UINT32(2, rig->wcb.usb.count);
    TEST_ASSERT_EQUAL_STRING("@SM BAT F=OK D=OK fs=4 ds=chg up=7 hb=30", piLast());

    pads.foot = 0x01;
    run(7100, 10200);
    TEST_ASSERT_EQUAL_UINT32(3, rig->wcb.usb.count);
    TEST_ASSERT_EQUAL_STRING("@SM BAT F=CRIT D=OK fs=1 ds=chg up=10 hb=30", piLast());

    pads.domeOn = false;  // dropped: reported on the next pass
    pads.dome = 0xEF;
    run(10200, 10220);
    TEST_ASSERT_EQUAL_UINT32(4, rig->wcb.usb.count);
    TEST_ASSERT_EQUAL_STRING("@SM BAT F=CRIT D=- fs=1 ds=- up=10 hb=30", piLast());

    // A change restarts the 30 s period.
    run(10220, 40200);
    TEST_ASSERT_EQUAL_UINT32(4, rig->wcb.usb.count);
    run(40200, 40220);
    TEST_ASSERT_EQUAL_UINT32(5, rig->wcb.usb.count);
}

static void test_reports_follow_hello_route_and_wait_for_room() {
    char line[96];
    rig->runtime.startBattery(rig->wcbPort, 0);
    TEST_ASSERT_TRUE(rig->protocol.handleLine(withCrc("@SM HELLO 12", line, sizeof(line)), rig->wcbPort));
    run(0, 5010);
    TEST_ASSERT_EQUAL_UINT8(12, rig->wcb.lastRoute);
    TEST_ASSERT_EQUAL_STRING("@SM BAT F=- D=- fs=- ds=- up=5 hb=30", piLast());

    // A report that falls due while the queue is full waits for room.
    rig->wcb.room = 0;  // the UART is busy: nothing drains this pass
    const char *filler[] = {";W1;Cfiller_filler_filler"};
    while (rig->tx.pushLine(filler, 1)) {
    }
    const uint8_t used = rig->tx.used();
    const uint32_t before = rig->wcb.usb.count;
    rig->runtime.batteries(35000, false, 0, false, 0);
    rig->runtime.service(35000, rig->wcb);
    TEST_ASSERT_EQUAL_UINT8(used, rig->tx.used());
    rig->wcb.refill();
    run(35010, 37000);
    TEST_ASSERT_EQUAL_UINT32(before + 1, rig->wcb.usb.count);
    TEST_ASSERT_EQUAL_STRING("@SM BAT F=- D=- fs=- ds=- up=35 hb=30", piLast());
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_status_byte_to_level);
    RUN_TEST(test_first_reading_settles_after_two_seconds);
    RUN_TEST(test_worse_takes_three_seconds_better_takes_thirty);
    RUN_TEST(test_flicker_at_a_boundary_does_not_flap);
    RUN_TEST(test_disconnect_forgets_at_once);
    RUN_TEST(test_bat_request);
    RUN_TEST(test_reports_start_after_five_seconds_then_every_thirty);
    RUN_TEST(test_a_change_is_reported_at_once);
    RUN_TEST(test_reports_follow_hello_route_and_wait_for_room);
    return UNITY_END();
}
