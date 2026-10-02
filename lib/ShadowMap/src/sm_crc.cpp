#include "sm_crc.h"

#include "sm_pgm.h"

namespace sm {

namespace detail {

// Byte-at-a-time tables split into byte planes, each 256-aligned in flash, so a lookup is
// "high byte = the plane's page, low byte = the index": no multiply, no 16-bit add. The compiler
// builds them from crc32Bits/crc16Bits (sm_crc.h); test_crc_slots checks them against the
// bitwise forms and the literal table they replaced.
#define SM_R4(f, i) f(i), f(i + 1), f(i + 2), f(i + 3)
#define SM_R16(f, i) SM_R4(f, i), SM_R4(f, i + 4), SM_R4(f, i + 8), SM_R4(f, i + 12)
#define SM_R64(f, i) SM_R16(f, i), SM_R16(f, i + 16), SM_R16(f, i + 32), SM_R16(f, i + 48)
#define SM_R256(f) SM_R64(f, 0), SM_R64(f, 64), SM_R64(f, 128), SM_R64(f, 192)
#define SM_C32(i, k) static_cast<uint8_t>(crc32Bits(static_cast<uint32_t>(i), 8) >> (8 * (k)))
#define SM_C32B0(i) SM_C32(i, 0)
#define SM_C32B1(i) SM_C32(i, 1)
#define SM_C32B2(i) SM_C32(i, 2)
#define SM_C32B3(i) SM_C32(i, 3)
#define SM_C16(i) crc16Bits(static_cast<uint16_t>(static_cast<uint16_t>(i) << 8), 8)
#define SM_C16HI(i) static_cast<uint8_t>(SM_C16(i) >> 8)
#define SM_C16LO(i) static_cast<uint8_t>(SM_C16(i))

// kCrc32Planes[k][i]: byte k of the CRC32 table entry for i (reflected 0xEDB88320).
alignas(256) const uint8_t kCrc32Planes[4][256] PROGMEM = {
    {SM_R256(SM_C32B0)},
    {SM_R256(SM_C32B1)},
    {SM_R256(SM_C32B2)},
    {SM_R256(SM_C32B3)},
};

// kCrc16Planes[0][i] / [1][i]: the high / low byte of the CRC16 table entry for i (0x1021).
alignas(256) const uint8_t kCrc16Planes[2][256] PROGMEM = {
    {SM_R256(SM_C16HI)},
    {SM_R256(SM_C16LO)},
};

#undef SM_R4
#undef SM_R16
#undef SM_R64
#undef SM_R256
#undef SM_C32
#undef SM_C32B0
#undef SM_C32B1
#undef SM_C32B2
#undef SM_C32B3
#undef SM_C16
#undef SM_C16HI
#undef SM_C16LO

}  // namespace detail

using detail::kCrc16Planes;
using detail::kCrc32Planes;

uint16_t crc16Update(uint16_t crc, uint8_t byte) {
    const uint8_t i = static_cast<uint8_t>((crc >> 8) ^ byte);
    const uint8_t hi = static_cast<uint8_t>(static_cast<uint8_t>(crc) ^ pgm_read_byte(&kCrc16Planes[0][i]));
    return static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint16_t>(hi) << 8) | pgm_read_byte(&kCrc16Planes[1][i]));
}

uint32_t crc32Update(uint32_t crc, uint8_t byte) {
    const uint8_t i = static_cast<uint8_t>(crc ^ byte);
    const uint32_t t = static_cast<uint32_t>(pgm_read_byte(&kCrc32Planes[0][i])) |
                       (static_cast<uint32_t>(pgm_read_byte(&kCrc32Planes[1][i])) << 8) |
                       (static_cast<uint32_t>(pgm_read_byte(&kCrc32Planes[2][i])) << 16) |
                       (static_cast<uint32_t>(pgm_read_byte(&kCrc32Planes[3][i])) << 24);
    return (crc >> 8) ^ t;
}

#if defined(__AVR__)

// The block loops in assembly: a lookup is one LPM per plane, the plane's page in r31 and the
// index in r30. The data pointer is in X; only r30/r31 are clobbered. The tables must sit in the
// first 64 KB of flash (LPM, not ELPM): PROGMEM data is placed right after the vectors, and
// test_avr_crc_asm checks the planes' addresses.

// 15 cycles a byte.
uint16_t crc16Block(uint16_t crc, const uint8_t *data, uint8_t n) {
    if (n == 0)
        return crc;
    uint8_t lo = static_cast<uint8_t>(crc);
    uint8_t hi = static_cast<uint8_t>(crc >> 8);
    const uint8_t page = static_cast<uint8_t>(reinterpret_cast<uint16_t>(&kCrc16Planes[0][0]) >> 8);
    __asm__ volatile(
        "1:                  \n\t"
        "ld   r30, %a[p]+    \n\t"  // index = high byte ^ data
        "eor  r30, %[hi]     \n\t"
        "mov  r31, %[pg]     \n\t"
        "lpm  %[hi], Z       \n\t"  // high = table high byte ^ low byte
        "eor  %[hi], %[lo]   \n\t"
        "inc  r31            \n\t"
        "lpm  %[lo], Z       \n\t"  // low = table low byte
        "dec  %[n]           \n\t"
        "brne 1b             \n\t"
        : [lo] "+r"(lo), [hi] "+r"(hi), [p] "+x"(data), [n] "+r"(n)
        : [pg] "r"(page)
        : "r30", "r31", "memory");
    return static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint16_t>(hi) << 8) | lo);
}

// One table step: index = low byte ^ data; c0 = t0 ^ c1, c1 = t1 ^ c2, c2 = t2 ^ c3, c3 = t3.
#define SM_CRC32_STEP                \
    "ld   r30, %a[p]+    \n\t"       \
    "eor  r30, %[c0]     \n\t"       \
    "mov  r31, %[pg]     \n\t"       \
    "lpm  %[c0], Z       \n\t"       \
    "inc  r31            \n\t"       \
    "eor  %[c0], %[c1]   \n\t"       \
    "lpm  %[c1], Z       \n\t"       \
    "inc  r31            \n\t"       \
    "eor  %[c1], %[c2]   \n\t"       \
    "lpm  %[c2], Z       \n\t"       \
    "inc  r31            \n\t"       \
    "eor  %[c2], %[c3]   \n\t"       \
    "lpm  %[c3], Z       \n\t"

// 23.5 cycles a byte: two bytes a loop, an odd byte first.
uint32_t crc32Block(uint32_t crc, const uint8_t *data, uint8_t n) {
    if (n == 0)
        return crc;
    uint8_t c0 = static_cast<uint8_t>(crc);
    uint8_t c1 = static_cast<uint8_t>(crc >> 8);
    uint8_t c2 = static_cast<uint8_t>(crc >> 16);
    uint8_t c3 = static_cast<uint8_t>(crc >> 24);
    const uint8_t page = static_cast<uint8_t>(reinterpret_cast<uint16_t>(&kCrc32Planes[0][0]) >> 8);
    __asm__ volatile(
        "    sbrs %[n], 0         \n\t"  // an odd count: one byte first
        "    rjmp 2f              \n\t"
        SM_CRC32_STEP
        "2:  lsr  %[n]            \n\t"  // then pairs
        "    breq 3f              \n\t"
        "1:                       \n\t"
        SM_CRC32_STEP
        SM_CRC32_STEP
        "    dec  %[n]            \n\t"
        "    brne 1b              \n\t"
        "3:                       \n\t"
        : [c0] "+r"(c0), [c1] "+r"(c1), [c2] "+r"(c2), [c3] "+r"(c3), [p] "+x"(data), [n] "+r"(n)
        : [pg] "r"(page)
        : "r30", "r31", "memory");
    return static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 8) | (static_cast<uint32_t>(c2) << 16) |
           (static_cast<uint32_t>(c3) << 24);
}
#undef SM_CRC32_STEP

#else

uint16_t crc16Block(uint16_t crc, const uint8_t *data, uint8_t n) {
    for (; n != 0; --n)
        crc = crc16Update(crc, *data++);
    return crc;
}

uint32_t crc32Block(uint32_t crc, const uint8_t *data, uint8_t n) {
    for (; n != 0; --n)
        crc = crc32Update(crc, *data++);
    return crc;
}

#endif

uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = kCrc32Init;
    while (len > 255) {
        crc = crc32Block(crc, data, 255);
        data += 255;
        len -= 255;
    }
    return crc32Final(crc32Block(crc, data, static_cast<uint8_t>(len)));
}

}  // namespace sm
