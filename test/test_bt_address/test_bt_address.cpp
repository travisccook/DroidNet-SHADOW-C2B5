// Checks formatBtAddress(), the text SHADOW compares with the addresses in
// include/controller_macs.h when a controller connects.
#include <unity.h>

#include <bt_address.h>
#include <sm_test_main.h>

// The USB Host library stores an address least significant byte first (Btd.disc_bdaddr).

static void test_last_byte_below_0x10_keeps_its_zero() {
    const uint8_t bdaddr[6] = {0x05, 0x0A, 0x00, 0x1B, 0x2C, 0x03};
    char text[kBtAddressBuf];
    formatBtAddress(bdaddr, text);
    TEST_ASSERT_EQUAL_STRING("03:2C:1B:00:0A:05", text);
}

static void test_uppercase_most_significant_byte_first() {
    const uint8_t bdaddr[6] = {0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45};
    char text[kBtAddressBuf];
    formatBtAddress(bdaddr, text);
    TEST_ASSERT_EQUAL_STRING("45:67:89:AB:CD:EF", text);
}

static void test_all_zero_and_all_ones() {
    const uint8_t zeros[6] = {0, 0, 0, 0, 0, 0};
    const uint8_t ones[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    char text[kBtAddressBuf];
    formatBtAddress(zeros, text);
    TEST_ASSERT_EQUAL_STRING("00:00:00:00:00:00", text);
    formatBtAddress(ones, text);
    TEST_ASSERT_EQUAL_STRING("FF:FF:FF:FF:FF:FF", text);
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_last_byte_below_0x10_keeps_its_zero);
    RUN_TEST(test_uppercase_most_significant_byte_first);
    RUN_TEST(test_all_zero_and_all_ones);
    return UNITY_END();
}
