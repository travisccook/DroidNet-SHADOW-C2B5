// Worst-case time of every @SM request, every pass of each write, DUMP and SUM, and every step
// of the EEPROM writer, with the data that makes each one longest. AVR only (simavr); see
// test/support/sm_avr_timing.h for the method. The PART lines are building blocks for the
// breakdowns, not passes.
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
MapStore store(io, kWorstDefaults, kWorstDefaultCount);
TxQueue tx;
WcbPort port(tx);
Protocol protocol(store, kFw11);
NullUart wcb;

constexpr uint8_t kRoute20 = 8;  // ";W20,;S0", the longest route, set by HELLO 20

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

const char *crcLineRam(const char *body) {
    withCrc(body, gLine, sizeof(gLine));
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

void hexOf(const char *s, char *out) {
    static const char kHex[] = "0123456789ABCDEF";
    for (; *s != '\0'; ++s) {
        *out++ = kHex[static_cast<uint8_t>(*s) >> 4];
        *out++ = kHex[static_cast<uint8_t>(*s) & 0x0F];
    }
    *out = '\0';
}

void worstState() {
    eeWorst();
    store.begin();
    char key[kKeyBuf];
    Source src = Source::Eeprom;
    TEST_ASSERT_TRUE(store.keyForSlot(kWide, key, &src));
    TEST_ASSERT_EQUAL_STRING(kDef15, key);
    TEST_ASSERT_TRUE(src == Source::Default);
    TEST_ASSERT_EQUAL_STRING(kPrefix11, store.prefix());
    TEST_ASSERT_EQUAL_STRING(kSuffix7, store.suffix());
}

struct OpWorst {
    Worst service, step, pass;
};

// Every pass of a write until it has finished and replied: service(), step() and the drain,
// timed one by one (Runtime::service() runs them in that order), and their sum per pass.
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

}  // namespace

// Each request on its own, inside one handleLine() (pump() calls it in the pass the line ends).
static void test_requests() {
    timerBegin();
    TEST_ASSERT_TRUE(kWorstDefaultCount >= defaultButtonMappingsCount + 2);
    worstState();
    protocol.setPads(true, true);
    protocol.setBattery({BattLevel::Crit, 1, BattLevel::Crit, 1, 0xFFFFFFFFUL});

    uint16_t t = timeLine(crcLine(PSTR("@SM HELLO 20")));
    TEST_ASSERT_EQUAL_UINT8(20, port.route());
    record(PSTR("handleLine HELLO 20 (fw 11 chars, gen 65534)"), t, false, replyLen());

    record(PSTR("handleLine CFG (11-char prefix, 7-char suffix)"), timeLine(crcLine(PSTR("@SM CFG"))), false, replyLen());
    record(PSTR("handleLine BAT (CRIT/CRIT, up=4294967)"), timeLine(crcLine(PSTR("@SM BAT"))), false, replyLen());
    record(PSTR("handleLine EVENTS 1"), timeLine(crcLine(PSTR("@SM EVENTS 1"))), false, replyLen());
    protocol.setPads(false, true);
    t = ticksOf([] { protocol.service(); });
    drainAll();
    record(PSTR("service(): CTL event (a controller dropped)"), t, false, replyLen());
    protocol.setPads(true, true);
    protocol.service();
    drainAll();

    t = timeLine(crcLine(PSTR("@SM GET F CIRCLE RIGHTL")));
    TEST_ASSERT_EQUAL_UINT8(78, replyLen());
    record(PSTR("handleLine GET F CIRCLE RIGHTL (stale 15-char record, 15-char default)"), t, false, replyLen());

    eeSlot(kWide, kGen, kKey15);
    t = timeLine(crcLine(PSTR("@SM GET F CIRCLE RIGHTL")));
    TEST_ASSERT_EQUAL_UINT8(77, replyLen());
    record(PSTR("handleLine GET F CIRCLE RIGHTL (15-char key over 15-char default)"), t, false, replyLen());
    eeSlot(kWide, kGen - 1, kKey15);

    record(PSTR("handleLine GET D CIRCLE RIGHT (stale record, 15-char default)"),
           timeLine(crcLine(PSTR("@SM GET D CIRCLE RIGHT"))), false, replyLen());

    // 72 characters, the longest line LineReader passes on.
    char body[LineReader::kMaxLen + 1];
    memset(body, 'x', 62);
    memcpy(body, "@SM ", 4);
    memcpy(body + 62, " *00000000", 11);
    record(PSTR("handleLine bad CRC (72-char line)"), timeLine(body), false, replyLen());

    memset(body, 'x', LineReader::kMaxLen);
    memcpy(body, "@SM ", 4);
    body[LineReader::kMaxLen] = '\0';
    record(PSTR("handleLine no CRC on the WCB port (72-char line)"), timeLine(body), false, replyLen());

    strcpy_P(body, PSTR("@SM ABCDEFGHIJ abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg"));
    TEST_ASSERT_EQUAL_UINT(62, strlen(body));
    record(PSTR("handleLine unknown verb, 7 tokens, valid CRC (72-char line)"), timeLine(crcLineRam(body)), false,
           replyLen());

    strcpy_P(body, PSTR("@SM GET F CIRCLE RIGHTL x"));
    record(PSTR("handleLine GET with a stray argument (ERR slot)"), timeLine(crcLineRam(body)), false, replyLen());
}

// The writes: the request, then every pass until the reply has gone.
static void test_writes() {
    worstState();
    OpWorst w;
    uint16_t t;

    // SET of a long-press slot: the slot CRC, and on its last step the RAM bit's reload.
    t = timeLine(crcLine(PSTR("@SM SET F CIRCLE RIGHTL zyxwvutsrqponml")));
    record(PSTR("handleLine SET F CIRCLE RIGHTL (15 chars)"), t, true);
    runOp(w);
    record(PSTR("SET long press: worst service() (OK reply)"), w.service.ticks, true, replyLen());
    record(PSTR("SET long press: worst step()"), w.step.ticks, true);
    record(PSTR("SET long press: worst pass (service+step+drain)"), w.pass.ticks, true);

    w = OpWorst();
    t = timeLine(crcLine(PSTR("@SM SET D CIRCLE RIGHT zyxwvutsrqponml")));
    record(PSTR("handleLine SET D CIRCLE RIGHT (15 chars)"), t, true);
    runOp(w);
    record(PSTR("SET single: worst service() (OK reply)"), w.service.ticks, true);
    record(PSTR("SET single: worst step()"), w.step.ticks, true);
    record(PSTR("SET single: worst pass (service+step+drain)"), w.pass.ticks, true);

    // REVERT of a long-press slot: on its last step the record reads as erased, so the RAM bit
    // comes from the defaults (a scan to the end of the table).
    w = OpWorst();
    t = timeLine(crcLine(PSTR("@SM REVERT F CIRCLE RIGHTL")));
    record(PSTR("handleLine REVERT F CIRCLE RIGHTL"), t);
    runOp(w);
    record(PSTR("REVERT long press: worst service() (OK reply)"), w.service.ticks);
    record(PSTR("REVERT long press: worst step()"), w.step.ticks);
    record(PSTR("REVERT long press: worst pass (service+step+drain)"), w.pass.ticks, false, replyLen());

    // PREFIX / SUFFIX: header writes, hex echoed in the reply.
    char body[LineReader::kMaxLen + 1];
    strcpy_P(body, PSTR("@SM PREFIX "));
    hexOf(kPrefix11, body + strlen(body));
    w = OpWorst();
    t = timeLine(crcLineRam(body));
    record(PSTR("handleLine PREFIX (11 chars)"), t);
    runOp(w);
    record(PSTR("PREFIX: worst service() (OK reply, 22 hex digits)"), w.service.ticks);
    record(PSTR("PREFIX: worst step()"), w.step.ticks);
    record(PSTR("PREFIX: worst pass (service+step+drain)"), w.pass.ticks, false, replyLen());

    strcpy_P(body, PSTR("@SM SUFFIX "));
    hexOf(kSuffix7, body + strlen(body));
    w = OpWorst();
    t = timeLine(crcLineRam(body));
    record(PSTR("handleLine SUFFIX (7 chars)"), t);
    runOp(w);
    record(PSTR("SUFFIX: worst service() (OK reply, 14 hex digits)"), w.service.ticks);
    record(PSTR("SUFFIX: worst step()"), w.step.ticks);
    record(PSTR("SUFFIX: worst pass (service+step+drain)"), w.pass.ticks, false, replyLen());

    // DEFAULTS: a header with the next generation; its last step reloads every double-tap and
    // long-press bit from the defaults (144 scans of the table).
    w = OpWorst();
    t = timeLine(crcLine(PSTR("@SM DEFAULTS")));
    record(PSTR("handleLine DEFAULTS"), t);
    runOp(w);
    TEST_ASSERT_EQUAL_UINT16(65535, store.generation());
    record(PSTR("DEFAULTS: worst service() (OK gen=65535)"), w.service.ticks, false, replyLen());
    record(PSTR("DEFAULTS: worst step()"), w.step.ticks);
    record(PSTR("DEFAULTS: worst pass (service+step+drain)"), w.pass.ticks);

    // The first write ever: header A (the format), then the slot.
    eeErase();
    store.begin();
    TEST_ASSERT_FALSE(store.formatted());
    w = OpWorst();
    t = timeLine(crcLine(PSTR("@SM SET F CIRCLE RIGHTL zyxwvutsrqponml")));
    record(PSTR("handleLine SET, first write (formats)"), t);
    runOp(w);
    TEST_ASSERT_TRUE(store.formatted());
    record(PSTR("SET first write: worst service() (OK reply)"), w.service.ticks);
    record(PSTR("SET first write: worst step()"), w.step.ticks);
    record(PSTR("SET first write: worst pass (service+step+drain)"), w.pass.ticks);

    // A write refused because a DUMP is running.
    worstState();
    timeLine(crcLine(PSTR("@SM DUMP")));
    t = timeLine(crcLine(PSTR("@SM SET F CIRCLE RIGHTL zyxwvutsrqponml")));
    record(PSTR("handleLine SET during a DUMP (ERR busy, slot named)"), t, false, replyLen());
    for (uint16_t i = 0; i < 1000 && protocol.opInProgress(); ++i) {
        protocol.service();
        drainAll();
    }
    TEST_ASSERT_FALSE(protocol.opInProgress());
}

// DUMP and SUM: one slot per pass. The worst pass is the slot with the longest line.
static void test_dump_and_sum() {
    worstState();
    OpWorst w;
    uint16_t t = timeLine(crcLine(PSTR("@SM DUMP")));
    record(PSTR("handleLine DUMP (starts it)"), t);
    wcb.longest = 0;
    runOp(w);
    TEST_ASSERT_EQUAL_UINT8(kRoute20 + 78, wcb.longest);
    record(PSTR("DUMP: worst service()"), w.service.ticks, true, static_cast<uint8_t>(wcb.longest - kRoute20));
    record(PSTR("DUMP: worst pass (service+step+drain)"), w.pass.ticks, true);

    eeSlot(kWide, kGen, kKey15);
    w = OpWorst();
    timeLine(crcLine(PSTR("@SM DUMP")));
    wcb.longest = 0;
    runOp(w);
    record(PSTR("DUMP, 15-char key on F CIRCLE RIGHTL: worst pass"), w.pass.ticks, true,
           static_cast<uint8_t>(wcb.longest - kRoute20));
    eeSlot(kWide, kGen - 1, kKey15);

    w = OpWorst();
    t = timeLine(crcLine(PSTR("@SM SUM")));
    record(PSTR("handleLine SUM (starts it)"), t);
    runOp(w);
    record(PSTR("SUM: worst service()"), w.service.ticks);
    record(PSTR("SUM: worst pass (service+step+drain)"), w.pass.ticks, false, replyLen());
}

// Building blocks, timed alone, for the breakdowns and the per-byte costs.
static void test_parts() {
    worstState();
    char key[kKeyBuf];
    Source src = Source::Default;
    uint8_t rec[MapStore::kSlotSize];
    char text[96];

    gSlotIn = kWide;
    part(PSTR("keyForSlot F CIRCLE RIGHTL, stale record (read, CRC16 fails, default)"),
         ticksOf([&] { store.keyForSlot(gSlotIn, key, &src); }));
    eeSlot(kWide, kGen, kKey15);
    part(PSTR("keyForSlot F CIRCLE RIGHTL, valid record (read, CRC16)"),
         ticksOf([&] { store.keyForSlot(gSlotIn, key, &src); }));
    eeSlot(kWide, kGen - 1, kKey15);
    gSlotIn = slotIndex(Ctrl::D, Mod::L2, Btn::LEFT, Tap::LONG);
    part(PSTR("keyForSlot of an erased slot (read, default)"), ticksOf([&] { store.keyForSlot(gSlotIn, key, &src); }));
    part(PSTR("defaultKey, no match in 38 entries"), ticksOf([&] { store.defaultKey(gSlotIn, key); }));
    gSlotIn = kWide;
    part(PSTR("defaultKey, match at the end of 38 entries"), ticksOf([&] { store.defaultKey(gSlotIn, key); }));
    part(PSTR("18 EEPROM reads (one slot record, AvrEeprom)"), ticksOf([&] {
             for (uint8_t i = 0; i < MapStore::kSlotSize; ++i)
                 gSink8 = io.read(static_cast<uint16_t>(64 + i));
         }));
    for (uint8_t i = 0; i < MapStore::kSlotSize; ++i)
        rec[i] = io.read(static_cast<uint16_t>(64 + i));
    part(PSTR("CRC16 of one slot record (20 bytes, bitwise)"), ticksOf([&] {
             uint16_t crc = kCrc16Init;
             for (uint8_t i = 0; i < 20; ++i)
                 crc = crc16Update(crc, rec[i % MapStore::kSlotSize]);
             gSink16 = crc;
         }));
    part(PSTR("CRC16 of a header (30 bytes, bitwise)"), ticksOf([&] {
             uint16_t crc = kCrc16Init;
             for (uint8_t i = 0; i < 30; ++i)
                 crc = crc16Update(crc, rec[i % MapStore::kSlotSize]);
             gSink16 = crc;
         }));
    memset(text, 'a', sizeof(text));
    part(PSTR("CRC32 of 68 bytes (a SLOT line before \" *\")"),
         ticksOf([&] { gSink32 = crc32(reinterpret_cast<const uint8_t *>(text), 68); }));
    part(PSTR("CRC32 of 10 bytes"), ticksOf([&] { gSink32 = crc32(reinterpret_cast<const uint8_t *>(text), 10); }));
    part(PSTR("slotReserved F CIRCLE RIGHTL"), ticksOf([&] { gSink8 = static_cast<uint8_t>(slotReserved(gSlotIn)); }));
    part(PSTR("slotCtrl + slotMod + slotBtn + slotTap"), ticksOf([&] {
             const uint8_t s = gSlotIn;
             gSink8 = static_cast<uint8_t>(static_cast<uint8_t>(slotCtrl(s)) + static_cast<uint8_t>(slotMod(s)) +
                                           static_cast<uint8_t>(slotBtn(s)) + static_cast<uint8_t>(slotTap(s)));
         }));
    part(PSTR("formatSlot F CIRCLE RIGHTL"), ticksOf([&] { gSink8 = formatSlot(text, sizeof(text), gSlotIn); }));

    drainAll();
    memset(text, 'a', 78);
    text[78] = '\0';
    const char *parts[] = {port.routePrefix(), text};
    part(PSTR("TxQueue::pushLine of 8 + 78 bytes + CR (put() per byte)"), ticksOf([&] { tx.pushLine(parts, 2); }));
    part(PSTR("TxQueue::pushLine of 10 bytes + CR"), ticksOf([&] {
             const char *p[] = {port.routePrefix(), "ab"};
             tx.pushLine(p, 2);
         }));
    part(PSTR("TxQueue::drain of 63 bytes to NullUart"), ticksOf([&] { tx.drain(wcb); }));
    wcb.note();
    drainAll();

    // The cost of one more key character in a SLOT reply (formatting, CRC32, queueing): GET of a
    // 15- and a 1-character key on F CIRCLE RIGHTL.
    eeSlot(kWide, kGen, kKey15);
    const uint16_t get15 = timeLine(crcLine(PSTR("@SM GET F CIRCLE RIGHTL")));
    eeSlot(kWide, kGen, "z");
    const uint16_t get1 = timeLine(crcLine(PSTR("@SM GET F CIRCLE RIGHTL")));
    part(PSTR("GET, 15-char key minus 1-char key (14 more chars)"), static_cast<uint16_t>(get15 - get1));
    eeSlot(kWide, kGen - 1, kKey15);

    // ... and of one more hex-encoded byte (2 digits): CFG with an 11- and a 1-character prefix.
    const uint16_t cfg11 = timeLine(crcLine(PSTR("@SM CFG")));
    eeHeader(0, 1, kGen, ";", kSuffix7);
    store.begin();
    const uint16_t cfg1 = timeLine(crcLine(PSTR("@SM CFG")));
    part(PSTR("CFG, 11-char prefix minus 1-char prefix (10 more hex bytes)"), static_cast<uint16_t>(cfg11 - cfg1));
    worstState();

    // Out::u: one 32-bit decimal digit (BAT's up= with 7 digits vs 1).
    protocol.setBattery({BattLevel::Crit, 1, BattLevel::Crit, 1, 0xFFFFFFFFUL});
    const uint16_t bat7 = timeLine(crcLine(PSTR("@SM BAT")));
    protocol.setBattery({BattLevel::Crit, 1, BattLevel::Crit, 1, 0});
    const uint16_t bat1 = timeLine(crcLine(PSTR("@SM BAT")));
    part(PSTR("BAT, up=4294967 minus up=0 (6 more digits)"), static_cast<uint16_t>(bat7 - bat1));
}

static void test_every_pass_is_under_1_ms() { checkBudget(); }

#else

static void test_requests() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_writes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_dump_and_sum() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_parts() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_every_pass_is_under_1_ms() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_requests);
    RUN_TEST(test_writes);
    RUN_TEST(test_dump_and_sum);
    RUN_TEST(test_parts);
    RUN_TEST(test_every_pass_is_under_1_ms);
    return UNITY_END();
}
