// Controller battery levels. Every input report from a PS3 Navigation controller carries a
// battery status byte (PS3BT::getBatteryStatus()); this turns it into a settled level per
// controller, which SHADOW reports to DroidNet as "@SM BAT" lines. See
// WIPDocs/COMMAND_MAPPING_DESIGN.md (Battery reports).
#pragma once

#include <stdint.h>

namespace sm {

// SHADOW's verdict, the value DroidNet shows. The same words as Sabé's remote-battery levels,
// so DroidNet can treat both alike. Ordered best to worst.
enum class BattLevel : uint8_t { None = 0, Ok, Low, Crit };

// What the status byte means. The controller reports five steps while on battery (the Linux
// hid-sony driver reads them as 0/1/25/50/75/100 %), and 0xEE and up while on a USB cable:
// even is charging, odd is charged.
//   0x03-0x05 (about 50-100 %) and anything on a cable: OK
//   0x02 (about 25 %): LOW
//   0x01 (nearly empty; the library calls it Shutdown) and 0x00: CRIT
// Returns false for any other byte (0x06-0xED), which isn't a battery status.
bool battLevelFor(uint8_t status, BattLevel *out);

// One controller's level. A new level has to hold for a while before it's adopted, so a
// reading flickering at a step boundary doesn't flap: a worse level for kWorseMs, a better one
// for kBetterMs (a battery only really recovers on a charger). The first level of a
// connection takes kFirstMs.
class BatteryWatch {
  public:
    static constexpr uint32_t kFirstMs = 2000;
    static constexpr uint32_t kWorseMs = 3000;
    static constexpr uint32_t kBetterMs = 30000;

    // Every pass. `status` is the controller's last battery status byte, 0 = no report yet.
    // A disconnected controller forgets its level at once. Returns true when level() changed.
    bool update(uint32_t nowMs, bool connected, uint8_t status);

    BattLevel level() const { return level_; }
    // The latest valid status byte behind level(), unsettled (0 = none). Detail only.
    uint8_t status() const { return status_; }

  private:
    BattLevel level_ = BattLevel::None;
    uint8_t status_ = 0;
    BattLevel candidate_ = BattLevel::None;
    uint32_t candidateSince_ = 0;
};

// Both controllers, as the "@SM BAT" line reports them.
struct BatteryReport {
    BattLevel foot;
    uint8_t footStatus;
    BattLevel dome;
    uint8_t domeStatus;
    uint32_t nowMs;  // millis(), for the uptime the line carries
};

// SHADOW sends "@SM BAT" to the WCB when a level changes, and every kBatteryPeriodMs anyway so
// DroidNet can tell SHADOW is there. The first one goes kBatteryFirstMs after startup.
constexpr uint32_t kBatteryPeriodMs = 30000;
constexpr uint32_t kBatteryFirstMs = 5000;

}  // namespace sm
