// Test doubles shared by the ShadowMap tests. No STL, so they also run on the emulated
// ATmega2560 (8 KB of SRAM: keep buffers small).
#pragma once

#include <ShadowMap.h>
#include <stdio.h>
#include <string.h>

// EEPROM that behaves like the ATmega2560's: each write keeps it busy for `writeTicks`
// calls to tick(). Touching it while busy counts as a violation (the store must never do
// that). `budget` simulates a power cut: once it reaches zero, writes stop landing, and
// with `cutGarbage` the last write lands as garbage, like a write cut off by a brown-out.
//
// The headers and single-press slots are backed byte for byte (`mem`). The double-tap slots
// after them read as erased until written; a written record takes one of kPoolRecords, so a
// test can use a handful of double-tap slots without the whole bank in SRAM (the emulated Mega
// has 8 KB). Writing more than that counts as a violation.
class FakeEeprom final : public sm::EepromIo {
  public:
    static constexpr uint16_t kSize = sm::MapStore::kEepromUsed;  // the mapping and the controller-address records
    static constexpr uint16_t kFlat = 2 * sm::MapStore::kHeaderSize + sm::kSingleSlotCount * sm::MapStore::kSlotSize;
    static constexpr uint8_t kPoolRecords = 6;

    FakeEeprom() { erase(); }

    // Back to a fresh, erased EEPROM, in place: a temporary FakeEeprom would put another
    // copy of `mem` on the stack, which the emulated Mega can't spare.
    void reset() {
        erase();
        busy = 0;
        writeTicks = 3;
        budget = -1;
        cutGarbage = false;
        stuckAddr = -1;
        writes = 0;
        reads = 0;
        violations = 0;
    }

    bool ready() override { return busy == 0; }
    uint8_t read(uint16_t addr) override {
        if (busy != 0 || addr >= kSize) {
            ++violations;
            return 0xFF;
        }
        ++reads;
        uint8_t v = peek(addr);
        if (static_cast<int32_t>(addr) == stuckAddr)
            v ^= 0x01;
        return v;
    }
    void write(uint16_t addr, uint8_t value) override {
        if (busy != 0 || addr >= kSize) {
            ++violations;
            return;
        }
        ++writes;
        if (budget == 0)
            return;  // no power: nothing lands
        uint8_t *cell = cellFor(addr);
        if (cell == nullptr) {
            ++violations;  // more double-tap records than the pool holds: raise kPoolRecords
            return;
        }
        if (budget > 0) {
            --budget;
            if (budget == 0 && cutGarbage) {
                *cell = static_cast<uint8_t>(value ^ 0xA5);
                return;
            }
        }
        *cell = value;
        busy = writeTicks;
    }
    void tick() {
        if (busy != 0)
            --busy;
    }
    // Power comes back: the EEPROM is idle and writes land again.
    void powerOn() {
        busy = 0;
        budget = -1;
    }

    // What's stored at `addr`, without the checks a read makes.
    uint8_t peek(uint16_t addr) const {
        if (addr < kFlat)
            return mem[addr];
        if (addr >= sm::MapStore::kFootprint)
            return macMem[addr - sm::MapStore::kFootprint];
        const Record *r = find(recordOf(addr));
        return r != nullptr ? r->bytes[(addr - kFlat) % sm::MapStore::kSlotSize] : 0xFF;
    }
    // Stores a byte directly, for tests that damage a record.
    void poke(uint16_t addr, uint8_t value) {
        uint8_t *cell = cellFor(addr);
        if (cell != nullptr)
            *cell = value;
    }
    uint8_t doubleRecordsUsed() const {
        uint8_t n = 0;
        for (const Record &r : pool)
            n = static_cast<uint8_t>(n + (r.record != kFree ? 1 : 0));
        return n;
    }

    uint8_t mem[kFlat];  // headers and single-press slots
    uint8_t macMem[sm::MapStore::kMacFootprint];  // the controller-address records
    uint8_t busy = 0;
    uint8_t writeTicks = 3;
    int32_t budget = -1;  // writes left before the power cut; -1 = no cut
    bool cutGarbage = false;
    int32_t stuckAddr = -1;  // reads of this address come back with bit 0 flipped
    uint32_t writes = 0;
    uint32_t reads = 0;
    uint32_t violations = 0;

  private:
    static constexpr uint8_t kFree = 0xFF;
    struct Record {
        uint8_t record;  // which double-tap record (0-71), or kFree
        uint8_t bytes[sm::MapStore::kSlotSize];
    };

    static uint8_t recordOf(uint16_t addr) { return static_cast<uint8_t>((addr - kFlat) / sm::MapStore::kSlotSize); }
    const Record *find(uint8_t record) const {
        for (const Record &r : pool) {
            if (r.record == record)
                return &r;
        }
        return nullptr;
    }
    uint8_t *cellFor(uint16_t addr) {
        if (addr < kFlat)
            return &mem[addr];
        if (addr >= sm::MapStore::kFootprint)
            return &macMem[addr - sm::MapStore::kFootprint];
        const uint8_t record = recordOf(addr);
        Record *r = const_cast<Record *>(find(record));
        for (uint8_t i = 0; r == nullptr && i < kPoolRecords; ++i) {
            if (pool[i].record == kFree) {
                r = &pool[i];
                r->record = record;
                memset(r->bytes, 0xFF, sizeof(r->bytes));
            }
        }
        return r != nullptr ? &r->bytes[(addr - kFlat) % sm::MapStore::kSlotSize] : nullptr;
    }
    void erase() {
        memset(mem, 0xFF, sizeof(mem));
        memset(macMem, 0xFF, sizeof(macMem));
        for (Record &r : pool)
            r.record = kFree;
    }

    Record pool[kPoolRecords];
};

// Remembers the last few lines seen.
class LineLog {
  public:
    static constexpr uint8_t kLines = 4;
    static constexpr uint8_t kWidth = 96;

    void add(const char *line, size_t len) {
        char *dst = lines_[next_];
        const size_t n = len < kWidth - 1 ? len : kWidth - 1;
        memcpy(dst, line, n);
        dst[n] = '\0';
        next_ = static_cast<uint8_t>((next_ + 1) % kLines);
        ++count;
    }
    // back = 0 is the most recent line.
    const char *last(uint8_t back = 0) const {
        if (back >= count || back >= kLines)
            return "";
        return lines_[(next_ + kLines - 1 - back) % kLines];
    }
    void clear() {
        count = 0;
        next_ = 0;
    }

    uint32_t count = 0;

  private:
    char lines_[kLines][kWidth] = {};
    uint8_t next_ = 0;
};

// Plays the part of SHADOW's WCB: takes what SHADOW writes to Serial1, checks it against
// the WCB's rules, and sorts it into what the Pi would see on the WCB's USB port (";S0"
// replies, route stripped) and commands the WCB would run.
class WcbSim {
  public:
    // HardwareSerial surface used by TxQueue::drain.
    // Like HardwareSerial: room shrinks as bytes are queued; refill() models the UART
    // sending them.
    int availableForWrite() { return room; }
    size_t write(const uint8_t *data, size_t n) {
        if (static_cast<int>(n) > room)
            ++overruns;
        room -= static_cast<int>(n);
        for (size_t i = 0; i < n; ++i)
            byte(static_cast<char>(data[i]));
        return n;
    }
    void refill(int bytes = 63) {
        room += bytes;
        if (room > 63)
            room = 63;
    }

    void byte(char c) {
        if (c == '\r') {
            buf_[len_] = '\0';
            line(buf_, len_);
            len_ = 0;
            return;
        }
        if (len_ < sizeof(buf_) - 1)
            buf_[len_++] = c;
        else
            ++overlong;
    }

    int room = 63;  // HardwareSerial's TX buffer when empty
    // Set by tests that fire raw commands: a line that isn't a reply or an announce then goes to
    // `commands` exactly as it is, bare or with '^' or ";T", because SHADOW sends raw commands as
    // typed. Replies are still checked.
    bool rawOk = false;
    LineLog usb;       // replies as the Pi sees them
    LineLog commands;  // everything else the WCB would act on
    LineLog announces;
    uint32_t violations = 0;  // bare lines, '^', ";T", stray line feeds
    uint32_t overruns = 0;
    uint32_t overlong = 0;
    uint8_t lastRoute = 0xFF;

  private:
    static bool isReply(const char *s) {
        if (strncmp(s, ";S0", 3) == 0)
            return true;
        if (s[0] != ';' || s[1] != 'W')
            return false;
        const char *p = s + 2;
        while (*p >= '0' && *p <= '9')
            ++p;
        if (*p == ',')
            ++p;
        return strncmp(p, ";S0", 3) == 0;
    }

    void line(const char *s, size_t n) {
        if (rawOk && strncmp(s, "@WDP", 4) != 0 && !isReply(s)) {
            commands.add(s, n);
            return;
        }
        if (strchr(s, '^') != nullptr || strchr(s, '\n') != nullptr)
            ++violations;
        if (strncmp(s, "@WDP", 4) == 0) {
            announces.add(s, n);
            return;
        }
        if (s[0] != ';') {
            ++violations;  // a bare line would be broadcast to every board
            return;
        }
        if (strstr(s, ";T") != nullptr || strstr(s, ";t") != nullptr)
            ++violations;  // the WCB would treat it as a timer command
        if (strncmp(s, ";S0", 3) == 0) {
            lastRoute = 0;
            usb.add(s + 3, n - 3);
            return;
        }
        if (s[1] == 'W') {
            // ;W<d>;S0<text> or ;W<dd>,;S0<text>
            const char *p = s + 2;
            uint8_t route = 0;
            while (*p >= '0' && *p <= '9')
                route = static_cast<uint8_t>(route * 10 + (*p++ - '0'));
            if (*p == ',')
                ++p;
            if (strncmp(p, ";S0", 3) == 0) {
                lastRoute = route;
                usb.add(p + 3, strlen(p + 3));
                return;
            }
        }
        commands.add(s, n);
    }

    char buf_[128] = {};
    size_t len_ = 0;
};

// A serial port with nothing to read (the USB link in suites that only drive the WCB).
struct NoSerial {
    int available() { return 0; }
    int read() { return -1; }
};

// A reply port nothing is sent to (paired with NoSerial).
class NullReplyPort final : public sm::ReplyPort {
  public:
    bool canSend(uint8_t) override { return false; }
    void send(const char *, uint8_t) override {}
    bool requiresCrc() const override { return false; }
};

// A reply port that records lines directly (the USB side of SHADOW).
class CapturePort final : public sm::ReplyPort {
  public:
    bool canSend(uint8_t len) override { return open && len <= sm::Protocol::kMaxLine; }
    void send(const char *line, uint8_t len) override {
        TEST_ASSERT_EQUAL_UINT_MESSAGE(strlen(line), len, "a reply's length");
        log.add(line, len);
    }
    bool requiresCrc() const override { return crcRequired; }

    bool open = true;
    bool crcRequired = false;
    LineLog log;
};

// "<body> *<CRC32>" into out.
inline const char *withCrc(const char *body, char *out, size_t cap) {
    const uint32_t crc = sm::crc32(reinterpret_cast<const uint8_t *>(body), strlen(body));
    snprintf(out, cap, "%s *%08lX", body, static_cast<unsigned long>(crc));
    return out;
}

// Checks a reply's " *<CRC32>" and copies the body (without it) to out. False if missing/bad.
inline bool stripCrc(const char *line, char *out, size_t cap) {
    const char *star = strstr(line, " *");
    if (star == nullptr || strlen(star + 2) != 8)
        return false;
    const size_t bodyLen = static_cast<size_t>(star - line);
    if (bodyLen + 1 > cap)
        return false;
    char want[9];
    snprintf(want, sizeof(want), "%08lX",
             static_cast<unsigned long>(sm::crc32(reinterpret_cast<const uint8_t *>(line), bodyLen)));
    if (strcmp(want, star + 2) != 0)
        return false;
    memcpy(out, line, bodyLen);
    out[bodyLen] = '\0';
    return true;
}

// Three defaults used by the store and protocol tests.
extern const sm::DefaultMapping kTestDefaults[] PROGMEM;
constexpr uint8_t kTestDefaultCount = 3;
constexpr uint8_t kSlotFootL1Up = sm::slotIndex(sm::Ctrl::F, sm::Mod::L1, sm::Btn::UP);
constexpr uint8_t kSlotFootNoneDown = sm::slotIndex(sm::Ctrl::F, sm::Mod::NONE, sm::Btn::DOWN);
constexpr uint8_t kSlotDomeCrossLeft = sm::slotIndex(sm::Ctrl::D, sm::Mod::CROSS, sm::Btn::LEFT);
constexpr uint8_t kSlotDomeL2Right = sm::slotIndex(sm::Ctrl::D, sm::Mod::L2, sm::Btn::RIGHT);

#define SM_DEFINE_TEST_DEFAULTS                                                                \
    const sm::DefaultMapping kTestDefaults[] PROGMEM = {                                       \
        {kSlotFootL1Up, "extremehappysou"}, {kSlotFootNoneDown, "angrysound"}, {kSlotDomeCrossLeft, "scanning"}, \
    };
