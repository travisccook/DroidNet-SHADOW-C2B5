// The store's faster paths against the rules they replaced (copied here from 0a8b73f, and the raw
// rule as the raw-commands plan writes it): which slot records count as valid, the double-tap and long-press bits loaded from a defaults table,
// defaultKey, the default readBlock, and a write that fails its verify.
#include <unity.h>

#include <ShadowMap.h>
#include <default_mappings.h>
#include <sm_fakes.h>
#define SM_TEST_OWN_SETUP
#include <sm_test_main.h>
#include <stdio.h>
#include <string.h>

using namespace sm;

SM_DEFINE_TEST_DEFAULTS

namespace {

namespace old {

bool isKeyChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool isValidKey(const char *key) {
    size_t n = 0;
    for (; key[n] != '\0'; ++n) {
        if (n >= kKeyMax || !isKeyChar(key[n]))
            return false;
    }
    return true;
}

bool fieldTerminated(const uint8_t *field, uint8_t width) {
    bool ended = false;
    for (uint8_t i = 0; i < width; ++i) {
        if (field[i] == 0)
            ended = true;
        else if (ended)
            return false;
    }
    return ended;
}

uint16_t crc16(uint16_t crc, uint8_t byte) {
    crc ^= static_cast<uint16_t>(byte) << 8;
    for (uint8_t i = 0; i < 8; ++i)
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    return crc;
}

uint16_t slotCrc(uint8_t slot, uint16_t gen, const uint8_t *name) {
    uint16_t crc = kCrc16Init;
    crc = crc16(crc, MapStore::kLayoutVersion);
    crc = crc16(crc, static_cast<uint8_t>(gen & 0xFF));
    crc = crc16(crc, static_cast<uint8_t>(gen >> 8));
    crc = crc16(crc, slot);
    for (uint8_t i = 0; i < kKeyBuf; ++i)
        crc = crc16(crc, name[i]);
    return crc;
}

// The raw rule as the raw-commands plan (Task 1) writes it.
bool isValidRaw(const char *text) {
    size_t n = 0;
    for (; text[n] != '\0'; ++n) {
        const uint8_t c = static_cast<uint8_t>(text[n]);
        if (n >= kKeyMax || c < 0x20 || c > 0x7E)
            return false;
    }
    return n > 0;
}

bool rawRecordValid(const uint8_t *rec) {
    if ((rec[0] & kRawFlag) == 0)
        return false;
    char text[kKeyBuf];
    memcpy(text, rec, kKeyBuf);
    text[0] = static_cast<char>(rec[0] & 0x7F);
    text[kKeyMax] = '\0';
    return isValidRaw(text);
}

bool slotRecordValid(const uint8_t *rec, uint8_t slot, uint16_t gen) {
    if (!fieldTerminated(rec, kKeyBuf))
        return false;
    if (!isValidKey(reinterpret_cast<const char *>(rec)) && !rawRecordValid(rec))
        return false;
    return static_cast<uint16_t>(rec[kKeyBuf] | (rec[kKeyBuf + 1] << 8)) == slotCrc(slot, gen, rec);
}

void defaultKey(const DefaultMapping *defaults, uint8_t count, uint8_t slot, char out[kKeyBuf]) {
    out[0] = '\0';
    for (uint8_t i = 0; i < count; ++i) {
        if (pgm_read_byte(&defaults[i].slot) == slot) {
            memcpy_P(out, defaults[i].key, kKeyBuf);
            out[kKeyMax] = '\0';
            return;
        }
    }
}

}  // namespace old

FakeEeprom *eeprom = nullptr;

uint32_t gRng = 0xC0FFEE11UL;
uint32_t rnd() {
    gRng ^= gRng << 13;
    gRng ^= gRng >> 17;
    gRng ^= gRng << 5;
    return gRng;
}

constexpr uint16_t kGen = 0x1234;

uint16_t slotAddr(uint8_t slot) {
    return static_cast<uint16_t>(2 * MapStore::kHeaderSize + static_cast<uint16_t>(slot) * MapStore::kSlotSize);
}

// A valid header A with generation kGen, written byte for byte.
void writeHeader() {
    uint8_t h[MapStore::kHeaderSize];
    memset(h, 0, sizeof(h));
    h[0] = 'S';
    h[1] = 'M';
    h[2] = MapStore::kLayoutVersion;
    h[3] = kSingleSlotCount;
    h[4] = 1;
    h[6] = kGen & 0xFF;
    h[7] = kGen >> 8;
    memcpy(h + 8, ";W1;C", 5);
    uint16_t crc = kCrc16Init;
    for (uint8_t i = 0; i < 30; ++i)
        crc = old::crc16(crc, h[i]);
    h[30] = static_cast<uint8_t>(crc & 0xFF);
    h[31] = static_cast<uint8_t>(crc >> 8);
    memcpy(eeprom->mem, h, sizeof(h));
}

// A record: `name` (16 bytes) and a CRC that's right, or off by one with `badCrc`.
void writeRecord(uint8_t slot, const uint8_t *name, bool badCrc, uint8_t *rec) {
    memcpy(rec, name, kKeyBuf);
    uint16_t crc = old::slotCrc(slot, kGen, name);
    if (badCrc)
        crc = static_cast<uint16_t>(crc ^ 1);
    rec[kKeyBuf] = static_cast<uint8_t>(crc & 0xFF);
    rec[kKeyBuf + 1] = static_cast<uint8_t>(crc >> 8);
    memcpy(eeprom->mem + slotAddr(slot), rec, MapStore::kSlotSize);
}

// The store reads the record as valid exactly when the old rule did.
void checkRecord(MapStore &store, uint8_t slot, const uint8_t *rec) {
    char key[kKeyBuf];
    Source src = Source::Reserved;
    TEST_ASSERT_TRUE(store.keyForSlot(slot, key, &src));
    const bool want = old::slotRecordValid(rec, slot, kGen);
    if ((src == Source::Eeprom) != want) {
        char msg[96];
        int n = snprintf(msg, sizeof(msg), "slot %u, want %d:", static_cast<unsigned>(slot), want ? 1 : 0);
        for (uint8_t i = 0; i < MapStore::kSlotSize && n < 90; ++i)
            n += snprintf(msg + n, sizeof(msg) - static_cast<size_t>(n), " %02X", rec[i]);
        TEST_FAIL_MESSAGE(msg);
    }
}

const char kKeyChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";

}  // namespace

void setUp() {
    static FakeEeprom instance;
    instance.reset();
    eeprom = &instance;
}
void tearDown() {}

static void test_record_validity_matches_the_old_rule() {
    writeHeader();
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_TRUE(store.formatted());
    TEST_ASSERT_EQUAL_UINT16(kGen, store.generation());
    uint8_t name[kKeyBuf], rec[MapStore::kSlotSize];
    static const uint8_t kOdd[] = {0x80, 0x7F, 0x1F, 0xFF, 0x01, ' ', '-', '^', 0xC3};
    uint32_t cases = 0;
#if defined(__AVR__)
    const uint16_t rounds = 300;
#else
    const uint16_t rounds = 20000;
#endif
    for (uint16_t r = 0; r < rounds; ++r) {
        uint8_t slot = static_cast<uint8_t>(rnd() % kSingleSlotCount);
        while (slotReserved(slot) != Reserved::No)  // reserved slots never read their record
            slot = static_cast<uint8_t>(rnd() % kSingleSlotCount);
        const uint8_t kind = static_cast<uint8_t>(r % 8);
        memset(name, 0, sizeof(name));
        const uint8_t len = static_cast<uint8_t>(rnd() % 17);  // 0-16 characters
        for (uint8_t i = 0; i < len; ++i)
            name[i] = static_cast<uint8_t>(kKeyChars[rnd() % (sizeof(kKeyChars) - 1)]);
        switch (kind) {
            case 0:  // a key, as written
                break;
            case 1:  // a NUL gap
                if (len >= 2)
                    name[rnd() % (len - 1)] = 0;
                break;
            case 2:  // an odd byte somewhere in the 16
                name[rnd() % kKeyBuf] = kOdd[rnd() % sizeof(kOdd)];
                break;
            case 3:  // an odd first byte
                name[0] = kOdd[rnd() % sizeof(kOdd)];
                break;
            case 4:  // junk after the NUL
                if (len < kKeyBuf)
                    name[len + (rnd() % (kKeyBuf - len))] = static_cast<uint8_t>(kKeyChars[rnd() % 10]);
                break;
            case 5:  // all 16 bytes random
                for (uint8_t i = 0; i < kKeyBuf; ++i)
                    name[i] = static_cast<uint8_t>(rnd());
                break;
            case 6:  // erased, or a raw command (the flag on the first byte), now and then with an odd byte
                if (r & 8) {
                    memset(name, 0, sizeof(name));
                    for (uint8_t i = 0; i < len; ++i)
                        name[i] = static_cast<uint8_t>(0x20 + rnd() % 95);
                    name[0] = static_cast<uint8_t>(name[0] | kRawFlag);
                    if (rnd() & 1)
                        name[rnd() % kKeyBuf] = kOdd[rnd() % sizeof(kOdd)];
                } else {
                    memset(name, 0xFF, sizeof(name));
                }
                break;
            default:  // 16 key characters, no NUL
                for (uint8_t i = 0; i < kKeyBuf; ++i)
                    name[i] = 'k';
                break;
        }
        writeRecord(slot, name, (rnd() & 3) == 0, rec);
        checkRecord(store, slot, rec);
        ++cases;
    }
    // Every key length, and every raw command length (flag on the first byte), with every odd byte
    // at every position, CRC right.
    for (uint8_t asRaw = 0; asRaw < 2; ++asRaw) {
        for (uint8_t len = 0; len <= kKeyMax + 1; ++len) {
            for (uint8_t at = 0; at < kKeyBuf; ++at) {
                for (uint8_t k = 0; k < sizeof(kOdd); ++k) {
                    memset(name, 0, sizeof(name));
                    memset(name, asRaw ? '~' : 'q', len < kKeyBuf ? len : kKeyBuf);
                    name[at] = kOdd[k];
                    if (asRaw)
                        name[0] = static_cast<uint8_t>(name[0] | kRawFlag);
                    writeRecord(kSlotFootNoneDown, name, false, rec);
                    checkRecord(store, kSlotFootNoneDown, rec);
                    ++cases;
                }
            }
        }
    }
    TEST_ASSERT_TRUE(cases > 2000);
}

// A tap slot's record decides its RAM bit at boot (loadTap) the same way.
static void test_tap_bits_follow_the_old_rule_at_boot() {
    writeHeader();
    uint8_t name[kKeyBuf], rec[MapStore::kSlotSize];
    const uint8_t slots[] = {slotIndex(Ctrl::F, Mod::L1, Btn::UP, Tap::DOUBLE),
                             slotIndex(Ctrl::D, Mod::CROSS, Btn::LEFT, Tap::LONG),
                             slotIndex(Ctrl::F, Mod::NONE, Btn::DOWN, Tap::LONG)};
    for (uint16_t r = 0; r < 64; ++r) {
        for (uint8_t slot : slots) {
            memset(name, 0, sizeof(name));
            const uint8_t len = static_cast<uint8_t>(rnd() % 17);
            for (uint8_t i = 0; i < len; ++i)
                name[i] = static_cast<uint8_t>((r & 1) ? kKeyChars[rnd() % 10] : static_cast<char>(rnd()));
            // Through the store's own writer path would format the slot; write it directly.
            uint16_t crc = old::slotCrc(slot, kGen, name);
            if ((rnd() & 3) == 0)
                crc ^= 0x100;
            uint8_t full[MapStore::kSlotSize];
            memcpy(full, name, kKeyBuf);
            full[kKeyBuf] = static_cast<uint8_t>(crc & 0xFF);
            full[kKeyBuf + 1] = static_cast<uint8_t>(crc >> 8);
            for (uint8_t i = 0; i < MapStore::kSlotSize; ++i) {
                // FakeEeprom keeps tap records in a pool; write() places them.
                eeprom->write(static_cast<uint16_t>(slotAddr(slot) + i), full[i]);
                eeprom->busy = 0;
            }
            memcpy(rec, full, sizeof(rec));
        }
        MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
        store.begin();
        for (uint8_t slot : slots) {
            for (uint8_t i = 0; i < MapStore::kSlotSize; ++i)
                rec[i] = eeprom->peek(static_cast<uint16_t>(slotAddr(slot) + i));
            char def[kKeyBuf];
            old::defaultKey(kTestDefaults, kTestDefaultCount, slot, def);
            const bool want = old::slotRecordValid(rec, slot, kGen) ? rec[0] != 0 : def[0] != '\0';
            const bool got = slotTap(slot) == Tap::LONG ? store.longPressMapped(slot) : store.doubleTapMapped(slot);
            TEST_ASSERT_EQUAL(want && slotReserved(slot) == Reserved::No, got);
        }
    }
}

namespace {

constexpr uint8_t kL2Up2 = slotIndex(Ctrl::F, Mod::L2, Btn::UP, Tap::DOUBLE);
constexpr uint8_t kL2UpL = slotIndex(Ctrl::F, Mod::L2, Btn::UP, Tap::LONG);
const DefaultMapping kCrafted[] PROGMEM = {
    {kL2Up2, ""},                                                         // an empty key first: unmapped wins
    {kL2Up2, "second"},                                                   // ... over this
    {kL2UpL, "first"},                                                    // mapped first
    {kL2UpL, ""},                                                         // ... over an empty one
    {slotIndex(Ctrl::F, Mod::L1, Btn::L3, Tap::DOUBLE), "overspeed2"},  // reserved
    {slotIndex(Ctrl::D, Mod::NONE, Btn::L3, Tap::LONG), "stick"},        // reserved (stick)
    {slotIndex(Ctrl::F, Mod::NONE, Btn::PS, Tap::DOUBLE), "psgate"},     // reserved
    {slotIndex(Ctrl::F, Mod::NONE, Btn::UP), "single"},                  // a single press: no bit
    {slotIndex(Ctrl::D, Mod::CROSS, Btn::DOWN, Tap::LONG), "lastlong"},
    {slotIndex(Ctrl::D, Mod::CROSS, Btn::DOWN, Tap::DOUBLE), "lastdouble"},
    {216, "past216"},
    {255, "past255"},
    {slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT, Tap::DOUBLE), "abcdefghijklmno"},
};

#define SMT_FILL(s) {static_cast<uint8_t>(s), "abcdefghijklmn"}
const DefaultMapping kWorst[] PROGMEM = {
    SMT_FILL(0),  SMT_FILL(1),  SMT_FILL(2),  SMT_FILL(3),  SMT_FILL(4),  SMT_FILL(5),  SMT_FILL(6),
    SMT_FILL(7),  SMT_FILL(8),  SMT_FILL(9),  SMT_FILL(10), SMT_FILL(11), SMT_FILL(12), SMT_FILL(13),
    SMT_FILL(14), SMT_FILL(15), SMT_FILL(16), SMT_FILL(17), SMT_FILL(18), SMT_FILL(19), SMT_FILL(20),
    SMT_FILL(21), SMT_FILL(22), SMT_FILL(23), SMT_FILL(24), SMT_FILL(25), SMT_FILL(26), SMT_FILL(27),
    SMT_FILL(28), SMT_FILL(29), SMT_FILL(30), SMT_FILL(31), SMT_FILL(32), SMT_FILL(33), SMT_FILL(34),
    SMT_FILL(35), {slotIndex(Ctrl::D, Mod::CIRCLE, Btn::RIGHT), "abcdefghijklmno"},
    {slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT, Tap::LONG), "abcdefghijklmno"},
};
#undef SMT_FILL

void checkTable(const DefaultMapping *table, uint8_t count) {
    eeprom->reset();
    MapStore store(*eeprom, table, count);
    store.begin();  // unformatted: every bit from the defaults (loadTapDefaults)
    for (uint16_t v = 0; v < 256; ++v) {
        const uint8_t slot = static_cast<uint8_t>(v);
        char want[kKeyBuf], got[kKeyBuf];
        old::defaultKey(table, count, slot, want);
        memset(got, '#', sizeof(got));
        store.defaultKey(slot, got);
        TEST_ASSERT_EQUAL_STRING(want, got);
        if (slot >= kSingleSlotCount && slot < kSlotCount) {
            const bool mapped = want[0] != '\0' && slotReserved(slot) == Reserved::No;
            const bool bit = slotTap(slot) == Tap::LONG ? store.longPressMapped(slot) : store.doubleTapMapped(slot);
            char msg[24];
            snprintf(msg, sizeof(msg), "slot %u", static_cast<unsigned>(slot));
            TEST_ASSERT_EQUAL_MESSAGE(mapped, bit, msg);
        }
    }
    // The same after a DEFAULTS (a new generation reloads every bit).
    TEST_ASSERT_TRUE(store.beginSet(slotIndex(Ctrl::F, Mod::NONE, Btn::DOWN, Tap::LONG), "x") == MapStore::Result::Ok);
    for (uint16_t i = 0; i < 2000 && store.busy(); ++i) {
        store.step();
        eeprom->tick();
    }
    store.takeCompletion();
    TEST_ASSERT_TRUE(store.longPressMapped(slotIndex(Ctrl::F, Mod::NONE, Btn::DOWN)));
    TEST_ASSERT_TRUE(store.beginDefaults() == MapStore::Result::Ok);
    for (uint16_t i = 0; i < 2000 && store.busy(); ++i) {
        store.step();
        eeprom->tick();
    }
    TEST_ASSERT_TRUE(store.takeCompletion() == MapStore::Completion::Done);
    for (uint8_t slot = kSingleSlotCount; slot < kSlotCount; ++slot) {
        char want[kKeyBuf];
        old::defaultKey(table, count, slot, want);
        const bool mapped = want[0] != '\0' && slotReserved(slot) == Reserved::No;
        const bool bit = slotTap(slot) == Tap::LONG ? store.longPressMapped(slot) : store.doubleTapMapped(slot);
        TEST_ASSERT_EQUAL(mapped, bit);
    }
}

}  // namespace

static void test_tap_bits_and_default_keys_from_every_table() {
    checkTable(defaultButtonMappings, defaultButtonMappingsCount);
    checkTable(kTestDefaults, kTestDefaultCount);
    checkTable(kWorst, sizeof(kWorst) / sizeof(kWorst[0]));
    checkTable(kCrafted, sizeof(kCrafted) / sizeof(kCrafted[0]));
    checkTable(kCrafted, 0);
}

static void test_read_block_default_is_reads() {
    for (uint16_t a = 0; a < 200; ++a)
        eeprom->mem[a] = static_cast<uint8_t>(a * 7 + 3);
    eeprom->stuckAddr = 77;
    uint8_t got[32];
    for (uint16_t addr = 60; addr < 100; addr = static_cast<uint16_t>(addr + 3)) {
        for (uint8_t n = 0; n <= 32; ++n) {
            const uint32_t before = eeprom->reads;
            memset(got, 0xEE, sizeof(got));
            static_cast<EepromIo *>(eeprom)->readBlock(addr, got, n);
            TEST_ASSERT_EQUAL_UINT32(before + n, eeprom->reads);
            for (uint8_t i = 0; i < n; ++i)
                TEST_ASSERT_EQUAL_HEX8(eeprom->read(static_cast<uint16_t>(addr + i)), got[i]);
            if (n < sizeof(got))
                TEST_ASSERT_EQUAL_HEX8(0xEE, got[n]);
        }
    }
}

// A write whose verify keeps failing (a stuck bit): one retry rewrites the bad byte, then it fails,
// with the same writes as before.
static void test_a_failed_verify_retries_then_fails() {
    MapStore store(*eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    TEST_ASSERT_TRUE(store.beginSet(kSlotFootNoneDown, "abc") == MapStore::Result::Ok);  // formats first
    for (uint16_t i = 0; i < 2000 && store.busy(); ++i) {
        store.step();
        eeprom->tick();
    }
    TEST_ASSERT_TRUE(store.takeCompletion() == MapStore::Completion::Done);
    eeprom->stuckAddr = slotAddr(kSlotFootNoneDown) + 2;  // 'c' reads back wrong
    const uint32_t before = eeprom->writes;
    TEST_ASSERT_TRUE(store.beginSet(kSlotFootNoneDown, "abd") == MapStore::Result::Ok);
    uint16_t steps = 0;
    for (; steps < 2000 && store.busy(); ++steps) {
        store.step();
        eeprom->tick();
    }
    TEST_ASSERT_TRUE(store.takeCompletion() == MapStore::Completion::Failed);
    // 'd' and the two CRC bytes, then the retry: the stuck byte reads wrong again and is rewritten.
    TEST_ASSERT_EQUAL_UINT32(4, eeprom->writes - before);
    TEST_ASSERT_EQUAL_UINT32(0, eeprom->violations);
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_record_validity_matches_the_old_rule);
    RUN_TEST(test_tap_bits_follow_the_old_rule_at_boot);
    RUN_TEST(test_tap_bits_and_default_keys_from_every_table);
    RUN_TEST(test_read_block_default_is_reads);
    RUN_TEST(test_a_failed_verify_retries_then_fails);
    return UNITY_END();
}
