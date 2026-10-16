// Worst-case time of the controller-address work in one loop() pass: each @SM MAC request, every
// pass of a MAC write and of the MAC list, a REFUSED event to each port, and macAllows(). AVR only
// (simavr); see test/support/sm_avr_timing.h for the method. One MapStore and one Protocol, as in
// the firmware.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#include <sm_test_main.h>
#include <sm_avr_timing.h>

#if defined(__AVR__)

using namespace sm;
using namespace smt;

namespace {

AvrEeprom io;
MapStore store(io, kWorstDefaults, kWorstDefaultCount);
TxQueue tx;
WcbPort port(tx);
Protocol protocol(store, kFw11);
NullUart wcb;

// The USB port: takes every line, keeps nothing.
class SinkPort final : public ReplyPort {
  public:
    bool canSend(uint8_t) override { return true; }
    void send(const char *, uint8_t len) override { last = len; }
    bool requiresCrc() const override { return false; }
    uint8_t last = 0;
};
SinkPort usb;

const uint8_t kMacA[kMacLen] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x05};
const uint8_t kMacB[kMacLen] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

void drainAll() {
    while (tx.used() > 0) {
        tx.drain(wcb);
        wcb.note();
    }
}

// The reply's length as the Pi sees it (route stripped).
uint8_t replyLen() {
    const uint8_t route = static_cast<uint8_t>(strlen(port.routePrefix()));
    return wcb.last > route ? static_cast<uint8_t>(wcb.last - route) : 0;
}

char gLine[LineReader::kMaxLen + 8];

// `body` (flash) + " *<CRC32>".
const char *crcLine(PGM_P body) {
    char b[LineReader::kMaxLen + 1];
    strncpy_P(b, body, sizeof(b) - 1);
    b[sizeof(b) - 1] = '\0';
    withCrc(b, gLine, sizeof(gLine));
    return gLine;
}

uint16_t timeLine(const char *line) {
    drainAll();
    wcb.last = 0;
    bool handled = false;
    const uint16_t t = ticksOf([&] { handled = protocol.handleLine(line, port); });
    TEST_ASSERT_TRUE(handled);
    drainAll();
    return t;
}

struct OpWorst {
    Worst service, step, pass;
};

// Every pass of an operation until it has finished and replied: service(), step() and the drain,
// timed one by one, and their sum per pass.
void runOp(OpWorst &w) {
    for (uint16_t i = 0; i < 4000 && (store.busy() || protocol.opInProgress()); ++i) {
        const uint16_t a = ticksOf([] { protocol.service(); });
        const uint16_t b = ticksOf([] { store.step(); });
        const uint16_t c = ticksOf([] { tx.drain(wcb); });
        wcb.note();
        w.service.add(a);
        w.step.add(b);
        w.pass.add(static_cast<uint16_t>(a + b + c));
    }
    TEST_ASSERT_FALSE(store.busy());
    TEST_ASSERT_FALSE(protocol.opInProgress());
    drainAll();
}

// The worst data: the mapping's worst state, and every controller slot holding an address in
// EEPROM and in the build, so each lookup and list line is the longest.
void worstState() {
    eeWorst();
    store.begin();
    for (uint8_t i = 0; i < kMacSlots; ++i) {
        TEST_ASSERT_TRUE(store.beginMac(i, MapStore::MacState::Address, kMacA) == MapStore::Result::Ok);
        while (store.busy())
            store.step();
        TEST_ASSERT_TRUE(store.takeCompletion() == MapStore::Completion::Done);
        store.setBuiltInMac(i, kMacB);
    }
    timeLine(crcLine(PSTR("@SM HELLO 20")));  // the longest route
}

}  // namespace

static void test_requests() {
    timerBegin();
    worstState();
    // Each time is taken before replyLen() is read: as arguments of one call, their order isn't fixed.
    uint16_t t = timeLine(crcLine(PSTR("@SM MAC F2")));
    record(PSTR("handleLine MAC F2 (an address, from EEPROM)"), t, kT1, replyLen());
    t = timeLine(crcLine(PSTR("@SM MAC F2 00:1A:2B:3C:4D:0G")));
    record(PSTR("handleLine MAC F2 with a bad address (ERR addr)"), t, kT1, replyLen());
    t = timeLine(crcLine(PSTR("@SM MAC F3 -")));
    record(PSTR("handleLine MAC F2 with a bad slot (ERR args)"), t, kT1, replyLen());

    OpWorst w;
    record(PSTR("handleLine MAC (the list starts)"), timeLine(crcLine(PSTR("@SM MAC"))), kT1);
    runOp(w);
    record(PSTR("MAC list: worst service() (a MAC line)"), w.service.ticks, kT1, replyLen());
    record(PSTR("MAC list: worst pass (service+step+drain)"), w.pass.ticks, kT1);
}

static void test_writes() {
    worstState();
    OpWorst w;
    record(PSTR("handleLine MAC F2 <address>"), timeLine(crcLine(PSTR("@SM MAC F2 12:34:56:78:9A:BC"))), kT1);
    runOp(w);
    record(PSTR("MAC <address>: worst service() (OK reply)"), w.service.ticks, kT1, replyLen());
    record(PSTR("MAC <address>: worst step() (its finish reloads the record)"), w.step.ticks, kT1);
    record(PSTR("MAC <address>: worst pass (service+step+drain)"), w.pass.ticks, kT1);

    w = OpWorst();
    record(PSTR("handleLine MAC D2 -"), timeLine(crcLine(PSTR("@SM MAC D2 -"))), kT1);
    runOp(w);
    record(PSTR("MAC -: worst pass (service+step+drain)"), w.pass.ticks, kT1, replyLen());

    w = OpWorst();
    record(PSTR("handleLine MAC D2 default"), timeLine(crcLine(PSTR("@SM MAC D2 default"))), kT1);
    runOp(w);
    record(PSTR("MAC default: worst pass (service+step+drain)"), w.pass.ticks, kT1, replyLen());
}

static void test_refused_and_allows() {
    worstState();
    protocol.setConsole(usb);
    timeLine(crcLine(PSTR("@SM EVENTS 1")));
    const uint8_t bdaddr[kMacLen] = {0xBC, 0x9A, 0x78, 0x56, 0x34, 0x13};  // in no slot: every slot is compared
    part(PSTR("noteRefused()"), ticksOf([&] { protocol.noteRefused(Ctrl::D, bdaddr); }));
    usb.last = 0;
    uint16_t t = ticksOf([] { protocol.service(); });
    TEST_ASSERT_EQUAL_UINT8(41, usb.last);
    record(PSTR("service(): REFUSED to the USB port"), t, kT1, usb.last);
    t = ticksOf([] { protocol.service(); });
    drainAll();
    record(PSTR("service(): REFUSED to the WCB (EVENTS 1)"), t, kT1, replyLen());

    bool allowed = true;
    part(PSTR("macAllows(): no match, both slots compared"),
         ticksOf([&] { allowed = store.macAllows(Ctrl::D, bdaddr); }));
    TEST_ASSERT_FALSE(allowed);
}

static void test_every_pass_is_under_1_ms() { checkBudget(); }

#else

static void test_requests() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_writes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_refused_and_allows() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_every_pass_is_under_1_ms() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_requests);
    RUN_TEST(test_writes);
    RUN_TEST(test_refused_and_allows);
    RUN_TEST(test_every_pass_is_under_1_ms);
    return UNITY_END();
}
