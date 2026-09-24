// The store against the ATmega2560's real EEPROM (avr-libc, busy flag and all). Runs under
// simavr; skipped on the host.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#include <sm_test_main.h>

SM_DEFINE_TEST_DEFAULTS

#if defined(__AVR__)

using namespace sm;

namespace {

AvrEeprom io;

void eraseFootprint() {
    for (uint16_t a = 0; a < MapStore::kFootprint; ++a)
        eeprom_update_byte(reinterpret_cast<uint8_t *>(a), 0xFF);
    eeprom_busy_wait();
}

void runUntilIdle(MapStore &store) {
    for (uint32_t i = 0; i < 200000UL && store.busy(); ++i)
        store.step();
    TEST_ASSERT_FALSE(store.busy());
}

}  // namespace

static void test_set_reboot_defaults_on_real_eeprom() {
    eraseFootprint();
    {
        MapStore store(io, kTestDefaults, kTestDefaultCount);
        store.begin();
        TEST_ASSERT_FALSE(store.formatted());
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                                static_cast<uint8_t>(store.beginSet(kSlotFootNoneDown, "realeeprom")));
        runUntilIdle(store);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                                static_cast<uint8_t>(store.takeCompletion()));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginPrefix(";SEQ")));
        runUntilIdle(store);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                                static_cast<uint8_t>(store.takeCompletion()));
    }
    MapStore rebooted(io, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    TEST_ASSERT_TRUE(rebooted.formatted());
    TEST_ASSERT_EQUAL_STRING(";SEQ", rebooted.prefix());
    char key[kKeyBuf];
    Source src;
    TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key, &src));
    TEST_ASSERT_EQUAL_STRING("realeeprom", key);
    TEST_ASSERT_TRUE(src == Source::Eeprom);
    TEST_ASSERT_EQUAL_UINT8(1, rebooted.validEepromSlots());

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(rebooted.beginDefaults()));
    runUntilIdle(rebooted);
    TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key, &src));
    TEST_ASSERT_EQUAL_STRING("angrysound", key);
    TEST_ASSERT_TRUE(src == Source::Default);
}

#else

static void test_set_reboot_defaults_on_real_eeprom() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_set_reboot_defaults_on_real_eeprom);
    return UNITY_END();
}
