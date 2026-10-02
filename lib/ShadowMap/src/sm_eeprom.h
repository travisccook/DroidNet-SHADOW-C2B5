// Byte-level EEPROM access. The store only calls read()/write() when ready() is true, so a
// write never waits on the previous one: each write starts in the background and the main
// loop keeps running while it completes (about 3.3 ms per byte on the ATmega2560).
#pragma once

#include <stdint.h>

namespace sm {

class EepromIo {
  public:
    virtual bool ready() = 0;
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t value) = 0;
    // `n` bytes from `addr`, as `n` read() calls would return them; only called when ready(). The
    // default is those calls, so a fake that counts or checks reads sees each one.
    virtual void readBlock(uint16_t addr, uint8_t *dst, uint8_t n) {
        for (uint8_t i = 0; i < n; ++i)
            dst[i] = read(static_cast<uint16_t>(addr + i));
    }

  protected:
    ~EepromIo() = default;
};

}  // namespace sm

#if defined(__AVR__)
#include <avr/eeprom.h>

namespace sm {

class AvrEeprom final : public EepromIo {
  public:
    bool ready() override { return eeprom_is_ready(); }
    uint8_t read(uint16_t addr) override {
        return eeprom_read_byte(reinterpret_cast<const uint8_t *>(addr));
    }
    // avr-libc: void eeprom_read_block(void *dst, const void *src, size_t n).
    void readBlock(uint16_t addr, uint8_t *dst, uint8_t n) override {
        eeprom_read_block(dst, reinterpret_cast<const void *>(addr), n);
    }
    // eeprom_write_byte starts the write and returns; it only spins if the EEPROM is still
    // busy, and the store never calls it then.
    void write(uint16_t addr, uint8_t value) override {
        eeprom_write_byte(reinterpret_cast<uint8_t *>(addr), value);
    }
};

}  // namespace sm
#endif
