// MapStore raw commands: the record (bit 7 of the first byte), its rules, and every way a slot
// is read back. A raw command is text SHADOW sends to its WCB exactly as typed.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#define SM_TEST_OWN_SETUP
#include <sm_test_main.h>
#include <stdio.h>

using namespace sm;

SM_DEFINE_TEST_DEFAULTS

namespace {

FakeEeprom *eeprom = nullptr;
// Power-cut snapshot: the headers and single-press slots, which is all these tests write there.
uint8_t baseMem[FakeEeprom::kFlat];

const char kRaw[] = ";W2;S4PLAY:3^;T";  // 15 characters, with the WCB's own '^' and ";T"

uint8_t result(MapStore::Result r) { return static_cast<uint8_t>(r); }

void snapshot() {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, eeprom->doubleRecordsUsed(), "snapshot() only covers the single-press slots");
    memcpy(baseMem, eeprom->mem, sizeof(baseMem));
}
void restore() {
    eeprom->reset();
    memcpy(eeprom->mem, baseMem, sizeof(baseMem));
}

void runUntilIdle(MapStore &store) {
    for (uint16_t passes = 0; store.busy() && passes < 5000; ++passes) {
        store.step();
        eeprom->tick();
    }
    TEST_ASSERT_FALSE_MESSAGE(store.busy(), "store never finished");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, eeprom->violations, "EEPROM touched while busy");
}

void expectValue(MapStore &store, uint8_t slot, const char *want, bool wantRaw, Source wantSrc) {
    char key[kKeyBuf];
    Source src = Source::Reserved;
    bool raw = !wantRaw;
    TEST_ASSERT_TRUE(store.keyForSlot(slot, key, &src, &raw));
    TEST_ASSERT_EQUAL_STRING(want, key);
    TEST_ASSERT_TRUE_MESSAGE(raw == wantRaw, want);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(wantSrc), static_cast<uint8_t>(src));
}

void set(MapStore &store, uint8_t slot, const char *text, bool raw) {
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok), result(store.beginSet(slot, text, raw)));
    runUntilIdle(store);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                            static_cast<uint8_t>(store.takeCompletion()));
}

uint16_t recordAddr(uint8_t slot) {
    return static_cast<uint16_t>(2 * MapStore::kHeaderSize + slot * MapStore::kSlotSize);
}

// Writes a single-press slot record by hand, with the CRC the store computes (slotCrc in
// sm_store.cpp: layout version, generation, slot index, then the 16 name bytes).
void writeRecord(uint8_t slot, uint16_t gen, const uint8_t name[kKeyBuf]) {
    uint16_t crc = kCrc16Init;
    crc = crc16Update(crc, MapStore::kLayoutVersion);
    crc = crc16Update(crc, static_cast<uint8_t>(gen & 0xFF));
    crc = crc16Update(crc, static_cast<uint8_t>(gen >> 8));
    crc = crc16Update(crc, slot);
    for (uint8_t i = 0; i < kKeyBuf; ++i)
        crc = crc16Update(crc, name[i]);
    uint8_t *rec = eeprom->mem + recordAddr(slot);
    memcpy(rec, name, kKeyBuf);
    rec[kKeyBuf] = static_cast<uint8_t>(crc & 0xFF);
    rec[kKeyBuf + 1] = static_cast<uint8_t>(crc >> 8);
}

}  // namespace

void tearDown() {}

void setUp() {
    static FakeEeprom instance;
    instance.reset();
    eeprom = &instance;
}

static void test_raw_text_rules() {
    TEST_ASSERT_TRUE(isValidRaw(kRaw));
    TEST_ASSERT_TRUE(isValidRaw(" "));
    TEST_ASSERT_TRUE(isValidRaw("*"));
    TEST_ASSERT_TRUE(isValidRaw("~~~~~~~~~~~~~~~"));    // 15
    TEST_ASSERT_FALSE(isValidRaw("~~~~~~~~~~~~~~~~"));  // 16
    TEST_ASSERT_FALSE(isValidRaw(""));
    TEST_ASSERT_FALSE(isValidRaw("a\nb"));
    TEST_ASSERT_FALSE(isValidRaw("\x1F"));
    TEST_ASSERT_FALSE(isValidRaw("\x7F"));
    TEST_ASSERT_FALSE(isValidRaw("caf\xC3\xA9"));
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::BadKey), result(store.beginSet(kSlotFootNoneDown, "", true)));
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::BadKey),
                            result(store.beginSet(kSlotFootNoneDown, "~~~~~~~~~~~~~~~~", true)));
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::BadKey),
                            result(store.beginSet(kSlotFootNoneDown, "a b", false)));  // keys keep their rule
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Reserved),
                            result(store.beginSet(slotIndex(Ctrl::F, Mod::L1, Btn::L3), "x", true)));
    TEST_ASSERT_FALSE(store.busy());
    TEST_ASSERT_EQUAL_UINT32(0, eeprom->writes);
}

static void test_raw_record_round_trips_with_the_flag_hidden() {
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, kSlotFootNoneDown, kRaw, true);
        expectValue(store, kSlotFootNoneDown, kRaw, true, Source::Eeprom);
        // In EEPROM: the first character with bit 7 set, the rest as typed, then NULs.
        const uint16_t at = recordAddr(kSlotFootNoneDown);
        TEST_ASSERT_EQUAL_HEX8(';' | kRawFlag, eeprom->peek(at));
        TEST_ASSERT_EQUAL_HEX8('W', eeprom->peek(static_cast<uint16_t>(at + 1)));
        TEST_ASSERT_EQUAL_HEX8('T', eeprom->peek(static_cast<uint16_t>(at + 14)));
        TEST_ASSERT_EQUAL_HEX8(0, eeprom->peek(static_cast<uint16_t>(at + 15)));
        set(store, kSlotDomeL2Right, " a ", true);  // spaces are kept, at both ends
        expectValue(store, kSlotDomeL2Right, " a ", true, Source::Eeprom);
    }
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    expectValue(rebooted, kSlotFootNoneDown, kRaw, true, Source::Eeprom);
    expectValue(rebooted, kSlotDomeL2Right, " a ", true, Source::Eeprom);
    expectValue(rebooted, kSlotFootL1Up, "extremehappysou", false, Source::Default);
    TEST_ASSERT_EQUAL_UINT8(2, rebooted.validEepromSlots());
    // Back to a key: the flag goes with the old value.
    set(rebooted, kSlotFootNoneDown, "scream", false);
    expectValue(rebooted, kSlotFootNoneDown, "scream", false, Source::Eeprom);
    TEST_ASSERT_EQUAL_HEX8('s', eeprom->peek(recordAddr(kSlotFootNoneDown)));
    // A caller that doesn't ask whether it's raw still gets the plain text.
    set(rebooted, kSlotFootNoneDown, "*", true);
    char key[kKeyBuf];
    TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key));
    TEST_ASSERT_EQUAL_STRING("*", key);
}

// Review Focus 2: the queued first write and the slot being written are answered from RAM, and
// that copy holds the flagged first byte. Neither may leak it.
static void test_raw_reads_without_the_flag_mid_write_and_before_format() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    // Unformatted: the first write waits behind the header; a lookup answers from the queue.
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok), result(store.beginSet(kSlotFootNoneDown, kRaw, true)));
    expectValue(store, kSlotFootNoneDown, kRaw, true, Source::Eeprom);
    runUntilIdle(store);
    store.takeCompletion();
    expectValue(store, kSlotFootNoneDown, kRaw, true, Source::Eeprom);
    // Mid-write: a lookup of the slot being written answers from the job.
    eeprom->writeTicks = 10;
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok),
                            result(store.beginSet(kSlotDomeCrossLeft, "?SEQ,LIST", true)));
    store.step();  // the first byte, the flagged one, is going in
    TEST_ASSERT_FALSE(eeprom->ready());
    expectValue(store, kSlotDomeCrossLeft, "?SEQ,LIST", true, Source::Eeprom);
    runUntilIdle(store);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                            static_cast<uint8_t>(store.takeCompletion()));
    expectValue(store, kSlotDomeCrossLeft, "?SEQ,LIST", true, Source::Eeprom);
    // A key going in over a raw command reads as that key, not raw.
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok), result(store.beginSet(kSlotFootNoneDown, "plain", false)));
    store.step();
    expectValue(store, kSlotFootNoneDown, "plain", false, Source::Eeprom);
    runUntilIdle(store);
    store.takeCompletion();
}

static void test_bad_raw_records_read_as_the_default() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotFootL1Up, "x", false);  // formats the EEPROM
    const uint16_t gen = store.generation();
    uint8_t name[kKeyBuf];
    // First a good hand-built record reads as raw: the helper's CRC is right, so the bad cases
    // below fail for their bytes, not their CRC.
    memset(name, 0, sizeof(name));
    memcpy(name, "hi there", 8);
    name[0] = static_cast<uint8_t>(name[0] | kRawFlag);
    writeRecord(kSlotFootNoneDown, gen, name);
    {
        MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
        rebooted.begin();
        expectValue(rebooted, kSlotFootNoneDown, "hi there", true, Source::Eeprom);
    }
    for (uint8_t which = 0; which < 6; ++which) {
        memset(name, 0, sizeof(name));
        memcpy(name, "hi there", 8);
        switch (which) {
            case 0: memset(name, 'a', sizeof(name)); break;  // no NUL
            case 1: name[2] = 0x01; break;                    // a control byte
            case 2: name[2] = 0x7F; break;                    // DEL
            case 3: name[2] = 0xC3; break;                    // not ASCII
            case 4: memset(name, 0, sizeof(name)); break;     // empty: the flag alone
            default: name[0] = 0x1F; break;                   // the first byte isn't printable
        }
        name[0] = static_cast<uint8_t>(name[0] | kRawFlag);
        writeRecord(kSlotFootNoneDown, gen, name);
        MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
        rebooted.begin();
        char msg[12];
        snprintf(msg, sizeof(msg), "case %u", static_cast<unsigned>(which));
        char key[kKeyBuf];
        Source src = Source::Eeprom;
        TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key, &src));
        TEST_ASSERT_EQUAL_STRING_MESSAGE("angrysound", key, msg);
        TEST_ASSERT_TRUE_MESSAGE(src == Source::Default, msg);
    }
}

// The downgrade path: v3 and older check a record with isValidKey() alone. A raw record fails it,
// so they read the slot's built-in default, exactly as for a record that failed its CRC.
static void test_a_raw_record_fails_the_v3_rule() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotFootNoneDown, "abc", true);
    char name[kKeyBuf];
    for (uint8_t i = 0; i < kKeyBuf; ++i)
        name[i] = static_cast<char>(eeprom->peek(static_cast<uint16_t>(recordAddr(kSlotFootNoneDown) + i)));
    TEST_ASSERT_TRUE((static_cast<uint8_t>(name[0]) & kRawFlag) != 0);
    TEST_ASSERT_FALSE(isValidKey(name));
}

// A power cut at every byte of a raw write leaves the old key, the new raw command or the
// default: never the text without its flag, never a key with one.
static void test_power_cut_during_a_raw_write() {
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, kSlotFootNoneDown, "happysound", false);
        set(store, kSlotDomeCrossLeft, "neighbour", false);
    }
    snapshot();
    uint32_t needed = 0;
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        const uint32_t before = eeprom->writes;
        set(store, kSlotFootNoneDown, kRaw, true);
        needed = eeprom->writes - before;
    }
    TEST_ASSERT_TRUE(needed > 5);
    for (uint32_t cut = 0; cut <= needed; ++cut) {
        for (uint8_t garbage = 0; garbage < 2; ++garbage) {
            restore();
            eeprom->budget = static_cast<int32_t>(cut);
            eeprom->cutGarbage = garbage != 0;
            {
                MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
                store.begin();
                store.beginSet(kSlotFootNoneDown, kRaw, true);
                runUntilIdle(store);  // after the cut nothing lands and verification fails
                store.takeCompletion();
            }
            eeprom->powerOn();
            MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
            rebooted.begin();
            char key[kKeyBuf];
            Source src = Source::Reserved;
            bool raw = false;
            TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key, &src, &raw));
            const bool oldValue = strcmp(key, "happysound") == 0 && !raw && src == Source::Eeprom;
            const bool newValue = strcmp(key, kRaw) == 0 && raw && src == Source::Eeprom;
            const bool fallback = strcmp(key, "angrysound") == 0 && !raw && src == Source::Default;
            TEST_ASSERT_TRUE_MESSAGE(oldValue || newValue || fallback, key);
            if (!garbage && cut >= needed)
                TEST_ASSERT_TRUE(newValue);
            expectValue(rebooted, kSlotDomeCrossLeft, "neighbour", false, Source::Eeprom);
        }
    }
}

static void test_raw_double_taps_and_long_presses_count_as_mapped() {
    const uint8_t dbl = slotIndex(Ctrl::F, Mod::L1, Btn::UP, Tap::DOUBLE);
    const uint8_t lng = slotIndex(Ctrl::F, Mod::L1, Btn::UP, Tap::LONG);
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, dbl, "?", true);
        set(store, lng, " ", true);
        TEST_ASSERT_TRUE(store.doubleTapMapped(kSlotFootL1Up));
        TEST_ASSERT_TRUE(store.longPressMapped(kSlotFootL1Up));
        expectValue(store, lng, " ", true, Source::Eeprom);
    }
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);  // the RAM copy is rebuilt from the records
    rebooted.begin();
    TEST_ASSERT_TRUE(rebooted.doubleTapMapped(kSlotFootL1Up));
    TEST_ASSERT_TRUE(rebooted.longPressMapped(kSlotFootL1Up));
    expectValue(rebooted, dbl, "?", true, Source::Eeprom);
}

static void test_revert_and_defaults_clear_raw_slots() {
    const uint8_t lng = slotIndex(Ctrl::D, Mod::CROSS, Btn::LEFT, Tap::LONG);
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotDomeCrossLeft, kRaw, true);
    set(store, lng, kRaw, true);
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok), result(store.beginRevert(kSlotDomeCrossLeft)));
    runUntilIdle(store);
    store.takeCompletion();
    expectValue(store, kSlotDomeCrossLeft, "scanning", false, Source::Default);
    expectValue(store, lng, kRaw, true, Source::Eeprom);
    set(store, kSlotFootNoneDown, kRaw, true);  // a single-press raw record for DEFAULTS to retire
    TEST_ASSERT_EQUAL_UINT8(result(MapStore::Result::Ok), result(store.beginDefaults()));
    runUntilIdle(store);
    store.takeCompletion();
    expectValue(store, lng, "", false, Source::Default);
    expectValue(store, kSlotFootNoneDown, "angrysound", false, Source::Default);
    TEST_ASSERT_FALSE(store.longPressMapped(kSlotDomeCrossLeft));
    // After a reboot the old generation's raw records stay dead (a raw branch in slotRecordValid
    // that skipped the CRC would bring them back): each slot reads its default, and loadTap,
    // rebuilt under the new generation, finds no long press.
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    expectValue(rebooted, lng, "", false, Source::Default);
    expectValue(rebooted, kSlotFootNoneDown, "angrysound", false, Source::Default);
    expectValue(rebooted, kSlotDomeCrossLeft, "scanning", false, Source::Default);
    TEST_ASSERT_FALSE(rebooted.longPressMapped(kSlotDomeCrossLeft));
    set(rebooted, lng, " a ", true);  // the new generation takes raw records
    expectValue(rebooted, lng, " a ", true, Source::Eeprom);
    TEST_ASSERT_TRUE(rebooted.longPressMapped(kSlotDomeCrossLeft));
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_raw_text_rules);
    RUN_TEST(test_raw_record_round_trips_with_the_flag_hidden);
    RUN_TEST(test_raw_reads_without_the_flag_mid_write_and_before_format);
    RUN_TEST(test_bad_raw_records_read_as_the_default);
    RUN_TEST(test_a_raw_record_fails_the_v3_rule);
    RUN_TEST(test_power_cut_during_a_raw_write);
    RUN_TEST(test_raw_double_taps_and_long_presses_count_as_mapped);
    RUN_TEST(test_revert_and_defaults_clear_raw_slots);
    return UNITY_END();
}
