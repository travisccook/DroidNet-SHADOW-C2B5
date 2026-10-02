#include "sm_runtime.h"

#include <string.h>

#include "sm_pgm.h"

namespace sm {

bool Runtime::buttons(uint32_t nowMs, const PadState &foot, const PadState &dome, FiredPress *fired) {
    protocol_.setPads(foot.connected, dome.connected);
    bool any = false;
    if (deferredSlot_ != kNoSlot) {
        // Only while it's still fresh and its controller is still there (a fault can hold up
        // the passes in between).
        const PadState &pad = slotCtrl(deferredSlot_) == Ctrl::F ? foot : dome;
        if (!pad.connected || nowMs - deferredAt_ > kDeferMaxMs) {
            deferredSlot_ = kNoSlot;
        } else if (fire(deferredSlot_, deferredFromOther_, fired)) {
            deferredSlot_ = kNoSlot;
            any = true;
        }
    }
    any = handlePad(Ctrl::D, dome, foot, nowMs, fired) || any;
    any = handlePad(Ctrl::F, foot, dome, nowMs, fired) || any;
    return any;
}

bool Runtime::handlePad(Ctrl ctrl, const PadState &self, const PadState &other, uint32_t nowMs, FiredPress *fired) {
    PadTaps &t = taps_[static_cast<uint8_t>(ctrl)];
    if (!self.connected) {
        t = PadTaps();  // a controller that drops loses a half-finished double tap
        return false;
    }
    bool fromOther = false;
    const uint8_t slot = gateOpen(self.held) ? resolveSlot(ctrl, self, other, &fromOther) : kNoSlot;

    bool any = false;
    if (t.pending != kNoSlot)
        any = settle(t, slot, nowMs, fired);  // before this pass's press, which is too late to be the second tap

    if (slot != t.held) {
        any = pressed(t, slot, fromOther, nowMs, fired) || any;
        t.held = slot;
    }

    if (t.mode == Hold::Repeat && nowMs - t.lastFire > kRepeatMs) {
        t.lastFire = nowMs;
        any = fireOrDefer(slot, fromOther, nowMs, fired) || any;
    } else if (t.mode == Hold::Legacy) {
        any = legacy(slot, fromOther, nowMs, fired) || any;
    }
    return any;
}

// A first press waiting on a double tap or a long press: fires it once its moment comes. A
// release is pressed()'s job; this handles the time running out. `slot` is the combo held
// this pass: a long press fires only if it is still that combo, so a release (or another
// combo) arriving after a gap in passes is left to pressed() and counts as a fresh single press.
// But a first pass more than the threshold + kDeferMaxMs after the press means passes were held
// up (a fault): the moment it waited for is long past, so the press is dropped, whatever this
// pass shows, rather than fired late.
bool Runtime::settle(PadTaps &t, uint8_t slot, uint32_t nowMs, FiredPress *fired) {
    const uint8_t first = t.pending;
    const uint32_t age = nowMs - t.pendingAt;
    if (t.mode == Hold::Waiting && store_.longPressMapped(first)) {
        if (age > static_cast<uint32_t>(longPressMs_) + kDeferMaxMs) {
            t.pending = kNoSlot;  // too late to mean anything, held or not
            if (slot == first)
                t.mode = Hold::Latched;
            return false;
        }
        // Still held, and it has a long press: nothing until the threshold.
        if (age < longPressMs_ || slot != first)
            return false;
        t.pending = kNoSlot;
        t.mode = Hold::Latched;  // once, no repeat; releasing it does nothing
        return fireOrDefer(longOf(first), t.pendingFromOther, nowMs, fired);
    }
    if (age < doubleTapMs_)
        return false;
    // No second press in time: it was a single press.
    t.pending = kNoSlot;
    if (age > static_cast<uint32_t>(doubleTapMs_) + kDeferMaxMs) {
        if (t.mode == Hold::Waiting)  // passes were held up (a fault): too late to mean anything
            t.mode = Hold::Latched;
        return false;
    }
    const bool any = fireOrDefer(first, t.pendingFromOther, nowMs, fired);
    if (t.mode == Hold::Waiting) {
        t.mode = Hold::Repeat;
        t.lastFire = nowMs;
    }
    return any;
}

// The held combo changed: pressed, released, or turned into another combo.
bool Runtime::pressed(PadTaps &t, uint8_t slot, bool fromOther, uint32_t nowMs, FiredPress *fired) {
    bool any = false;
    if (t.pending != kNoSlot) {
        if (slot != kNoSlot && slot == t.pending) {
            t.pending = kNoSlot;
            t.mode = Hold::Latched;  // no repeat for a held double tap
            t.quiet = slot;
            t.quietFrom = t.pendingAt;
            return fireOrDefer(doubleOf(slot), fromOther, nowMs, fired);
        }
        if (slot != kNoSlot) {
            // Another combo: the first one was a single press.
            any = fireOrDefer(t.pending, t.pendingFromOther, nowMs, fired);
            t.pending = kNoSlot;
        } else if (t.mode == Hold::Waiting &&
                   (!store_.doubleTapMapped(t.pending) || nowMs - t.pendingAt >= doubleTapMs_)) {
            // Let go before its long press, with no double tap left to wait for: a single press, now.
            any = fireOrDefer(t.pending, t.pendingFromOther, nowMs, fired);
            t.pending = kNoSlot;
        }
    }

    if (slot == kNoSlot) {
        t.mode = Hold::None;
    } else if (!store_.doubleTapMapped(slot) && !store_.longPressMapped(slot)) {
        t.mode = Hold::Legacy;
    } else if (slot == t.quiet && nowMs - t.quietFrom <= kRepeatMs) {
        t.mode = Hold::Latched;  // a third tap right after a double tap
    } else {
        t.mode = Hold::Waiting;
        t.pending = slot;
        t.pendingFromOther = fromOther;
        t.pendingAt = nowMs;
    }
    return any;
}

// The original timing: fires when pressed and again every second while held. The timer is
// shared by both controllers and doesn't reset on release, so taps inside the same second
// are ignored.
bool Runtime::legacy(uint8_t slot, bool fromOther, uint32_t nowMs, FiredPress *fired) {
    if (nowMs - repeatStart_ > kRepeatMs) {
        repeatCount_ = 0;
        repeatStart_ = nowMs;
    }
    if (repeatCount_ < 0xFFFF)
        ++repeatCount_;
    if (repeatCount_ != 1)
        return false;
    return fireOrDefer(slot, fromOther, nowMs, fired);
}

bool Runtime::fireOrDefer(uint8_t slot, bool modFromOther, uint32_t nowMs, FiredPress *fired) {
    if (fire(slot, modFromOther, fired))
        return true;
    deferredSlot_ = slot;  // EEPROM mid-write: send it next pass
    deferredFromOther_ = modFromOther;
    deferredAt_ = nowMs;
    return false;
}

bool Runtime::fire(uint8_t slot, bool modFromOther, FiredPress *fired) {
    char key[kKeyBuf];
    Source src = Source::Default;
    if (!store_.keyForSlot(slot, key, &src))
        return false;
    fired_ = true;
    bool queued = false;
    if (key[0] != '\0')
        queued = pushCommand(tx_, store_.prefix(), key, store_.suffix());
    protocol_.notifyPress(slot, key, src, modFromOther);
    if (fired != nullptr) {
        fired->slot = slot;
        memcpy(fired->key, key, kKeyBuf);
        fired->src = src;
        fired->modFromOther = modFromOther;
        fired->queued = queued;
    }
    return true;
}

void Runtime::startAnnounce(uint32_t nowMs, uint32_t seed) {
    rng_ = seed != 0 ? seed : 0x2545F491UL;  // xorshift can't start from 0
    nextAnnounce_ = nowMs + between(kFirstAnnounceMinMs, kFirstAnnounceMaxMs);
    announcing_ = announceFw_ != nullptr;
}

uint32_t Runtime::between(uint32_t lo, uint32_t hi) {
    // xorshift32: plenty for spreading announces out, and cheap on AVR.
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return lo + rng_ % (hi - lo + 1);
}

namespace {
const char kAnnounceHead[] PROGMEM = "@WDP1 {\"type\":\"Shadow\",\"fw\":\"";
constexpr uint8_t kAnnounceHeadLen = sizeof(kAnnounceHead) - 1;
static_assert(kAnnounceHeadLen + 2 + 1 == Runtime::kAnnounceOverhead, "announce overhead");
}  // namespace

bool Runtime::announce(uint32_t nowMs) {
    if (!announcing_ || static_cast<int32_t>(nowMs - nextAnnounce_) < 0)
        return false;
    // @WDP1 {"type":"Shadow","fw":"<fw>"}
    char line[64];
    const size_t fwLen = strlen(announceFw_);
    if (kAnnounceHeadLen + fwLen + 2 >= sizeof(line)) {
        announcing_ = false;  // main.cpp's static_assert rules this out; stop rather than truncate
        return false;
    }
    const uint8_t len = static_cast<uint8_t>(kAnnounceHeadLen + fwLen + 2);
    if (tx_.freeSpace() < len + 1 + kCommandHeadroom)
        return false;  // busy right now; try again next pass
    memcpy_P(line, kAnnounceHead, kAnnounceHeadLen);
    memcpy(line + kAnnounceHeadLen, announceFw_, fwLen);
    line[len - 2] = '"';
    line[len - 1] = '}';
    tx_.pushLine2(line, len, nullptr, 0);
    nextAnnounce_ = nowMs + between(kAnnounceMinMs, kAnnounceMaxMs);
    return true;
}

void Runtime::startBattery(ReplyPort &port, uint32_t nowMs) {
    batteryPort_ = &port;
    nextBattery_ = nowMs + kBatteryFirstMs;
}

bool Runtime::batteries(uint32_t nowMs, bool footConnected, uint8_t footStatus, bool domeConnected,
                        uint8_t domeStatus) {
    bool changed = footBattery_.update(nowMs, footConnected, footStatus);
    changed = domeBattery_.update(nowMs, domeConnected, domeStatus) || changed;
    if (changed)
        batteryChanged_ = true;
    protocol_.setBattery({footBattery_.level(), footBattery_.status(), domeBattery_.level(), domeBattery_.status(),
                          nowMs});
    return changed;
}

bool Runtime::reportBattery(uint32_t nowMs) {
    if (batteryPort_ == nullptr)
        return false;
    if (!batteryChanged_ && static_cast<int32_t>(nowMs - nextBattery_) < 0)
        return false;
    if (!protocol_.sendBattery(*batteryPort_))
        return false;  // busy right now; try again next pass
    batteryChanged_ = false;
    nextBattery_ = nowMs + kBatteryPeriodMs;
    return true;
}

}  // namespace sm
