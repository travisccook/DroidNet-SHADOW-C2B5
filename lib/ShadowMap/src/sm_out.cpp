#include "sm_out.h"

#include <string.h>

#include "sm_slots.h"

namespace sm {

namespace {

// The high nibble. b >> 4 is a four-step 16-bit shift in avr-gcc 7 (int promotion); a nibble
// swap is one instruction.
inline uint8_t hiNibble(uint8_t b) {
#if defined(__AVR__)
    return static_cast<uint8_t>(__builtin_avr_swap(b) & 0x0F);
#else
    return static_cast<uint8_t>(b >> 4);
#endif
}

inline char hexDigit(uint8_t n) { return static_cast<char>(n < 10 ? '0' + n : 'A' - 10 + n); }

// Powers of ten from 10^9 down to 10: a digit is how many times its power can be subtracted
// (a 32-bit divide is a slow library call on AVR).
const uint32_t kPow10[9] PROGMEM = {1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
                                    10000UL,      1000UL,      100UL,      10UL};

// Writes v's decimal digits, without leading zeros. Returns the count.
uint8_t digits(uint32_t v, char *out) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < 9; ++i) {
        const uint32_t p = pgm_read_dword(&kPow10[i]);
        uint8_t d = 0;
        while (v >= p) {
            v -= p;
            ++d;
        }
        if (d != 0 || n != 0)
            out[n++] = static_cast<char>('0' + d);
    }
    out[n++] = static_cast<char>('0' + static_cast<uint8_t>(v));
    return n;
}

// The CRC32 state after "<head><ctrl> <mod> ", foot then dome, each modifier in Mod order.
#define SM_HEAD_STATES(h)                                                                                             \
    {crc32Text(kCrc32Init, h "F NONE "),  crc32Text(kCrc32Init, h "F L1 "),     crc32Text(kCrc32Init, h "F L2 "),    \
     crc32Text(kCrc32Init, h "F PS "),    crc32Text(kCrc32Init, h "F CIRCLE "), crc32Text(kCrc32Init, h "F CROSS "), \
     crc32Text(kCrc32Init, h "D NONE "),  crc32Text(kCrc32Init, h "D L1 "),     crc32Text(kCrc32Init, h "D L2 "),    \
     crc32Text(kCrc32Init, h "D PS "),    crc32Text(kCrc32Init, h "D CIRCLE "), crc32Text(kCrc32Init, h "D CROSS ")}
constexpr uint32_t kHeadStates[2][12] PROGMEM = {SM_HEAD_STATES("@SM SLOT "), SM_HEAD_STATES("@SM PRESS ")};
#undef SM_HEAD_STATES
const char kSlotHead[] PROGMEM = "@SM SLOT ";
const char kPressHead[] PROGMEM = "@SM PRESS ";

}  // namespace

Out &Out::p(PGM_P src) {
    if (overflow_)
        return *this;
    // Copies up to and including the NUL, or one byte past the room if it doesn't fit.
    const uint8_t room = static_cast<uint8_t>(kLineMax - len_);
    const char *end = static_cast<const char *>(memccpy_P(buf_ + len_, src, '\0', room + 1u));
    if (end == nullptr) {
        buf_[len_] = '\0';
        overflow_ = true;
        return *this;
    }
    len_ = static_cast<uint8_t>(end - buf_ - 1);
    return *this;
}

Out &Out::s(const char *src) {
    if (overflow_)
        return *this;
    const uint8_t room = static_cast<uint8_t>(kLineMax - len_);
    const char *end = static_cast<const char *>(memccpy(buf_ + len_, src, '\0', room + 1u));
    if (end == nullptr) {
        buf_[len_] = '\0';
        overflow_ = true;
        return *this;
    }
    len_ = static_cast<uint8_t>(end - buf_ - 1);
    return *this;
}

Out &Out::u(uint32_t v) {
    char tmp[10];
    const uint8_t n = digits(v, tmp);
    if (reserve(n)) {
        memcpy(buf_ + len_, tmp, n);
        advance(n);
    }
    return *this;
}

Out &Out::uThousandths(uint32_t v) {
    char tmp[7];
    // Down to the thousands: 10^9 ... 10^4, then what's left over 1000 is the last digit.
    uint8_t n = 0;
    for (uint8_t i = 0; i < 6; ++i) {
        const uint32_t p = pgm_read_dword(&kPow10[i]);
        uint8_t d = 0;
        while (v >= p) {
            v -= p;
            ++d;
        }
        if (d != 0 || n != 0)
            tmp[n++] = static_cast<char>('0' + d);
    }
    uint8_t d = 0;
    while (v >= 1000) {
        v -= 1000;
        ++d;
    }
    tmp[n++] = static_cast<char>('0' + d);
    if (reserve(n)) {
        memcpy(buf_ + len_, tmp, n);
        advance(n);
    }
    return *this;
}

Out &Out::hex(const char *src) {
    if (*src == '\0')
        return c('-');
    const size_t n = strlen(src);
    if (!reserve(static_cast<uint16_t>(2 * n)))  // one check for the whole field
        return *this;
    char *d = buf_ + len_;
    for (; *src != '\0'; ++src) {
        const uint8_t b = static_cast<uint8_t>(*src);
        *d++ = hexDigit(hiNibble(b));
        *d++ = hexDigit(static_cast<uint8_t>(b & 0x0F));
    }
    advance(static_cast<uint8_t>(2 * n));
    return *this;
}

Out &Out::hex32(uint32_t v) {
    if (!reserve(8))
        return *this;
    char *d = buf_ + len_;
    for (int8_t shift = 24; shift >= 0; shift -= 8) {
        const uint8_t b = static_cast<uint8_t>(v >> shift);
        *d++ = hexDigit(hiNibble(b));
        *d++ = hexDigit(static_cast<uint8_t>(b & 0x0F));
    }
    advance(8);
    return *this;
}

Out &Out::slot(uint8_t slot) {
    const uint8_t n = slotTextLen(slot);
    if (n == 0) {
        overflow_ = true;  // not a slot
        return *this;
    }
    if (reserve(n)) {
        writeSlotText(buf_ + len_, slot);
        advance(n);
    }
    return *this;
}

Out &Out::head(Head head, uint8_t slot) {
    const bool empty = len_ == 0;  // the states are for a line that starts here
    p(head == Head::Press ? kPressHead : kSlotHead);
    const uint8_t at = len_;
    this->slot(slot);
    if (!overflow_ && empty) {
        const SlotParts parts = splitSlot(slot);
        crc_ = pgm_read_dword(&kHeadStates[head == Head::Press ? 1 : 0][parts.ctrl * kModCount + parts.mod]);
        crcFrom_ = static_cast<uint8_t>(at + 3 + modTextLen(parts.mod));
    }
    return *this;
}

bool Out::finish() {
    if (overflow_)
        return false;
    const uint32_t crc =
        crc32Final(crc32Block(crc_, reinterpret_cast<const uint8_t *>(buf_) + crcFrom_, static_cast<uint8_t>(len_ - crcFrom_)));
    if (!reserve(10))
        return false;
    buf_[len_] = ' ';
    buf_[len_ + 1] = '*';
    advance(2);
    hex32(crc);
    return true;
}

}  // namespace sm
