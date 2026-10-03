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
    // Request bytes read in one pass, shared by both ports (the WCB first, USB gets what's left).
    // At 9600 baud about 1 byte arrives a millisecond, so 32 a pass still empties the UART's 64-byte
    // RX buffer far faster than it fills; a request longer than 32 bytes takes one more pass to read.
    // 64 left the worst read pass (a full budget ending a GET of v4's 93-character SLOT line,
    // answered) about 11 us under 1 ms on the emulated ATmega2560.
    static constexpr uint8_t kMaxBytesPerPass = 32;
    static constexpr uint32_t kDeferMaxMs = 250;  // a deferred press older than this is dropped
    static constexpr uint16_t kDoubleTapMs = 350;  // default window from the first press to the second
    static constexpr uint16_t kLongPressMs = 600;  // default hold for a long press
    static constexpr uint8_t kMaxDeferred = 8;      // presses waiting to fire (see buttons())
    // Presses fired in one pass; more wait a pass, in order. Two keeps the worst press pass (two
    // presses, their PRESS events and the EEPROM writer finishing a job) under 1 ms with v4's events.
    static constexpr uint8_t kMaxFiresPerPass = 2;

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

    // Once per pass, after the controllers have been read: presses that waited, oldest first, then the
    // dome controller, then the foot, as the old code did. A combo fires when pressed and again every
    // second while held; the repeat timer is shared by both controllers. A combo whose double tap or long
    // press is mapped waits instead (see WIPDocs/COMMAND_MAPPING_DESIGN.md, Double taps and Long presses).
    // A press that can't fire yet (the EEPROM mid-write, no room in the TX queue for its command, or
    // kMaxFiresPerPass already fired this pass) waits, in order, and fires on a later pass; it is dropped
    // only if its controller has gone or it is more than kDeferMaxMs old. While one waits, the TX queue
    // takes no replies (TxQueue::holdForCommand). Returns true, filling `fired` with the last, when a
    // press fired this pass.
    bool buttons(uint32_t nowMs, const PadState &foot, const PadState &dome, FiredPress *fired);

    // True while a first tap waits to see whether a second one follows, or a held combo with a
    // long press waits to be released or held long enough.
    bool tapPending() const { return taps_[0].pending != kNoSlot || taps_[1].pending != kNoSlot; }

    // A pass in which readUSB() reported a fault (buttons() isn't called): no press can fire, so the
    // TX queue isn't held for one. Presses still waiting are dropped by buttons() once they are old.
    void faultPass() { tx_.holdForCommand(false); }

    // Diagnostics, for tests and the timing harness.
    uint8_t deferredCount() const { return deferredCount_; }
    uint8_t firesLastPass() const { return firesThisPass_; }
    uint16_t droppedPresses() const { return droppedPresses_; }  // the waiting list was full (never expected)

    // A serial port's input, its line reader and where its replies go, for afterButtons().
    template <class Serial>
    struct Link {
        Serial &port;
        LineReader &lines;
        ReplyPort &replies;
    };

    // What the last afterButtons() did besides presses.
    enum class Work : uint8_t { Presses, Request, Background };
    Work lastWork() const { return lastWork_; }

    // Reads @SM requests from `port` without blocking, at most `budget` bytes (each byte read comes
    // off it: afterButtons() shares one kMaxBytesPerPass budget between both ports), and handles at
    // most one. Returns true when it handled one: answered it, or found it can't be answered yet (reply
    // queue full, EEPROM busy), in which case it stays in `reader` for a later pass and bytes arriving
    // meanwhile are read and dropped so the UART's RX buffer can't overflow. The Pi sends one request at
    // a time and resends after a timeout.
    template <class Serial>
    bool pump(Serial &port, LineReader &reader, ReplyPort &replies, uint8_t &budget) {
        while (true) {
            if (reader.ready()) {
                if (protocol_.handleLine(reader.line(), replies)) {
                    reader.consume();
                    return true;
                }
                while (budget > 0) {
                    uint8_t n = batch(port.available(), budget);
                    if (n == 0)
                        break;
                    budget = static_cast<uint8_t>(budget - n);
                    for (; n > 0; --n)
                        reader.discard(static_cast<char>(port.read()));
                }
                return true;
            }
            // As many bytes as are waiting (up to the budget), with one available() call: a line
            // that completes stops the batch so it's handled before the next byte is read.
            uint8_t n = batch(port.available(), budget);
            if (n == 0)
                return false;
            for (; n > 0; --n) {
                --budget;
                if (reader.feed(static_cast<char>(port.read())))
                    break;
            }
        }
    }

    // pump() with its own fresh kMaxBytesPerPass budget (for tests and tools that read one port).
    template <class Serial>
    bool pump(Serial &port, LineReader &reader, ReplyPort &replies) {
        uint8_t budget = kMaxBytesPerPass;
        return pump(port, reader, replies, budget);
    }

    // The background unit: protocol work (a DUMP line, a SUM step, a write's reply, a CTL event), the
    // announce (not while `mayAnnounce` is false: a controller fault; a due announce then waits for the
    // fault to clear), and the battery report (which carries on through a fault). Each waits for room.
    void background(uint32_t nowMs, bool mayAnnounce);

    // Every pass, faults included: the EEPROM writer's step, then queued bytes to the WCB's UART.
    template <class Out>
    void finishPass(Out &wcb) {
        store_.step();
        tx_.drain(wcb);
    }

    // background() and finishPass() in one call, with no pass rule: for tests and tools that drive one
    // thing at a time. loop() uses afterButtons().
    template <class Out>
    void service(uint32_t nowMs, Out &wcb, bool mayAnnounce = true) {
        background(nowMs, mayAnnounce);
        finishPass(wcb);
    }

    // The rest of the mapping's pass, once per loop() pass after buttons() (faults included, with
    // `fired` false: buttons() isn't called then). WIPDocs/TIMING_ROUND_2_DESIGN.md, section 3:
    //   - a pass that fired a press reads no request and runs no background unit;
    //   - the first pass without a press after one that handled a request runs the background unit;
    //   - any other pass handles at most one request (WCB first, then USB) and, if it handled none,
    //     runs the background unit; both ports share one kMaxBytesPerPass read budget a pass (USB
    //     gets what the WCB left), so the reads are bounded whatever arrives;
    // then finishPass() into `wcbOut`, every pass. No unit waits more than one pass for another.
    template <class WcbIn, class UsbIn, class Out>
    void afterButtons(uint32_t nowMs, bool fired, bool faulted, Link<WcbIn> wcb, Link<UsbIn> usb, Out &wcbOut) {
        if (faulted)
            faultPass();
        if (fired) {
            lastWork_ = Work::Presses;
        } else if (backgroundOwed_) {
            background(nowMs, !faulted);
            backgroundOwed_ = false;
            lastWork_ = Work::Background;
        } else if (uint8_t budget = kMaxBytesPerPass;
                   pump(wcb.port, wcb.lines, wcb.replies, budget) || pump(usb.port, usb.lines, usb.replies, budget)) {
            backgroundOwed_ = true;
            lastWork_ = Work::Request;
        } else {
            background(nowMs, !faulted);
            lastWork_ = Work::Background;
        }
        finishPass(wcbOut);
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
    void defer(uint8_t slot, bool modFromOther, uint32_t nowMs);
    void announce(uint32_t nowMs);
    void reportBattery(uint32_t nowMs);
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
    struct Deferred {
        uint8_t slot;
        bool fromOther;
        uint32_t at;  // when it first tried to fire
    };
    Deferred deferred_[kMaxDeferred];
    uint8_t deferredCount_ = 0;
    uint8_t firesThisPass_ = 0;
    uint16_t droppedPresses_ = 0;
    bool backgroundOwed_ = false;  // a request was handled; the next pass without a press is the background unit's
    Work lastWork_ = Work::Background;
    bool announcing_ = false;
    uint32_t nextAnnounce_ = 0;
    uint32_t rng_ = 1;

    BatteryWatch footBattery_;
    BatteryWatch domeBattery_;
    ReplyPort *batteryPort_ = nullptr;
    bool batteryChanged_ = false;  // a level changed since the last report
    uint32_t nextBattery_ = 0;
};

}  // namespace sm
