#include <unity.h>

#include <ShadowMap.h>
#include <sm_test_main.h>

using sm::Btn;
using sm::Ctrl;
using sm::Mod;

static const uint8_t kCheck[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

static void test_crc16_ccitt_false_check_value() {
    uint16_t crc = sm::kCrc16Init;
    for (uint8_t b : kCheck)
        crc = sm::crc16Update(crc, b);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc);
}

static void test_crc32_ieee_check_value_and_incremental() {
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, sm::crc32(kCheck, sizeof(kCheck)));
    uint32_t crc = sm::kCrc32Init;
    for (uint8_t b : kCheck)
        crc = sm::crc32Update(crc, b);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, sm::crc32Final(crc));
    TEST_ASSERT_EQUAL_HEX32(0x00000000UL, sm::crc32(kCheck, 0));
}

static void test_slot_index_round_trip() {
    TEST_ASSERT_EQUAL_UINT8(72, sm::kSingleSlotCount);
    TEST_ASSERT_EQUAL_UINT8(144, sm::kSlotCount);
    TEST_ASSERT_EQUAL_UINT8(0, sm::slotIndex(Ctrl::F, Mod::NONE, Btn::UP));
    TEST_ASSERT_EQUAL_UINT8(71, sm::slotIndex(Ctrl::D, Mod::CROSS, Btn::PS));
    TEST_ASSERT_EQUAL_UINT8(72, sm::slotIndex(Ctrl::F, Mod::NONE, Btn::UP, sm::Tap::DOUBLE));
    TEST_ASSERT_EQUAL_UINT8(143, sm::slotIndex(Ctrl::D, Mod::CROSS, Btn::PS, sm::Tap::DOUBLE));
    for (uint8_t slot = 0; slot < sm::kSlotCount; ++slot) {
        TEST_ASSERT_EQUAL_UINT8(slot, sm::slotIndex(sm::slotCtrl(slot), sm::slotMod(slot), sm::slotBtn(slot),
                                                    sm::slotTap(slot)));
        TEST_ASSERT_EQUAL_UINT8(slot % 72 + 72, sm::doubleOf(slot));
    }
}

static void test_format_and_parse_every_slot() {
    char text[sm::kSlotTextBuf];
    TEST_ASSERT_EQUAL_UINT8(7, sm::formatSlot(text, sizeof(text), sm::slotIndex(Ctrl::F, Mod::L1, Btn::UP)));
    TEST_ASSERT_EQUAL_STRING("F L1 UP", text);
    sm::formatSlot(text, sizeof(text), sm::slotIndex(Ctrl::D, Mod::CIRCLE, Btn::RIGHT));
    TEST_ASSERT_EQUAL_STRING("D CIRCLE RIGHT", text);
    // The longest slot name fits the buffer exactly.
    TEST_ASSERT_EQUAL_UINT8(15, sm::formatSlot(text, sizeof(text),
                                               sm::slotIndex(Ctrl::D, Mod::CIRCLE, Btn::RIGHT, sm::Tap::DOUBLE)));
    TEST_ASSERT_EQUAL_STRING("D CIRCLE RIGHT2", text);
    TEST_ASSERT_EQUAL_UINT8(0, sm::formatSlot(text, 5, 0));
    TEST_ASSERT_EQUAL_UINT8(0, sm::formatSlot(text, sizeof(text), sm::kSlotCount));

    for (uint8_t slot = 0; slot < sm::kSlotCount; ++slot) {
        TEST_ASSERT_TRUE(sm::formatSlot(text, sizeof(text), slot) > 0);
        char *c = text;
        char *m = strchr(c, ' ');
        *m++ = '\0';
        char *b = strchr(m, ' ');
        *b++ = '\0';
        Ctrl pc;
        Mod pm;
        Btn pb;
        sm::Tap pt;
        TEST_ASSERT_TRUE(sm::parseCtrl(c, &pc));
        TEST_ASSERT_TRUE(sm::parseMod(m, &pm));
        TEST_ASSERT_TRUE(sm::parseBtn(b, &pb, &pt));
        TEST_ASSERT_EQUAL_UINT8(slot, sm::slotIndex(pc, pm, pb, pt));
        TEST_ASSERT_EQUAL(slot < 72, sm::parseBtn(b, &pb));  // without `tap`, only single presses
    }
}

static void test_parse_rejects_near_misses() {
    Ctrl c;
    Mod m;
    Btn b;
    TEST_ASSERT_FALSE(sm::parseCtrl("f", &c));
    TEST_ASSERT_FALSE(sm::parseCtrl("FOOT", &c));
    TEST_ASSERT_FALSE(sm::parseCtrl("", &c));
    TEST_ASSERT_FALSE(sm::parseMod("l1", &m));
    TEST_ASSERT_FALSE(sm::parseMod("NONE ", &m));
    TEST_ASSERT_FALSE(sm::parseMod("X", &m));
    TEST_ASSERT_FALSE(sm::parseBtn("UPX", &b));
    TEST_ASSERT_FALSE(sm::parseBtn("CROSS", &b));
    TEST_ASSERT_TRUE(sm::parseMod("PS", &m));
    TEST_ASSERT_TRUE(sm::parseBtn("PS", &b));
    sm::Tap t;
    TEST_ASSERT_FALSE(sm::parseBtn("UP3", &b, &t));
    TEST_ASSERT_FALSE(sm::parseBtn("UP22", &b, &t));
    TEST_ASSERT_FALSE(sm::parseBtn("2", &b, &t));
    TEST_ASSERT_FALSE(sm::parseBtn("CROSS2", &b, &t));
    TEST_ASSERT_FALSE(sm::parseBtn("up2", &b, &t));
    TEST_ASSERT_FALSE(sm::parseBtn("RIGHTX2", &b, &t));
    TEST_ASSERT_TRUE(sm::parseBtn("RIGHT2", &b, &t));
    TEST_ASSERT_TRUE(b == Btn::RIGHT && t == sm::Tap::DOUBLE);
    TEST_ASSERT_TRUE(sm::parseBtn("L32", &b, &t));  // a token; the slot is reserved
    TEST_ASSERT_TRUE(b == Btn::L3 && t == sm::Tap::DOUBLE);
}

static void test_reserved_slots() {
    uint8_t count = 0;
    for (uint8_t slot = 0; slot < sm::kSlotCount; ++slot) {
        if (sm::slotReserved(slot) != sm::Reserved::No)
            ++count;
    }
    TEST_ASSERT_EQUAL_UINT8(29, count);  // 9 single presses; their double taps and every L3 double tap
    for (uint8_t c = 0; c < 2; ++c) {
        const Ctrl ctrl = static_cast<Ctrl>(c);
        TEST_ASSERT_TRUE(sm::slotReserved(sm::slotIndex(ctrl, Mod::NONE, Btn::PS)) == sm::Reserved::PsGate);
        TEST_ASSERT_TRUE(sm::slotReserved(sm::slotIndex(ctrl, Mod::PS, Btn::PS)) == sm::Reserved::PsGate);
        TEST_ASSERT_TRUE(sm::slotReserved(sm::slotIndex(ctrl, Mod::CIRCLE, Btn::PS)) == sm::Reserved::PsGate);
        TEST_ASSERT_TRUE(sm::slotReserved(sm::slotIndex(ctrl, Mod::CROSS, Btn::PS)) == sm::Reserved::PsGate);
        TEST_ASSERT_TRUE(sm::slotReserved(sm::slotIndex(ctrl, Mod::L1, Btn::PS)) == sm::Reserved::No);
        TEST_ASSERT_TRUE(sm::slotReserved(sm::slotIndex(ctrl, Mod::L2, Btn::PS)) == sm::Reserved::No);
    }
    TEST_ASSERT_TRUE(sm::slotReserved(sm::slotIndex(Ctrl::F, Mod::L1, Btn::L3)) == sm::Reserved::OverSpeed);
    TEST_ASSERT_TRUE(sm::slotReserved(sm::slotIndex(Ctrl::D, Mod::L1, Btn::L3)) == sm::Reserved::No);
    TEST_ASSERT_NULL(sm::reservedToken(sm::Reserved::No));

    // Double taps: the same reservations, and none on the stick click.
    for (uint8_t slot = 0; slot < 72; ++slot) {
        const sm::Reserved single = sm::slotReserved(slot);
        const sm::Reserved dbl = sm::slotReserved(sm::doubleOf(slot));
        if (single != sm::Reserved::No)
            TEST_ASSERT_TRUE(dbl == single);
        else if (sm::slotBtn(slot) == Btn::L3)
            TEST_ASSERT_TRUE(dbl == sm::Reserved::Stick);
        else
            TEST_ASSERT_TRUE(dbl == sm::Reserved::No);
    }
    TEST_ASSERT_TRUE(sm::slotReserved(sm::slotIndex(Ctrl::D, Mod::L1, Btn::L3, sm::Tap::DOUBLE)) == sm::Reserved::Stick);
    TEST_ASSERT_TRUE(sm::slotReserved(sm::slotIndex(Ctrl::F, Mod::L2, Btn::PS, sm::Tap::DOUBLE)) == sm::Reserved::No);
}

static void test_key_validation() {
    TEST_ASSERT_TRUE(sm::isValidKey(""));
    TEST_ASSERT_TRUE(sm::isValidKey("a"));
    TEST_ASSERT_TRUE(sm::isValidKey("GD_recruit_09"));
    TEST_ASSERT_TRUE(sm::isValidKey("extremehappysou"));  // 15
    TEST_ASSERT_FALSE(sm::isValidKey("extremehappysoun"));  // 16
    TEST_ASSERT_FALSE(sm::isValidKey("a-b"));
    TEST_ASSERT_FALSE(sm::isValidKey("a b"));
    TEST_ASSERT_FALSE(sm::isValidKey("a^b"));
    TEST_ASSERT_FALSE(sm::isValidKey("\xE9t\xE9"));
}

static void test_prefix_and_suffix_validation() {
    TEST_ASSERT_TRUE(sm::isValidPrefix(";W1;C"));
    TEST_ASSERT_TRUE(sm::isValidPrefix(";SEQ"));
    TEST_ASSERT_TRUE(sm::isValidPrefix(";W12,;SEQ12"));  // 11
    TEST_ASSERT_FALSE(sm::isValidPrefix(";W12,;SEQ123"));  // 12
    TEST_ASSERT_FALSE(sm::isValidPrefix(""));
    TEST_ASSERT_FALSE(sm::isValidPrefix("W1;C"));   // bare line would be broadcast
    TEST_ASSERT_FALSE(sm::isValidPrefix(";W1;"));   // ";" + key could read as ";t..."
    TEST_ASSERT_FALSE(sm::isValidPrefix(";W1^C"));
    TEST_ASSERT_FALSE(sm::isValidPrefix(";W1 C"));
    TEST_ASSERT_FALSE(sm::isValidPrefix(";T1;W;C"));  // a WCB timer command
    TEST_ASSERT_FALSE(sm::isValidPrefix(";W1;tC"));

    TEST_ASSERT_TRUE(sm::isValidSuffix(""));
    TEST_ASSERT_TRUE(sm::isValidSuffix(",L"));
    TEST_ASSERT_TRUE(sm::isValidSuffix("1234567"));
    TEST_ASSERT_FALSE(sm::isValidSuffix("12345678"));
    TEST_ASSERT_FALSE(sm::isValidSuffix("^;C"));
    TEST_ASSERT_FALSE(sm::isValidSuffix(", L"));
    TEST_ASSERT_FALSE(sm::isValidSuffix(";t5"));
    TEST_ASSERT_FALSE(sm::isValidSuffix(",;T"));
    TEST_ASSERT_TRUE(sm::isValidSuffix(",T"));
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_crc16_ccitt_false_check_value);
    RUN_TEST(test_crc32_ieee_check_value_and_incremental);
    RUN_TEST(test_slot_index_round_trip);
    RUN_TEST(test_format_and_parse_every_slot);
    RUN_TEST(test_parse_rejects_near_misses);
    RUN_TEST(test_reserved_slots);
    RUN_TEST(test_key_validation);
    RUN_TEST(test_prefix_and_suffix_validation);
    return UNITY_END();
}
