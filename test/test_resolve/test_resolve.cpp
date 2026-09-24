#include <unity.h>

#include <ShadowMap.h>
#include <sm_test_main.h>

using namespace sm;

static const PadState kOff = {false, 0};

static uint8_t foot(uint16_t held, PadState dome = kOff) { return resolveSlot(Ctrl::F, {true, held}, dome); }

static void test_gate() {
    TEST_ASSERT_FALSE(gateOpen(0));
    TEST_ASSERT_TRUE(gateOpen(kHeldUp));
    TEST_ASSERT_TRUE(gateOpen(kHeldDown));
    TEST_ASSERT_TRUE(gateOpen(kHeldLeft));
    TEST_ASSERT_TRUE(gateOpen(kHeldRight));
    TEST_ASSERT_TRUE(gateOpen(kHeldL3));
    TEST_ASSERT_FALSE(gateOpen(kHeldPs));
    TEST_ASSERT_TRUE(gateOpen(kHeldPs | kHeldL1));
    TEST_ASSERT_TRUE(gateOpen(kHeldPs | kHeldL2));
    TEST_ASSERT_FALSE(gateOpen(kHeldPs | kHeldCross));  // drive stick toggle, not a mapping
    TEST_ASSERT_FALSE(gateOpen(kHeldL1 | kHeldL2 | kHeldCircle | kHeldCross));
}

static void test_nothing_without_connection_or_gate() {
    TEST_ASSERT_EQUAL_UINT8(kNoSlot, resolveSlot(Ctrl::F, {false, kHeldUp}, kOff));
    TEST_ASSERT_EQUAL_UINT8(kNoSlot, foot(kHeldL1));
    TEST_ASSERT_EQUAL_UINT8(kNoSlot, foot(kHeldPs | kHeldCircle));
}

static void test_layers_and_priority() {
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::NONE, Btn::UP), foot(kHeldUp));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::L1, Btn::UP), foot(kHeldUp | kHeldL1));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::L1, Btn::UP), foot(kHeldUp | kHeldL1 | kHeldL2));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::L2, Btn::UP), foot(kHeldUp | kHeldL2 | kHeldCircle));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::PS, Btn::UP), foot(kHeldUp | kHeldPs | kHeldCircle));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::CIRCLE, Btn::UP), foot(kHeldUp | kHeldCircle | kHeldCross));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::CROSS, Btn::DOWN), foot(kHeldDown | kHeldCross));
}

static void test_primary_priority() {
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::NONE, Btn::UP), foot(kHeldUp | kHeldLeft));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::NONE, Btn::LEFT), foot(kHeldLeft | kHeldRight));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::NONE, Btn::RIGHT), foot(kHeldRight | kHeldDown));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::NONE, Btn::DOWN), foot(kHeldDown | kHeldL3));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::L1, Btn::PS), foot(kHeldPs | kHeldL1));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::L2, Btn::PS), foot(kHeldPs | kHeldL3 | kHeldL2));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::NONE, Btn::L3), foot(kHeldL3));
    // PS as the modifier never picks PS as the button.
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::PS, Btn::L3), foot(kHeldPs | kHeldL3));
    // L3 before PS in the L1 layer, PS before L3 elsewhere (the old tables' order).
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::D, Mod::L1, Btn::L3),
                            resolveSlot(Ctrl::D, {true, kHeldL1 | kHeldL3 | kHeldPs}, kOff));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::L1, Btn::L3), foot(kHeldL1 | kHeldL3 | kHeldPs));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::L2, Btn::PS), foot(kHeldL2 | kHeldL3 | kHeldPs));
}

static void test_reports_where_the_modifier_came_from() {
    bool fromOther = true;
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::CROSS, Btn::UP),
                            resolveSlot(Ctrl::F, {true, kHeldUp}, {true, kHeldCross}, &fromOther));
    TEST_ASSERT_TRUE(fromOther);
    resolveSlot(Ctrl::F, {true, kHeldUp | kHeldCross}, {true, kHeldCross}, &fromOther);
    TEST_ASSERT_FALSE(fromOther);  // held on both: this controller counts
    resolveSlot(Ctrl::F, {true, kHeldUp | kHeldL1}, {true, kHeldCross}, &fromOther);
    TEST_ASSERT_FALSE(fromOther);  // L1 decided
    resolveSlot(Ctrl::F, {true, kHeldUp}, kOff, &fromOther);
    TEST_ASSERT_FALSE(fromOther);
    TEST_ASSERT_EQUAL_UINT8(kNoSlot, resolveSlot(Ctrl::F, {true, 0}, kOff, &fromOther));
    TEST_ASSERT_FALSE(fromOther);
}

static void test_reserved_combo_resolves_to_its_slot() {
    // Foot L3+L1 is the over-speed toggle: it resolves, and the store maps it to nothing.
    const uint8_t slot = foot(kHeldL3 | kHeldL1);
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::L1, Btn::L3), slot);
    TEST_ASSERT_TRUE(slotReserved(slot) == Reserved::OverSpeed);
}

static void test_cross_controller_modifiers() {
    const PadState domeCross = {true, kHeldCross};
    const PadState domeL1 = {true, kHeldL1};
    const PadState domeGone = {false, kHeldCross};
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::CROSS, Btn::UP), foot(kHeldUp, domeCross));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::NONE, Btn::UP), foot(kHeldUp, domeGone));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::NONE, Btn::UP), foot(kHeldUp, domeL1));  // L1/L2 stay local
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::F, Mod::PS, Btn::LEFT), foot(kHeldLeft, {true, kHeldPs}));
    TEST_ASSERT_EQUAL_UINT8(slotIndex(Ctrl::D, Mod::CIRCLE, Btn::RIGHT),
                            resolveSlot(Ctrl::D, {true, kHeldRight}, {true, kHeldCircle}));
    // Primaries never come from the other controller.
    TEST_ASSERT_EQUAL_UINT8(kNoSlot, resolveSlot(Ctrl::D, {true, kHeldCross}, {true, kHeldUp}));
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_gate);
    RUN_TEST(test_nothing_without_connection_or_gate);
    RUN_TEST(test_layers_and_priority);
    RUN_TEST(test_primary_priority);
    RUN_TEST(test_reserved_combo_resolves_to_its_slot);
    RUN_TEST(test_cross_controller_modifiers);
    RUN_TEST(test_reports_where_the_modifier_came_from);
    return UNITY_END();
}
