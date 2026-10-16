// Checks MapStore's controller-address records (WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md, Storage).
#include <unity.h>

#include <ShadowMap.h>
#include <new>
#include <sm_fakes.h>
#define SM_TEST_OWN_SETUP
#include <sm_test_main.h>

using namespace sm;

SM_DEFINE_TEST_DEFAULTS

namespace {

const uint8_t kMacA[kMacLen] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x05};
const uint8_t kMacB[kMacLen] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
constexpr uint8_t kF = static_cast<uint8_t>(MacSlot::F);
constexpr uint8_t kD2 = static_cast<uint8_t>(MacSlot::D2);

struct Rig {
    FakeEeprom eeprom;
    MapStore store{eeprom, kTestDefaults, kTestDefaultCount};
};
Rig *rig = nullptr;

// A fresh MapStore over the same EEPROM, as after a reboot.
void reboot() {
    rig->store.~MapStore();
    new (&rig->store) MapStore(rig->eeprom, kTestDefaults, kTestDefaultCount);
    rig->store.begin();
}

MapStore::Completion run() {
    for (int i = 0; i < 2000 && rig->store.busy(); ++i) {
        rig->store.step();
        rig->eeprom.tick();
    }
    TEST_ASSERT_FALSE(rig->store.busy());
    return rig->store.takeCompletion();
}

void write(uint8_t index, MapStore::MacState state, const uint8_t *mac) {
    TEST_ASSERT_TRUE(rig->store.beginMac(index, state, mac) == MapStore::Result::Ok);
    TEST_ASSERT_TRUE(run() == MapStore::Completion::Done);
}

void reversed(const uint8_t *mac, uint8_t *bdaddr) {
    for (uint8_t i = 0; i < kMacLen; ++i)
        bdaddr[i] = mac[kMacLen - 1 - i];
}

uint16_t recordAddr(uint8_t index) {
    return static_cast<uint16_t>(MapStore::kFootprint + index * MapStore::kMacRecordSize);
}

}  // namespace

void setUp() {
    static Rig instance;
    instance.~Rig();
    new (&instance) Rig();
    rig = &instance;
    rig->store.begin();
}

void tearDown() { TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->eeprom.violations, "EEPROM touched while busy"); }

static void test_layout() {
    TEST_ASSERT_EQUAL_UINT16(3952, MapStore::kFootprint);
    TEST_ASSERT_EQUAL_UINT16(36, MapStore::kMacFootprint);
    TEST_ASSERT_EQUAL_UINT16(3988, MapStore::kEepromUsed);
}

static void test_fresh_eeprom_uses_the_build() {
    uint8_t mac[kMacLen];
    Source src = Source::Eeprom;
    TEST_ASSERT_FALSE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_TRUE(src == Source::Default);
    rig->store.setBuiltInMac(kF, kMacA);
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mac, kMacLen);
    TEST_ASSERT_TRUE(src == Source::Default);
    rig->store.setBuiltInMac(kF, nullptr);
    TEST_ASSERT_FALSE(rig->store.macFor(kF, mac));
}

static void test_record_overrides_the_build_and_survives_a_reboot() {
    rig->store.setBuiltInMac(kF, kMacA);
    write(kF, MapStore::MacState::Address, kMacB);
    uint8_t mac[kMacLen];
    Source src = Source::Default;
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacB, mac, kMacLen);
    TEST_ASSERT_TRUE(src == Source::Eeprom);
    reboot();
    rig->store.setBuiltInMac(kF, kMacA);
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacB, mac, kMacLen);
}

static void test_none_blocks_the_build() {
    rig->store.setBuiltInMac(kF, kMacA);
    write(kF, MapStore::MacState::None, nullptr);
    uint8_t mac[kMacLen];
    Source src = Source::Default;
    TEST_ASSERT_FALSE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_TRUE(src == Source::Eeprom);
    uint8_t bdaddr[kMacLen];
    reversed(kMacA, bdaddr);
    TEST_ASSERT_FALSE(rig->store.macAllows(Ctrl::F, bdaddr));
}

static void test_default_erases_the_record() {
    rig->store.setBuiltInMac(kF, kMacA);
    write(kF, MapStore::MacState::Address, kMacB);
    write(kF, MapStore::MacState::NotSet, nullptr);
    for (uint8_t i = 0; i < MapStore::kMacRecordSize; ++i)
        TEST_ASSERT_EQUAL_HEX8(0xFF, rig->eeprom.peek(static_cast<uint16_t>(recordAddr(kF) + i)));
    uint8_t mac[kMacLen];
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mac, kMacLen);
}

static void test_damaged_records_read_as_not_set() {
    rig->store.setBuiltInMac(kF, kMacA);
    write(kF, MapStore::MacState::Address, kMacB);
    rig->eeprom.poke(static_cast<uint16_t>(recordAddr(kF) + 3), 0x00);  // an address byte: the CRC fails
    reboot();
    rig->store.setBuiltInMac(kF, kMacA);
    uint8_t mac[kMacLen];
    Source src = Source::Eeprom;
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mac, kMacLen);
    TEST_ASSERT_TRUE(src == Source::Default);

    // An unknown state with a CRC that matches it still reads as not set.
    uint8_t rec[MapStore::kMacRecordSize] = {3, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0, 0};
    const uint8_t index = kF;
    const uint16_t crc = crc16Block(crc16Block(kCrc16Init, &index, 1), rec, 1 + kMacLen);
    rec[7] = static_cast<uint8_t>(crc & 0xFF);
    rec[8] = static_cast<uint8_t>(crc >> 8);
    for (uint8_t i = 0; i < MapStore::kMacRecordSize; ++i)
        rig->eeprom.poke(static_cast<uint16_t>(recordAddr(kF) + i), rec[i]);
    reboot();
    TEST_ASSERT_FALSE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_TRUE(src == Source::Default);
}

static void test_power_cut_mid_write() {
    rig->store.setBuiltInMac(kF, kMacA);
    rig->eeprom.budget = 4;  // four bytes land, then the power goes
    TEST_ASSERT_TRUE(rig->store.beginMac(kF, MapStore::MacState::Address, kMacB) == MapStore::Result::Ok);
    TEST_ASSERT_TRUE(run() == MapStore::Completion::Failed);
    rig->eeprom.powerOn();
    reboot();
    rig->store.setBuiltInMac(kF, kMacA);
    uint8_t mac[kMacLen];
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mac, kMacLen);  // the torn record reads as not set
}

static void test_defaults_leave_the_records_alone() {
    TEST_ASSERT_TRUE(rig->store.beginSet(kSlotFootL1Up, "custom") == MapStore::Result::Ok);  // formats the map
    TEST_ASSERT_TRUE(run() == MapStore::Completion::Done);
    write(kD2, MapStore::MacState::Address, kMacB);
    TEST_ASSERT_TRUE(rig->store.beginDefaults() == MapStore::Result::Ok);
    TEST_ASSERT_TRUE(run() == MapStore::Completion::Done);
    reboot();
    uint8_t mac[kMacLen];
    TEST_ASSERT_TRUE(rig->store.macFor(kD2, mac));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacB, mac, kMacLen);
}

static void test_allows_by_role() {
    write(kF, MapStore::MacState::Address, kMacA);
    rig->store.setBuiltInMac(kD2, kMacB);
    uint8_t a[kMacLen];
    uint8_t b[kMacLen];
    reversed(kMacA, a);
    reversed(kMacB, b);
    TEST_ASSERT_TRUE(rig->store.macAllows(Ctrl::F, a));
    TEST_ASSERT_FALSE(rig->store.macAllows(Ctrl::D, a));
    TEST_ASSERT_TRUE(rig->store.macAllows(Ctrl::D, b));
    TEST_ASSERT_FALSE(rig->store.macAllows(Ctrl::F, b));
    const uint8_t zeros[kMacLen] = {0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(rig->store.macAllows(Ctrl::F, zeros));  // empty slots match nothing
    TEST_ASSERT_FALSE(rig->store.macAllows(Ctrl::D, zeros));
}

static void test_changes_count_finished_writes() {
    const uint8_t before = rig->store.macChanges();
    write(kF, MapStore::MacState::Address, kMacA);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before + 1), rig->store.macChanges());
    write(kF, MapStore::MacState::None, nullptr);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before + 2), rig->store.macChanges());
}

static void test_busy_and_bad_arguments() {
    const uint8_t zeros[kMacLen] = {0, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(rig->store.beginMac(4, MapStore::MacState::Address, kMacA) == MapStore::Result::BadArg);
    TEST_ASSERT_TRUE(rig->store.beginMac(kF, MapStore::MacState::Address, zeros) == MapStore::Result::BadArg);
    TEST_ASSERT_TRUE(rig->store.beginMac(kF, MapStore::MacState::Address, nullptr) == MapStore::Result::BadArg);
    TEST_ASSERT_TRUE(rig->store.beginMac(kF, MapStore::MacState::Address, kMacA) == MapStore::Result::Ok);
    TEST_ASSERT_TRUE(rig->store.beginMac(kD2, MapStore::MacState::Address, kMacB) == MapStore::Result::Busy);
    TEST_ASSERT_TRUE(rig->store.beginSet(kSlotFootL1Up, "custom") == MapStore::Result::Busy);
    TEST_ASSERT_TRUE(run() == MapStore::Completion::Done);
}

static void test_rewriting_the_same_value_writes_nothing() {
    write(kF, MapStore::MacState::Address, kMacA);
    const uint16_t before = rig->store.writesIssued();
    write(kF, MapStore::MacState::Address, kMacA);
    TEST_ASSERT_EQUAL_UINT16(before, rig->store.writesIssued());
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_layout);
    RUN_TEST(test_fresh_eeprom_uses_the_build);
    RUN_TEST(test_record_overrides_the_build_and_survives_a_reboot);
    RUN_TEST(test_none_blocks_the_build);
    RUN_TEST(test_default_erases_the_record);
    RUN_TEST(test_damaged_records_read_as_not_set);
    RUN_TEST(test_power_cut_mid_write);
    RUN_TEST(test_defaults_leave_the_records_alone);
    RUN_TEST(test_allows_by_role);
    RUN_TEST(test_changes_count_finished_writes);
    RUN_TEST(test_busy_and_bad_arguments);
    RUN_TEST(test_rewriting_the_same_value_writes_nothing);
    return UNITY_END();
}
