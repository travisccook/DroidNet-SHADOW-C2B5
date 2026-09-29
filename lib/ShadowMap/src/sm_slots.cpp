#include "sm_slots.h"

#include "sm_pgm.h"

namespace sm {

namespace {

const char kCtrlTokens[kCtrlCount][2] PROGMEM = {"F", "D"};
const char kModTokens[kModCount][7] PROGMEM = {"NONE", "L1", "L2", "PS", "CIRCLE", "CROSS"};
const char kBtnTokens[kBtnCount][6] PROGMEM = {"UP", "RIGHT", "DOWN", "LEFT", "L3", "PS"};
const char kReservedPsGate[] PROGMEM = "psgate";
const char kReservedOverSpeed[] PROGMEM = "overspeed";
const char kReservedStick[] PROGMEM = "stick";
const char kSpace[] PROGMEM = " ";
const char kDoubleMark[] PROGMEM = "2";
const char kLongMark[] PROGMEM = "L";

template <uint8_t N, uint8_t W>
bool parseToken(const char *s, const char (&tokens)[N][W], uint8_t *out) {
    for (uint8_t i = 0; i < N; ++i) {
        if (strcmp_P(s, tokens[i]) == 0) {
            *out = i;
            return true;
        }
    }
    return false;
}

// Appends a flash string; returns false if it doesn't fit (leaving room for the NUL).
bool appendP(char *dst, uint8_t cap, uint8_t *len, PGM_P src) {
    const size_t n = strlen_P(src);
    if (*len + n + 1 > cap)
        return false;
    memcpy_P(dst + *len, src, n);
    *len = static_cast<uint8_t>(*len + n);
    dst[*len] = '\0';
    return true;
}

}  // namespace

Reserved slotReserved(uint8_t slot) {
    if (slot >= kSlotCount)
        return Reserved::No;
    const Mod m = slotMod(slot);
    if (slotBtn(slot) == Btn::PS && m != Mod::L1 && m != Mod::L2)
        return Reserved::PsGate;
    if (slotCtrl(slot) == Ctrl::F && m == Mod::L1 && slotBtn(slot) == Btn::L3)
        return Reserved::OverSpeed;
    if (slotTap(slot) != Tap::SINGLE && slotBtn(slot) == Btn::L3)
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
    if (parseToken(s, kBtnTokens, &i)) {
        *out = static_cast<Btn>(i);
        if (tap != nullptr)
            *tap = Tap::SINGLE;
        return true;
    }
    // "UP2" / "UPL": a button token and the double-tap or long-press mark.
    const size_t n = strlen(s);
    char base[sizeof(kBtnTokens[0])];
    if (tap == nullptr || n < 2 || n > sizeof(base))
        return false;
    const char mark = s[n - 1];
    if (mark != '2' && mark != 'L')
        return false;
    memcpy(base, s, n - 1);
    base[n - 1] = '\0';
    if (!parseToken(base, kBtnTokens, &i))
        return false;
    *out = static_cast<Btn>(i);
    *tap = mark == '2' ? Tap::DOUBLE : Tap::LONG;
    return true;
}

uint8_t formatSlot(char *dst, uint8_t cap, uint8_t slot) {
    if (slot >= kSlotCount || cap == 0)
        return 0;
    uint8_t len = 0;
    dst[0] = '\0';
    const bool ok = appendP(dst, cap, &len, kCtrlTokens[static_cast<uint8_t>(slotCtrl(slot))]) &&
                    appendP(dst, cap, &len, kSpace) &&
                    appendP(dst, cap, &len, kModTokens[static_cast<uint8_t>(slotMod(slot))]) &&
                    appendP(dst, cap, &len, kSpace) &&
                    appendP(dst, cap, &len, kBtnTokens[static_cast<uint8_t>(slotBtn(slot))]) &&
                    (slotTap(slot) == Tap::SINGLE ||
                     appendP(dst, cap, &len, slotTap(slot) == Tap::DOUBLE ? kDoubleMark : kLongMark));
    if (!ok) {
        dst[0] = '\0';
        return 0;
    }
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
