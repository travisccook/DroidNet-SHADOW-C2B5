// The slot functions against the forms they replaced (copied here from 0a8b73f): the slot parts
// against / and %, slotReserved, formatSlot against its appendP version for every slot and buffer
// size, and the token parsers against theirs on every token, marked token and some junk.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_test_main.h>
#include <stdio.h>
#include <string.h>

using namespace sm;

namespace old {

Ctrl slotCtrl(uint8_t slot) { return static_cast<Ctrl>((slot % kSingleSlotCount) / kSlotsPerCtrl); }
Mod slotMod(uint8_t slot) { return static_cast<Mod>((slot % kSlotsPerCtrl) / kBtnCount); }
Btn slotBtn(uint8_t slot) { return static_cast<Btn>(slot % kBtnCount); }
Tap slotTap(uint8_t slot) { return static_cast<Tap>(slot / kSingleSlotCount); }
uint8_t doubleOf(uint8_t slot) { return static_cast<uint8_t>(slot % kSingleSlotCount + kSingleSlotCount); }
uint8_t longOf(uint8_t slot) { return static_cast<uint8_t>(slot % kSingleSlotCount + 2 * kSingleSlotCount); }

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

const char kCtrlTokens[kCtrlCount][2] = {"F", "D"};
const char kModTokens[kModCount][7] = {"NONE", "L1", "L2", "PS", "CIRCLE", "CROSS"};
const char kBtnTokens[kBtnCount][6] = {"UP", "RIGHT", "DOWN", "LEFT", "L3", "PS"};

template <uint8_t N, uint8_t W>
bool parseToken(const char *s, const char (&tokens)[N][W], uint8_t *out) {
    for (uint8_t i = 0; i < N; ++i) {
        if (strcmp(s, tokens[i]) == 0) {
            *out = i;
            return true;
        }
    }
    return false;
}

bool appendP(char *dst, uint8_t cap, uint8_t *len, const char *src) {
    const size_t n = strlen(src);
    if (*len + n + 1 > cap)
        return false;
    memcpy(dst + *len, src, n);
    *len = static_cast<uint8_t>(*len + n);
    dst[*len] = '\0';
    return true;
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
                    appendP(dst, cap, &len, " ") &&
                    appendP(dst, cap, &len, kModTokens[static_cast<uint8_t>(slotMod(slot))]) &&
                    appendP(dst, cap, &len, " ") &&
                    appendP(dst, cap, &len, kBtnTokens[static_cast<uint8_t>(slotBtn(slot))]) &&
                    (slotTap(slot) == Tap::SINGLE || appendP(dst, cap, &len, slotTap(slot) == Tap::DOUBLE ? "2" : "L"));
    if (!ok) {
        dst[0] = '\0';
        return 0;
    }
    return len;
}

}  // namespace old

static void test_slot_parts_match_divide_and_modulo() {
    for (uint16_t v = 0; v < 256; ++v) {
        const uint8_t slot = static_cast<uint8_t>(v);
        const SlotParts p = splitSlot(slot);
        TEST_ASSERT_EQUAL_UINT8(slot / kSingleSlotCount, p.tap);
        TEST_ASSERT_EQUAL_UINT8(slot % kSingleSlotCount, p.bank);
        TEST_ASSERT_TRUE(old::slotCtrl(slot) == slotCtrl(slot));
        TEST_ASSERT_TRUE(old::slotMod(slot) == slotMod(slot));
        TEST_ASSERT_TRUE(old::slotBtn(slot) == slotBtn(slot));
        TEST_ASSERT_TRUE(old::slotTap(slot) == slotTap(slot));
        TEST_ASSERT_EQUAL_UINT8(old::doubleOf(slot), doubleOf(slot));
        TEST_ASSERT_EQUAL_UINT8(old::longOf(slot), longOf(slot));
        TEST_ASSERT_TRUE(old::slotReserved(slot) == slotReserved(slot));
    }
}

static void test_format_slot_matches_for_every_slot_and_size() {
    for (uint16_t v = 0; v < 256; ++v) {
        const uint8_t slot = static_cast<uint8_t>(v);
        char want[24];
        old::formatSlot(want, sizeof(want), slot);
        TEST_ASSERT_EQUAL_UINT8(slot < kSlotCount ? strlen(want) : 0, slotTextLen(slot));
        for (uint8_t cap = 0; cap <= 17; ++cap) {
            char a[24], b[24];
            memset(a, '#', sizeof(a));
            memset(b, '#', sizeof(b));
            const uint8_t na = old::formatSlot(a, cap, slot);
            const uint8_t nb = formatSlot(b, cap, slot);
            char msg[32];
            snprintf(msg, sizeof(msg), "slot %u cap %u", static_cast<unsigned>(slot), static_cast<unsigned>(cap));
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(na, nb, msg);
            TEST_ASSERT_EQUAL_CHAR_MESSAGE(a[0], b[0], msg);  // untouched ('#') or a NUL, alike
            if (na > 0) {
                TEST_ASSERT_EQUAL_STRING_MESSAGE(a, b, msg);
                char c[24];
                memset(c, '#', sizeof(c));
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(na, writeSlotText(c, slot), msg);
                TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, c, na, msg);
                TEST_ASSERT_EQUAL_CHAR_MESSAGE('#', c[na], msg);  // no NUL
            }
        }
    }
}

namespace {

void checkParse(const char *s) {
    Ctrl c1 = Ctrl::D, c2 = Ctrl::D;
    TEST_ASSERT_EQUAL_MESSAGE(old::parseCtrl(s, &c1), parseCtrl(s, &c2), s);
    TEST_ASSERT_TRUE_MESSAGE(c1 == c2, s);
    Mod m1 = Mod::CROSS, m2 = Mod::CROSS;
    TEST_ASSERT_EQUAL_MESSAGE(old::parseMod(s, &m1), parseMod(s, &m2), s);
    TEST_ASSERT_TRUE_MESSAGE(m1 == m2, s);
    Btn b1 = Btn::PS, b2 = Btn::PS;
    Tap t1 = Tap::LONG, t2 = Tap::LONG;
    TEST_ASSERT_EQUAL_MESSAGE(old::parseBtn(s, &b1, &t1), parseBtn(s, &b2, &t2), s);
    TEST_ASSERT_TRUE_MESSAGE(b1 == b2 && t1 == t2, s);
    b1 = b2 = Btn::PS;
    TEST_ASSERT_EQUAL_MESSAGE(old::parseBtn(s, &b1, nullptr), parseBtn(s, &b2, nullptr), s);
    TEST_ASSERT_TRUE_MESSAGE(b1 == b2, s);
}

}  // namespace

static void test_parsers_match_on_tokens_and_junk() {
    static const char *const kTokens[] = {"F", "D", "NONE", "L1", "L2", "PS", "CIRCLE", "CROSS", "UP", "RIGHT", "DOWN",
                                          "LEFT", "L3"};
    char s[16];
    for (const char *t : kTokens) {
        checkParse(t);
        static const char *const kMarks[] = {"2", "L", "3", "X", "22", "2L", "L2", "l", " "};
        for (const char *mark : kMarks) {
            snprintf(s, sizeof(s), "%s%s", t, mark);
            checkParse(s);
        }
        // Lower case, and every one-character change at the front.
        snprintf(s, sizeof(s), "%s", t);
        for (char *p = s; *p != '\0'; ++p)
            *p = static_cast<char>(*p | 0x20);
        checkParse(s);
        for (int c = 1; c < 128; ++c) {
            snprintf(s, sizeof(s), "%c%s", c, t + 1);
            checkParse(s);
        }
    }
    static const char *const kJunk[] = {"", "UPX", "L", "2", "L32", "PSL", "RIGHT2", "RIGHTL", "up", "Up", "RIGHTXX",
                                        "CIRCLE2", "CROSSL", "UP22", "UPLL", "LEFTL2", "ABCDEFGH", "RIGHTRIGHT", "U",
                                        "R", "D2", "F2", " UP", "UP ", "-", "*"};
    for (const char *j : kJunk)
        checkParse(j);
    // Every two-character string.
    for (int a = 1; a < 128; ++a) {
        for (int b = 0; b < 128; ++b) {
            s[0] = static_cast<char>(a);
            s[1] = static_cast<char>(b);
            s[2] = '\0';
            checkParse(s);
        }
    }
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_slot_parts_match_divide_and_modulo);
    RUN_TEST(test_format_slot_matches_for_every_slot_and_size);
    RUN_TEST(test_parsers_match_on_tokens_and_junk);
    return UNITY_END();
}
