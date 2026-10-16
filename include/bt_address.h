// A controller's Bluetooth address as text, the form SHADOW compares with the addresses in
// include/controller_macs.h when a controller connects: "AA:BB:CC:DD:EE:FF".
#pragma once

#include <stdint.h>

constexpr uint8_t kBtAddressBuf = 18;  // 17 characters and the terminator

// `bdaddr` is least significant byte first, as the USB Host library stores it
// (Btd.disc_bdaddr).
inline char btHexDigit(uint8_t d) { return d < 10 ? '0' + d : 'A' + (d - 10); }

// Every byte is two digits: the last one used to lose its leading zero ("...:5"), so an
// address entered as "...:05" in controller_macs.h never matched.
inline void formatBtAddress(const uint8_t *bdaddr, char *out) {
    uint8_t n = 0;
    for (int8_t i = 5; i >= 0; --i) {
        out[n++] = btHexDigit(bdaddr[i] >> 4);
        out[n++] = btHexDigit(bdaddr[i] & 0x0F);
        if (i > 0)
            out[n++] = ':';
    }
    out[n] = '\0';
}
