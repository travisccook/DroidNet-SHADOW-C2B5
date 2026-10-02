#include "sm_slots.h"

#include "sm_pgm.h"

namespace sm {

namespace {

const char kCtrlTokens[kCtrlCount][2] PROGMEM = {"F", "D"};
const char kModTokens[kModCount][7] PROGMEM = {"NONE", "L1", "L2", "PS", "CIRCLE", "CROSS"};
const char kBtnTokens[kBtnCount][6] PROGMEM = {"UP", "RIGHT", "DOWN", "LEFT", "L3", "PS"};
const uint8_t kModLen[kModCount] PROGMEM = {4, 2, 2, 2, 6, 5};
const uint8_t kBtnLen[kBtnCount] PROGMEM = {2, 5, 4, 4, 2, 2};
const char kReservedPsGate[] PROGMEM = "psgate";
const char kReservedOverSpeed[] PROGMEM = "overspeed";
const char kReservedStick[] PROGMEM = "stick";

// The token `s` spells, if any. Only a token with the same first character is compared in full.
template <uint8_t N, uint8_t W>
bool parseToken(const char *s, const char (&tokens)[N][W], uint8_t *out) {
    const char first = s[0];
    for (uint8_t i = 0; i < N; ++i) {
        if (static_cast<char>(pgm_read_byte(&tokens[i][0])) == first && strcmp_P(s, tokens[i]) == 0) {
            *out = i;
            return true;
        }
    }
    return false;
}

uint8_t textLen(const SlotParts &p) {
    return static_cast<uint8_t>(3 + pgm_read_byte(&kModLen[p.mod]) + pgm_read_byte(&kBtnLen[p.btn]) + (p.tap != 0 ? 1 : 0));
}

}  // namespace

Reserved slotReserved(uint8_t slot) {
    if (slot >= kSlotCount)
        return Reserved::No;
    return slotReserved(splitSlot(slot));
}

Reserved slotReserved(const SlotParts &p) {
    const Mod m = static_cast<Mod>(p.mod);
    const Btn b = static_cast<Btn>(p.btn);
    if (b == Btn::PS && m != Mod::L1 && m != Mod::L2)
        return Reserved::PsGate;
    if (p.ctrl == static_cast<uint8_t>(Ctrl::F) && m == Mod::L1 && b == Btn::L3)
        return Reserved::OverSpeed;
    if (p.tap != static_cast<uint8_t>(Tap::SINGLE) && b == Btn::L3)
        return Reserved::Stick;
    return Reserved::No;
}

bool parseCtrl(const char *s, Ctrl *out) {
    uint8_t i = 0;
    if (!parseToken(s, kCtrlTokens, &i))
        return false;
    *out = static_cast<Ctrl>(i);
    return true;
}

bool parseMod(const char *s, Mod *out) {
    uint8_t i = 0;
    if (!parseToken(s, kModTokens, &i))
        return false;
    *out = static_cast<Mod>(i);
    return true;
}

bool parseBtn(const char *s, Btn *out, Tap *tap) {
    uint8_t i = 0;
    const size_t n = strlen(s);
    const char mark = n > 0 ? s[n - 1] : '\0';
    if (mark != '2' && mark != 'L') {
        // No button token ends in '2' or 'L' (UP RIGHT DOWN LEFT L3 PS), so only an unmarked one
        // can be a plain button.
        if (!parseToken(s, kBtnTokens, &i))
            return false;
        *out = static_cast<Btn>(i);
        if (tap != nullptr)
            *tap = Tap::SINGLE;
        return true;
    }
    // "UP2" / "UPL": a button token and the double-tap or long-press mark.
    char base[sizeof(kBtnTokens[0])];
    if (tap == nullptr || n < 2 || n > sizeof(base))
        return false;
    memcpy(base, s, n - 1);
    base[n - 1] = '\0';
    if (!parseToken(base, kBtnTokens, &i))
        return false;
    *out = static_cast<Btn>(i);
    *tap = mark == '2' ? Tap::DOUBLE : Tap::LONG;
    return true;
}

uint8_t slotTextLen(uint8_t slot) { return slot < kSlotCount ? textLen(splitSlot(slot)) : 0; }

uint8_t slotTextLen(const SlotParts &p) { return textLen(p); }

uint8_t modTextLen(uint8_t mod) { return pgm_read_byte(&kModLen[mod]); }

uint8_t writeSlotText(char *dst, const SlotParts &p) {
    const uint8_t ml = pgm_read_byte(&kModLen[p.mod]);
    const uint8_t bl = pgm_read_byte(&kBtnLen[p.btn]);
    dst[0] = p.ctrl != 0 ? 'D' : 'F';
    dst[1] = ' ';
    memcpy_P(dst + 2, kModTokens[p.mod], ml);
    dst[2 + ml] = ' ';
    memcpy_P(dst + 3 + ml, kBtnTokens[p.btn], bl);
    uint8_t len = static_cast<uint8_t>(3 + ml + bl);
    if (p.tap != 0)
        dst[len++] = p.tap == static_cast<uint8_t>(Tap::DOUBLE) ? '2' : 'L';
    return len;
}

uint8_t writeSlotText(char *dst, uint8_t slot) { return writeSlotText(dst, splitSlot(slot)); }

uint8_t formatSlot(char *dst, uint8_t cap, uint8_t slot) {
    if (slot >= kSlotCount || cap == 0)
        return 0;
    if (slotTextLen(slot) >= cap) {  // the text and its NUL must fit
        dst[0] = '\0';
        return 0;
    }
    const uint8_t len = writeSlotText(dst, slot);
    dst[len] = '\0';
    return len;
}

const char *reservedToken(Reserved r) {
    switch (r) {
        case Reserved::PsGate:
            return kReservedPsGate;
        case Reserved::OverSpeed:
            return kReservedOverSpeed;
        case Reserved::Stick:
            return kReservedStick;
        case Reserved::No:
            break;
    }
    return nullptr;
}

}  // namespace sm
