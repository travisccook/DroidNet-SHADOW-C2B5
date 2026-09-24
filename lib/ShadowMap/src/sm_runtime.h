// What main.cpp's loop() does for the mapping, as one testable piece: button presses to WCB
// commands (with the original once-a-second repeat), @SM requests in, and everything the
// mapping sends out. Serial ports are template parameters so tests can pass fakes.
#pragma once

#include <stdint.h>

#include "sm_line.h"
#include "sm_protocol.h"
#include "sm_resolve.h"
#include "sm_store.h"
#include "sm_tx.h"

namespace sm {

// A combo that fired this pass, for logging.
struct FiredPress {
    uint8_t slot;
    char key[kKeyBuf];  // "" = unmapped
    Source src;
    bool modFromOther;  // the modifier was held on the other controller
    bool queued;        // false only if the command didn't fit in the TX queue (shouldn't happen)
};

class Runtime {
  public:
    static constexpr uint32_t kRepeatMs = 1000;
    // The WCB device announce: the first 0.5-3 s after startAnnounce(), then every 25-30 s,
    // each interval drawn afresh so boards that booted together don't stay in step.
    static constexpr uint32_t kFirstAnnounceMinMs = 500;
    static constexpr uint32_t kFirstAnnounceMaxMs = 3000;
    static constexpr uint32_t kAnnounceMinMs = 25000;
    static constexpr uint32_t kAnnounceMaxMs = 30000;
    // Announce line length (with its \r) minus the firmware version's length:
    // @WDP1 {"type":"Shadow","fw":"<fw>"}\r
    static constexpr uint8_t kAnnounceOverhead = 32;
    static constexpr uint8_t kMaxBytesPerPass = 64;
    static constexpr uint32_t kDeferMaxMs = 250;  // a deferred press older than this is dropped

    // `announceFw` is the firmware version for the WCB device announce ("@WDP1 {...}");
    // nullptr turns the announce off. It starts with startAnnounce().
    Runtime(MapStore &store, Protocol &protocol, TxQueue &tx, const char *announceFw)
        : store_(store), protocol_(protocol), tx_(tx), announceFw_(announceFw) {}

    // Call once at the end of setup(). `seed` varies the announce intervals (any value).
    void startAnnounce(uint32_t nowMs, uint32_t seed);

    // Once per pass, after the controllers have been read: the dome controller, then the foot,
    // as the old code did. A combo fires when pressed and again every second while held; the
    // repeat timer is shared by both controllers. Returns true, filling `fired`, when a combo
    // fired this pass.
    bool buttons(uint32_t nowMs, const PadState &foot, const PadState &dome, FiredPress *fired);

    // Reads @SM requests from `port` without blocking, at most kMaxBytesPerPass bytes a pass.
    // A request that can't be answered yet (reply queue full, EEPROM busy) stays in `reader`
    // for the next pass; bytes arriving meanwhile are read and dropped so the UART's RX buffer
    // can't overflow. The Pi sends one request at a time and resends after a timeout.
    template <class Serial>
    void pump(Serial &port, LineReader &reader, ReplyPort &replies) {
        uint8_t budget = kMaxBytesPerPass;
        while (budget > 0) {
            if (reader.ready()) {
                if (!protocol_.handleLine(reader.line(), replies)) {
                    for (; budget > 0 && port.available() > 0; --budget)
                        reader.discard(static_cast<char>(port.read()));
                    return;
                }
                reader.consume();
            }
            if (port.available() <= 0)
                return;
            --budget;
            reader.feed(static_cast<char>(port.read()));
        }
    }

    // Every pass, faults included: finishes protocol work, advances EEPROM writes, sends the
    // announce when due, and hands queued bytes to the WCB's UART. Pass `mayAnnounce` false
    // while a controller fault is active: a due announce then waits for the fault to clear.
    template <class Serial>
    void service(uint32_t nowMs, Serial &wcb, bool mayAnnounce = true) {
        protocol_.service();
        store_.step();
        if (mayAnnounce)
            announce(nowMs);
        tx_.drain(wcb);
    }

  private:
    bool handlePad(Ctrl ctrl, const PadState &self, const PadState &other, uint32_t nowMs, FiredPress *fired);
    bool fire(uint8_t slot, bool modFromOther, FiredPress *fired);
    void announce(uint32_t nowMs);
    uint32_t between(uint32_t lo, uint32_t hi);  // pseudo-random, lo..hi inclusive

    MapStore &store_;
    Protocol &protocol_;
    TxQueue &tx_;
    const char *announceFw_;

    uint32_t repeatStart_ = 0;
    uint16_t repeatCount_ = 0;
    uint8_t deferredSlot_ = kNoSlot;  // pressed while the EEPROM was mid-write
    bool deferredFromOther_ = false;
    uint32_t deferredAt_ = 0;
    bool announcing_ = false;
    uint32_t nextAnnounce_ = 0;
    uint32_t rng_ = 1;
};

}  // namespace sm
