// Picks the mapping slot for the buttons currently held. Pure logic, so it can be tested
// against the old table-driven code for every button combination.
#pragma once

#include <stdint.h>

#include "sm_slots.h"

namespace sm {

// One bit per physical button that matters for the mapping.
enum HeldBit : uint16_t {
    kHeldUp = 1u << 0,
    kHeldRight = 1u << 1,
    kHeldDown = 1u << 2,
    kHeldLeft = 1u << 3,
    kHeldL3 = 1u << 4,
    kHeldPs = 1u << 5,
    kHeldL1 = 1u << 6,
    kHeldL2 = 1u << 7,
    kHeldCircle = 1u << 8,
    kHeldCross = 1u << 9,
};

struct PadState {
    bool connected;
    uint16_t held;  // HeldBit mask
};

// True when this controller has something that can trigger a mapping: a direction, L3,
// or PS with L1/L2. Matches the gate the old simpleMarcDuinoFoot/Dome() used.
bool gateOpen(uint16_t held);

// The slot for the held combo, or kNoSlot. The highest-priority held modifier picks the
// layer (L1, L2, PS, CIRCLE, CROSS, then none) and the first held primary in that layer
// (UP, LEFT, RIGHT, DOWN, then PS and L3; L3 before PS in the L1 layer) picks the slot. An
// unmapped slot does not fall through to another layer. CROSS, CIRCLE and PS count as held
// when they are held on the other controller, if it is connected; `modFromOther` (optional)
// says whether that's where the deciding modifier came from.
uint8_t resolveSlot(Ctrl ctrl, const PadState &self, const PadState &other, bool *modFromOther = nullptr);

}  // namespace sm
