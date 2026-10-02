// AVR-only checks, run under simavr (skipped on the host):
//   - the store against the ATmega2560's real EEPROM (avr-libc, busy flag and all)
//   - worst-case time of the per-pass work the mapping adds to loop(), in emulated cycles
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#include <sm_test_main.h>
#include <stdio.h>

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
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                                static_cast<uint8_t>(store.beginSet(kSlotDomeCrossLeft, "a ;T^b", true)));
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
    bool raw = false;
    TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotDomeCrossLeft, key, &src, &raw));
    TEST_ASSERT_EQUAL_STRING("a ;T^b", key);  // the flag is bit 7 on the real EEPROM too
    TEST_ASSERT_TRUE(raw && src == Source::Eeprom);
    TEST_ASSERT_EQUAL_UINT8(2, rebooted.validEepromSlots());

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok), static_cast<uint8_t>(rebooted.beginDefaults()));
    runUntilIdle(rebooted);
    TEST_ASSERT_TRUE(rebooted.keyForSlot(kSlotFootNoneDown, key, &src));
    TEST_ASSERT_EQUAL_STRING("angrysound", key);
    TEST_ASSERT_TRUE(src == Source::Default);
}

namespace {

// Loop work must stay far below the 25 ms motor update interval (SerialLatency).
constexpr unsigned long kBudgetMicros = 1000;

// Drain target that costs nothing, so the timings are the library's own.
struct NullUart {
    int availableForWrite() { return 63; }
    size_t write(const uint8_t *, size_t n) {
        bytes += n;
        return n;
    }
    uint32_t bytes = 0;
};

void report(const char *what, unsigned long micros) {
    char msg[64];
    snprintf(msg, sizeof(msg), "%s: %lu us", what, micros);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(micros < kBudgetMicros, what);
}

}  // namespace

static void test_per_pass_work_is_bounded() {
    eraseFootprint();
    static MapStore store(io, kTestDefaults, kTestDefaultCount);
    static TxQueue tx;
    static WcbPort port(tx);
    static Protocol protocol(store, "2.0.0");
    static NullUart wcb;
    store.begin();

    // Longest request: a SET with a 15-character key and its CRC.
    char line[96];
    withCrc("@SM SET D CIRCLE RIGHT abcdefghijklmno", line, sizeof(line));
    unsigned long t = micros();
    TEST_ASSERT_TRUE(protocol.handleLine(line, port));
    report("handleLine(SET)", micros() - t);

    // Every pass of the write, then of a full DUMP: service + step + drain.
    unsigned long worst = 0;
    for (uint32_t i = 0; i < 200000UL && (store.busy() || protocol.opInProgress()); ++i) {
        if (i == 50)
            TEST_ASSERT_TRUE(protocol.handleLine(line, port) || true);
        t = micros();
        protocol.service();
        store.step();
        tx.drain(wcb);
        const unsigned long dt = micros() - t;
        if (dt > worst)
            worst = dt;
    }
    report("pass during SET", worst);

    // A double-tap slot: its write also refreshes the RAM copy of what's mapped.
    withCrc("@SM SET D CIRCLE RIGHT2 abcdefghijklmno", line, sizeof(line));
    t = micros();
    TEST_ASSERT_TRUE(protocol.handleLine(line, port));
    report("handleLine(SET double)", micros() - t);
    worst = 0;
    for (uint32_t i = 0; i < 200000UL && (store.busy() || protocol.opInProgress()); ++i) {
        t = micros();
        protocol.service();
        store.step();
        tx.drain(wcb);
        const unsigned long dt = micros() - t;
        if (dt > worst)
            worst = dt;
    }
    report("pass during SET double", worst);
    TEST_ASSERT_TRUE(store.doubleTapMapped(slotIndex(Ctrl::D, Mod::CIRCLE, Btn::RIGHT)));

    // The longest request now: a SET with 15 raw bytes, hex-decoded on the way in.
    withCrc("@SM SET D CIRCLE UP !3B57323B5334504C41593A335E3B54", line, sizeof(line));
    t = micros();
    TEST_ASSERT_TRUE(protocol.handleLine(line, port));
    report("handleLine(SET raw)", micros() - t);
    worst = 0;
    for (uint32_t i = 0; i < 200000UL && (store.busy() || protocol.opInProgress()); ++i) {
        t = micros();
        protocol.service();
        store.step();
        tx.drain(wcb);
        const unsigned long dt = micros() - t;
        if (dt > worst)
            worst = dt;
    }
    report("pass during SET raw", worst);

    // Boot reads every double-tap record once (setup(), not the loop).
    static MapStore rebooted(io, kTestDefaults, kTestDefaultCount);
    t = micros();
    rebooted.begin();
    const unsigned long bootMicros = micros() - t;
    char bootMsg[48];
    snprintf(bootMsg, sizeof(bootMsg), "begin() with double taps: %lu us", bootMicros);
    TEST_MESSAGE(bootMsg);
    TEST_ASSERT_TRUE(bootMicros < 50000UL);
    TEST_ASSERT_TRUE(rebooted.doubleTapMapped(slotIndex(Ctrl::D, Mod::CIRCLE, Btn::RIGHT)));

    // The longest SLOT lines (Protocol::kMaxLine): a 15-character raw command over a 15-character
    // default on F CIRCLE RIGHTL (93 with the CRC), and a 15-character key over a 15-character
    // default (77). No real firmware has defaults there; the table is made up, and the store has
    // its own EEPROM base (all 216 slots fit above it) so the store above keeps its records. A GET
    // reads its slot on one pass and sends the line on a later one; so does each line of a DUMP.
    static const DefaultMapping kWide[] PROGMEM = {
        {slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT, Tap::LONG), "abcdefghijklmno"},
        {slotIndex(Ctrl::F, Mod::CIRCLE, Btn::LEFT, Tap::LONG), "abcdefghijklmno"},
    };
    static_assert(144 + MapStore::kFootprint <= 4096, "the second store must fit the EEPROM");
    static MapStore wide(io, kWide, 2, 144);
    static Protocol wideProtocol(wide, "2.0.0");
    for (uint16_t a = 144; a < 144 + 2 * MapStore::kHeaderSize; ++a)
        eeprom_update_byte(reinterpret_cast<uint8_t *>(a), 0xFF);  // unformatted there
    eeprom_busy_wait();
    wide.begin();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(wide.beginSet(slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT, Tap::LONG),
                                                               "~~~~~~~~~~~~~~~", true)));
    runUntilIdle(wide);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(wide.beginSet(slotIndex(Ctrl::F, Mod::CIRCLE, Btn::LEFT, Tap::LONG),
                                                               "abcdefghijklmn2")));
    runUntilIdle(wide);
    tx.drain(wcb);
    withCrc("@SM GET F CIRCLE RIGHTL", line, sizeof(line));
    t = micros();
    TEST_ASSERT_TRUE(wideProtocol.handleLine(line, port));
    report("pass with a GET (the longest SLOT line)", micros() - t);
    t = micros();
    wideProtocol.service();
    tx.drain(wcb);
    report("pass sending that SLOT line", micros() - t);
    TEST_ASSERT_FALSE(wideProtocol.opInProgress());
    const char *const verbs[] = {"@SM DUMP", "@SM SUM"};
    for (const char *verb : verbs) {
        withCrc(verb, line, sizeof(line));
        while (!wideProtocol.handleLine(line, port))
            tx.drain(wcb);
        worst = 0;
        while (wideProtocol.opInProgress()) {
            t = micros();
            wideProtocol.service();
            wide.step();
            tx.drain(wcb);
            const unsigned long dt = micros() - t;
            if (dt > worst)
                worst = dt;
        }
        report(verb == verbs[0] ? "pass during DUMP (the longest SLOT lines)" : "pass during SUM (the longest SLOT lines)",
               worst);
    }

    withCrc("@SM DUMP", line, sizeof(line));
    while (!protocol.handleLine(line, port))
        tx.drain(wcb);
    worst = 0;
    while (protocol.opInProgress()) {
        t = micros();
        protocol.service();
        store.step();
        tx.drain(wcb);
        const unsigned long dt = micros() - t;
        if (dt > worst)
            worst = dt;
    }
    report("pass during DUMP", worst);
    TEST_ASSERT_TRUE(wcb.bytes > 72UL * 30);

    // A button press: resolve, read the slot, queue the command.
    t = micros();
    const uint8_t slot = resolveSlot(Ctrl::F, {true, kHeldDown}, {false, 0});
    char key[kKeyBuf];
    TEST_ASSERT_TRUE(store.keyForSlot(slot, key));
    TEST_ASSERT_TRUE(pushCommand(tx, store.prefix(), key, store.suffix()));
    tx.drain(wcb);
    report("button press", micros() - t);

    // A raw press: read the slot (raw), queue the text.
    t = micros();
    char rawText[kKeyBuf];
    bool isRaw = false;
    TEST_ASSERT_TRUE(store.keyForSlot(slotIndex(Ctrl::D, Mod::CIRCLE, Btn::UP), rawText, nullptr, &isRaw));
    TEST_ASSERT_TRUE(isRaw);
    TEST_ASSERT_TRUE(pushRaw(tx, rawText));
    tx.drain(wcb);
    report("button press (raw)", micros() - t);

    // Button passes through the runtime: the second tap of a double tap, the worst of them.
    static Runtime taps(store, protocol, tx, nullptr);
    FiredPress fired;
    const PadState none = {true, 0}, circle = {true, kHeldCircle}, right = {true, kHeldCircle | kHeldRight};
    worst = 0;
    const PadState seq[] = {right, circle, right, none};
    for (uint8_t i = 0; i < 4; ++i) {
        t = micros();
        taps.buttons(20000UL + i * 50UL, none, seq[i], &fired);
        const unsigned long dt = micros() - t;
        if (dt > worst)
            worst = dt;
    }
    tx.drain(wcb);
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmno", fired.key);
    report("button pass (double tap)", worst);

    // A pass that sends the WCB device announce (builds the line, draws the next interval).
    static Runtime runtime(store, protocol, tx, "abcdef0123+");
    runtime.startAnnounce(0, 42);
    const uint32_t before = wcb.bytes;
    t = micros();
    runtime.service(Runtime::kFirstAnnounceMaxMs, wcb);
    report("pass with the announce", micros() - t);
    TEST_ASSERT_TRUE(wcb.bytes > before);
}

#else

static void test_set_reboot_defaults_on_real_eeprom() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_per_pass_work_is_bounded() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_set_reboot_defaults_on_real_eeprom);
    RUN_TEST(test_per_pass_work_is_bounded);
    return UNITY_END();
}
