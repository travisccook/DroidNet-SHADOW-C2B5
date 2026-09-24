// CRC16 (CCITT-FALSE) guards EEPROM records; CRC32 (IEEE, same as zlib and the WCB) guards
// protocol lines, so the Pi side can use zlib.crc32 / binascii.crc32 directly.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sm {

constexpr uint16_t kCrc16Init = 0xFFFF;
uint16_t crc16Update(uint16_t crc, uint8_t byte);

constexpr uint32_t kCrc32Init = 0xFFFFFFFFUL;
uint32_t crc32Update(uint32_t crc, uint8_t byte);
inline uint32_t crc32Final(uint32_t crc) { return ~crc; }
uint32_t crc32(const uint8_t *data, size_t len);

}  // namespace sm
