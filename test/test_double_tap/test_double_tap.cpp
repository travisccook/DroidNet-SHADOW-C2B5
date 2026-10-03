// sm::Runtime double taps, driven pass by pass with the real built-in defaults, a fake EEPROM
// and a simulated WCB. F NONE UP is "happysound" by default; these tests map its double tap.
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

struct Rig {
    FakeEeprom eeprom;
    MapStore store{eeprom, defaultButtonMappings, defaultButtonMappingsCount};
    TxQueue tx;
    WcbPort wcbPort{tx};
    Protocol protocol{store, "2.0.0"};
    Runtime runtime{store, protocol, tx, "2.0.0"};
    WcbSim wcb;
    LineReader reader;
    NoSerial in;
    FiredPress fired = {};
};

Rig *rig = nullptr;
NoSerial noUsb;
LineReader noUsbLines;
NullReplyPort noUsbReplies;

const PadState kOff = {false, 0};
const uint8_t kUp2 = slotIndex(Ctrl::F, Mod::NONE, Btn::UP, Tap::DOUBLE);

bool pass(uint32_t t, PadState foot, PadState dome = kOff) {
    const bool fired = rig->runtime.buttons(t, foot, dome, &rig->fired);
    rig->runtime.afterButtons(t, fired, false, Runtime::Link<NoSerial>{rig->in, rig->reader, rig->wcbPort},
                              Runtime::Link<NoSerial>{noUsb, noUsbLines, noUsbReplies}, rig->wcb);
    rig->eeprom.tick();
    rig->wcb.refill();
    return fired;
}

PadState held(uint16_t bits) { return {true, bits}; }

// Runs passes every ms from `from` to `to` (inclusive) with the same buttons.
void hold(uint32_t from, uint32_t to, PadState foot, PadState dome = kOff) {
    for (uint32_t t = from; t <= to; ++t)
        pass(t, foot, dome);
}

void mapSlot(uint8_t slot, const char *key) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(rig->store.beginSet(slot, key)));
    while (rig->store.busy()) {
        rig->store.step();
        rig->eeprom.tick();
    }
    rig->store.takeCompletion();
}

uint32_t commands() { return rig->wcb.commands.count; }
const char *lastCommand() { return rig->wcb.commands.last(); }

}  // namespace

void setUp() {
    static Rig instance;
    instance.~Rig();
    new (&instance) Rig();
    rig = &instance;
    rig->store.begin();
    mapSlot(kUp2, "twice");
}

void tearDown() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.violations, "SHADOW broke a WCB rule");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->eeprom.violations, "EEPROM touched while busy");
}

static void test_double_tap_fires_only_the_double() {
    TEST_ASSERT_FALSE(pass(0, held(kHeldUp)));  // waits for a possible second tap
    hold(1, 60, held(kHeldUp));
    hold(61, 150, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
    TEST_ASSERT_TRUE(rig->runtime.tapPending());
    TEST_ASSERT_TRUE(pass(151, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Ctwice", lastCommand());
    TEST_ASSERT_EQUAL_UINT8(kUp2, rig->fired.slot);
    TEST_ASSERT_FALSE(rig->runtime.tapPending());
    // Held after a double tap: nothing more, and the single press never fires.
    hold(152, 4000, held(kHeldUp));
    hold(4001, 6000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_single_tap_fires_when_the_window_ends() {
    hold(0, 60, held(kHeldUp));
    hold(61, Runtime::kDoubleTapMs - 1, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
    TEST_ASSERT_TRUE(pass(Runtime::kDoubleTapMs, held(0)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    hold(Runtime::kDoubleTapMs + 1, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_held_past_the_window_fires_then_repeats() {
    hold(0, Runtime::kDoubleTapMs - 1, held(kHeldUp));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
    TEST_ASSERT_TRUE(pass(Runtime::kDoubleTapMs, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    hold(Runtime::kDoubleTapMs + 1, Runtime::kDoubleTapMs + 1000, held(kHeldUp));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
    TEST_ASSERT_TRUE(pass(Runtime::kDoubleTapMs + 1001, held(kHeldUp)));  // once a second, as always
    TEST_ASSERT_EQUAL_UINT32(2, commands());
    hold(Runtime::kDoubleTapMs + 1002, 5000, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_a_late_second_tap_is_two_single_presses() {
    hold(0, 60, held(kHeldUp));
    hold(61, 399, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());  // the window closed at 350
    hold(400, 460, held(kHeldUp));
    hold(461, 2000, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
}

static void test_the_window_runs_from_the_first_press() {
    hold(0, 300, held(kHeldUp));  // a long first press
    hold(301, 340, held(0));
    TEST_ASSERT_TRUE(pass(341, held(kHeldUp)));  // 341 ms after the first press: still a double tap
    TEST_ASSERT_EQUAL_STRING(";W1;Ctwice", lastCommand());
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_taps_right_after_a_double_are_ignored() {
    hold(0, 40, held(kHeldUp));
    hold(41, 100, held(0));
    hold(101, 140, held(kHeldUp));  // double tap
    hold(141, 200, held(0));
    hold(201, 240, held(kHeldUp));  // a third tap: ignored
    hold(241, 300, held(0));
    hold(301, 340, held(kHeldUp));  // and a fourth
    hold(341, 1100, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
    TEST_ASSERT_EQUAL_STRING(";W1;Ctwice", lastCommand());
    // A second after the double tap started, the combo works again.
    hold(1101, 1140, held(kHeldUp));
    hold(1141, 1180, held(0));
    hold(1181, 1220, held(kHeldUp));
    hold(1221, 1300, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
    TEST_ASSERT_EQUAL_STRING(";W1;Ctwice", lastCommand());
}

static void test_another_combo_ends_the_wait() {
    hold(0, 40, held(kHeldUp));
    hold(41, 100, held(0));
    TEST_ASSERT_TRUE(pass(101, held(kHeldDown)));  // F NONE DOWN: no double tap
    TEST_ASSERT_EQUAL_UINT32(2, commands());  // the first tap as a single press, at once, then DOWN
    TEST_ASSERT_EQUAL_STRING(";W1;Cangrysound", rig->wcb.commands.last(0));
    hold(102, 2000, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_combo_changing_under_a_held_first_press() {
    // UP pressed a moment before L1: the first pass sees F NONE UP, then F L1 UP.
    TEST_ASSERT_FALSE(pass(0, held(kHeldUp)));
    TEST_ASSERT_TRUE(pass(10, held(kHeldUp | kHeldL1)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cextremehappysou", lastCommand());
    TEST_ASSERT_EQUAL_UINT32(2, commands());  // F NONE UP counted as a single press (as it always has)
    hold(11, 2000, held(0));
    TEST_ASSERT_EQUAL_UINT32(2, commands());
}

static void test_combos_without_a_double_tap_fire_at_once() {
    TEST_ASSERT_TRUE(pass(0, held(kHeldLeft)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cscaredsound", lastCommand());
    TEST_ASSERT_FALSE(pass(1, held(kHeldLeft)));
}

static void test_controllers_are_separate() {
    // Foot first tap waiting; the dome fires straight away and doesn't disturb it.
    hold(0, 40, held(kHeldUp));
    TEST_ASSERT_TRUE(pass(41, held(0), held(kHeldDown)));
    TEST_ASSERT_EQUAL_STRING(";W1;Cresetsilent", lastCommand());
    hold(42, 100, held(0), held(0));
    TEST_ASSERT_TRUE(pass(101, held(kHeldUp), held(0)));
    TEST_ASSERT_EQUAL_STRING(";W1;Ctwice", lastCommand());
    TEST_ASSERT_EQUAL_UINT32(2, commands());

    // The dome's own double tap, while the foot is busy with something else.
    mapSlot(slotIndex(Ctrl::D, Mod::NONE, Btn::UP, Tap::DOUBLE), "domedouble");
    hold(3000, 3040, held(0), held(kHeldUp));
    hold(3041, 3100, held(kHeldLeft), held(0));
    hold(3101, 3140, held(0), held(kHeldUp));
    hold(3141, 5000, held(0), held(0));
    TEST_ASSERT_EQUAL_STRING(";W1;Cdomedouble", lastCommand());
    TEST_ASSERT_EQUAL_UINT32(4, commands());  // + scaredsound from the foot
}

static void test_a_dropped_controller_loses_its_first_tap() {
    hold(0, 40, held(kHeldUp));
    hold(41, 100, held(0));
    pass(101, kOff);
    hold(102, 2000, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
    TEST_ASSERT_FALSE(rig->runtime.tapPending());
}

static void test_a_first_tap_held_up_by_a_fault_is_dropped() {
    hold(0, 40, held(kHeldUp));
    pass(41, held(0));
    // No button passes for 2 s (a controller fault skips them), then carry on.
    hold(2041, 3000, held(0));
    TEST_ASSERT_EQUAL_UINT32(0, commands());
    TEST_ASSERT_FALSE(rig->runtime.tapPending());
}

static void test_unmapping_the_double_tap_restores_the_old_timing() {
    mapSlot(kUp2, "");
    TEST_ASSERT_TRUE(pass(0, held(kHeldUp)));
    TEST_ASSERT_EQUAL_STRING(";W1;Chappysound", lastCommand());
    hold(1, 40, held(kHeldUp));
    hold(41, 100, held(0));
    TEST_ASSERT_FALSE(pass(101, held(kHeldUp)));  // a quick second tap: ignored, as before
    hold(102, 150, held(kHeldUp));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_double_tap_with_a_modifier_and_an_unmapped_single() {
    // F PS UP has no single-press mapping; its double tap can still be used.
    mapSlot(slotIndex(Ctrl::F, Mod::PS, Btn::UP, Tap::DOUBLE), "psdouble");
    hold(0, 40, held(kHeldPs | kHeldUp));
    hold(41, 100, held(kHeldPs));  // PS stays down between the taps
    hold(101, 140, held(kHeldPs | kHeldUp));
    TEST_ASSERT_EQUAL_STRING(";W1;Cpsdouble", lastCommand());
    hold(141, 2000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
    // A single tap of it fires nothing (unmapped), late.
    hold(3000, 3040, held(kHeldPs | kHeldUp));
    hold(3041, 5000, held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
}

static void test_press_event_names_the_double_tap() {
    mapSlot(slotIndex(Ctrl::F, Mod::CROSS, Btn::UP, Tap::DOUBLE), "leiatwice");
    char req[64];
    withCrc("@SM EVENTS 1", req, sizeof(req));
    TEST_ASSERT_TRUE(rig->protocol.handleLine(req, rig->wcbPort));
    // UP twice on the foot, with CROSS held on the dome.
    hold(0, 40, held(kHeldUp), held(kHeldCross));
    hold(41, 100, held(0), held(kHeldCross));
    hold(101, 140, held(kHeldUp), held(kHeldCross));
    hold(141, 200, held(0), held(0));
    TEST_ASSERT_EQUAL_UINT32(1, commands());
    TEST_ASSERT_EQUAL_STRING(";W1;Cleiatwice", lastCommand());
    TEST_ASSERT_TRUE(rig->fired.modFromOther);
    char body[96];
    bool saw = false;
    for (uint8_t i = 0; i < LineLog::kLines; ++i) {
        if (stripCrc(rig->wcb.usb.last(i), body, sizeof(body)) &&
            strcmp(body, "@SM PRESS F CROSS UP2 leiatwice eeprom from=D") == 0)
            saw = true;
    }
    TEST_ASSERT_TRUE(saw);
}

static void test_window_is_configurable() {
    Runtime quick(rig->store, rig->protocol, rig->tx, nullptr, 200);
    FiredPress fired;
    const PadState up = held(kHeldUp), none = held(0);
    quick.buttons(0, up, kOff, &fired);
    quick.buttons(40, none, kOff, &fired);
    TEST_ASSERT_FALSE(quick.buttons(199, none, kOff, &fired));
    TEST_ASSERT_TRUE(quick.buttons(200, none, kOff, &fired));  // single, at 200 ms
    TEST_ASSERT_EQUAL_STRING("happysound", fired.key);
    quick.buttons(1000, up, kOff, &fired);
    quick.buttons(1040, none, kOff, &fired);
    // The next pass comes 210 ms later: too late for a double tap. The first tap fires as a
    // single press, and this press starts a new one.
    fired.key[0] = '\0';
    TEST_ASSERT_TRUE(quick.buttons(1210, up, kOff, &fired));
    TEST_ASSERT_EQUAL_STRING("happysound", fired.key);
    TEST_ASSERT_TRUE(quick.tapPending());
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_double_tap_fires_only_the_double);
    RUN_TEST(test_single_tap_fires_when_the_window_ends);
    RUN_TEST(test_held_past_the_window_fires_then_repeats);
    RUN_TEST(test_a_late_second_tap_is_two_single_presses);
    RUN_TEST(test_the_window_runs_from_the_first_press);
    RUN_TEST(test_taps_right_after_a_double_are_ignored);
    RUN_TEST(test_another_combo_ends_the_wait);
    RUN_TEST(test_combo_changing_under_a_held_first_press);
    RUN_TEST(test_combos_without_a_double_tap_fire_at_once);
    RUN_TEST(test_controllers_are_separate);
    RUN_TEST(test_a_dropped_controller_loses_its_first_tap);
    RUN_TEST(test_a_first_tap_held_up_by_a_fault_is_dropped);
    RUN_TEST(test_unmapping_the_double_tap_restores_the_old_timing);
    RUN_TEST(test_double_tap_with_a_modifier_and_an_unmapped_single);
    RUN_TEST(test_press_event_names_the_double_tap);
    RUN_TEST(test_window_is_configurable);
    return UNITY_END();
}
