// Outgoing byte queue for the WCB serial port. Whole lines are queued atomically and drained
// only as fast as the UART's transmit buffer has room, so nothing ever blocks and a button
// command can never land in the middle of a protocol reply.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sm_store.h"

namespace sm {

class TxQueue {
  public:
    static constexpr uint8_t kCapacity = 160;

    uint8_t used() const { return used_; }
    uint8_t freeSpace() const { return static_cast<uint8_t>(kCapacity - used_); }

    // Queues the concatenation of `parts` (NUL-terminated strings) followed by '\r', or
    // nothing if it doesn't all fit.
    bool pushLine(const char *const *parts, uint8_t count);

    // Writes as much as `out.availableForWrite()` allows. `Out` is HardwareSerial on the
    // droid and a fake in tests.
    template <class Out>
    void drain(Out &out) {
        while (used_ > 0) {
            const int room = out.availableForWrite();
            if (room <= 0)
                return;
            uint8_t chunk = static_cast<uint8_t>(kCapacity - head_);
            if (chunk > used_)
                chunk = used_;
            if (chunk > room)
                chunk = static_cast<uint8_t>(room);
            out.write(reinterpret_cast<const uint8_t *>(buf_ + head_), chunk);
            head_ = static_cast<uint8_t>((head_ + chunk) % kCapacity);
            used_ = static_cast<uint8_t>(used_ - chunk);
        }
    }

  private:
    void put(char c);

    char buf_[kCapacity] = {};
    uint8_t head_ = 0;
    uint8_t used_ = 0;
};

// Room always kept free for one button command (prefix + key + suffix + '\r'), so replies,
// events and announcements can never crowd out a button press.
constexpr uint8_t kCommandHeadroom = kPrefixMax + kKeyMax + kSuffixMax + 1;

// Queues prefix + key + suffix + '\r'. Ignores the headroom: this is what it is reserved for.
bool pushCommand(TxQueue &tx, const char *prefix, const char *key, const char *suffix);

}  // namespace sm
