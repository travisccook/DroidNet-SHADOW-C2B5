// Builds one @SM line in a fixed buffer for the protocol (sm_protocol.cpp). Every append is all
// or nothing; once one doesn't fit, finish() fails, so a line is sent whole or not at all.
#pragma once

#include <stdint.h>

#include "sm_crc.h"
#include "sm_pgm.h"

namespace sm {

// The longest line, before port framing: Protocol::kMaxLine.
constexpr uint8_t kLineMax = 80;

class Out {
  public:
    Out() { buf_[0] = '\0'; }

    Out &p(PGM_P s);           // a flash string
    Out &s(const char *s);     // a RAM string
    Out &c(char ch) {
        if (reserve(1)) {
            buf_[len_] = ch;
            advance(1);
        }
        return *this;
    }
    Out &u(uint32_t v);             // decimal
    Out &uThousandths(uint32_t v);  // v / 1000 in decimal ("0" below 1000)
    Out &hex(const char *s);        // each byte as two hex digits; "" as "-"
    Out &hex32(uint32_t v);         // eight hex digits
    Out &key(const char *k) { return *k == '\0' ? c('-') : s(k); }
    Out &slot(uint8_t slot);  // "F L1 UP"
    // A line's start, on an empty Out: "@SM SLOT " or "@SM PRESS " and the slot's name. The CRC32
    // state after "<head><ctrl> <mod> " comes from a table the compiler works out, so finish()
    // runs the CRC over the rest only.
    enum class Head : uint8_t { Slot, Press };
    Out &head(Head head, uint8_t slot);
    // Appends " *<CRC32 of everything before>"; false if the line didn't fit.
    bool finish();

    const char *str() const { return buf_; }
    uint8_t len() const { return len_; }

  private:
    bool reserve(uint16_t n) {
        if (overflow_ || len_ + n > kLineMax) {
            overflow_ = true;
            return false;
        }
        return true;
    }
    void advance(uint8_t n) {
        len_ = static_cast<uint8_t>(len_ + n);
        buf_[len_] = '\0';
    }

    char buf_[kLineMax + 1];
    uint8_t len_ = 0;
    bool overflow_ = false;
    uint8_t crcFrom_ = 0;         // buf_[0, crcFrom_) is already in crc_
    uint32_t crc_ = kCrc32Init;
};

}  // namespace sm
