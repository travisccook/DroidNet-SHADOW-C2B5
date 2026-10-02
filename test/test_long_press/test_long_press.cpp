// sm::Runtime long presses, driven pass by pass with the real built-in defaults, a fake EEPROM
// and a simulated WCB. F NONE UP is "happysound" by default; these tests map its long press
// ("quiet") and, where they say so, its double tap ("twice").
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
const uint8_t kUp2 =
    slotIndex(Ctrl::F, Mod::NONE, Btn::UP, Tap::DOUBLE);
const uint8_t kUpL = slotIndex(Ctrl::F, Mod::NONE, Btn::UP, Tap::LONG);
const uint32_t kLong = Runtime::kLongPressMs;
const uint32_t kWindow = Runtime::kDoubleTapMs;

bool pass(uint32_t t, PadState foot, PadState dome = kOff) {
    const bool fired = rig->runtime.buttons(t, foot, dome, &rig->fired);  // main.cpp's order
    rig->runtime.pump(rig->in, rig->reader, rig->wcbPort);
    rig->runtime.service(t, rig->wcb);
    rig->eeprom.tick();
    rig->wcb.refill();
    return fired;
}

PadState held(uint16_t bits) { return {true, bits}; }

void hold(uint32_t from, uint32_t to, PadState foot, PadState dome = kOff) {
    for (uint32_t t = from; t <= to; ++t)
        pass(t, foot, dome);
}

void mapSlot(uint8_t slot, const char *key) {
    const MapStore::Result r = rig->store.beginSet(slot, key);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(r));
    while (rig->store.busy()) {
        rig->store.step();
        rig->eeprom.tick();
    }
    rig->store.takeCompletion();
}

uint32_t commands() { return rig->wcb.commands.count; }
const char *lastCommand(uint8_t back = 0) { return rig->wcb.commands.last(back); }

}  // namespace

void setUp() {
    static Rig instance;
    instance.~Rig();
    new (&instance) Rig();
    rig = &instance;
    rig->store.begin();
    mapSlot(kUpL, "quiet");
}

void tearDown() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.violations, "SHADOW broke a WCB rule");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->eeprom.violations, "EEPROM touched while busy");
}

static void test_long_press_fires_at_the_threshold_once() {
    hold(0, kLong - 1, held(kHeldUp));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
    TEST_ASSERT_TRUE(pass(kLong, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cquiet", lastCommand());
    TEST_ASSERT_EQUAL_UINT8(kUpL, rig->fired.slot);
    hold(kLong + 1, 4000, held(kHeldUp));  // no repeat
    hold(4001, 5000, held(0));             // and nothing on release
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_release_before_the_threshold_fires_the_single_on_release() {
    hold(0, 120, held(kHeldUp));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
    TEST_ASSERT_TRUE(pass(121, held(0)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    hold(122, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_quick_taps_are_single_presses_each() {
    hold(0, 40, held(kHeldUp));
    hold(41, 100, held(0));
    hold(101, 140, held(kHeldUp));  // no double tap mapped: a second single press
    hold(141, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
}

static void test_with_a_double_tap_quick_taps_still_double() {
    mapSlot(kUp2, "twice");
    hold(0, 40, held(kHeldUp));
    hold(41, 100, held(0));
    TEST_ASSERT_TRUE(pass(101, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Ctwice", lastCommand());
    hold(102, 3000, held(kHeldUp));  // tap, then tap and hold: the double tap only
    hold(3001, 4000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_with_a_double_tap_a_single_tap_fires_when_the_window_ends() {
    mapSlot(kUp2, "twice");
    hold(0, 40, held(kHeldUp));
    hold(41, kWindow - 1, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
    TEST_ASSERT_TRUE(pass(kWindow, held(0)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    hold(kWindow + 1, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_release_between_the_window_and_the_threshold_fires_the_single() {
    mapSlot(kUp2, "twice");
    hold(0, 499, held(kHeldUp));  // past the window, still held: no single yet, no repeat
    TEST_ASSERT_EQUAL_UINT32(0, commands());
    TEST_ASSERT_TRUE(pass(500, held(0)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    hold(501, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_long_press_with_a_double_tap_mapped() {
    mapSlot(kUp2, "twice");
    hold(0, kLong - 1, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(kLong, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cquiet", lastCommand());
    hold(kLong + 1, 3000, held(kHeldUp));
    hold(3001, 4000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_another_combo_while_waiting_fires_the_single() {
    hold(0, 100, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(101, held(kHeldDown)));  // F NONE DOWN: no long press, fires at once
    TEST_ASSERT_EQUAL_UINT32(2, commands());
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand(1));
    TEST_ASSERT_EQUAL_STRING(";W1;Cangrysound", lastCommand(0));
    hold(102, 2000, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_modifier_landing_after_the_button() {
    TEST_ASSERT_FALSE(pass(0, held(kHeldUp)));
    TEST_ASSERT_TRUE(pass(10, held(kHeldUp | kHeldL1)));
    TEST_ASSERT_EQUAL_UINT32(2, commands());  // F NONE UP as a single press, then F L1 UP
    TEST_ASSERT_EQUAL_STRING(";W1;Cextremehappysou", lastCommand());
    hold(11, 2000, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_combos_without_a_long_press_fire_at_once_and_repeat() {
    TEST_ASSERT_TRUE(pass(0, held(kHeldLeft)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cscaredsound", lastCommand());
    hold(1, 1000, held(kHeldLeft));
    TEST_ASSERT_TRUE(pass(1001, held(kHeldLeft)));  // hold-repeat, as always
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_controllers_are_separate() {
    hold(0, 40, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(41, held(kHeldUp), held(kHeldDown)));  // the dome fires at once
    TEST_ASSERT_EQUAL_STRING(";W1;Cresetsilent", lastCommand());
    hold(42, kLong - 1, held(kHeldUp), held(0));
    TEST_ASSERT_TRUE(pass(kLong, held(kHeldUp), held(0)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cquiet", lastCommand());
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_a_dropped_controller_loses_the_press() {
    hold(0, 300, held(kHeldUp));
    pass(301, kOff);
    hold(302, 2000, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
    TEST_ASSERT_FALSE(rig->runtime.tapPending());
}

static void test_a_long_press_held_up_by_a_fault_is_dropped() {
    hold(0, 100, held(kHeldUp));
    // No button passes until 1.2 s (a controller fault skips them), the combo still held.
    hold(1200, 3000, held(kHeldUp));
    hold(3001, 4000, held(0));  // releasing afterwards fires nothing either
    TEST_ASSERT_EQUAL_UINT32(0, commands());
}

static void test_a_long_press_after_a_short_gap_still_fires() {
    hold(0, 100, held(kHeldUp));
    // within 250 ms of the threshold
    TEST_ASSERT_TRUE(pass(kLong + 200, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cquiet", lastCommand());
}

static void test_release_after_a_short_gap_is_fresh() {
    hold(0, 100, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(500, held(0)));  // a 400 ms gap, released before the threshold
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
}

static void test_release_after_a_gap_past_the_threshold_is_a_single() {
    hold(0, 100, held(kHeldUp));
    // A fault skips passes 101..649; the first pass back shows the button released.
    TEST_ASSERT_TRUE(pass(650, held(0)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    hold(651, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_modifier_after_a_gap_past_the_threshold_is_a_single_then_the_combo() {
    hold(0, 100, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(650, held(kHeldUp | kHeldL1)));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand(1));
    TEST_ASSERT_EQUAL_STRING(";W1;Cextremehappysou", lastCommand(0));
    hold(651, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_release_after_a_gap_with_a_double_tap_mapped_is_a_single() {
    mapSlot(kUp2, "twice");
    hold(0, 100, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(650, held(0)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    hold(651, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_stale_single_after_a_gap_is_dropped_when_still_held() {
    hold(0, 100, held(kHeldUp));
    pass(5000, held(kHeldUp));
    hold(5001, 6000, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
}

static void test_stale_single_after_a_gap_is_dropped_when_released() {
    hold(0, 100, held(kHeldUp));
    pass(5000, held(0));
    hold(5001, 6000, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
}

static void test_stale_single_is_dropped_but_another_combo_fires_alone() {
    hold(0, 100, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(5000, held(kHeldDown)));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
    TEST_ASSERT_EQUAL_STRING(";W1;Cangrysound", lastCommand());
    hold(5001, 7000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_stale_single_release_boundary() {
    hold(0, 100, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(kLong + Runtime::kDeferMaxMs, held(0)));  // exactly 850: fresh
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    setUp();
    mapSlot(kUpL, "quiet");
    hold(0, 100, held(kHeldUp));
    TEST_ASSERT_FALSE(pass(kLong + Runtime::kDeferMaxMs + 1, held(0)));  // 851: dropped
    hold(kLong + Runtime::kDeferMaxMs + 2, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
}

static void test_stale_single_with_a_double_tap_mapped_is_dropped() {
    mapSlot(kUp2, "twice");
    hold(0, 100, held(kHeldUp));
    pass(5000, held(0));
    hold(5001, 6000, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
}

static void test_a_tap_after_a_long_press_is_an_ordinary_press() {
    hold(0, kLong, held(kHeldUp));
    hold(kLong + 1, 999, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
    hold(1000, 1040, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(1041, held(0)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    hold(1042, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_no_long_press_inside_the_quiet_period_after_a_double_tap() {
    mapSlot(kUp2, "twice");
    hold(0, 40, held(kHeldUp));
    hold(41, 100, held(0));
    TEST_ASSERT_TRUE(pass(101, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Ctwice", lastCommand());
    hold(102, 140, held(kHeldUp));
    hold(141, 299, held(0));
    hold(300, 1200, held(kHeldUp));
    hold(1201, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_staleness_boundary() {
    hold(0, 100, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(kLong + Runtime::kDeferMaxMs, held(kHeldUp)));  // exactly 850: still fires
    TEST_ASSERT_EQUAL_STRING(";W1;Cquiet", lastCommand());
    TEST_ASSERT_EQUAL_UINT32(1, commands());
    setUp();
    mapSlot(kUpL, "quiet");
    hold(0, 100, held(kHeldUp));
    TEST_ASSERT_FALSE(pass(kLong + Runtime::kDeferMaxMs + 1, held(kHeldUp)));  // 851: dropped
    hold(kLong + Runtime::kDeferMaxMs + 2, 3000, held(kHeldUp));
    hold(3001, 4000, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
}

static void test_press_event_names_the_long_press() {
    char req[64];
    withCrc("@SM EVENTS 1", req, sizeof(req));
    TEST_ASSERT_TRUE(rig->protocol.handleLine(req, rig->wcbPort));
    hold(0, kLong, held(kHeldUp));
    hold(kLong + 1, kLong + 200, held(0));
    char body[96];
    bool saw = false;
    for (uint8_t i = 0; i < LineLog::kLines; ++i) {
        if (stripCrc(rig->wcb.usb.last(i), body, sizeof(body)) &&
            strcmp(body, "@SM PRESS F NONE UPL quiet eeprom") == 0)
            saw = true;
    }
    TEST_ASSERT_TRUE(saw);
}

static void test_unmapping_the_long_press_restores_the_old_timing() {
    mapSlot(kUpL, "");
    TEST_ASSERT_TRUE(pass(0, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    hold(1, 1000, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(1001, held(kHeldUp)));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_threshold_is_configurable() {
    Runtime quick(rig->store, rig->protocol, rig->tx, nullptr, Runtime::kDoubleTapMs, 400);
    FiredPress fired;
    const PadState up = held(kHeldUp);
    quick.buttons(0, up, kOff, &fired);
    TEST_ASSERT_FALSE(quick.buttons(399, up, kOff, &fired));
    TEST_ASSERT_TRUE(quick.buttons(400, up, kOff, &fired));
    TEST_ASSERT_EQUAL_STRING("quiet", fired.key);
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_long_press_fires_at_the_threshold_once);
    RUN_TEST(test_release_before_the_threshold_fires_the_single_on_release);
    RUN_TEST(test_quick_taps_are_single_presses_each);
    RUN_TEST(test_with_a_double_tap_quick_taps_still_double);
    RUN_TEST(test_with_a_double_tap_a_single_tap_fires_when_the_window_ends);
    RUN_TEST(test_release_between_the_window_and_the_threshold_fires_the_single);
    RUN_TEST(test_long_press_with_a_double_tap_mapped);
    RUN_TEST(test_another_combo_while_waiting_fires_the_single);
    RUN_TEST(test_modifier_landing_after_the_button);
    RUN_TEST(test_combos_without_a_long_press_fire_at_once_and_repeat);
    RUN_TEST(test_controllers_are_separate);
    RUN_TEST(test_a_dropped_controller_loses_the_press);
    RUN_TEST(test_a_long_press_held_up_by_a_fault_is_dropped);
    RUN_TEST(test_a_long_press_after_a_short_gap_still_fires);
    RUN_TEST(test_release_after_a_short_gap_is_fresh);
    RUN_TEST(test_release_after_a_gap_past_the_threshold_is_a_single);
    RUN_TEST(test_modifier_after_a_gap_past_the_threshold_is_a_single_then_the_combo);
    RUN_TEST(test_release_after_a_gap_with_a_double_tap_mapped_is_a_single);
    RUN_TEST(test_stale_single_after_a_gap_is_dropped_when_still_held);
    RUN_TEST(test_stale_single_after_a_gap_is_dropped_when_released);
    RUN_TEST(test_stale_single_is_dropped_but_another_combo_fires_alone);
    RUN_TEST(test_stale_single_release_boundary);
    RUN_TEST(test_stale_single_with_a_double_tap_mapped_is_dropped);
    RUN_TEST(test_a_tap_after_a_long_press_is_an_ordinary_press);
    RUN_TEST(test_no_long_press_inside_the_quiet_period_after_a_double_tap);
    RUN_TEST(test_staleness_boundary);
    RUN_TEST(test_press_event_names_the_long_press);
    RUN_TEST(test_unmapping_the_long_press_restores_the_old_timing);
    RUN_TEST(test_threshold_is_configurable);
    return UNITY_END();
}
