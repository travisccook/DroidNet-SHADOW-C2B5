#include "sm_battery.h"

namespace sm {

bool battLevelFor(uint8_t status, BattLevel *out) {
    if (status >= 0xEE) {
        *out = BattLevel::Ok;  // on a cable
    } else if (status >= 0x06) {
        return false;
    } else if (status >= 0x03) {
        *out = BattLevel::Ok;
    } else if (status == 0x02) {
        *out = BattLevel::Low;
    } else {
        *out = BattLevel::Crit;
    }
    return true;
}

bool BatteryWatch::update(uint32_t nowMs, bool connected, uint8_t status) {
    BattLevel seen = BattLevel::None;
    if (!connected) {
        const bool changed = level_ != BattLevel::None;
        level_ = BattLevel::None;
        status_ = 0;
        candidate_ = BattLevel::None;
        return changed;
    }
    // 0 is also what PS3BT holds before the first report, so it's never taken as a reading.
    if (status == 0 || !battLevelFor(status, &seen))
        return false;
    status_ = status;
    if (seen == level_) {
        candidate_ = BattLevel::None;  // back where it was: drop a pending change
        return false;
    }
    if (seen != candidate_) {
        candidate_ = seen;
        candidateSince_ = nowMs;
    }
    uint32_t hold = kFirstMs;
    if (level_ != BattLevel::None)
        hold = seen > level_ ? kWorseMs : kBetterMs;
    if (nowMs - candidateSince_ < hold)
        return false;
    level_ = seen;
    candidate_ = BattLevel::None;
    return true;
}

}  // namespace sm
