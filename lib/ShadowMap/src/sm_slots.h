// Slot layout: every (controller, modifier, button, tap) combo has a fixed slot index, so a
// lookup is arithmetic instead of a table scan. Slots 0-71 are single presses; 72-143 are
// the double taps of the same combos, in the same order.
#pragma once

#include <stdint.h>

namespace sm {

enum class Ctrl : uint8_t { F = 0, D = 1 };                          // foot, dome
enum class Mod : uint8_t { NONE = 0, L1, L2, PS, CIRCLE, CROSS };    // held modifier
enum class Btn : uint8_t { UP = 0, RIGHT, DOWN, LEFT, L3, PS };      // primary button
enum class Tap : uint8_t { SINGLE = 0, DOUBLE };                     // pressed once, or twice quickly

constexpr uint8_t kCtrlCount = 2;
constexpr uint8_t kModCount = 6;
constexpr uint8_t kBtnCount = 6;
constexpr uint8_t kSlotsPerCtrl = kModCount * kBtnCount;           // 36
constexpr uint8_t kSingleSlotCount = kCtrlCount * kSlotsPerCtrl;   // 72
constexpr uint8_t kSlotCount = 2 * kSingleSlotCount;               // 144
constexpr uint8_t kNoSlot = 0xFF;

constexpr uint8_t slotIndex(Ctrl c, Mod m, Btn b, Tap t = Tap::SINGLE) {
    return static_cast<uint8_t>(static_cast<uint8_t>(t) * kSingleSlotCount + static_cast<uint8_t>(c) * kSlotsPerCtrl +
                                static_cast<uint8_t>(m) * kBtnCount + static_cast<uint8_t>(b));
}

inline Ctrl slotCtrl(uint8_t slot) { return static_cast<Ctrl>((slot % kSingleSlotCount) / kSlotsPerCtrl); }
inline Mod slotMod(uint8_t slot) { return static_cast<Mod>((slot % kSlotsPerCtrl) / kBtnCount); }
inline Btn slotBtn(uint8_t slot) { return static_cast<Btn>(slot % kBtnCount); }
inline Tap slotTap(uint8_t slot) { return slot < kSingleSlotCount ? Tap::SINGLE : Tap::DOUBLE; }
// The double-tap slot for a single-press slot.
inline uint8_t doubleOf(uint8_t slot) { return static_cast<uint8_t>(slot % kSingleSlotCount + kSingleSlotCount); }

// Why a slot can't be mapped. These combos are hardcoded in main.cpp or never reach the
// mapping, so SET rejects them and lookups return nothing.
enum class Reserved : uint8_t {
    No = 0,
    PsGate,     // PS is only a primary with L1/L2 held; PS+CROSS / PS+CIRCLE toggle the drive stick
    OverSpeed,  // foot L3+L1 toggles over-speed
    Stick,      // L3 (the stick click) has no double tap
};

Reserved slotReserved(uint8_t slot);

// Protocol tokens: controller "F"/"D", modifier "NONE".."CROSS", button "UP".."PS". A double
// tap is the button with "2" after it ("UP2"); parseBtn() only accepts that with `tap`.
bool parseCtrl(const char *s, Ctrl *out);
bool parseMod(const char *s, Mod *out);
bool parseBtn(const char *s, Btn *out, Tap *tap = nullptr);

// Writes "F L1 UP" or "F L1 UP2" (NUL-terminated) and returns its length, or 0 if it doesn't
// fit. The longest, "F CIRCLE RIGHT2", needs 16 bytes.
uint8_t formatSlot(char *dst, uint8_t cap, uint8_t slot);

constexpr uint8_t kSlotTextBuf = 16;

// Flash strings for a reserved reason ("psgate", "overspeed", "stick"), or nullptr for Reserved::No.
const char *reservedToken(Reserved r);

}  // namespace sm
