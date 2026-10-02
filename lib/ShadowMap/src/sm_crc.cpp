#include "sm_crc.h"

#include "sm_pgm.h"

namespace sm {

namespace {

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

}  // namespace

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
