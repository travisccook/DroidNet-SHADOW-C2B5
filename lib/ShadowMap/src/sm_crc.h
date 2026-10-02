// CRC16 (CCITT-FALSE) guards EEPROM records; CRC32 (IEEE, same as zlib and the WCB) guards
// protocol lines, so the Pi side can use zlib.crc32 / binascii.crc32 directly.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sm {

constexpr uint16_t kCrc16Init = 0xFFFF;
uint16_t crc16Update(uint16_t crc, uint8_t byte);
// The CRC16 state after `n` more bytes.
uint16_t crc16Block(uint16_t crc, const uint8_t *data, uint8_t n);

constexpr uint32_t kCrc32Init = 0xFFFFFFFFUL;
uint32_t crc32Update(uint32_t crc, uint8_t byte);
// The CRC32 state (not yet finalized) after `n` more bytes.
uint32_t crc32Block(uint32_t crc, const uint8_t *data, uint8_t n);
inline uint32_t crc32Final(uint32_t crc) { return ~crc; }
uint32_t crc32(const uint8_t *data, size_t len);

// Compile-time forms, the definitions the tables are built from (C++11 constexpr, so each is one
// return). crc32Text/crc16Text give the state after a literal, such as a line's constant head.
constexpr uint32_t crc32Bits(uint32_t c, uint8_t k) {
    return k == 0 ? c : crc32Bits((c & 1u) != 0 ? ((c >> 1) ^ 0xEDB88320UL) : (c >> 1), static_cast<uint8_t>(k - 1));
}
constexpr uint32_t crc32Text(uint32_t state, const char *s) {
    return *s == '\0' ? state : crc32Text(crc32Bits(state ^ static_cast<uint8_t>(*s), 8), s + 1);
}
constexpr uint16_t crc16Bits(uint16_t c, uint8_t k) {
    return k == 0 ? c
                  : crc16Bits((c & 0x8000u) != 0 ? static_cast<uint16_t>(static_cast<uint16_t>(c << 1) ^ 0x1021u)
                                                  : static_cast<uint16_t>(c << 1),
                              static_cast<uint8_t>(k - 1));
}
constexpr uint16_t crc16Text(uint16_t state, const char *s) {
    return *s == '\0'
               ? state
               : crc16Text(crc16Bits(static_cast<uint16_t>(state ^ (static_cast<uint16_t>(static_cast<uint8_t>(*s)) << 8)), 8),
                           s + 1);
}

namespace detail {
// The byte-plane tables (sm_crc.cpp), each 256-aligned in the first 64 KB of flash, where the AVR
// block code reaches them with LPM. Declared here for test_avr_crc_asm.
extern const uint8_t kCrc32Planes[4][256];
extern const uint8_t kCrc16Planes[2][256];
}  // namespace detail

static_assert(crc32Text(kCrc32Init, "123456789") == static_cast<uint32_t>(~0xCBF43926UL), "CRC32 check value");
static_assert(crc16Text(kCrc16Init, "123456789") == 0x29B1, "CRC16 (CCITT-FALSE) check value");

}  // namespace sm
