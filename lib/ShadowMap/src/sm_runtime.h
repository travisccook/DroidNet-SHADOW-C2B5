// What main.cpp's loop() does for the mapping, as one testable piece: button presses to WCB
// commands (with the original once-a-second repeat), @SM requests in, and everything the
// mapping sends out. Serial ports are template parameters so tests can pass fakes.
#pragma once

#include <stdint.h>

#include "sm_battery.h"
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
    bool raw;           // key is a raw command, sent as typed
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
    static constexpr uint8_t kFireRxBytes = 16;  // both ports together, in a pass that fired a press
    static constexpr uint8_t kRxHeavy = 16;      // a pass that read this many bytes takes no request in
    static constexpr uint32_t kDeferMaxMs = 250;  // a deferred press older than this is dropped
    static constexpr uint16_t kDoubleTapMs = 350;  // default window from the first press to the second
    static constexpr uint16_t kLongPressMs = 600;  // default hold for a long press

    // `announceFw` is the firmware version for the WCB device announce ("@WDP1 {...}");
    // nullptr turns the announce off. It starts with startAnnounce(). `doubleTapMs` is how
    // soon after the first press a second one has to come to count as a double tap;
    // `longPressMs` is how long a combo has to be held for its long press.
    Runtime(MapStore &store, Protocol &protocol, TxQueue &tx, const char *announceFw,
            uint16_t doubleTapMs = kDoubleTapMs, uint16_t longPressMs = kLongPressMs)
        : store_(store), protocol_(protocol), tx_(tx), announceFw_(announceFw), doubleTapMs_(doubleTapMs),
          longPressMs_(longPressMs) {}

    // Call once at the end of setup(). `seed` varies the announce intervals (any value).
    void startAnnounce(uint32_t nowMs, uint32_t seed);

    // Call once at the end of setup(): "@SM BAT" lines go to `port` (the WCB) when a
    // controller's battery level changes and every kBatteryPeriodMs, the first
    // kBatteryFirstMs from now. They don't depend on EVENTS.
    void startBattery(ReplyPort &port, uint32_t nowMs);

    // Once per pass, faults included: each controller's connection and battery status byte
    // (PS3BT::getBatteryStatus()). Returns true when either controller's level changed.
    bool batteries(uint32_t nowMs, bool footConnected, uint8_t footStatus, bool domeConnected, uint8_t domeStatus);
    const BatteryWatch &footBattery() const { return footBattery_; }
    const BatteryWatch &domeBattery() const { return domeBattery_; }

    // Once per pass, after the controllers have been read: the dome controller, then the foot,
    // as the old code did. A combo fires when pressed and again every second while held; the
    // repeat timer is shared by both controllers. A combo whose double tap or long press is mapped waits
    // instead (see WIPDocs/COMMAND_MAPPING_DESIGN.md, Double taps and Long presses): a second press within
    // the window fires the double tap, a hold to the threshold fires the long press, anything
    // else fires the combo itself: late, or on release if it has a long press. Returns true,
    // filling `fired`, when a combo fired this pass.
    bool buttons(uint32_t nowMs, const PadState &foot, const PadState &dome, FiredPress *fired);

    // True while a first tap waits to see whether a second one follows, or a held combo with a
    // long press waits to be released or held long enough.
    bool tapPending() const { return taps_[0].pending != kNoSlot || taps_[1].pending != kNoSlot; }

    // Reads @SM requests from `port` without blocking, at most kMaxBytesPerPass bytes a pass
    // (kFireRxBytes for both ports together in a pass that fired a press). One request a pass is
    // taken in, and not in a pass that fired a press, read kRxHeavy bytes or more, or finishes an
    // EEPROM job (MapStore::finishDue): a ready line then waits, and the bytes behind it wait in
    // the UART, for a later pass. Nothing more is read in a pass that took a request in. A request
    // that can't be answered yet (reply queue full, EEPROM busy) stays in `reader`; bytes arriving
    // meanwhile are read and dropped so the UART's RX buffer can't overflow. The Pi sends one
    // request at a time and resends after a timeout.
    template <class Serial>
    void pump(Serial &port, LineReader &reader, ReplyPort &replies) {
        uint8_t budget = kMaxBytesPerPass;
        if (fired_)
            budget = rxBytes_ < kFireRxBytes ? static_cast<uint8_t>(kFireRxBytes - rxBytes_) : 0;
        for (;;) {
            if (reader.ready()) {
                if (intake_ || fired_ || rxBytes_ >= kRxHeavy || store_.finishDue())
                    return;  // a later pass takes it in
                // A line that can't be answered yet is held without being taken in (so the pass
                // stays light and a SLOT reply in hand can go out), or taken in and refused
                // (EEPROM busy). Behind a short wait the bytes stay in the UART; behind a long
                // one they are read and dropped.
                const Protocol::Hold hold = protocol_.holdFor(reader.line(), replies);
                if (hold == Protocol::Hold::Wait)
                    return;
                if (hold == Protocol::Hold::Take)
                    intake_ = true;
                if (hold == Protocol::Hold::WaitDrain || !protocol_.handleLine(reader.line(), replies)) {
                    while (budget > 0) {
                        uint8_t n = batch(port.available(), budget);
                        if (n == 0)
                            return;
                        budget = static_cast<uint8_t>(budget - n);
                        rxBytes_ = static_cast<uint8_t>(rxBytes_ + n);
                        for (; n > 0; --n)
                            reader.discard(static_cast<char>(port.read()));
                    }
                    return;
                }
                reader.consume();
                return;
            }
            // As many bytes as are waiting (up to the budget), with one available() call: a line
            // that completes stops the batch so it's seen before the next byte is read.
            const uint8_t n = batch(port.available(), budget);
            if (n == 0)
                return;
            uint8_t took = 0;  // counted here, not in rxBytes_: the reader's stores may alias it
            while (took < n) {
                ++took;
                if (reader.feed(static_cast<char>(port.read())))
                    break;
            }
            budget = static_cast<uint8_t>(budget - took);
            rxBytes_ = static_cast<uint8_t>(rxBytes_ + took);
        }
    }

    // Every pass, faults included, after buttons() and both pumps: in a pass with nothing heavy in
    // it (no press fired, no request taken in, kRxHeavy bytes or more read, an EEPROM job
    // finishing), the first of these that has work does one unit: the announce (due every 25-30 s,
    // so it can't hold anything up, and nothing can hold it up), the protocol's next line or DUMP
    // read, the battery report (which a DUMP holds until its end, as before), a SUM step. Then,
    // every pass, the EEPROM writer's step (whose finishing step waits a pass after a press) and
    // the drain of queued bytes into the WCB's UART. Pass
    // `mayAnnounce` false while a controller fault is active: a due announce then waits for the
    // fault to clear. Battery reports carry on through a fault.
    template <class Serial>
    void service(uint32_t nowMs, Serial &wcb, bool mayAnnounce = true) {
        const bool heavy = fired_ || intake_ || rxBytes_ >= kRxHeavy || store_.finishDue();
        if (!heavy) {
            if (!(mayAnnounce && announce(nowMs)) && !protocol_.serviceLine() && !reportBattery(nowMs))
                protocol_.serviceSum();
        }
        // The writer's verify-and-finish step (never a write) waits one pass when a press fired in
        // this one, as a lookup's read makes it wait (MapStore's readWanted_): with @SM v4's PRESS
        // events, three presses and a finishing job in one pass came to 1.04 ms.
        if (fired_ && store_.finishDue() && !finishYielded_) {
            finishYielded_ = true;
        } else {
            finishYielded_ = false;
            store_.step();
        }
        tx_.drain(wcb);
        fired_ = false;
        intake_ = false;
        rxBytes_ = 0;
    }

  private:
    // What the combo held on one controller is doing.
    enum class Hold : uint8_t {
        None,     // nothing held
        Legacy,   // no double tap or long press mapped: fires at once, then every second (the shared timer)
        Waiting,  // first press of a double-tap or long-press combo, still held
        Repeat,   // held past the window (double tap only): fired as a single press, repeats every second
        Latched,  // double tap or long press fired (or a press ignored after one): nothing until released
    };

    struct PadTaps {
        uint8_t held = kNoSlot;     // the slot resolved last pass
        Hold mode = Hold::None;
        uint8_t pending = kNoSlot;  // a first tap waiting for a second; may be released already
        bool pendingFromOther = false;
        uint32_t pendingAt = 0;     // when it was pressed
        uint32_t lastFire = 0;      // Repeat: when it last fired
        uint8_t quiet = kNoSlot;    // just double-tapped: presses of it are ignored until
        uint32_t quietFrom = 0;     //   a second after its first press
    };

    static uint8_t batch(int available, uint8_t budget) {
        if (available <= 0)
            return 0;
        return available < budget ? static_cast<uint8_t>(available) : budget;
    }
    bool handlePad(Ctrl ctrl, const PadState &self, const PadState &other, uint32_t nowMs, FiredPress *fired);
    bool settle(PadTaps &t, uint8_t slot, uint32_t nowMs, FiredPress *fired);
    bool pressed(PadTaps &t, uint8_t slot, bool fromOther, uint32_t nowMs, FiredPress *fired);
    bool legacy(uint8_t slot, bool fromOther, uint32_t nowMs, FiredPress *fired);
    bool fireOrDefer(uint8_t slot, bool modFromOther, uint32_t nowMs, FiredPress *fired);
    bool fire(uint8_t slot, bool modFromOther, FiredPress *fired);
    bool announce(uint32_t nowMs);       // true if it queued the announce
    bool reportBattery(uint32_t nowMs);  // true if it queued a battery report
    uint32_t between(uint32_t lo, uint32_t hi);  // pseudo-random, lo..hi inclusive

    MapStore &store_;
    Protocol &protocol_;
    TxQueue &tx_;
    const char *announceFw_;
    uint16_t doubleTapMs_;
    uint16_t longPressMs_;

    PadTaps taps_[kCtrlCount];
    uint32_t repeatStart_ = 0;
    uint16_t repeatCount_ = 0;
    uint8_t deferredSlot_ = kNoSlot;  // pressed while the EEPROM was mid-write
    bool deferredFromOther_ = false;
    uint32_t deferredAt_ = 0;
    bool announcing_ = false;
    uint32_t nextAnnounce_ = 0;
    uint32_t rng_ = 1;

    BatteryWatch footBattery_;
    BatteryWatch domeBattery_;
    ReplyPort *batteryPort_ = nullptr;
    bool batteryChanged_ = false;  // a level changed since the last report
    uint32_t nextBattery_ = 0;

    // This pass so far (cleared at the end of service()).
    bool fired_ = false;   // a press fired
    bool intake_ = false;  // a request was taken in
    uint8_t rxBytes_ = 0;  // bytes read from both ports
    bool finishYielded_ = false;  // the writer's finishing step waited out a press's pass
};

}  // namespace sm
