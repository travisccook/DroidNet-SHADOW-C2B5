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

    // While a button command waits for room (sm::Runtime), replies, events and the announce stay
    // out of the queue so the drain frees room for it. pushCommand() ignores the hold.
    void holdForCommand(bool on) { held_ = on; }
    bool heldForCommand() const { return held_; }

    // Queues the concatenation of `parts` (NUL-terminated strings, at most kMaxParts) followed
    // by '\r', or nothing if it doesn't all fit.
    static constexpr uint8_t kMaxParts = 4;
    bool pushLine(const char *const *parts, uint8_t count);
    // The same for two parts of known length (`b` may be nullptr when `nb` is 0).
    bool pushLine2(const char *a, uint8_t na, const char *b, uint8_t nb);
    // The same for `count` parts of known length.
    bool pushParts(const char *const *parts, const uint8_t *lens, uint8_t count);

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
            head_ = static_cast<uint8_t>(head_ + chunk);
            if (head_ == kCapacity)
                head_ = 0;
            used_ = static_cast<uint8_t>(used_ - chunk);
        }
    }

  private:
    void put(char c);
    void putBytes(const char *s, uint8_t n);  // n bytes that fit, in at most two copies

    char buf_[kCapacity] = {};
    uint8_t head_ = 0;  // next byte to send
    uint8_t tail_ = 0;  // next free byte
    uint8_t used_ = 0;
    bool held_ = false;
};

// Room always kept free for one button command (prefix + key + suffix + '\r'), so replies,
// events and announcements can never crowd out a button press.
constexpr uint8_t kCommandHeadroom = kPrefixMax + kKeyMax + kSuffixMax + 1;

// Queues prefix + key + suffix + '\r'. Ignores the headroom: this is what it is reserved for.
bool pushCommand(TxQueue &tx, const char *prefix, const char *key, const char *suffix);

}  // namespace sm
