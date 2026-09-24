#include "sm_resolve.h"

namespace sm {

namespace {

struct ModBit {
    Mod mod;
    uint16_t bit;
    bool fromEitherController;
};

// Priority order, highest first. Mod::NONE is handled after the loop.
const ModBit kModOrder[] = {
    {Mod::L1, kHeldL1, false},       {Mod::L2, kHeldL2, false},         {Mod::PS, kHeldPs, true},
    {Mod::CIRCLE, kHeldCircle, true}, {Mod::CROSS, kHeldCross, true},
};

struct BtnBit {
    Btn btn;
    uint16_t bit;
};

// Directions first, then PS and L3. The L1 layer checks L3 before PS and every other layer PS
// before L3, which is what the old tables' row order did (dome L1+L3 is Front Holo Center,
// foot L2+PS is MahnaMahna), so no combo changes meaning.
const BtnBit kBtnOrder[] = {
    {Btn::UP, kHeldUp}, {Btn::LEFT, kHeldLeft}, {Btn::RIGHT, kHeldRight},
    {Btn::DOWN, kHeldDown}, {Btn::PS, kHeldPs}, {Btn::L3, kHeldL3},
};
const BtnBit kBtnOrderL1[] = {
    {Btn::UP, kHeldUp}, {Btn::LEFT, kHeldLeft}, {Btn::RIGHT, kHeldRight},
    {Btn::DOWN, kHeldDown}, {Btn::L3, kHeldL3}, {Btn::PS, kHeldPs},
};

bool modifierHeld(const ModBit &m, const PadState &self, const PadState &other) {
    if (self.held & m.bit)
        return true;
    return m.fromEitherController && other.connected && (other.held & m.bit);
}

uint8_t firstPrimaryInLayer(Ctrl ctrl, Mod mod, uint16_t held) {
    for (const BtnBit &b : (mod == Mod::L1 ? kBtnOrderL1 : kBtnOrder)) {
        if (mod == Mod::PS && b.btn == Btn::PS)
            continue;  // PS can't be its own modifier
        if (held & b.bit)
            return slotIndex(ctrl, mod, b.btn);
    }
    return kNoSlot;
}

}  // namespace

bool gateOpen(uint16_t held) {
    const uint16_t primaries = kHeldUp | kHeldDown | kHeldLeft | kHeldRight | kHeldL3;
    if (held & primaries)
        return true;
    return (held & kHeldPs) && (held & (kHeldL1 | kHeldL2));
}

uint8_t resolveSlot(Ctrl ctrl, const PadState &self, const PadState &other, bool *modFromOther) {
    if (modFromOther != nullptr)
        *modFromOther = false;
    if (!self.connected || !gateOpen(self.held))
        return kNoSlot;
    for (const ModBit &m : kModOrder) {
        if (!modifierHeld(m, self, other))
            continue;
        const uint8_t slot = firstPrimaryInLayer(ctrl, m.mod, self.held);
        if (slot != kNoSlot) {
            if (modFromOther != nullptr)
                *modFromOther = (self.held & m.bit) == 0;
            return slot;
        }
    }
    return firstPrimaryInLayer(ctrl, Mod::NONE, self.held);
}

}  // namespace sm
