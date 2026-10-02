// The line builder (sm_out.h) against the one it replaced (copied here from 0a8b73f's
// sm_protocol.cpp), the request front end's CRC rules, and the TX queue's block copies.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#include <sm_test_main.h>
#include <stdio.h>
#include <string.h>

using namespace sm;

namespace {

namespace old {

const char kHexDigits[] = "0123456789ABCDEF";

class Out {
  public:
    Out() { buf_[0] = '\0'; }
    Out &p(const char *s) { return this->s(s); }
    Out &s(const char *s) {
        const size_t n = strlen(s);
        if (reserve(n)) {
            memcpy(buf_ + len_, s, n);
            advance(n);
        }
        return *this;
    }
    Out &c(char ch) {
        if (reserve(1)) {
            buf_[len_] = ch;
            advance(1);
        }
        return *this;
    }
    Out &u(uint32_t v) {
        char tmp[10];
        uint8_t n = 0;
        do {
            tmp[n++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v != 0);
        while (n > 0)
            c(tmp[--n]);
        return *this;
    }
    Out &hex(const char *s) {
        if (*s == '\0')
            return c('-');
        for (; *s != '\0'; ++s) {
            const uint8_t b = static_cast<uint8_t>(*s);
            c(kHexDigits[b >> 4]).c(kHexDigits[b & 0x0F]);
        }
        return *this;
    }
    Out &key(const char *k) { return *k == '\0' ? c('-') : s(k); }
    Out &slot(uint8_t slot) {
        char tmp[kSlotTextBuf];
        tmp[0] = '\0';
        if (formatSlot(tmp, sizeof(tmp), slot) == 0)
            overflow_ = true;
        return s(tmp);
    }
    bool finish() {
        const uint32_t crc = crc32(reinterpret_cast<const uint8_t *>(buf_), len_);
        c(' ').c('*');
        for (int8_t shift = 28; shift >= 0; shift -= 4)
            c(kHexDigits[(crc >> shift) & 0x0F]);
        return !overflow_;
    }
    const char *str() const { return buf_; }
    uint8_t len() const { return len_; }

  private:
    bool reserve(size_t n) {
        if (overflow_ || len_ + n > 80) {
            overflow_ = true;
            return false;
        }
        return true;
    }
    void advance(size_t n) {
        len_ = static_cast<uint8_t>(len_ + n);
        buf_[len_] = '\0';
    }
    char buf_[81];
    uint8_t len_ = 0;
    bool overflow_ = false;
};

}  // namespace old

uint32_t gRng = 0x51ED2701UL;
uint32_t rnd() {
    gRng ^= gRng << 13;
    gRng ^= gRng >> 17;
    gRng ^= gRng << 5;
    return gRng;
}

void randomText(char *out, uint8_t n) {
    for (uint8_t i = 0; i < n; ++i)
        out[i] = static_cast<char>(0x20 + rnd() % 95);
    out[n] = '\0';
}

}  // namespace

static void test_u_matches_printf() {
    static const uint32_t kValues[] = {0, 9, 10, 99, 100, 216, 999, 1000, 65535, 65536, 999999999UL, 1000000000UL,
                                       4294967295UL};
    char want[16];
    for (uint32_t v : kValues) {
        snprintf(want, sizeof(want), "%lu", static_cast<unsigned long>(v));
        Out o;
        o.u(v);
        TEST_ASSERT_EQUAL_STRING(want, o.str());
    }
#if defined(__AVR__)
    const uint16_t n = 300;
#else
    const uint16_t n = 10000;
#endif
    for (uint16_t i = 0; i < n; ++i) {
        uint32_t v = rnd();
        v >>= rnd() % 32;  // every magnitude
        snprintf(want, sizeof(want), "%lu", static_cast<unsigned long>(v));
        Out o;
        o.u(v);
        TEST_ASSERT_EQUAL_STRING(want, o.str());
    }
}

static void test_u_thousandths_is_u_of_v_over_1000() {
    char want[16];
    for (uint32_t v = 0; v <= 2000; ++v) {
        Out a, b;
        a.uThousandths(v);
        b.u(v / 1000);
        TEST_ASSERT_EQUAL_STRING(b.str(), a.str());
    }
    for (uint32_t v = 4294966000UL; v != 0; ++v) {  // up to 4294967295, then it wraps to 0
        Out a;
        a.uThousandths(v);
        snprintf(want, sizeof(want), "%lu", static_cast<unsigned long>(v / 1000));
        TEST_ASSERT_EQUAL_STRING(want, a.str());
    }
    for (uint16_t i = 0; i < 2000; ++i) {
        const uint32_t v = rnd() >> (rnd() % 32);
        Out a;
        a.uThousandths(v);
        snprintf(want, sizeof(want), "%lu", static_cast<unsigned long>(v / 1000));
        TEST_ASSERT_EQUAL_STRING(want, a.str());
    }
}

// Random sequences of appends give the same line as the old builder, and finish() the same
// verdict, including every overflow.
static void test_appends_match_the_old_builder() {
#if defined(__AVR__)
    const uint16_t rounds = 200;
#else
    const uint16_t rounds = 20000;
#endif
    char text[100];
    for (uint16_t r = 0; r < rounds; ++r) {
        Out a;
        old::Out b;
        const uint8_t ops = static_cast<uint8_t>(1 + rnd() % 12);
        for (uint8_t k = 0; k < ops; ++k) {
            switch (rnd() % 7) {
                case 0:
                    randomText(text, static_cast<uint8_t>(rnd() % 30));
                    a.s(text);
                    b.s(text);
                    break;
                case 1:
                    randomText(text, static_cast<uint8_t>(rnd() % 20));
#if defined(__AVR__)
                    a.s(text);  // p() reads flash there; the protocol's lines cover it
#else
                    a.p(text);  // the host's PGM_P is RAM
#endif
                    b.p(text);
                    break;
                case 2: {
                    const char ch = static_cast<char>(0x20 + rnd() % 95);
                    a.c(ch);
                    b.c(ch);
                    break;
                }
                case 3: {
                    const uint32_t v = rnd() >> (rnd() % 32);
                    a.u(v);
                    b.u(v);
                    break;
                }
                case 4:
                    randomText(text, static_cast<uint8_t>(rnd() % 16));
                    for (char *t = text; *t != '\0'; ++t)
                        *t = static_cast<char>(*t | (rnd() & 0x80));  // bytes over 0x7F too
                    a.hex(text);
                    b.hex(text);
                    break;
                case 5: {
                    randomText(text, static_cast<uint8_t>(rnd() % 16));
                    const char *k = rnd() % 4 == 0 ? "" : text;
                    a.key(k);
                    b.key(k);
                    break;
                }
                default: {
                    const uint8_t slot = static_cast<uint8_t>(rnd() % 230);  // some past the last
                    a.slot(slot);
                    b.slot(slot);
                    break;
                }
            }
        }
        const bool fa = a.finish();
        const bool fb = b.finish();
        TEST_ASSERT_EQUAL(fb, fa);
        if (fb) {
            TEST_ASSERT_EQUAL_STRING(b.str(), a.str());
            TEST_ASSERT_EQUAL_UINT8(b.len(), a.len());
        }
    }
}

// A line of kMaxLine - 11, - 10 and - 9 characters before its CRC: the first two are sent, the
// last doesn't fit.
static void test_lines_at_the_limit() {
    char text[100];
    for (uint8_t len = Protocol::kMaxLine - 12; len <= Protocol::kMaxLine; ++len) {
        memset(text, 'x', len);
        text[len] = '\0';
        Out a;
        old::Out b;
        a.s(text);
        b.s(text);
        const bool fa = a.finish();
        TEST_ASSERT_EQUAL(b.finish(), fa);
        TEST_ASSERT_EQUAL(len + 10 <= Protocol::kMaxLine, fa);
        if (fa)
            TEST_ASSERT_EQUAL_STRING(b.str(), a.str());
    }
}

// head() starts the CRC from a state the compiler worked out: every slot, both heads.
static void test_head_states_for_every_slot() {
    char want[100];
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        for (uint8_t h = 0; h < 2; ++h) {
            Out a;
            old::Out b;
            a.head(h != 0 ? Out::Head::Press : Out::Head::Slot, slot).c(' ').key("abcdefghijklmno");
            b.p(h != 0 ? "@SM PRESS " : "@SM SLOT ").slot(slot).c(' ').key("abcdefghijklmno");
            TEST_ASSERT_TRUE(a.finish());
            TEST_ASSERT_TRUE(b.finish());
            TEST_ASSERT_EQUAL_STRING(b.str(), a.str());
            TEST_ASSERT_TRUE(stripCrc(a.str(), want, sizeof(want)));
        }
    }
    // head() on a line already started falls back to the whole-line CRC.
    Out a;
    old::Out b;
    a.c('x').head(Out::Head::Slot, 3);
    b.c('x').p("@SM SLOT ").slot(3);
    TEST_ASSERT_TRUE(a.finish());
    TEST_ASSERT_TRUE(b.finish());
    TEST_ASSERT_EQUAL_STRING(b.str(), a.str());
}

static void test_hex32() {
    for (uint16_t i = 0; i < 1000; ++i) {
        const uint32_t v = i == 0 ? 0 : (i == 1 ? 0xFFFFFFFFUL : rnd());
        char want[12];
        snprintf(want, sizeof(want), "%08lX", static_cast<unsigned long>(v));
        Out o;
        o.hex32(v);
        TEST_ASSERT_EQUAL_STRING(want, o.str());
    }
}

// The request front end: exactly eight hex digits (either case), the last " *" in the line.
static void test_request_crc_rules() {
    static FakeEeprom eeprom;
    eeprom.reset();
    MapStore store(eeprom, kTestDefaults, kTestDefaultCount);
    store.begin();
    Protocol protocol(store, "t");
    CapturePort port;
    port.crcRequired = true;
    char line[96], body[96];
    auto reply = [&](const char *l) -> const char * {
        TEST_ASSERT_TRUE(protocol.handleLine(l, port));
        static char out[96];
        TEST_ASSERT_TRUE(stripCrc(port.log.last(), out, sizeof(out)));
        return out;
    };
    withCrc("@SM CFG", line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("@SM CFG pre=3B57313B43 suf=-", reply(line));
    for (char *p = strstr(line, " *") + 2; *p != '\0'; ++p)
        *p = static_cast<char>(*p >= 'A' && *p <= 'F' ? *p + 32 : *p);  // lower case
    TEST_ASSERT_EQUAL_STRING("@SM CFG pre=3B57313B43 suf=-", reply(line));
    withCrc("@SM CFG", line, sizeof(line));
    const size_t n = strlen(line);
    strcpy(body, line);
    body[n - 1] = '\0';  // 7 digits
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", reply(body));
    snprintf(body, sizeof(body), "%s0", line);  // 9 digits
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", reply(body));
    strcpy(body, line);
    body[n - 3] = 'G';
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", reply(body));
    strcpy(body, line);
    body[n - 3] = '\0';  // a NUL inside: the line ends there
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", reply(body));
    // The last " *" counts: one earlier in the line is part of the body.
    withCrc("@SM CFG * x", line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args CFG", reply(line));
    withCrc("@SM CFG *", line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args CFG", reply(line));
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", reply("@SM CFG *"));      // " *" at the end, no digits
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", reply("@SM *12345678"));  // " *" right after "@SM"
    // "@SM *<CRC of '@SM'>": the CRC is good; the tokens are read from after "@SM ", which is the
    // CRC itself (as at 0a8b73f), not a verb.
    withCrc("@SM", line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("@SM ERR verb", reply(line));
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", reply("@SM CFG"));  // the WCB port needs a CRC
    port.crcRequired = false;
    TEST_ASSERT_EQUAL_STRING("@SM CFG pre=3B57313B43 suf=-", reply("@SM CFG"));
    // 73 characters is too long; 72 is read.
    memset(body, 'x', 73);
    memcpy(body, "@SM ", 4);
    body[73] = '\0';
    TEST_ASSERT_EQUAL_STRING("@SM ERR args", reply(body));
    body[72] = '\0';
    TEST_ASSERT_EQUAL_STRING("@SM ERR verb", reply(body));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args", reply("@SM"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args", reply("@SX CFG"));
}

// putBytes() at every position of the ring, every length up to 96.
static void test_tx_copies_at_every_tail() {
    struct Sink {
        char got[200];
        uint8_t n = 0;
        int availableForWrite() { return 200; }
        size_t write(const uint8_t *d, size_t k) {
            memcpy(got + n, d, k);
            n = static_cast<uint8_t>(n + k);
            return k;
        }
    };
    char a[100], b[100], fill[TxQueue::kCapacity];
    for (uint8_t tail = 0; tail < TxQueue::kCapacity; ++tail) {
        for (uint8_t len = 0; len <= 96; ++len) {
            TxQueue q;
            Sink sink;
            if (tail > 0) {
                memset(fill, 'f', tail - 1);
                fill[tail - 1] = '\0';
                const char *parts[] = {fill};
                TEST_ASSERT_TRUE(q.pushLine(parts, 1));
                q.drain(sink);
                sink.n = 0;
            }
            const uint8_t na = static_cast<uint8_t>(len / 3);
            randomText(a, na);
            randomText(b, static_cast<uint8_t>(len - na));
            TEST_ASSERT_TRUE(q.pushLine2(a, na, b, static_cast<uint8_t>(len - na)));
            TEST_ASSERT_EQUAL_UINT8(len + 1, q.used());
            q.drain(sink);
            TEST_ASSERT_EQUAL_UINT8(len + 1, sink.n);
            TEST_ASSERT_TRUE(memcmp(a, sink.got, na) == 0);
            TEST_ASSERT_TRUE(memcmp(b, sink.got + na, len - na) == 0);
            TEST_ASSERT_EQUAL_CHAR('\r', sink.got[len]);
        }
    }
    // All or nothing: a line one byte too long for the room leaves the queue as it was.
    TxQueue q;
    memset(a, 'x', 99);
    a[99] = '\0';
    const char *parts[] = {a, a};
    TEST_ASSERT_TRUE(q.pushLine(parts, 1));
    TEST_ASSERT_EQUAL_UINT8(100, q.used());
    TEST_ASSERT_FALSE(q.pushLine2(a, 60, nullptr, 0));
    TEST_ASSERT_TRUE(q.pushLine2(a, 59, nullptr, 0));
    TEST_ASSERT_EQUAL_UINT8(160, q.used());
    TEST_ASSERT_FALSE(q.pushLine(parts, 2));
}

#if !defined(__AVR__)
namespace {

// The whole EEPROM, for a store with every slot written (the shared fake keeps only a few
// double-tap and long-press records).
class FullEeprom final : public EepromIo {
  public:
    FullEeprom() { memset(mem, 0xFF, sizeof(mem)); }
    bool ready() override { return true; }
    uint8_t read(uint16_t addr) override { return mem[addr]; }
    void write(uint16_t addr, uint8_t value) override { mem[addr] = value; }
    uint8_t mem[4096];
};

DefaultMapping gAllDefaults[kSlotCount];

}  // namespace

// Every slot with a 15-character default and a 15-character record, stale (so its line says
// "default", the longer word) or valid: every DUMP line fits kMaxLine and SUM covers them all.
static void test_every_slot_at_its_longest_fits() {
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        gAllDefaults[slot].slot = slot;
        memcpy(gAllDefaults[slot].key, "abcdefghijklmno", kKeyBuf);
    }
    for (uint8_t stale = 0; stale < 2; ++stale) {
        static FullEeprom eeprom;
        memset(eeprom.mem, 0xFF, sizeof(eeprom.mem));
        MapStore store(eeprom, gAllDefaults, kSlotCount);
        store.begin();
        for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
            if (slotReserved(slot) != Reserved::No)
                continue;
            TEST_ASSERT_TRUE(store.beginSet(slot, "zyxwvutsrqponml") == MapStore::Result::Ok);
            while (store.busy())
                store.step();
            store.takeCompletion();
        }
        if (stale != 0) {
            TEST_ASSERT_TRUE(store.beginDefaults() == MapStore::Result::Ok);  // every record now stale
            while (store.busy())
                store.step();
            store.takeCompletion();
        }
        Protocol protocol(store, "t");
        CapturePort port;
        TEST_ASSERT_TRUE(protocol.handleLine("@SM DUMP", port));
        uint32_t crc = kCrc32Init;
        uint16_t lines = 0;
        uint8_t longest = 0;
        char body[100];
        for (uint16_t i = 0; i < 2000 && protocol.opInProgress(); ++i) {
            const uint32_t before = port.log.count;
            protocol.service();
            if (port.log.count != before) {
                const char *line = port.log.last();
                const uint8_t len = static_cast<uint8_t>(strlen(line));
                longest = len > longest ? len : longest;
                TEST_ASSERT_TRUE(stripCrc(line, body, sizeof(body)));
                if (strncmp(body, "@SM SLOT ", 9) == 0) {
                    ++lines;
                    crc = crc32Block(crc, reinterpret_cast<const uint8_t *>(body + 9), static_cast<uint8_t>(strlen(body + 9)));
                    crc = crc32Update(crc, '\n');
                }
            }
        }
        TEST_ASSERT_EQUAL_UINT16(kSlotCount, lines);
        TEST_ASSERT_EQUAL_UINT8(stale != 0 ? 78 : 77, longest);
        TEST_ASSERT_TRUE(longest <= Protocol::kMaxLine);
        TEST_ASSERT_TRUE(protocol.handleLine("@SM SUM", port));
        for (uint16_t i = 0; i < 2000 && protocol.opInProgress(); ++i)
            protocol.service();
        char want[40];
        snprintf(want, sizeof(want), "@SM SUM %08lX slots=216", static_cast<unsigned long>(crc32Final(crc)));
        TEST_ASSERT_TRUE(stripCrc(port.log.last(), body, sizeof(body)));
        TEST_ASSERT_EQUAL_STRING(want, body);
    }
}
#else
static void test_every_slot_at_its_longest_fits() { TEST_IGNORE_MESSAGE("host only (4 KB EEPROM image)"); }
#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_every_slot_at_its_longest_fits);
    RUN_TEST(test_u_matches_printf);
    RUN_TEST(test_u_thousandths_is_u_of_v_over_1000);
    RUN_TEST(test_appends_match_the_old_builder);
    RUN_TEST(test_lines_at_the_limit);
    RUN_TEST(test_head_states_for_every_slot);
    RUN_TEST(test_hex32);
    RUN_TEST(test_request_crc_rules);
    RUN_TEST(test_tx_copies_at_every_tail);
    return UNITY_END();
}

SM_DEFINE_TEST_DEFAULTS
