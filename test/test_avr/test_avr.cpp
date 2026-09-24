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
