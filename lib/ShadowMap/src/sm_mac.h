// Controller (PS3 Navigation) Bluetooth addresses: the four allowed-controller slots and the
// text form "@SM MAC" and "@SM REFUSED" use. See WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md.
//
// A `mac` is most significant byte first, the order it's written in ("00:1A:2B:3C:4D:05").
// A `bdaddr` is least significant byte first, as the USB Host library stores it (Btd.disc_bdaddr).
#pragma once

#include <stdint.h>

namespace sm {

constexpr uint8_t kMacLen = 6;
constexpr uint8_t kMacTextBuf = 18;  // "HH:HH:HH:HH:HH:HH" and the terminator

// The allowed-controller slots, in EEPROM and list order. Slot i is for role i & 1 (Ctrl::F, Ctrl::D).
enum class MacSlot : uint8_t { F = 0, D = 1, F2 = 2, D2 = 3 };
constexpr uint8_t kMacSlots = 4;

// "HH:HH:HH:HH:HH:HH", hex digits in either case, to bytes. False, with `out` untouched, for
// any other text and for an address that can't be a controller's (macUsable()).
bool parseMac(const char *text, uint8_t out[kMacLen]);
// Upper case, two digits a byte.
void formatMac(const uint8_t mac[kMacLen], char out[kMacTextBuf]);
// False for all zero (Btd.disc_bdaddr before any connection) and all FF (erased EEPROM).
bool macUsable(const uint8_t mac[kMacLen]);
// Whether `mac` is the address the library holds as `bdaddr`.
bool macMatches(const uint8_t mac[kMacLen], const uint8_t bdaddr[kMacLen]);
// "F", "D", "F2", "D2" to the slot index; false for anything else.
bool parseMacSlot(const char *token, uint8_t *index);
// The slot index's token (index < kMacSlots).
void macSlotToken(uint8_t index, char out[3]);

}  // namespace sm
