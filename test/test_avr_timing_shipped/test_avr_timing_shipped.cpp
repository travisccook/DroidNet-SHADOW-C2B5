// The protocol's worst passes with the firmware's own 35 defaults (src/default_mappings.cpp), as
// the droid runs today: its longest lines are shorter than test_avr_timing_protocol's (no
// 15-character default on a long slot name), but DEFAULTS scans the same way. AVR only (simavr);
// see test/support/sm_avr_timing.h for the method. Its own program, so the library has one
// MapStore and one Protocol, as in the firmware.
#include <unity.h>

#include <ShadowMap.h>
#include <default_mappings.h>
#include <sm_fakes.h>
#include <sm_test_main.h>
#include <sm_avr_timing.h>

#if defined(__AVR__)

using namespace sm;
using namespace smt;

namespace {

AvrEeprom io;
MapStore store(io, defaultButtonMappings, defaultButtonMappingsCount);
TxQueue tx;
WcbPort port(tx);
Protocol protocol(store, kFw11);
NullUart wcb;

constexpr uint8_t kRoute20 = 8;

void drainAll() {
    while (tx.used() > 0) {
        tx.drain(wcb);
        wcb.note();
    }
}

char gLine[LineReader::kMaxLen + 8];
const char *crcLine(PGM_P body) {
    char b[LineReader::kMaxLen + 1];
    strncpy_P(b, body, sizeof(b) - 1);
    b[sizeof(b) - 1] = '\0';
    withCrc(b, gLine, sizeof(gLine));
    return gLine;
}

// 15-character custom keys on the slots with the longest lines: F L1 UP and D L1 L3 have
// 15-character defaults, F CIRCLE RIGHTL has the longest name (and no default).
void shippedState() {
    eeErase();
    eeHeader(0, 1, kGen, kPrefix11, kSuffix7);
    eeSlot(slotIndex(Ctrl::F, Mod::L1, Btn::UP), kGen, kKey15);
    eeSlot(slotIndex(Ctrl::D, Mod::L1, Btn::L3), kGen, kKey15);
    eeSlot(kWide, kGen, kKey15);
    store.begin();
}

}  // namespace

static void test_shipped_defaults() {
    timerBegin();
    TEST_ASSERT_TRUE(defaultButtonMappingsCount > 0);
    shippedState();
    drainAll();
    TEST_ASSERT_TRUE(protocol.handleLine(crcLine(PSTR("@SM HELLO 20")), port));
    drainAll();

    Worst pass;
    TEST_ASSERT_TRUE(protocol.handleLine(crcLine(PSTR("@SM DUMP")), port));
    wcb.longest = 0;
    for (uint16_t i = 0; i < 4000 && protocol.opInProgress(); ++i) {
        const uint16_t a = ticksOf([] { protocol.service(); });
        const uint16_t b = ticksOf([] { store.step(); });
        const uint16_t c = ticksOf([] { tx.drain(wcb); });
        wcb.note();
        pass.add(static_cast<uint16_t>(a + b + c));
    }
    drainAll();
    record(PSTR("firmware defaults: DUMP worst pass (service+step+drain)"), pass.ticks, kT1,
           static_cast<uint8_t>(wcb.longest - kRoute20));

    pass = Worst();
    TEST_ASSERT_TRUE(protocol.handleLine(crcLine(PSTR("@SM SUM")), port));
    for (uint16_t i = 0; i < 4000 && protocol.opInProgress(); ++i) {
        pass.add(ticksOf([] { protocol.service(); }));
        drainAll();
    }
    record(PSTR("firmware defaults: SUM worst service()"), pass.ticks);

    drainAll();
    bool handled = false;
    const char *get = crcLine(PSTR("@SM GET D L1 L3"));
    uint16_t t = ticksOf([&] { handled = protocol.handleLine(get, port); });
    TEST_ASSERT_TRUE(handled);
    drainAll();
    record(PSTR("firmware defaults: handleLine GET D L1 L3 (15-char key, 15-char default)"), t, kT1,
           static_cast<uint8_t>(wcb.last - kRoute20));

    pass = Worst();
    TEST_ASSERT_TRUE(protocol.handleLine(crcLine(PSTR("@SM DEFAULTS")), port));
    for (uint16_t i = 0; i < 4000 && (store.busy() || protocol.opInProgress()); ++i) {
        protocol.service();
        pass.add(ticksOf([] { store.step(); }));
        drainAll();
    }
    TEST_ASSERT_EQUAL_UINT16(65535, store.generation());
    record(PSTR("firmware defaults: DEFAULTS worst step()"), pass.ticks);

    // Boot (setup(), not a loop pass): every double-tap and long-press record read once.
    shippedState();
    t = ticksOf([] { store.begin(); });
    part(PSTR("begin() at boot, firmware defaults (setup(), not a pass)"), t);
}

static void test_every_pass_is_under_1_ms() { checkBudget(); }

#else

static void test_shipped_defaults() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_every_pass_is_under_1_ms() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_shipped_defaults);
    RUN_TEST(test_every_pass_is_under_1_ms);
    return UNITY_END();
}
