// Worst-case timing of the work the mapping can do in one loop() pass, on the emulated
// ATmega2560 (simavr, 16 MHz). Shared by the test_avr_timing_* suites; AVR only.
//
//   pio test -e simavr -f "test_avr_timing*" --without-uploading -v
//
// Every figure is printed as a "TIMING" line:  TIMING <us> <class> <what>
//   T1  a pass with one heavy unit (a request taken in, a line sent, a slot read, one press fired)
//       plus the work every pass does (the EEPROM writer's step and the drain into the UART)
//   T2  one press fired with the EEPROM writer finishing a job in the same pass, or two presses
//   T3  three or four presses in one pass (report only)
//   -   a building block or an older figure kept for comparison, not a whole pass
// The last test of each suite fails when any figure but a building block reaches 1 ms (the limit
// test_avr enforces), and, built with -DSM_TIMING_TARGET, when a T1 figure is over 750 us or a T2
// figure over 900 us. To only print the figures, build with -DSM_TIMING_REPORT_ONLY.
//
// How it times: Timer1 free-running at 2 MHz (prescaler 8, 0.5 us a tick), interrupts off while
// timing, so a figure is the code's own cycles. A real pass also pays for whatever interrupts
// land in it (the millis() tick and the UART interrupts: a few us per ms). Serial (Unity's output)
// is flushed before each measurement.
//
// Worst-case data: the firmware's defaults table holds 35 entries (src/default_mappings.cpp);
// kWorstDefaults has 38, with the two 15-character defaults the measurements read at its end, so
// every default lookup scans the whole table, as an unmapped slot does on the droid.
// test_avr_timing_protocol checks the firmware's table stays at least 2 shorter.
// test_avr_timing_shipped runs the protocol with the firmware's own table.
#pragma once

#if defined(__AVR__)

#include <Arduino.h>
#include <ShadowMap.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

namespace smt {

using namespace sm;

constexpr uint16_t kBudgetUs = 1000;  // test_avr's limit
constexpr uint16_t kT1Us = 750;       // the target for a pass with one heavy unit
constexpr uint16_t kT2Us = 900;       // ... with a press and a finishing EEPROM job, or two presses

// Figure classes (see the top of this file).
enum : uint8_t { kPart = 0, kT1 = 1, kT2 = 2, kT3 = 3 };

// --- Slots and values that make the longest lines -------------------------------------------

// "F CIRCLE RIGHTL", the longest slot name, and "D CIRCLE RIGHT", the longest single press with
// a modifier that can come from the other controller.
constexpr uint8_t kWide = slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT, Tap::LONG);
constexpr uint8_t kWideSingle = slotIndex(Ctrl::D, Mod::CIRCLE, Btn::RIGHT);
constexpr uint16_t kGen = 65534;  // five digits, and DEFAULTS makes it 65535

const char kKey15[] = "zyxwvutsrqponml";
const char kDef15[] = "abcdefghijklmno";
const char kPrefix11[] = ";W12,;Cabcd";
const char kSuffix7[] = "abcdefg";
const char kFw11[] = "abcdef0123+";

#define SMT_FILL(s) {static_cast<uint8_t>(s), "abcdefghijklmn"}
const DefaultMapping kWorstDefaults[] PROGMEM = {
    SMT_FILL(0),  SMT_FILL(1),  SMT_FILL(2),  SMT_FILL(3),  SMT_FILL(4),  SMT_FILL(5),  SMT_FILL(6),
    SMT_FILL(7),  SMT_FILL(8),  SMT_FILL(9),  SMT_FILL(10), SMT_FILL(11), SMT_FILL(12), SMT_FILL(13),
    SMT_FILL(14), SMT_FILL(15), SMT_FILL(16), SMT_FILL(17), SMT_FILL(18), SMT_FILL(19), SMT_FILL(20),
    SMT_FILL(21), SMT_FILL(22), SMT_FILL(23), SMT_FILL(24), SMT_FILL(25), SMT_FILL(26), SMT_FILL(27),
    SMT_FILL(28), SMT_FILL(29), SMT_FILL(30), SMT_FILL(31), SMT_FILL(32), SMT_FILL(33), SMT_FILL(34),
    SMT_FILL(35), {kWideSingle, "abcdefghijklmno"}, {kWide, "abcdefghijklmno"},
};
#undef SMT_FILL
constexpr uint8_t kWorstDefaultCount = sizeof(kWorstDefaults) / sizeof(kWorstDefaults[0]);

// --- Timing ------------------------------------------------------------------------------------

inline void timerBegin() {
    TIMSK1 = 0;
    TCCR1A = 0;
    TCCR1C = 0;
    TCCR1B = _BV(CS11);
}

// Ticks (0.5 us) that f() takes, with interrupts off. 0xFFFF if it ran past the timer's range.
template <class F>
uint16_t ticksOf(F &&f) {
    Serial.flush();  // Unity output finished, so simulated time stays in step with the figures
    const uint8_t sreg = SREG;
    cli();
    TIFR1 = _BV(TOV1);
    const uint16_t t0 = TCNT1;
    f();
    const uint16_t t1 = TCNT1;
    const bool wrapped = (TIFR1 & _BV(TOV1)) != 0;
    SREG = sreg;
    if (wrapped && t1 >= t0)
        return 0xFFFF;
    return static_cast<uint16_t>(t1 - t0);
}

struct Worst {
    uint16_t ticks = 0;
    uint16_t passes = 0;
    void add(uint16_t t) {
        if (t > ticks)
            ticks = t;
        ++passes;
    }
};

struct Entry {
    PGM_P what;
    uint16_t ticks;
    uint8_t cls;
};
constexpr uint8_t kMaxEntries = 80;
static Entry gEntries[kMaxEntries];
static uint8_t gEntryCount = 0;

inline void say(PGM_P fmt, ...) {
    char msg[112];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf_P(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    TEST_MESSAGE(msg);
}

inline char classMark(uint8_t cls) { return cls == kPart ? '-' : static_cast<char>('0' + cls); }

// Prints and keeps one worst case. `what` is a flash string (PSTR); `cls` its class (kT1, kT2,
// kT3, or kPart for a building block); `line` the length of the longest line it sent, without
// framing (0: none or not relevant).
inline void record(PGM_P what, uint16_t ticks, uint8_t cls = kT1, uint8_t line = 0) {
    if (line != 0)
        say(PSTR("TIMING %4u.%u us T%c %S [%u-char line]"), ticks / 2, (ticks & 1) ? 5 : 0, classMark(cls), what,
            line);
    else
        say(PSTR("TIMING %4u.%u us T%c %S"), ticks / 2, (ticks & 1) ? 5 : 0, classMark(cls), what);
    if (gEntryCount < kMaxEntries)
        gEntries[gEntryCount++] = {what, ticks, cls};
    else
        TEST_FAIL_MESSAGE("raise kMaxEntries");
}

// A building block, not a pass: printed for the breakdowns, left out of the budget check.
inline void part(PGM_P what, uint16_t ticks) {
    say(PSTR("PART   %4u.%u us   %S"), ticks / 2, (ticks & 1) ? 5 : 0, what);
}

// The last test of a suite: no pass may reach 1 ms; with SM_TIMING_TARGET, T1 passes must be
// within 750 us and T2 passes within 900 us.
inline void checkBudget() {
    uint8_t over = 0;
    uint8_t missed = 0;
    for (uint8_t i = 0; i < gEntryCount; ++i) {
        const Entry &e = gEntries[i];
        if (e.cls == kPart)
            continue;
        if (e.ticks >= 2u * kBudgetUs) {
            ++over;
            say(PSTR("OVER 1 ms:     %4u.%u us T%c %S"), e.ticks / 2, (e.ticks & 1) ? 5 : 0, classMark(e.cls), e.what);
        } else if ((e.cls == kT1 && e.ticks > 2u * kT1Us) || (e.cls == kT2 && e.ticks > 2u * kT2Us)) {
            ++missed;
            say(PSTR("over target:   %4u.%u us T%c %S"), e.ticks / 2, (e.ticks & 1) ? 5 : 0, classMark(e.cls),
                e.what);
        }
    }
    say(PSTR("%u figures, %u at or over 1 ms, %u over their target (T1 750 us, T2 900 us)"), gEntryCount, over,
        missed);
#ifndef SM_TIMING_REPORT_ONLY
    if (over != 0)
        TEST_FAIL_MESSAGE("a mapping pass reaches 1 ms (OVER lines)");
#ifdef SM_TIMING_TARGET
    if (missed != 0)
        TEST_FAIL_MESSAGE("a mapping pass misses its target (over target lines)");
#endif
#endif
}

// --- Ports -------------------------------------------------------------------------------------

// Drain target that costs nothing (as test_avr's), so a pass's figure is the library's own.
// It only notes where the bytes were; note() reads them afterwards, outside the timing, for the
// longest line.
struct NullUart {
    int availableForWrite() { return 63; }
    size_t write(const uint8_t *data, size_t n) {
        if (chunks < 4) {
            chunk[chunks] = data;
            len[chunks++] = static_cast<uint8_t>(n);
        }
        bytes += n;
        return n;
    }
    // Call after each drain, before anything else is queued.
    void note() {
        for (uint8_t c = 0; c < chunks; ++c) {
            for (uint8_t i = 0; i < len[c]; ++i) {
                if (chunk[c][i] == '\r') {
                    if (run > longest)
                        longest = run;
                    last = run;
                    run = 0;
                    ++lines;
                } else {
                    ++run;
                }
            }
        }
        chunks = 0;
    }
    uint32_t bytes = 0;
    uint16_t lines = 0;
    uint8_t run = 0;
    uint8_t last = 0;     // the last complete line, without its '\r'
    uint8_t longest = 0;  // the longest one
    const uint8_t *chunk[4] = {};
    uint8_t len[4] = {};
    uint8_t chunks = 0;
};

// Results that must not be optimized away (avr-libc declares eeprom_read_byte pure).
static volatile uint8_t gSink8;
static volatile uint16_t gSink16;
static volatile uint32_t gSink32;
static volatile uint8_t gSlotIn = kWide;  // a slot the optimizer can't see through

// Reaches HardwareSerial's ring buffers: puts bytes in Serial1's RX buffer as if they had
// arrived, and throws away what is waiting to go out, so a test can time the real
// HardwareSerial without waiting for 9600 baud.
struct SerialPoke : HardwareSerial {
    static void inject(HardwareSerial &s, const char *text) {
        unsigned char(HardwareSerial::*buf)[SERIAL_RX_BUFFER_SIZE] = &SerialPoke::_rx_buffer;
        volatile rx_buffer_index_t HardwareSerial::*head = &SerialPoke::_rx_buffer_head;
        for (; *text != '\0'; ++text) {
            const rx_buffer_index_t h = s.*head;
            (s.*buf)[h] = static_cast<unsigned char>(*text);
            s.*head = static_cast<rx_buffer_index_t>((h + 1) % SERIAL_RX_BUFFER_SIZE);
        }
    }
    static void clearRx(HardwareSerial &s) {
        volatile rx_buffer_index_t HardwareSerial::*head = &SerialPoke::_rx_buffer_head;
        volatile rx_buffer_index_t HardwareSerial::*tail = &SerialPoke::_rx_buffer_tail;
        s.*tail = s.*head;
    }
    // Serial1's TX ring back to empty (UDRIE1 off), as if the UART had sent it all, and the UART
    // idle (its data register free), so the next drain moves the most it can: 64 bytes, the first
    // straight into UDR1.
    static void dropTx1() {
        volatile tx_buffer_index_t HardwareSerial::*head = &SerialPoke::_tx_buffer_head;
        volatile tx_buffer_index_t HardwareSerial::*tail = &SerialPoke::_tx_buffer_tail;
        const uint8_t sreg = SREG;
        cli();
        UCSR1B = static_cast<uint8_t>(UCSR1B & ~_BV(UDRIE1));
        Serial1.*tail = Serial1.*head;
        SREG = sreg;
        if (bit_is_set(UCSR1B, TXEN1))
            loop_until_bit_is_set(UCSR1A, UDRE1);  // at most one byte time
    }
};

// The ATmega2560's EEPROM through avr-libc, as the firmware's AvrEeprom, except that a write
// keeps it busy for `busyPasses` calls to tick(). simavr finishes a write at once; on the Mega
// it takes 3.4 ms, which spans loop passes. 0 (the default) behaves like AvrEeprom. With
// `corrupt`, every write lands with bit 0 flipped (a worn cell), so a write fails its verify.
class PacedEeprom final : public EepromIo {
  public:
    bool ready() override { return busy_ == 0 && eeprom_is_ready(); }
    uint8_t read(uint16_t addr) override { return eeprom_read_byte(reinterpret_cast<const uint8_t *>(addr)); }
    void readBlock(uint16_t addr, uint8_t *dst, uint8_t n) override {
        eeprom_read_block(dst, reinterpret_cast<const void *>(addr), n);
    }
    void write(uint16_t addr, uint8_t value) override {
        eeprom_write_byte(reinterpret_cast<uint8_t *>(addr), corrupt ? static_cast<uint8_t>(value ^ 1) : value);
        busy_ = busyPasses;
    }
    void tick() {
        if (busy_ != 0)
            --busy_;
    }
    // Busy for the next `passes` ticks, as if a write had just started.
    void holdBusy(uint8_t passes) { busy_ = passes; }
    uint8_t busyPasses = 0;
    bool corrupt = false;

  private:
    uint8_t busy_ = 0;
};

// --- EEPROM contents ---------------------------------------------------------------------------

inline void eeErase() {
    for (uint16_t a = 0; a < MapStore::kFootprint; ++a)
        eeprom_update_byte(reinterpret_cast<uint8_t *>(a), 0xFF);
    eeprom_busy_wait();
}

// A valid header, laid out as MapStore writes it (sm_store.cpp).
inline void eeHeader(uint8_t index, uint16_t seq, uint16_t gen, const char *prefix, const char *suffix) {
    uint8_t h[MapStore::kHeaderSize];
    memset(h, 0, sizeof(h));
    h[0] = 'S';
    h[1] = 'M';
    h[2] = MapStore::kLayoutVersion;
    h[3] = kSingleSlotCount;
    h[4] = static_cast<uint8_t>(seq & 0xFF);
    h[5] = static_cast<uint8_t>(seq >> 8);
    h[6] = static_cast<uint8_t>(gen & 0xFF);
    h[7] = static_cast<uint8_t>(gen >> 8);
    strncpy(reinterpret_cast<char *>(h + 8), prefix, kPrefixMax);
    strncpy(reinterpret_cast<char *>(h + 8 + kPrefixMax + 1), suffix, kSuffixMax);
    uint16_t crc = kCrc16Init;
    for (uint8_t i = 0; i < 30; ++i)
        crc = crc16Update(crc, h[i]);
    h[30] = static_cast<uint8_t>(crc & 0xFF);
    h[31] = static_cast<uint8_t>(crc >> 8);
    eeprom_update_block(h, reinterpret_cast<void *>(static_cast<uint16_t>(index) * MapStore::kHeaderSize), sizeof(h));
}

// A slot record written under `gen`: valid while the header has that generation, stale after.
inline void eeSlot(uint8_t slot, uint16_t gen, const char *key) {
    uint8_t r[MapStore::kSlotSize];
    memset(r, 0, sizeof(r));
    strncpy(reinterpret_cast<char *>(r), key, kKeyMax);
    uint16_t crc = kCrc16Init;
    crc = crc16Update(crc, MapStore::kLayoutVersion);
    crc = crc16Update(crc, static_cast<uint8_t>(gen & 0xFF));
    crc = crc16Update(crc, static_cast<uint8_t>(gen >> 8));
    crc = crc16Update(crc, slot);
    for (uint8_t i = 0; i < kKeyBuf; ++i)
        crc = crc16Update(crc, r[i]);
    r[kKeyBuf] = static_cast<uint8_t>(crc & 0xFF);
    r[kKeyBuf + 1] = static_cast<uint8_t>(crc >> 8);
    const uint16_t addr = static_cast<uint16_t>(2 * MapStore::kHeaderSize + static_cast<uint16_t>(slot) * MapStore::kSlotSize);
    eeprom_update_block(r, reinterpret_cast<void *>(addr), sizeof(r));
}

// The worst state: formatted (gen 65534, 11-character prefix, 7-character suffix), with stale
// 15-character records on kWide and kWideSingle. A stale record passes every check but its CRC,
// so a lookup pays for the record and for the default, and its line says "default", one
// character longer than "eeprom".
inline void eeWorst() {
    eeErase();
    eeHeader(0, 1, kGen, kPrefix11, kSuffix7);
    eeSlot(kWide, kGen - 1, kKey15);
    eeSlot(kWideSingle, kGen - 1, kKey15);
}

}  // namespace smt

#endif  // __AVR__
