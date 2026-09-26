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
    if (!self.connected || !gateOpen(self.held))
        return false;

    if (nowMs - repeatStart_ > kRepeatMs) {
        repeatCount_ = 0;
        repeatStart_ = nowMs;
    }
    if (repeatCount_ < 0xFFFF)
        ++repeatCount_;
    if (repeatCount_ != 1)
        return false;

    bool fromOther = false;
    const uint8_t slot = resolveSlot(ctrl, self, other, &fromOther);
    if (slot == kNoSlot)
        return false;
    if (!fire(slot, fromOther, fired)) {
        deferredSlot_ = slot;  // EEPROM mid-write: send it next pass
        deferredFromOther_ = fromOther;
        deferredAt_ = nowMs;
        return false;
    }
    return true;
}

bool Runtime::fire(uint8_t slot, bool modFromOther, FiredPress *fired) {
    char key[kKeyBuf];
    Source src = Source::Default;
    if (!store_.keyForSlot(slot, key, &src))
        return false;
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

void Runtime::announce(uint32_t nowMs) {
    if (!announcing_ || static_cast<int32_t>(nowMs - nextAnnounce_) < 0)
        return;
    // @WDP1 {"type":"Shadow","fw":"<fw>"}
    char line[64];
    strcpy_P(line, PSTR("@WDP1 {\"type\":\"Shadow\",\"fw\":\""));
    const size_t used = strlen(line);
    const size_t fwLen = strlen(announceFw_);
    if (used + fwLen + 2 >= sizeof(line)) {
        announcing_ = false;  // main.cpp's static_assert rules this out; stop rather than truncate
        return;
    }
    memcpy(line + used, announceFw_, fwLen);
    line[used + fwLen] = '"';
    line[used + fwLen + 1] = '}';
    line[used + fwLen + 2] = '\0';
    if (tx_.freeSpace() < strlen(line) + 1 + kCommandHeadroom)
        return;  // busy right now; try again next pass
    const char *parts[] = {line};
    tx_.pushLine(parts, 1);
    nextAnnounce_ = nowMs + between(kAnnounceMinMs, kAnnounceMaxMs);
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

void Runtime::reportBattery(uint32_t nowMs) {
    if (batteryPort_ == nullptr)
        return;
    if (!batteryChanged_ && static_cast<int32_t>(nowMs - nextBattery_) < 0)
        return;
    if (!protocol_.sendBattery(*batteryPort_))
        return;  // busy right now; try again next pass
    batteryChanged_ = false;
    nextBattery_ = nowMs + kBatteryPeriodMs;
}

}  // namespace sm
