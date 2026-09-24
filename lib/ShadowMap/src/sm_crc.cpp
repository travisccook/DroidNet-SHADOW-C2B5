#include "sm_crc.h"

#include "sm_pgm.h"

namespace sm {

uint16_t crc16Update(uint16_t crc, uint8_t byte) {
    crc ^= static_cast<uint16_t>(byte) << 8;
    for (uint8_t i = 0; i < 8; ++i)
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    return crc;
}

namespace {

// Nibble-at-a-time table for the reflected IEEE polynomial 0xEDB88320: 64 bytes of flash,
// about four times faster than bit-at-a-time on AVR.
const uint32_t kCrc32Nibble[16] PROGMEM = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL, 0x76DC4190UL, 0x6B6B51F4UL,
    0x4DB26158UL, 0x5005713CUL, 0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL,
};

}  // namespace

uint32_t crc32Update(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    crc = (crc >> 4) ^ pgm_read_dword(&kCrc32Nibble[crc & 0x0F]);
    crc = (crc >> 4) ^ pgm_read_dword(&kCrc32Nibble[crc & 0x0F]);
    return crc;
}

uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = kCrc32Init;
    for (size_t i = 0; i < len; ++i)
        crc = crc32Update(crc, data[i]);
    return crc32Final(crc);
}

}  // namespace sm
