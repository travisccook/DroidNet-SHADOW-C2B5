// Checks the controller-address helpers in lib/ShadowMap/src/sm_mac.h.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_test_main.h>

using namespace sm;

namespace {
const uint8_t kMacA[kMacLen] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x05};
}

static void test_parse_either_case() {
    uint8_t mac[kMacLen];
    TEST_ASSERT_TRUE(parseMac("00:1a:2B:3c:4D:05", mac));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mac, kMacLen);
}

static void test_parse_refuses_other_text() {
    const char *const bad[] = {"",
                               "00:1A:2B:3C:4D",
                               "00:1A:2B:3C:4D:05:",
                               "00:1A:2B:3C:4D:5",
                               "00:1A:2B:3C:4D:050",
                               "00-1A-2B-3C-4D-05",
                               "001A2B3C4D05",
                               "00:1A:2B:3C:4D:0G",
                               " 00:1A:2B:3C:4D:05",
                               "XX:XX:XX:XX:XX:XX",
                               "00:00:00:00:00:00",
                               "ff:ff:ff:ff:ff:ff"};
    uint8_t mac[kMacLen] = {1, 2, 3, 4, 5, 6};
    for (const char *s : bad)
        TEST_ASSERT_FALSE_MESSAGE(parseMac(s, mac), s);
    const uint8_t untouched[kMacLen] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(untouched, mac, kMacLen);
}

static void test_format_upper_case_two_digits_a_byte() {
    char text[kMacTextBuf];
    formatMac(kMacA, text);
    TEST_ASSERT_EQUAL_STRING("00:1A:2B:3C:4D:05", text);
}

static void test_usable() {
    const uint8_t zeros[kMacLen] = {0, 0, 0, 0, 0, 0};
    const uint8_t ones[kMacLen] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(macUsable(zeros));
    TEST_ASSERT_FALSE(macUsable(ones));
    TEST_ASSERT_TRUE(macUsable(kMacA));
}

static void test_matches_the_libraries_byte_order() {
    const uint8_t bdaddr[kMacLen] = {0x05, 0x4D, 0x3C, 0x2B, 0x1A, 0x00};  // as Btd.disc_bdaddr holds it
    TEST_ASSERT_TRUE(macMatches(kMacA, bdaddr));
    TEST_ASSERT_FALSE(macMatches(kMacA, kMacA));
}

static void test_slot_tokens() {
    const char *const names[] = {"F", "D", "F2", "D2"};
    for (uint8_t i = 0; i < kMacSlots; ++i) {
        uint8_t index = 0xFF;
        TEST_ASSERT_TRUE_MESSAGE(parseMacSlot(names[i], &index), names[i]);
        TEST_ASSERT_EQUAL_UINT8(i, index);
        char token[3];
        macSlotToken(i, token);
        TEST_ASSERT_EQUAL_STRING(names[i], token);
    }
    const char *const bad[] = {"", "f", "d2", "F3", "FD", "2", "D22", "F2 "};
    for (const char *s : bad) {
        uint8_t index = 0;
        TEST_ASSERT_FALSE_MESSAGE(parseMacSlot(s, &index), s);
    }
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_either_case);
    RUN_TEST(test_parse_refuses_other_text);
    RUN_TEST(test_format_upper_case_two_digits_a_byte);
    RUN_TEST(test_usable);
    RUN_TEST(test_matches_the_libraries_byte_order);
    RUN_TEST(test_slot_tokens);
    return UNITY_END();
}
