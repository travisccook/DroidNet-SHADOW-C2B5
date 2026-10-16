#include "sm_mac.h"

#include <string.h>

namespace sm {

namespace {

int8_t hexDigit(char ch) {
    if (ch >= '0' && ch <= '9')
        return static_cast<int8_t>(ch - '0');
    if (ch >= 'A' && ch <= 'F')
        return static_cast<int8_t>(ch - 'A' + 10);
    if (ch >= 'a' && ch <= 'f')
        return static_cast<int8_t>(ch - 'a' + 10);
    return -1;
}

char hexChar(uint8_t v) { return static_cast<char>(v < 10 ? '0' + v : 'A' + (v - 10)); }

}  // namespace

bool macUsable(const uint8_t mac[kMacLen]) {
    bool zero = true;
    bool ones = true;
    for (uint8_t i = 0; i < kMacLen; ++i) {
        zero = zero && mac[i] == 0x00;
        ones = ones && mac[i] == 0xFF;
    }
    return !zero && !ones;
}

bool parseMac(const char *text, uint8_t out[kMacLen]) {
    uint8_t mac[kMacLen];
    for (uint8_t i = 0; i < kMacLen; ++i) {
        // Each check stops at the first character that doesn't fit, so nothing past the
        // string's end is read.
        const char *p = text + 3 * i;
        const int8_t hi = hexDigit(p[0]);
        if (hi < 0)
            return false;
        const int8_t lo = hexDigit(p[1]);
        if (lo < 0)
            return false;
        if (p[2] != (i + 1 < kMacLen ? ':' : '\0'))
            return false;
        mac[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    if (!macUsable(mac))
        return false;
    memcpy(out, mac, kMacLen);
    return true;
}

void formatMac(const uint8_t mac[kMacLen], char out[kMacTextBuf]) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kMacLen; ++i) {
        out[n++] = hexChar(static_cast<uint8_t>(mac[i] >> 4));
        out[n++] = hexChar(static_cast<uint8_t>(mac[i] & 0x0F));
        if (i + 1 < kMacLen)
            out[n++] = ':';
    }
    out[n] = '\0';
}

bool macMatches(const uint8_t mac[kMacLen], const uint8_t bdaddr[kMacLen]) {
    for (uint8_t i = 0; i < kMacLen; ++i) {
        if (mac[i] != bdaddr[kMacLen - 1 - i])
            return false;
    }
    return true;
}

bool parseMacSlot(const char *token, uint8_t *index) {
    if (token[0] != 'F' && token[0] != 'D')
        return false;
    const uint8_t role = token[0] == 'F' ? 0 : 1;
    if (token[1] == '\0') {
        *index = role;
        return true;
    }
    if (token[1] == '2' && token[2] == '\0') {
        *index = static_cast<uint8_t>(role + 2);
        return true;
    }
    return false;
}

void macSlotToken(uint8_t index, char out[3]) {
    out[0] = (index & 1) == 0 ? 'F' : 'D';
    out[1] = index >= 2 ? '2' : '\0';
    out[2] = '\0';
}

}  // namespace sm
