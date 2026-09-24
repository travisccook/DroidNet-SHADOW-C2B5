// Slot layout: every (controller, modifier, button) combo has a fixed slot index, so a
// lookup is arithmetic instead of a table scan.
#pragma once

#include <stdint.h>

namespace sm {

enum class Ctrl : uint8_t { F = 0, D = 1 };                          // foot, dome
enum class Mod : uint8_t { NONE = 0, L1, L2, PS, CIRCLE, CROSS };    // held modifier
enum class Btn : uint8_t { UP = 0, RIGHT, DOWN, LEFT, L3, PS };      // primary button

constexpr uint8_t kCtrlCount = 2;
constexpr uint8_t kModCount = 6;
constexpr uint8_t kBtnCount = 6;
constexpr uint8_t kSlotsPerCtrl = kModCount * kBtnCount;  // 36
constexpr uint8_t kSlotCount = kCtrlCount * kSlotsPerCtrl;  // 72
constexpr uint8_t kNoSlot = 0xFF;

constexpr uint8_t slotIndex(Ctrl c, Mod m, Btn b) {
    return static_cast<uint8_t>(static_cast<uint8_t>(c) * kSlotsPerCtrl + static_cast<uint8_t>(m) * kBtnCount +
                                static_cast<uint8_t>(b));
}

inline Ctrl slotCtrl(uint8_t slot) { return static_cast<Ctrl>(slot / kSlotsPerCtrl); }
inline Mod slotMod(uint8_t slot) { return static_cast<Mod>((slot % kSlotsPerCtrl) / kBtnCount); }
inline Btn slotBtn(uint8_t slot) { return static_cast<Btn>(slot % kBtnCount); }

// Why a slot can't be mapped. These combos are hardcoded in main.cpp or never reach the
// mapping, so SET rejects them and lookups return nothing.
enum class Reserved : uint8_t {
    No = 0,
    PsGate,     // PS is only a primary with L1/L2 held; PS+CROSS / PS+CIRCLE toggle the drive stick
    OverSpeed,  // foot L3+L1 toggles over-speed
};

Reserved slotReserved(uint8_t slot);

// Protocol tokens: controller "F"/"D", modifier "NONE".."CROSS", button "UP".."PS".
bool parseCtrl(const char *s, Ctrl *out);
bool parseMod(const char *s, Mod *out);
bool parseBtn(const char *s, Btn *out);

// Writes "F L1 UP" (NUL-terminated) and returns its length, or 0 if it doesn't fit.
uint8_t formatSlot(char *dst, uint8_t cap, uint8_t slot);

// Flash strings for a reserved reason ("psgate", "overspeed"), or nullptr for Reserved::No.
const char *reservedToken(Reserved r);

}  // namespace sm
