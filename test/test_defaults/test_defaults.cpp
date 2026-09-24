// Checks the real built-in table in src/default_mappings.cpp.
#include <unity.h>

#include <ShadowMap.h>
#include <default_mappings.h>
#include <sm_test_main.h>

using namespace sm;

namespace {

class NoEeprom final : public EepromIo {
  public:
    bool ready() override { return true; }
    uint8_t read(uint16_t) override { return 0xFF; }
    void write(uint16_t, uint8_t) override {}
};

const char *defaultFor(Ctrl c, Mod m, Btn b) {
    static NoEeprom io;
    static MapStore store(io, defaultButtonMappings, defaultButtonMappingsCount);
    static char key[kKeyBuf];
    store.defaultKey(slotIndex(c, m, b), key);
    return key;
}

}  // namespace

static void test_every_entry_is_valid_and_unique() {
    bool seen[kSlotCount] = {};
    TEST_ASSERT_EQUAL_UINT8(35, defaultButtonMappingsCount);
    for (uint8_t i = 0; i < defaultButtonMappingsCount; ++i) {
        const uint8_t slot = pgm_read_byte(&defaultButtonMappings[i].slot);
        char key[kKeyBuf];
        memcpy_P(key, defaultButtonMappings[i].key, kKeyBuf);
        TEST_ASSERT_TRUE(slot < kSlotCount);
        TEST_ASSERT_FALSE_MESSAGE(seen[slot], key);
        seen[slot] = true;
        TEST_ASSERT_TRUE_MESSAGE(slotReserved(slot) == Reserved::No, key);
        TEST_ASSERT_EQUAL_CHAR('\0', key[kKeyMax]);
        TEST_ASSERT_TRUE_MESSAGE(key[0] != '\0' && isValidKey(key), key);
    }
}

static void test_spot_checks() {
    TEST_ASSERT_EQUAL_STRING("extremehappysou", defaultFor(Ctrl::F, Mod::L1, Btn::UP));
    TEST_ASSERT_EQUAL_STRING("mahna", defaultFor(Ctrl::F, Mod::L2, Btn::PS));
    TEST_ASSERT_EQUAL_STRING("angrysound", defaultFor(Ctrl::F, Mod::NONE, Btn::DOWN));
    TEST_ASSERT_EQUAL_STRING("frontholocenter", defaultFor(Ctrl::D, Mod::L1, Btn::L3));
    TEST_ASSERT_EQUAL_STRING("GDrecruitsong", defaultFor(Ctrl::D, Mod::CROSS, Btn::DOWN));
    TEST_ASSERT_EQUAL_STRING("resetsilent", defaultFor(Ctrl::D, Mod::NONE, Btn::DOWN));
}

static void test_volume_and_brightness_are_gone() {
    const Btn dirs[] = {Btn::UP, Btn::RIGHT, Btn::DOWN, Btn::LEFT};
    for (Btn b : dirs) {
        TEST_ASSERT_EQUAL_STRING("", defaultFor(Ctrl::F, Mod::PS, b));  // was brightness
        TEST_ASSERT_EQUAL_STRING("", defaultFor(Ctrl::D, Mod::PS, b));  // was volume
    }
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_every_entry_is_valid_and_unique);
    RUN_TEST(test_spot_checks);
    RUN_TEST(test_volume_and_brightness_are_gone);
    return UNITY_END();
}
