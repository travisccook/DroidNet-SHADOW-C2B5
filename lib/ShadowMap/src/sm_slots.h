// Slot layout: every (controller, modifier, button, tap) combo has a fixed slot index, so a
// lookup is arithmetic instead of a table scan. Slots 0-71 are single presses, 72-143 the
// double taps of the same combos, and 144-215 their long presses, in the same order.
#pragma once

#include <stdint.h>

namespace sm {

enum class Ctrl : uint8_t { F = 0, D = 1 };                          // foot, dome
enum class Mod : uint8_t { NONE = 0, L1, L2, PS, CIRCLE, CROSS };    // held modifier
enum class Btn : uint8_t { UP = 0, RIGHT, DOWN, LEFT, L3, PS };      // primary button
enum class Tap : uint8_t { SINGLE = 0, DOUBLE, LONG };               // pressed once, twice quickly, or held

constexpr uint8_t kCtrlCount = 2;
constexpr uint8_t kModCount = 6;
constexpr uint8_t kBtnCount = 6;
constexpr uint8_t kTapCount = 3;
constexpr uint8_t kSlotsPerCtrl = kModCount * kBtnCount;           // 36
constexpr uint8_t kSingleSlotCount = kCtrlCount * kSlotsPerCtrl;   // 72
constexpr uint8_t kSlotCount = kTapCount * kSingleSlotCount;       // 216
constexpr uint8_t kNoSlot = 0xFF;

constexpr uint8_t slotIndex(Ctrl c, Mod m, Btn b, Tap t = Tap::SINGLE) {
    return static_cast<uint8_t>(static_cast<uint8_t>(t) * kSingleSlotCount + static_cast<uint8_t>(c) * kSlotsPerCtrl +
                                static_cast<uint8_t>(m) * kBtnCount + static_cast<uint8_t>(b));
}

// A slot's parts, worked out by subtraction (an 8-bit divide is a library call on AVR). Any
// value 0-255 gives what / and % would: tap = slot / 72 (3 for 216-255), bank = slot % 72.
struct SlotParts {
    uint8_t tap;   // 0 single, 1 double tap, 2 long press (3 past the last slot)
    uint8_t bank;  // slot % 72: the single-press slot it belongs to
    uint8_t ctrl;  // 0 foot, 1 dome
    uint8_t mod;
    uint8_t btn;
};
inline SlotParts splitSlot(uint8_t slot) {
    SlotParts p;
    uint8_t s = slot;
    uint8_t t = 0;
    while (s >= kSingleSlotCount) {
        s = static_cast<uint8_t>(s - kSingleSlotCount);
        ++t;
    }
    p.tap = t;
    p.bank = s;
    p.ctrl = 0;
    if (s >= kSlotsPerCtrl) {
        s = static_cast<uint8_t>(s - kSlotsPerCtrl);
        p.ctrl = 1;
    }
    uint8_t m = 0;
    while (s >= kBtnCount) {
        s = static_cast<uint8_t>(s - kBtnCount);
        ++m;
    }
    p.mod = m;
    p.btn = s;
    return p;
}

inline Ctrl slotCtrl(uint8_t slot) { return static_cast<Ctrl>(splitSlot(slot).ctrl); }
inline Mod slotMod(uint8_t slot) { return static_cast<Mod>(splitSlot(slot).mod); }
inline Btn slotBtn(uint8_t slot) { return static_cast<Btn>(splitSlot(slot).btn); }
inline Tap slotTap(uint8_t slot) { return static_cast<Tap>(splitSlot(slot).tap); }
// The double-tap slot for a single-press slot.
inline uint8_t doubleOf(uint8_t slot) { return static_cast<uint8_t>(splitSlot(slot).bank + kSingleSlotCount); }
// The long-press slot for a single-press slot.
inline uint8_t longOf(uint8_t slot) { return static_cast<uint8_t>(splitSlot(slot).bank + 2 * kSingleSlotCount); }

// Why a slot can't be mapped. These combos are hardcoded in main.cpp or never reach the
// mapping, so SET rejects them and lookups return nothing.
enum class Reserved : uint8_t {
    No = 0,
    PsGate,     // PS is only a primary with L1/L2 held; PS+CROSS / PS+CIRCLE toggle the drive stick
    OverSpeed,  // foot L3+L1 toggles over-speed
    Stick,      // L3 (the stick click) has no double tap or long press
};

Reserved slotReserved(uint8_t slot);

// Protocol tokens: controller "F"/"D", modifier "NONE".."CROSS", button "UP".."PS". A double
// tap is the button with "2" after it ("UP2"), a long press the button with "L" ("UPL");
// parseBtn() only accepts those with `tap`.
bool parseCtrl(const char *s, Ctrl *out);
bool parseMod(const char *s, Mod *out);
bool parseBtn(const char *s, Btn *out, Tap *tap = nullptr);

// Writes "F L1 UP", "F L1 UP2" or "F L1 UPL" (NUL-terminated) and returns its length, or 0 if it
// doesn't fit. The longest, "F CIRCLE RIGHT2" / "F CIRCLE RIGHTL", needs 16 bytes.
uint8_t formatSlot(char *dst, uint8_t cap, uint8_t slot);

constexpr uint8_t kSlotTextBuf = 16;

// The length of a slot's text (0 for a slot past the last), and the text itself without a NUL
// (`slot` must be a slot; returns the length). For lines built in place (Out::slot).
uint8_t slotTextLen(uint8_t slot);
uint8_t writeSlotText(char *dst, uint8_t slot);
// The length of a modifier's token ("NONE" 4 ... "CIRCLE" 6).
uint8_t modTextLen(uint8_t mod);

// Flash strings for a reserved reason ("psgate", "overspeed", "stick"), or nullptr for Reserved::No.
const char *reservedToken(Reserved r);

}  // namespace sm
