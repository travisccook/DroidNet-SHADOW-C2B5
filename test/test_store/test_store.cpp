#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#define SM_TEST_OWN_SETUP
#include <sm_test_main.h>

using namespace sm;

SM_DEFINE_TEST_DEFAULTS

namespace {

FakeEeprom *eeprom = nullptr;
// Snapshot for the power-cut loops: the headers and single-press slots, which is all they
// write (the double-tap slots stay erased). One copy only: SRAM is tight on AVR.
uint8_t baseMem[FakeEeprom::kFlat];

void snapshot() {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, eeprom->doubleRecordsUsed(), "snapshot() only covers the single-press slots");
    memcpy(baseMem, eeprom->mem, sizeof(baseMem));
}
void restore() {
    eeprom->reset();
    memcpy(eeprom->mem, baseMem, sizeof(baseMem));
}

// Runs loop passes (step + EEPROM tick) until the store is idle. Returns the passes taken.
uint16_t runUntilIdle(MapStore &store, uint16_t maxPasses = 5000) {
    uint16_t passes = 0;
    while (store.busy() && passes < maxPasses) {
        store.step();
        eeprom->tick();
        ++passes;
    }
    TEST_ASSERT_FALSE_MESSAGE(store.busy(), "store never finished");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, eeprom->violations, "EEPROM touched while busy");
    return passes;
}

void expectKey(MapStore &store, uint8_t slot, const char *want, Source wantSrc) {
    char key[kKeyBuf];
    Source src;
    TEST_ASSERT_TRUE(store.keyForSlot(slot, key, &src));
    TEST_ASSERT_EQUAL_STRING(want, key);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(wantSrc), static_cast<uint8_t>(src));
}

void set(MapStore &store, uint8_t slot, const char *key) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginSet(slot, key)));
    runUntilIdle(store);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                            static_cast<uint8_t>(store.takeCompletion()));
}

}  // namespace

void tearDown() {}

void setUp() {
    static FakeEeprom instance;
    instance.reset();
    eeprom = &instance;
}

static void test_blank_eeprom_reads_defaults() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_FALSE(store.formatted());
    TEST_ASSERT_EQUAL_STRING(";W1;C", store.prefix());
    TEST_ASSERT_EQUAL_STRING("", store.suffix());
    expectKey(store, kSlotFootL1Up, "extremehappysou", Source::Default);
    expectKey(store, kSlotDomeL2Right, "", Source::Default);
    expectKey(store, slotIndex(Ctrl::F, Mod::L1, Btn::L3), "", Source::Reserved);
    expectKey(store, slotIndex(Ctrl::D, Mod::NONE, Btn::PS), "", Source::Reserved);
    TEST_ASSERT_EQUAL_UINT32(0, eeprom->writes);
}

static void test_first_set_formats_then_writes_and_survives_reboot() {
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                                static_cast<uint8_t>(store.beginSet(kSlotFootNoneDown, "scream")));
        TEST_ASSERT_TRUE(store.busy());
        expectKey(store, kSlotFootNoneDown, "scream", Source::Eeprom);  // pending value, before a byte lands
        runUntilIdle(store);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done),
                                static_cast<uint8_t>(store.takeCompletion()));
        TEST_ASSERT_TRUE(store.formatted());
        expectKey(store, kSlotFootNoneDown, "scream", Source::Eeprom);
        expectKey(store, kSlotFootL1Up, "extremehappysou", Source::Default);
    }
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    TEST_ASSERT_TRUE(rebooted.formatted());
    expectKey(rebooted, kSlotFootNoneDown, "scream", Source::Eeprom);
    expectKey(rebooted, kSlotFootL1Up, "extremehappysou", Source::Default);
    expectKey(rebooted, kSlotDomeCrossLeft, "scanning", Source::Default);
}

static void test_clear_and_overwrite() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotFootL1Up, "");
    expectKey(store, kSlotFootL1Up, "", Source::Eeprom);
    set(store, kSlotFootL1Up, "a_much_longer_1");
    set(store, kSlotFootL1Up, "short");
    expectKey(store, kSlotFootL1Up, "short", Source::Eeprom);  // NUL padding cleared the tail
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    expectKey(rebooted, kSlotFootL1Up, "short", Source::Eeprom);
}

static void test_rejections() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::BadSlot), static_cast<uint8_t>(store.beginSet(kSlotCount, "x")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Reserved),
                            static_cast<uint8_t>(store.beginSet(slotIndex(Ctrl::F, Mod::L1, Btn::L3), "x")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::BadKey),
                            static_cast<uint8_t>(store.beginSet(kSlotFootL1Up, "bad key")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::BadArg), static_cast<uint8_t>(store.beginPrefix("W1;C")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::BadArg), static_cast<uint8_t>(store.beginSuffix("^x")));
    TEST_ASSERT_FALSE(store.busy());
    TEST_ASSERT_EQUAL_UINT32(0, eeprom->writes);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginSet(kSlotFootL1Up, "x")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Busy), static_cast<uint8_t>(store.beginSet(kSlotFootNoneDown, "y")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Busy), static_cast<uint8_t>(store.beginDefaults()));
    runUntilIdle(store);
}

static void test_unchanged_bytes_are_not_rewritten() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotFootL1Up, "yes");
    const uint32_t before = eeprom->writes;
    set(store, kSlotFootL1Up, "yes");
    TEST_ASSERT_EQUAL_UINT32(before, eeprom->writes);
    set(store, kSlotFootL1Up, "yet");  // one letter + the CRC
    TEST_ASSERT_TRUE(eeprom->writes - before <= 3);
}

static void test_lookup_defers_while_eeprom_busy_and_writer_yields() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotDomeCrossLeft, "custom");
    eeprom->writeTicks = 10;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(store.beginSet(kSlotFootNoneDown, "longkeyvalue123")));
    store.step();  // starts the first byte
    TEST_ASSERT_FALSE(eeprom->ready());

    char key[kKeyBuf] = "untouched";
    TEST_ASSERT_FALSE(store.keyForSlot(kSlotDomeCrossLeft, key));  // would have to wait: defer
    TEST_ASSERT_EQUAL_STRING("untouched", key);
    expectKey(store, kSlotFootNoneDown, "longkeyvalue123", Source::Eeprom);  // slot being written: no EEPROM read

    // Once idle, the writer skips one pass so the deferred lookup gets the EEPROM.
    while (!eeprom->ready())
        eeprom->tick();
    const uint32_t writesBefore = eeprom->writes;
    store.step();
    TEST_ASSERT_EQUAL_UINT32(writesBefore, eeprom->writes);
    expectKey(store, kSlotDomeCrossLeft, "custom", Source::Eeprom);
    runUntilIdle(store);
    expectKey(store, kSlotFootNoneDown, "longkeyvalue123", Source::Eeprom);
}

static void test_defaults_bumps_generation_and_switches_header() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotFootL1Up, "one");
    set(store, kSlotDomeL2Right, "two");
    const uint16_t gen = store.generation();
    const uint8_t header = store.activeHeader();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginDefaults()));
    runUntilIdle(store);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done), static_cast<uint8_t>(store.takeCompletion()));
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(gen + 1), store.generation());
    TEST_ASSERT_NOT_EQUAL(header, store.activeHeader());
    expectKey(store, kSlotFootL1Up, "extremehappysou", Source::Default);
    expectKey(store, kSlotDomeL2Right, "", Source::Default);

    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(gen + 1), rebooted.generation());
    expectKey(rebooted, kSlotFootL1Up, "extremehappysou", Source::Default);
    set(rebooted, kSlotFootL1Up, "three");
    expectKey(rebooted, kSlotFootL1Up, "three", Source::Eeprom);
    expectKey(rebooted, kSlotDomeL2Right, "", Source::Default);  // old generation stays dead
}

static void test_defaults_on_blank_store_writes_nothing() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginDefaults()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done), static_cast<uint8_t>(store.takeCompletion()));
    TEST_ASSERT_EQUAL_UINT32(0, eeprom->writes);
}

static void test_prefix_and_suffix_persist() {
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, kSlotFootL1Up, "kept");
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginPrefix(";SEQ")));
        runUntilIdle(store);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginSuffix(",L")));
        runUntilIdle(store);
        TEST_ASSERT_EQUAL_STRING(";SEQ", store.prefix());
        TEST_ASSERT_EQUAL_STRING(",L", store.suffix());
    }
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    TEST_ASSERT_EQUAL_STRING(";SEQ", rebooted.prefix());
    TEST_ASSERT_EQUAL_STRING(",L", rebooted.suffix());
    expectKey(rebooted, kSlotFootL1Up, "kept", Source::Eeprom);  // same generation: slots survive
}

// Cuts power after every possible number of byte writes during a slot write, and checks
// that after the reboot the slot reads as the old value, the new value or its default:
// never a mix, and no other slot is disturbed.
static void test_power_cut_during_slot_write() {
    // Measure how many byte writes the change takes.
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, kSlotFootNoneDown, "happysound");
        set(store, kSlotDomeCrossLeft, "neighbour");
    }
    snapshot();
    uint32_t needed = 0;
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        const uint32_t before = eeprom->writes;
        set(store, kSlotFootNoneDown, "scaredsound");
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
                store.beginSet(kSlotFootNoneDown, "scaredsound");
                runUntilIdle(store);  // after the cut, nothing lands and verification fails
                store.takeCompletion();
            }
            eeprom->powerOn();
            MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
            rebooted.begin();
            TEST_ASSERT_TRUE(rebooted.formatted());
            char key[kKeyBuf];
            Source src;
            TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key, &src));
            const bool oldValue = strcmp(key, "happysound") == 0 && src == Source::Eeprom;
            const bool newValue = strcmp(key, "scaredsound") == 0 && src == Source::Eeprom;
            const bool fallback = strcmp(key, "angrysound") == 0 && src == Source::Default;
            TEST_ASSERT_TRUE_MESSAGE(oldValue || newValue || fallback, key);
            if (!garbage && cut >= needed)
                TEST_ASSERT_TRUE(newValue);
            expectKey(rebooted, kSlotDomeCrossLeft, "neighbour", Source::Eeprom);
            expectKey(rebooted, kSlotFootL1Up, "extremehappysou", Source::Default);
        }
    }
}

// The same for header writes: a cut while changing the prefix or resetting to defaults
// always leaves one complete header, old or new.
static void test_power_cut_during_header_write() {
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, kSlotFootL1Up, "custom");
    }
    snapshot();

    for (uint8_t which = 0; which < 2; ++which) {  // 0 = PREFIX, 1 = DEFAULTS
        for (uint32_t cut = 0; cut <= MapStore::kHeaderSize; ++cut) {
            restore();
            eeprom->budget = static_cast<int32_t>(cut);
            eeprom->cutGarbage = (cut % 2) != 0;
            {
                MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
                store.begin();
                if (which == 0)
                    store.beginPrefix(";W3;C");
                else
                    store.beginDefaults();
                runUntilIdle(store);
                store.takeCompletion();
            }
            eeprom->powerOn();
            MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
            rebooted.begin();
            TEST_ASSERT_TRUE(rebooted.formatted());
            char key[kKeyBuf];
            TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootL1Up, key));
            if (which == 0) {
                TEST_ASSERT_TRUE(strcmp(rebooted.prefix(), ";W1;C") == 0 || strcmp(rebooted.prefix(), ";W3;C") == 0);
                TEST_ASSERT_EQUAL_STRING("custom", key);
            } else {
                TEST_ASSERT_TRUE(strcmp(key, "custom") == 0 || strcmp(key, "extremehappysou") == 0);
            }
        }
    }
}

static void test_verify_failure_is_reported() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    set(store, kSlotFootL1Up, "fine");
    // A bit that reads back wrong in the slot's key area.
    eeprom->stuckAddr = 2 * MapStore::kHeaderSize + kSlotFootNoneDown * MapStore::kSlotSize + 1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(store.beginSet(kSlotFootNoneDown, "stuck")));
    runUntilIdle(store);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Failed), static_cast<uint8_t>(store.takeCompletion()));
    expectKey(store, kSlotFootNoneDown, "angrysound", Source::Default);  // bad CRC: default
    expectKey(store, kSlotFootL1Up, "fine", Source::Eeprom);
}

static void test_lost_header_does_not_resurrect_old_slots() {
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, kSlotDomeL2Right, "stale");
        TEST_ASSERT_EQUAL_UINT8(0, store.activeHeader());
    }
    eeprom->mem[0] = 'X';  // header A (the only one) loses its magic
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_FALSE(store.formatted());
    expectKey(store, kSlotDomeL2Right, "", Source::Default);
    set(store, kSlotFootL1Up, "fresh");  // re-formats with a newer generation
    expectKey(store, kSlotFootL1Up, "fresh", Source::Eeprom);
    expectKey(store, kSlotDomeL2Right, "", Source::Default);
}

static void test_newer_sequence_wins_across_wraparound() {
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, kSlotFootL1Up, "x");                  // header A, seq 1
        store.beginPrefix(";W2;C");                       // header B, seq 2
        runUntilIdle(store);
        TEST_ASSERT_EQUAL_UINT8(1, store.activeHeader());
    }
    // Rewrite the sequence numbers: A = 0xFFFF, B = 0x0000 (B is newer, one step past the wrap).
    const uint16_t seqs[2] = {0xFFFF, 0x0000};
    for (uint8_t h = 0; h < 2; ++h) {
        uint8_t *hdr = eeprom->mem + h * MapStore::kHeaderSize;
        hdr[4] = static_cast<uint8_t>(seqs[h] & 0xFF);
        hdr[5] = static_cast<uint8_t>(seqs[h] >> 8);
        uint16_t crc = kCrc16Init;
        for (uint8_t i = 0; i < 30; ++i)
            crc = crc16Update(crc, hdr[i]);
        hdr[30] = static_cast<uint8_t>(crc & 0xFF);
        hdr[31] = static_cast<uint8_t>(crc >> 8);
    }
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_EQUAL_UINT8(1, store.activeHeader());
    TEST_ASSERT_EQUAL_STRING(";W2;C", store.prefix());
    // The next header write goes to A with seq 0x0001 and must win after a reboot.
    store.beginPrefix(";W4;C");
    runUntilIdle(store);
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    TEST_ASSERT_EQUAL_UINT8(0, rebooted.activeHeader());
    TEST_ASSERT_EQUAL_STRING(";W4;C", rebooted.prefix());
}

static void test_valid_eeprom_slot_count() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_EQUAL_UINT8(0, store.validEepromSlots());
    set(store, kSlotFootL1Up, "a");
    set(store, kSlotDomeL2Right, "b");
    TEST_ASSERT_EQUAL_UINT8(2, store.validEepromSlots());
}


static void test_revert_erases_one_slot() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    // Nothing stored yet: reverting is already done and writes nothing.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginRevert(kSlotFootL1Up)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done), static_cast<uint8_t>(store.takeCompletion()));
    TEST_ASSERT_EQUAL_UINT32(0, eeprom->writes);

    set(store, kSlotFootL1Up, "custom");
    set(store, kSlotDomeL2Right, "other");
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginRevert(kSlotFootL1Up)));
    expectKey(store, kSlotFootL1Up, "extremehappysou", Source::Default);  // mid-erase reads the default
    runUntilIdle(store);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done), static_cast<uint8_t>(store.takeCompletion()));
    expectKey(store, kSlotFootL1Up, "extremehappysou", Source::Default);
    expectKey(store, kSlotDomeL2Right, "other", Source::Eeprom);
    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    expectKey(rebooted, kSlotFootL1Up, "extremehappysou", Source::Default);
    expectKey(rebooted, kSlotDomeL2Right, "other", Source::Eeprom);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Reserved),
                            static_cast<uint8_t>(rebooted.beginRevert(slotIndex(Ctrl::F, Mod::L1, Btn::L3))));
}

static void test_power_cut_during_revert() {
    {
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        set(store, kSlotFootNoneDown, "custom");
    }
    snapshot();
    for (uint32_t cut = 0; cut <= MapStore::kSlotSize; ++cut) {
        restore();
        eeprom->budget = static_cast<int32_t>(cut);
        eeprom->cutGarbage = (cut % 3) == 1;
        {
            MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
            store.begin();
            store.beginRevert(kSlotFootNoneDown);
            runUntilIdle(store);
            store.takeCompletion();
        }
        eeprom->powerOn();
        MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
        rebooted.begin();
        char key[kKeyBuf];
        Source src;
        TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key, &src));
        const bool old = strcmp(key, "custom") == 0 && src == Source::Eeprom;
        const bool reverted = strcmp(key, "angrysound") == 0 && src == Source::Default;
        TEST_ASSERT_TRUE_MESSAGE(old || reverted, key);
    }
}


// Double taps: the second bank after the single presses, with a RAM copy of which are mapped.
static void test_double_taps_follow_the_single_slots() {
    const uint8_t dbl = slotIndex(Ctrl::F, Mod::L1, Btn::UP, Tap::DOUBLE);
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_FALSE(store.doubleTapMapped(kSlotFootL1Up));
    expectKey(store, dbl, "", Source::Default);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginSet(dbl, "twice")));
    TEST_ASSERT_FALSE(store.doubleTapMapped(kSlotFootL1Up));  // not until it's written
    runUntilIdle(store);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Done), static_cast<uint8_t>(store.takeCompletion()));
    TEST_ASSERT_TRUE(store.doubleTapMapped(kSlotFootL1Up));
    TEST_ASSERT_TRUE(store.doubleTapMapped(dbl));
    TEST_ASSERT_FALSE(store.doubleTapMapped(slotIndex(Ctrl::D, Mod::L1, Btn::UP)));
    expectKey(store, dbl, "twice", Source::Eeprom);
    expectKey(store, kSlotFootL1Up, "extremehappysou", Source::Default);

    // Where it lives, and the header still says 72 so older firmware keeps reading it.
    TEST_ASSERT_EQUAL_UINT8('t', eeprom->peek(2 * MapStore::kHeaderSize + dbl * MapStore::kSlotSize));
    TEST_ASSERT_TRUE(2 * MapStore::kHeaderSize + 72 * MapStore::kSlotSize <= 64 + dbl * MapStore::kSlotSize);
    TEST_ASSERT_EQUAL_UINT8(72, eeprom->mem[3]);
    TEST_ASSERT_EQUAL_UINT16(2656, MapStore::kFootprint);

    MapStore rebooted(*eeprom, kTestDefaults, kTestDefaultCount);
    rebooted.begin();
    TEST_ASSERT_TRUE(rebooted.doubleTapMapped(kSlotFootL1Up));
    expectKey(rebooted, dbl, "twice", Source::Eeprom);

    set(rebooted, dbl, "");
    TEST_ASSERT_FALSE(rebooted.doubleTapMapped(kSlotFootL1Up));
    set(rebooted, dbl, "again");
    TEST_ASSERT_TRUE(rebooted.doubleTapMapped(kSlotFootL1Up));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(rebooted.beginRevert(dbl)));
    runUntilIdle(rebooted);
    TEST_ASSERT_FALSE(rebooted.doubleTapMapped(kSlotFootL1Up));
    set(rebooted, dbl, "again");
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(rebooted.beginDefaults()));
    runUntilIdle(rebooted);
    TEST_ASSERT_FALSE(rebooted.doubleTapMapped(kSlotFootL1Up));
    expectKey(rebooted, dbl, "", Source::Default);

    // L3 has no double tap.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Reserved),
                            static_cast<uint8_t>(rebooted.beginSet(slotIndex(Ctrl::D, Mod::NONE, Btn::L3, Tap::DOUBLE), "x")));
}

static void test_double_tap_defaults_and_failed_writes() {
    const uint8_t dbl = slotIndex(Ctrl::D, Mod::CROSS, Btn::LEFT, Tap::DOUBLE);
    static const DefaultMapping withDouble[] PROGMEM = {{kSlotDomeCrossLeft, "scanning"}, {dbl, "scantwice"}};
    MapStore store(*eeprom, withDouble, 2);
    store.begin();
    TEST_ASSERT_TRUE(store.doubleTapMapped(kSlotDomeCrossLeft));  // a built-in double tap
    set(store, dbl, "");
    TEST_ASSERT_FALSE(store.doubleTapMapped(kSlotDomeCrossLeft));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginRevert(dbl)));
    runUntilIdle(store);
    TEST_ASSERT_TRUE(store.doubleTapMapped(kSlotDomeCrossLeft));
    set(store, dbl, "");
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginDefaults()));
    runUntilIdle(store);
    TEST_ASSERT_TRUE(store.doubleTapMapped(kSlotDomeCrossLeft));

    // A write that doesn't verify: the RAM copy follows what's really in EEPROM (a bad
    // record reads as the default).
    set(store, dbl, "");
    eeprom->stuckAddr = 2 * MapStore::kHeaderSize + dbl * MapStore::kSlotSize;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(store.beginSet(dbl, "abc")));
    runUntilIdle(store);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Completion::Failed), static_cast<uint8_t>(store.takeCompletion()));
    TEST_ASSERT_TRUE(store.doubleTapMapped(kSlotDomeCrossLeft));
    char key[kKeyBuf];
    TEST_ASSERT_TRUE(store.keyForSlot(dbl, key));
    TEST_ASSERT_EQUAL_STRING("scantwice", key);
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_blank_eeprom_reads_defaults);
    RUN_TEST(test_first_set_formats_then_writes_and_survives_reboot);
    RUN_TEST(test_clear_and_overwrite);
    RUN_TEST(test_rejections);
    RUN_TEST(test_unchanged_bytes_are_not_rewritten);
    RUN_TEST(test_lookup_defers_while_eeprom_busy_and_writer_yields);
    RUN_TEST(test_defaults_bumps_generation_and_switches_header);
    RUN_TEST(test_defaults_on_blank_store_writes_nothing);
    RUN_TEST(test_prefix_and_suffix_persist);
    RUN_TEST(test_power_cut_during_slot_write);
    RUN_TEST(test_power_cut_during_header_write);
    RUN_TEST(test_verify_failure_is_reported);
    RUN_TEST(test_lost_header_does_not_resurrect_old_slots);
    RUN_TEST(test_newer_sequence_wins_across_wraparound);
    RUN_TEST(test_valid_eeprom_slot_count);
    RUN_TEST(test_revert_erases_one_slot);
    RUN_TEST(test_power_cut_during_revert);
    RUN_TEST(test_double_taps_follow_the_single_slots);
    RUN_TEST(test_double_tap_defaults_and_failed_writes);
    return UNITY_END();
}
