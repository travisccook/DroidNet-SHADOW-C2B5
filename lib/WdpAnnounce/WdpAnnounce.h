// =======================================================================================
//                     WDP Device Announce (WDP-DA) timing
// =======================================================================================
// Decides when SHADOW sends its "@WDP1 {...}" announce line to WCB 1, and whether
// Serial1 has room to take the whole line without blocking. Pure logic with no Arduino
// dependencies: the caller passes in millis(), availableForWrite() and the random
// interval, so the same code runs on the Mega and in the native unit tests.
// Protocol: Wireless_Communication_Board-WCB docs/WDP_DEVICE_ANNOUNCE.md

#ifndef WDP_ANNOUNCE_H
#define WDP_ANNOUNCE_H

#include <stddef.h>
#include <stdint.h>

namespace wdp {

constexpr uint32_t kFirstDelayMinMs = 500;  // first announce after boot
constexpr uint32_t kFirstDelayMaxMs = 3000;
constexpr uint32_t kIntervalMinMs = 25000;  // WCB forgets a device after 90 s of silence
constexpr uint32_t kIntervalMaxMs = 30000;
constexpr size_t kMaxLineLen = 63;          // Serial1 TX ring is 64 bytes; availableForWrite() tops out at 63
constexpr size_t kMaxFwLen = 14;            // ?WDP,DA prints fw in a 14-character column

class AnnounceTimer {
public:
    // Arm the timer: the first announce is due firstDelayMs after now.
    void start(uint32_t now, uint32_t firstDelayMs) {
        last_ = now;
        delay_ = firstDelayMs;
        started_ = true;
    }

    // True when an announce is due and freeTx bytes of TX buffer can hold the whole
    // line. Unsigned subtraction keeps this correct across the millis() rollover.
    // No side effects: when it returns false the caller simply asks again next loop.
    bool ready(uint32_t now, int freeTx, size_t lineLen) const {
        if (!started_ || freeTx < 0) {
            return false;
        }
        if (static_cast<uint32_t>(now - last_) < delay_) {
            return false;
        }
        return static_cast<size_t>(freeTx) >= lineLen;
    }

    // Record a send at now; the next announce is due nextDelayMs later.
    void markSent(uint32_t now, uint32_t nextDelayMs) {
        last_ = now;
        delay_ = nextDelayMs;
    }

private:
    uint32_t last_ = 0;
    uint32_t delay_ = 0;
    bool started_ = false;
};

}  // namespace wdp

#endif  // WDP_ANNOUNCE_H
