# Controller Addresses at Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let anyone flash a published SHADOW `.hex` with DroidNet and then allow their own PS3 Navigation controllers at runtime (`@SM MAC`), with SHADOW reporting refused controllers (`@SM REFUSED`).

**Architecture:** Address helpers (parse, format, compare, slot tokens) go in a new `lib/ShadowMap/src/sm_mac.*`. `MapStore` gains four 9-byte EEPROM records after the button map, written by its existing one-byte-a-pass job engine, plus the build's addresses and the lookups `macFor()` / `macAllows()`. `Protocol` gains the `MAC` verb and the `REFUSED` event. `main.cpp` asks `mapStore.macAllows()` instead of comparing `String`s, reports refusals, and drops a connected controller whose address is revoked. A release script builds the `.hex` from a clean checkout and checks no local address is in it.

**Tech Stack:** C++ (avr-gcc via PlatformIO `atmelavr@5.1.0`, host g++ for `native`), Unity tests on `native` and `simavr`, Python 3 for the release check.

**Spec:** `WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md`. Three implementation choices differ from its wording; Task 8 updates the spec to match:

1. The spec's `ControllerList` is `MapStore`'s `macFor()` / `macAllows()` / `setBuiltInMac()`. `MapStore` owns the EEPROM writer and its busy rule, and `Protocol` already holds a `MapStore&`, so no constructor anywhere changes.
2. `parseMac()` / `formatMac()` live in `lib/ShadowMap/src/sm_mac.*`, not `include/bt_address.h`: the protocol needs them and the library can't include the project's `include/`. `include/bt_address.h` (`formatBtAddress()`, least significant byte first) stays for `main.cpp`'s debug print.
3. `REFUSED` on the WCB port waits in its one-entry slot until the port has room, like `CTL`, instead of being dropped.

## Global Constraints

- Every mapping pass stays under 1 ms on the emulated ATmega2560; the `test_avr_timing_*` suites fail at 1 ms (`kBudgetUs = 1000`).
- Protocol stays `v4`: `HELLO`/`READY` and every existing request, reply and event are byte-for-byte unchanged, and `python3 -m unittest tools/shadowmap/test_shadowmap.py tools/shadowmap/test_transcripts.py` keeps passing with the existing goldens.
- Everything SHADOW sends on Serial1 after `setup()` goes through `sm::TxQueue` (the `WcbPort`); nothing new calls `Serial1.print`.
- EEPROM: the records are 4 x 9 bytes at `MapStore::kFootprint` (3952) to 3987; `DEFAULTS` and the generation never touch them; nothing else is written past 3987.
- `lib/ShadowMap` has no Arduino dependency (host build), and on AVR every string literal in a test lives in SRAM: keep each new test program small (the emulated Mega has 8 KB), and use `PSTR` labels in timing suites.
- No real controller or dongle address anywhere in the repository or a release `.hex`. Tests use made-up addresses: `00:1A:2B:3C:4D:05` and `12:34:56:78:9A:BC`.
- Build and test with `~/.platformio/penv/bin/pio` (`pio` isn't on PATH here). Never run two `pio test` commands at once in this checkout.
- Commit straight to `main` (the owner works without branches), one commit per task, each message ending with the line `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Push after each task's tests pass.
- Match the surrounding C++: 4-space indent, `//` comments that say why, `static_cast`, no STL in `lib/` or tests (they run on AVR).

## Review Focus

1. **Typing in DroidNet's Serial tab.** Lower case, a missing colon, dashes, no separators, a stray space, a trailing CR: SHADOW must answer `ERR addr` or `ERR args`, write nothing, and accept lower case (echoing upper case). Task 3, `test_typed_variants`.
2. **Nobody reading SHADOW's USB port.** A refusal while the USB port has no room must neither block nor be lost: the line waits and goes out when there's room. Task 4, `test_refused_waits_for_a_full_console`.
3. **A write already running.** `MAC <S> <x>` during a `SET`'s EEPROM write, or during a `DUMP`, must answer `ERR busy MAC` and change nothing; `MAC <S>` (the single query) must still be answered. Task 3, `test_busy`.
4. **The USB port subscribed to EVENTS.** If the USB port sent `EVENTS 1`, a refusal must reach it once, not twice. Task 4, `test_refused_once_when_usb_has_events`.
5. **A release built from the working tree.** A `.hex` that contains a local address, as text in either case or as bytes in either order, must fail the release check. Task 7, `test_finds_text_and_bytes`.

---

### Task 1: Address helpers (`sm_mac`)

**Files:**

- Create: `lib/ShadowMap/src/sm_mac.h`
- Create: `lib/ShadowMap/src/sm_mac.cpp`
- Modify: `lib/ShadowMap/src/ShadowMap.h` (add the include)
- Test: `test/test_mac/test_mac.cpp`

**Interfaces:**

- Consumes: nothing new.
- Produces (namespace `sm`): `constexpr uint8_t kMacLen = 6;`, `constexpr uint8_t kMacTextBuf = 18;`, `constexpr uint8_t kMacSlots = 4;`, `enum class MacSlot : uint8_t { F = 0, D = 1, F2 = 2, D2 = 3 };`, `bool parseMac(const char *text, uint8_t out[kMacLen]);`, `void formatMac(const uint8_t mac[kMacLen], char out[kMacTextBuf]);`, `bool macUsable(const uint8_t mac[kMacLen]);`, `bool macMatches(const uint8_t mac[kMacLen], const uint8_t bdaddr[kMacLen]);`, `bool parseMacSlot(const char *token, uint8_t *index);`, `void macSlotToken(uint8_t index, char out[3]);`. A `mac` is most significant byte first (the order it's written in); a `bdaddr` is least significant byte first (`Btd.disc_bdaddr`). Slot index `i` is for role `i & 1` (`Ctrl::F == 0`, `Ctrl::D == 1`).

- [ ] **Step 1: Write the failing test**

Create `test/test_mac/test_mac.cpp`:

```cpp
// Checks the controller-address helpers in lib/ShadowMap/src/sm_mac.h.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_test_main.h>

using namespace sm;

namespace {
const uint8_t kMacA[kMacLen] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x05};
}

static void test_parse_either_case() {
    uint8_t mac[kMacLen];
    TEST_ASSERT_TRUE(parseMac("00:1a:2B:3c:4D:05", mac));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mac, kMacLen);
}

static void test_parse_refuses_other_text() {
    const char *const bad[] = {"",
                               "00:1A:2B:3C:4D",
                               "00:1A:2B:3C:4D:05:",
                               "00:1A:2B:3C:4D:5",
                               "00:1A:2B:3C:4D:050",
                               "00-1A-2B-3C-4D-05",
                               "001A2B3C4D05",
                               "00:1A:2B:3C:4D:0G",
                               " 00:1A:2B:3C:4D:05",
                               "XX:XX:XX:XX:XX:XX",
                               "00:00:00:00:00:00",
                               "ff:ff:ff:ff:ff:ff"};
    uint8_t mac[kMacLen] = {1, 2, 3, 4, 5, 6};
    for (const char *s : bad)
        TEST_ASSERT_FALSE_MESSAGE(parseMac(s, mac), s);
    const uint8_t untouched[kMacLen] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(untouched, mac, kMacLen);
}

static void test_format_upper_case_two_digits_a_byte() {
    char text[kMacTextBuf];
    formatMac(kMacA, text);
    TEST_ASSERT_EQUAL_STRING("00:1A:2B:3C:4D:05", text);
}

static void test_usable() {
    const uint8_t zeros[kMacLen] = {0, 0, 0, 0, 0, 0};
    const uint8_t ones[kMacLen] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(macUsable(zeros));
    TEST_ASSERT_FALSE(macUsable(ones));
    TEST_ASSERT_TRUE(macUsable(kMacA));
}

static void test_matches_the_libraries_byte_order() {
    const uint8_t bdaddr[kMacLen] = {0x05, 0x4D, 0x3C, 0x2B, 0x1A, 0x00};  // as Btd.disc_bdaddr holds it
    TEST_ASSERT_TRUE(macMatches(kMacA, bdaddr));
    TEST_ASSERT_FALSE(macMatches(kMacA, kMacA));
}

static void test_slot_tokens() {
    const char *const names[] = {"F", "D", "F2", "D2"};
    for (uint8_t i = 0; i < kMacSlots; ++i) {
        uint8_t index = 0xFF;
        TEST_ASSERT_TRUE_MESSAGE(parseMacSlot(names[i], &index), names[i]);
        TEST_ASSERT_EQUAL_UINT8(i, index);
        char token[3];
        macSlotToken(i, token);
        TEST_ASSERT_EQUAL_STRING(names[i], token);
    }
    const char *const bad[] = {"", "f", "d2", "F3", "FD", "2", "D22", "F2 "};
    for (const char *s : bad) {
        uint8_t index = 0;
        TEST_ASSERT_FALSE_MESSAGE(parseMacSlot(s, &index), s);
    }
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_either_case);
    RUN_TEST(test_parse_refuses_other_text);
    RUN_TEST(test_format_upper_case_two_digits_a_byte);
    RUN_TEST(test_usable);
    RUN_TEST(test_matches_the_libraries_byte_order);
    RUN_TEST(test_slot_tokens);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_mac`
Expected: build error, `'parseMac' was not declared` (or `kMacLen`): the helpers don't exist.

- [ ] **Step 3: Write the helpers**

Create `lib/ShadowMap/src/sm_mac.h`:

```cpp
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
```

Create `lib/ShadowMap/src/sm_mac.cpp`:

```cpp
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
```

In `lib/ShadowMap/src/ShadowMap.h`, add `#include "sm_mac.h"` after `#include "sm_line.h"`.

- [ ] **Step 4: Run it to verify it passes, natively and on the emulated Mega**

Run: `~/.platformio/penv/bin/pio test -e native -f test_mac && ~/.platformio/penv/bin/pio test -e simavr --without-uploading -f test_mac`
Expected: 6 tests PASS in each environment.

- [ ] **Step 5: Commit**

```bash
git add lib/ShadowMap/src/sm_mac.h lib/ShadowMap/src/sm_mac.cpp lib/ShadowMap/src/ShadowMap.h test/test_mac/test_mac.cpp
git commit -m "Controller addresses: parse, format and compare helpers (sm_mac)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
```

---

### Task 2: Controller-address records in `MapStore`

**Files:**

- Modify: `lib/ShadowMap/src/sm_store.h` (constants, `MacState`, new public methods, private members, two job kinds)
- Modify: `lib/ShadowMap/src/sm_store.cpp` (record CRC and validity, `begin()`, `beginMac()`, `finishJob()`, lookups)
- Modify: `test/support/sm_fakes.h` (`FakeEeprom` backs the 36 record bytes; `poke()`)
- Test: `test/test_store_mac/test_store_mac.cpp`

**Interfaces:**

- Consumes (Task 1): `kMacLen`, `kMacSlots`, `macUsable()`, `macMatches()`.
- Produces (class `sm::MapStore`): `static constexpr uint8_t kMacRecordSize = 9;`, `static constexpr uint16_t kMacFootprint = 36;`, `static constexpr uint16_t kEepromUsed = 3988;`, `enum class MacState : uint8_t { NotSet = 0, Address = 1, None = 2 };`, `void setBuiltInMac(uint8_t index, const uint8_t *mac);`, `bool macFor(uint8_t index, uint8_t out[kMacLen], Source *src = nullptr) const;`, `bool macAllows(Ctrl role, const uint8_t bdaddr[kMacLen]) const;`, `uint8_t macChanges() const;`, `Result beginMac(uint8_t index, MacState state, const uint8_t *mac);`. `beginMac` returns `Result::Busy` while any write runs and `Result::BadArg` for `index >= kMacSlots` or an `Address` without a usable `mac`; on `Ok` the outcome comes from `takeCompletion()` as for every other write. Test helper: `FakeEeprom::poke(uint16_t addr, uint8_t value)`.

- [ ] **Step 1: Write the failing test**

Create `test/test_store_mac/test_store_mac.cpp`:

```cpp
// Checks MapStore's controller-address records (WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md, Storage).
#include <unity.h>

#include <ShadowMap.h>
#include <new>
#include <sm_fakes.h>
#define SM_TEST_OWN_SETUP
#include <sm_test_main.h>

using namespace sm;

SM_DEFINE_TEST_DEFAULTS

namespace {

const uint8_t kMacA[kMacLen] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x05};
const uint8_t kMacB[kMacLen] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
constexpr uint8_t kF = static_cast<uint8_t>(MacSlot::F);
constexpr uint8_t kD2 = static_cast<uint8_t>(MacSlot::D2);

struct Rig {
    FakeEeprom eeprom;
    MapStore store{eeprom, kTestDefaults, kTestDefaultCount};
};
Rig *rig = nullptr;

// A fresh MapStore over the same EEPROM, as after a reboot.
void reboot() {
    rig->store.~MapStore();
    new (&rig->store) MapStore(rig->eeprom, kTestDefaults, kTestDefaultCount);
    rig->store.begin();
}

MapStore::Completion run() {
    for (int i = 0; i < 2000 && rig->store.busy(); ++i) {
        rig->store.step();
        rig->eeprom.tick();
    }
    TEST_ASSERT_FALSE(rig->store.busy());
    return rig->store.takeCompletion();
}

void write(uint8_t index, MapStore::MacState state, const uint8_t *mac) {
    TEST_ASSERT_TRUE(rig->store.beginMac(index, state, mac) == MapStore::Result::Ok);
    TEST_ASSERT_TRUE(run() == MapStore::Completion::Done);
}

void reversed(const uint8_t *mac, uint8_t *bdaddr) {
    for (uint8_t i = 0; i < kMacLen; ++i)
        bdaddr[i] = mac[kMacLen - 1 - i];
}

uint16_t recordAddr(uint8_t index) {
    return static_cast<uint16_t>(MapStore::kFootprint + index * MapStore::kMacRecordSize);
}

}  // namespace

void setUp() {
    static Rig instance;
    instance.~Rig();
    new (&instance) Rig();
    rig = &instance;
    rig->store.begin();
}

void tearDown() { TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->eeprom.violations, "EEPROM touched while busy"); }

static void test_layout() {
    TEST_ASSERT_EQUAL_UINT16(3952, MapStore::kFootprint);
    TEST_ASSERT_EQUAL_UINT16(36, MapStore::kMacFootprint);
    TEST_ASSERT_EQUAL_UINT16(3988, MapStore::kEepromUsed);
}

static void test_fresh_eeprom_uses_the_build() {
    uint8_t mac[kMacLen];
    Source src = Source::Eeprom;
    TEST_ASSERT_FALSE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_TRUE(src == Source::Default);
    rig->store.setBuiltInMac(kF, kMacA);
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mac, kMacLen);
    TEST_ASSERT_TRUE(src == Source::Default);
    rig->store.setBuiltInMac(kF, nullptr);
    TEST_ASSERT_FALSE(rig->store.macFor(kF, mac));
}

static void test_record_overrides_the_build_and_survives_a_reboot() {
    rig->store.setBuiltInMac(kF, kMacA);
    write(kF, MapStore::MacState::Address, kMacB);
    uint8_t mac[kMacLen];
    Source src = Source::Default;
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacB, mac, kMacLen);
    TEST_ASSERT_TRUE(src == Source::Eeprom);
    reboot();
    rig->store.setBuiltInMac(kF, kMacA);
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacB, mac, kMacLen);
}

static void test_none_blocks_the_build() {
    rig->store.setBuiltInMac(kF, kMacA);
    write(kF, MapStore::MacState::None, nullptr);
    uint8_t mac[kMacLen];
    Source src = Source::Default;
    TEST_ASSERT_FALSE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_TRUE(src == Source::Eeprom);
    uint8_t bdaddr[kMacLen];
    reversed(kMacA, bdaddr);
    TEST_ASSERT_FALSE(rig->store.macAllows(Ctrl::F, bdaddr));
}

static void test_default_erases_the_record() {
    rig->store.setBuiltInMac(kF, kMacA);
    write(kF, MapStore::MacState::Address, kMacB);
    write(kF, MapStore::MacState::NotSet, nullptr);
    for (uint8_t i = 0; i < MapStore::kMacRecordSize; ++i)
        TEST_ASSERT_EQUAL_HEX8(0xFF, rig->eeprom.peek(static_cast<uint16_t>(recordAddr(kF) + i)));
    uint8_t mac[kMacLen];
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mac, kMacLen);
}

static void test_damaged_records_read_as_not_set() {
    rig->store.setBuiltInMac(kF, kMacA);
    write(kF, MapStore::MacState::Address, kMacB);
    rig->eeprom.poke(static_cast<uint16_t>(recordAddr(kF) + 3), 0x00);  // an address byte: the CRC fails
    reboot();
    rig->store.setBuiltInMac(kF, kMacA);
    uint8_t mac[kMacLen];
    Source src = Source::Eeprom;
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mac, kMacLen);
    TEST_ASSERT_TRUE(src == Source::Default);

    // An unknown state with a CRC that matches it still reads as not set.
    uint8_t rec[MapStore::kMacRecordSize] = {3, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0, 0};
    const uint8_t index = kF;
    const uint16_t crc = crc16Block(crc16Block(kCrc16Init, &index, 1), rec, 1 + kMacLen);
    rec[7] = static_cast<uint8_t>(crc & 0xFF);
    rec[8] = static_cast<uint8_t>(crc >> 8);
    for (uint8_t i = 0; i < MapStore::kMacRecordSize; ++i)
        rig->eeprom.poke(static_cast<uint16_t>(recordAddr(kF) + i), rec[i]);
    reboot();
    TEST_ASSERT_FALSE(rig->store.macFor(kF, mac, &src));
    TEST_ASSERT_TRUE(src == Source::Default);
}

static void test_power_cut_mid_write() {
    rig->store.setBuiltInMac(kF, kMacA);
    rig->eeprom.budget = 4;  // four bytes land, then the power goes
    TEST_ASSERT_TRUE(rig->store.beginMac(kF, MapStore::MacState::Address, kMacB) == MapStore::Result::Ok);
    TEST_ASSERT_TRUE(run() == MapStore::Completion::Failed);
    rig->eeprom.powerOn();
    reboot();
    rig->store.setBuiltInMac(kF, kMacA);
    uint8_t mac[kMacLen];
    TEST_ASSERT_TRUE(rig->store.macFor(kF, mac));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mac, kMacLen);  // the torn record reads as not set
}

static void test_defaults_leave_the_records_alone() {
    TEST_ASSERT_TRUE(rig->store.beginSet(kSlotFootL1Up, "custom") == MapStore::Result::Ok);  // formats the map
    TEST_ASSERT_TRUE(run() == MapStore::Completion::Done);
    write(kD2, MapStore::MacState::Address, kMacB);
    TEST_ASSERT_TRUE(rig->store.beginDefaults() == MapStore::Result::Ok);
    TEST_ASSERT_TRUE(run() == MapStore::Completion::Done);
    reboot();
    uint8_t mac[kMacLen];
    TEST_ASSERT_TRUE(rig->store.macFor(kD2, mac));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacB, mac, kMacLen);
}

static void test_allows_by_role() {
    write(kF, MapStore::MacState::Address, kMacA);
    rig->store.setBuiltInMac(kD2, kMacB);
    uint8_t a[kMacLen];
    uint8_t b[kMacLen];
    reversed(kMacA, a);
    reversed(kMacB, b);
    TEST_ASSERT_TRUE(rig->store.macAllows(Ctrl::F, a));
    TEST_ASSERT_FALSE(rig->store.macAllows(Ctrl::D, a));
    TEST_ASSERT_TRUE(rig->store.macAllows(Ctrl::D, b));
    TEST_ASSERT_FALSE(rig->store.macAllows(Ctrl::F, b));
    const uint8_t zeros[kMacLen] = {0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(rig->store.macAllows(Ctrl::F, zeros));  // empty slots match nothing
    TEST_ASSERT_FALSE(rig->store.macAllows(Ctrl::D, zeros));
}

static void test_changes_count_finished_writes() {
    const uint8_t before = rig->store.macChanges();
    write(kF, MapStore::MacState::Address, kMacA);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before + 1), rig->store.macChanges());
    write(kF, MapStore::MacState::None, nullptr);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before + 2), rig->store.macChanges());
}

static void test_busy_and_bad_arguments() {
    const uint8_t zeros[kMacLen] = {0, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(rig->store.beginMac(4, MapStore::MacState::Address, kMacA) == MapStore::Result::BadArg);
    TEST_ASSERT_TRUE(rig->store.beginMac(kF, MapStore::MacState::Address, zeros) == MapStore::Result::BadArg);
    TEST_ASSERT_TRUE(rig->store.beginMac(kF, MapStore::MacState::Address, nullptr) == MapStore::Result::BadArg);
    TEST_ASSERT_TRUE(rig->store.beginMac(kF, MapStore::MacState::Address, kMacA) == MapStore::Result::Ok);
    TEST_ASSERT_TRUE(rig->store.beginMac(kD2, MapStore::MacState::Address, kMacB) == MapStore::Result::Busy);
    TEST_ASSERT_TRUE(rig->store.beginSet(kSlotFootL1Up, "custom") == MapStore::Result::Busy);
    TEST_ASSERT_TRUE(run() == MapStore::Completion::Done);
}

static void test_rewriting_the_same_value_writes_nothing() {
    write(kF, MapStore::MacState::Address, kMacA);
    const uint16_t before = rig->store.writesIssued();
    write(kF, MapStore::MacState::Address, kMacA);
    TEST_ASSERT_EQUAL_UINT16(before, rig->store.writesIssued());
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_layout);
    RUN_TEST(test_fresh_eeprom_uses_the_build);
    RUN_TEST(test_record_overrides_the_build_and_survives_a_reboot);
    RUN_TEST(test_none_blocks_the_build);
    RUN_TEST(test_default_erases_the_record);
    RUN_TEST(test_damaged_records_read_as_not_set);
    RUN_TEST(test_power_cut_mid_write);
    RUN_TEST(test_defaults_leave_the_records_alone);
    RUN_TEST(test_allows_by_role);
    RUN_TEST(test_changes_count_finished_writes);
    RUN_TEST(test_busy_and_bad_arguments);
    RUN_TEST(test_rewriting_the_same_value_writes_nothing);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_store_mac`
Expected: build error, `'kMacRecordSize' is not a member of 'sm::MapStore'` (and `beginMac`, `poke`).

- [ ] **Step 3: Give `FakeEeprom` the record bytes**

In `test/support/sm_fakes.h`, class `FakeEeprom`:

1. Change `static constexpr uint16_t kSize = sm::MapStore::kFootprint;` to `static constexpr uint16_t kSize = sm::MapStore::kEepromUsed;  // the mapping and the controller-address records`.
2. In `peek()`, after `if (addr < kFlat) return mem[addr];`, add:
   ```cpp
           if (addr >= sm::MapStore::kFootprint)
               return macMem[addr - sm::MapStore::kFootprint];
   ```
3. In `cellFor()`, after `if (addr < kFlat) return &mem[addr];`, add:
   ```cpp
           if (addr >= sm::MapStore::kFootprint)
               return &macMem[addr - sm::MapStore::kFootprint];
   ```
4. In `erase()`, add `memset(macMem, 0xFF, sizeof(macMem));`.
5. After `uint8_t mem[kFlat];` add:
   ```cpp
       uint8_t macMem[sm::MapStore::kMacFootprint];  // the controller-address records
   ```
6. After `peek()`, add:
   ```cpp
       // Stores a byte directly, for tests that damage a record.
       void poke(uint16_t addr, uint8_t value) {
           uint8_t *cell = cellFor(addr);
           if (cell != nullptr)
               *cell = value;
       }
   ```
   (`cellFor` is private and declared later in the class; a member function can call it.)

- [ ] **Step 4: Add the records to `MapStore`**

In `lib/ShadowMap/src/sm_store.h`:

1. Add `#include "sm_mac.h"` after `#include "sm_eeprom.h"`.
2. In the file's top comment, after the slot lines, add:
   ```cpp
   //   4 controller addresses x 9 B        from base + 3952: state, address (most significant
   //                                       byte first), CRC16 over (index, state, address); see
   //                                       WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md. Independent of
   //                                       the header: no generation, so DEFAULTS leaves them.
   ```
3. In `class MapStore`'s public section, after `kMaxReadsPerStep`, add:
   ```cpp
       static constexpr uint8_t kMacRecordSize = 1 + kMacLen + 2;                          // state, address, CRC16
       static constexpr uint16_t kMacFootprint = static_cast<uint16_t>(kMacSlots) * kMacRecordSize;
       static constexpr uint16_t kEepromUsed = kFootprint + kMacFootprint;                 // 3988

       // A controller-address record's state. NotSet is never stored: an erased or damaged record
       // reads as it, and the slot uses the build's address.
       enum class MacState : uint8_t { NotSet = 0, Address = 1, None = 2 };
   ```
4. After `bool longPressMapped(uint8_t slot) const;`, add:
   ```cpp
       // The build's address for a controller slot (MacSlot), or nullptr (or an unusable address) for
       // none. Call from setup(), after begin().
       void setBuiltInMac(uint8_t index, const uint8_t *mac);

       // The address in force for a controller slot: its EEPROM record's, else the build's. False, with
       // `out` untouched, when the slot has no controller. `*src` is Eeprom when a record decides (an
       // address, or "no controller"), Default when the build does. Kept in RAM: never waits.
       bool macFor(uint8_t index, uint8_t out[kMacLen], Source *src = nullptr) const;

       // Whether the controller at `bdaddr` (Btd.disc_bdaddr, least significant byte first) may connect
       // as `role`: Ctrl::F checks slots F and F2, Ctrl::D slots D and D2.
       bool macAllows(Ctrl role, const uint8_t bdaddr[kMacLen]) const;

       // Counts finished controller-address writes, so a caller can tell the list may have changed.
       uint8_t macChanges() const { return macChanges_; }
   ```
5. After `Result beginSuffix(const char *suffix);`, add:
   ```cpp
       // A controller slot's record: an address, "no controller" (MacState::None, `mac` ignored), or
       // back to the build's (MacState::NotSet: the record is erased).
       Result beginMac(uint8_t index, MacState state, const uint8_t *mac);
   ```
6. Change `enum class JobKind : uint8_t { Header, Slot, SlotErase };` to `enum class JobKind : uint8_t { Header, Slot, SlotErase, Mac };`.
7. After `slotAddr()`, add:
   ```cpp
       uint16_t macAddr(uint8_t index) const {
           return static_cast<uint16_t>(base_ + kFootprint + static_cast<uint16_t>(index) * kMacRecordSize);
       }
       void loadMac(uint8_t index);  // its EEPROM record into RAM; the EEPROM must be ready
   ```
8. After `uint8_t longPresses_[...]`, add:
   ```cpp
       MacState macState_[kMacSlots] = {};  // each record, as loaded (NotSet: use builtIn_)
       uint8_t mac_[kMacSlots][kMacLen] = {};
       bool builtInSet_[kMacSlots] = {};
       uint8_t builtIn_[kMacSlots][kMacLen] = {};
       uint8_t macChanges_ = 0;
   ```

In `lib/ShadowMap/src/sm_store.cpp`:

1. In the anonymous namespace, after `slotRecordValid()`, add:
   ```cpp
   uint16_t macCrc(uint8_t index, const uint8_t *rec) {
       return crc16Block(crc16Block(kCrc16Init, &index, 1), rec, 1 + kMacLen);
   }

   // A controller-address record: state Address with a usable address, or state None, under its CRC.
   // Anything else (erased, torn, unknown state) reads as not set.
   bool macRecordValid(const uint8_t *rec, uint8_t index) {
       const uint8_t state = rec[0];
       if (state != static_cast<uint8_t>(MapStore::MacState::Address) &&
           state != static_cast<uint8_t>(MapStore::MacState::None))
           return false;
       if (get16(rec + 1 + kMacLen) != macCrc(index, rec))
           return false;
       return state == static_cast<uint8_t>(MapStore::MacState::None) || macUsable(rec + 1);
   }
   ```
2. In `MapStore::begin()`, at the very start (before the header loop, so it runs even when the map is unformatted), add:
   ```cpp
       for (uint8_t i = 0; i < kMacSlots; ++i) {
           while (!io_.ready()) {
           }
           loadMac(i);
       }
   ```
3. After `MapStore::loadTap()`, add:
   ```cpp
   void MapStore::loadMac(uint8_t index) {
       uint8_t rec[kMacRecordSize];
       io_.readBlock(macAddr(index), rec, kMacRecordSize);
       if (!macRecordValid(rec, index)) {
           macState_[index] = MacState::NotSet;
           return;
       }
       macState_[index] = static_cast<MacState>(rec[0]);
       memcpy(mac_[index], rec + 1, kMacLen);
   }

   void MapStore::setBuiltInMac(uint8_t index, const uint8_t *mac) {
       if (index >= kMacSlots)
           return;
       builtInSet_[index] = mac != nullptr && macUsable(mac);
       if (builtInSet_[index])
           memcpy(builtIn_[index], mac, kMacLen);
   }

   bool MapStore::macFor(uint8_t index, uint8_t out[kMacLen], Source *src) const {
       if (index >= kMacSlots)
           return false;
       const MacState state = macState_[index];
       if (src != nullptr)
           *src = state == MacState::NotSet ? Source::Default : Source::Eeprom;
       if (state == MacState::Address) {
           memcpy(out, mac_[index], kMacLen);
           return true;
       }
       if (state == MacState::None || !builtInSet_[index])
           return false;
       memcpy(out, builtIn_[index], kMacLen);
       return true;
   }

   bool MapStore::macAllows(Ctrl role, const uint8_t bdaddr[kMacLen]) const {
       uint8_t mac[kMacLen];
       for (uint8_t i = static_cast<uint8_t>(role); i < kMacSlots; i = static_cast<uint8_t>(i + 2)) {
           if (macFor(i, mac) && macMatches(mac, bdaddr))
               return true;
       }
       return false;
   }
   ```
4. After `MapStore::beginSuffix()`, add:
   ```cpp
   MapStore::Result MapStore::beginMac(uint8_t index, MacState state, const uint8_t *mac) {
       if (busy())
           return Result::Busy;
       if (index >= kMacSlots || (state == MacState::Address && (mac == nullptr || !macUsable(mac))))
           return Result::BadArg;
       if (state == MacState::NotSet) {
           memset(jobData_, 0xFF, kMacRecordSize);  // erased: reads as not set
       } else {
           memset(jobData_, 0, kMacRecordSize);
           jobData_[0] = static_cast<uint8_t>(state);
           if (state == MacState::Address)
               memcpy(jobData_ + 1, mac, kMacLen);
           put16(jobData_ + 1 + kMacLen, macCrc(index, jobData_));
       }
       completion_ = Completion::None;
       kind_ = JobKind::Mac;
       jobIndex_ = index;
       jobAddr_ = macAddr(index);
       jobLen_ = kMacRecordSize;
       jobPos_ = 0;
       retries_ = 0;
       phase_ = Phase::Write;
       return Result::Ok;
   }
   ```
5. In `MapStore::finishJob()`, after `phase_ = Phase::Idle;`, add:
   ```cpp
       if (kind_ == JobKind::Mac) {
           loadMac(jobIndex_);  // what's really there now, written or not (the EEPROM is idle: step() checked)
           ++macChanges_;
           completion_ = ok ? Completion::Done : Completion::Failed;
           return;
       }
   ```

`keyForSlot()` needs no change: its in-flight checks test `kind_ == JobKind::Slot` / `SlotErase`, so a MAC job never answers for a map slot.

- [ ] **Step 5: Run the new test and the whole native suite**

Run: `~/.platformio/penv/bin/pio test -e native -f test_store_mac`
Expected: 12 tests PASS.
Run: `~/.platformio/penv/bin/pio test -e native`
Expected: every suite PASS (the existing store and protocol suites now also read the 36 record bytes in `begin()`; no test counts those reads).

- [ ] **Step 6: Run both on the emulated Mega**

Run: `~/.platformio/penv/bin/pio test -e simavr --without-uploading -f test_store_mac`
Expected: 12 tests PASS. If the run hangs, the program ran out of SRAM: move the last four tests into `test/test_store_mac2/test_store_mac2.cpp` with the same `Rig`, `setUp()` and helpers.

- [ ] **Step 7: Commit**

```bash
git add lib/ShadowMap/src/sm_store.h lib/ShadowMap/src/sm_store.cpp test/support/sm_fakes.h test/test_store_mac
git commit -m "Store: four controller-address records after the map (beginMac, macFor, macAllows)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
```

---

### Task 3: The `MAC` request

**Files:**

- Modify: `lib/ShadowMap/src/sm_protocol.h` (`Op::Mac`, `Op::MacList`, `handleMac()`, `sendMacLine()`, `opMac_`)
- Modify: `lib/ShadowMap/src/sm_protocol.cpp` (dispatch, list pacing, replies, `addr` error)
- Test: `test/test_protocol_mac/test_protocol_mac.cpp`

**Interfaces:**

- Consumes (Task 1): `parseMac`, `formatMac`, `parseMacSlot`, `macSlotToken`, `kMacTextBuf`, `kMacSlots`. (Task 2): `MapStore::MacState`, `beginMac`, `macFor`.
- Produces (wire): requests `MAC`, `MAC <S>`, `MAC <S> <address|-|default>`; replies `MAC <S> <address|-> <eeprom|default>`, `OK MAC <S> <ADDRESS|-|default>`; errors `ERR args MAC`, `ERR addr MAC`, `ERR busy MAC`, `ERR eeprom MAC <S>`.

- [ ] **Step 1: Write the failing test**

Create `test/test_protocol_mac/test_protocol_mac.cpp`:

```cpp
// @SM MAC: the controller-address requests (WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md).
#include <sm_protocol_rig.h>

namespace {

// A request typed on the USB port (no CRC, as in DroidNet's Serial tab); returns the reply body.
const char *usb(const char *line) {
    static char body[96];
    const uint32_t before = rig->usb.log.count;
    TEST_ASSERT_TRUE(rig->protocol.handleLine(line, rig->usb));
    for (int i = 0; i < 2000 && rig->usb.log.count == before; ++i)
        pass();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before + 1, rig->usb.log.count, "no USB reply");
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), body, sizeof(body)));
    return body;
}

}  // namespace

static void test_get_an_empty_slot() {
    TEST_ASSERT_EQUAL_STRING("@SM MAC F - default", ask("@SM MAC F"));
    TEST_ASSERT_EQUAL_STRING("@SM MAC D2 - default", ask("@SM MAC D2"));
}

static void test_set_get_none_and_default() {
    TEST_ASSERT_EQUAL_STRING("@SM OK MAC F 00:1A:2B:3C:4D:05", ask("@SM MAC F 00:1A:2B:3C:4D:05"));
    TEST_ASSERT_EQUAL_STRING("@SM MAC F 00:1A:2B:3C:4D:05 eeprom", ask("@SM MAC F"));
    TEST_ASSERT_EQUAL_STRING("@SM OK MAC F -", ask("@SM MAC F -"));
    TEST_ASSERT_EQUAL_STRING("@SM MAC F - eeprom", ask("@SM MAC F"));
    TEST_ASSERT_EQUAL_STRING("@SM OK MAC F default", ask("@SM MAC F default"));
    TEST_ASSERT_EQUAL_STRING("@SM MAC F - default", ask("@SM MAC F"));
}

static void test_build_address_shows_as_default() {
    const uint8_t mac[kMacLen] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    rig->store.setBuiltInMac(static_cast<uint8_t>(MacSlot::D), mac);
    TEST_ASSERT_EQUAL_STRING("@SM MAC D 12:34:56:78:9A:BC default", ask("@SM MAC D"));
    TEST_ASSERT_EQUAL_STRING("@SM OK MAC D -", ask("@SM MAC D -"));
    TEST_ASSERT_EQUAL_STRING("@SM MAC D - eeprom", ask("@SM MAC D"));
}

static void test_list_sends_four_lines() {
    ask("@SM MAC D2 12:34:56:78:9A:BC");
    send("@SM MAC");
    await(4);
    char body[96];
    const char *const want[] = {"@SM MAC F - default", "@SM MAC D - default", "@SM MAC F2 - default",
                                "@SM MAC D2 12:34:56:78:9A:BC eeprom"};
    for (uint8_t k = 0; k < 4; ++k) {
        TEST_ASSERT_TRUE(stripCrc(rig->wcb.usb.last(static_cast<uint8_t>(3 - k)), body, sizeof(body)));
        TEST_ASSERT_EQUAL_STRING(want[k], body);
    }
}

static void test_typed_variants() {
    // Lower case is accepted and echoed in upper case.
    TEST_ASSERT_EQUAL_STRING("@SM OK MAC F2 00:1A:2B:3C:4D:05", usb("@SM MAC F2 00:1a:2b:3c:4d:05"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR addr MAC", usb("@SM MAC F 00-1A-2B-3C-4D-05"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR addr MAC", usb("@SM MAC F 001A2B3C4D05"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR addr MAC", usb("@SM MAC F 00:1A:2B:3C:4D"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR addr MAC", usb("@SM MAC F 00:00:00:00:00:00"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args MAC", usb("@SM MAC f 00:1A:2B:3C:4D:05"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args MAC", usb("@SM MAC F3"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args MAC", usb("@SM MAC F 00:1A:2B:3C:4D:05 x"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args MAC", usb("@SM MAC F  00:1A:2B:3C:4D:05"));  // two spaces
    // Nothing was written by the refused ones.
    TEST_ASSERT_EQUAL_STRING("@SM MAC F - default", usb("@SM MAC F"));
    TEST_ASSERT_EQUAL_STRING("@SM MAC F2 00:1A:2B:3C:4D:05 eeprom", usb("@SM MAC F2"));
}

static void test_crc_rules() {
    char line[96];
    // The WCB port needs the CRC; the USB port doesn't.
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM MAC F", rig->wcbPort));
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", await());
    withCrc("@SM MAC F", line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("@SM MAC F - default", usb(line));
}

static void test_busy() {
    // During a SET's EEPROM write: a MAC write is refused, the single query is still answered.
    send("@SM SET F L1 UP custom");
    TEST_ASSERT_TRUE(rig->store.busy());
    TEST_ASSERT_EQUAL_STRING("@SM ERR busy MAC", usb("@SM MAC F 00:1A:2B:3C:4D:05"));
    TEST_ASSERT_EQUAL_STRING("@SM MAC F - default", usb("@SM MAC F"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F L1 UP custom", await());

    // During a DUMP: the list and writes are refused.
    send("@SM DUMP");
    TEST_ASSERT_EQUAL_STRING("@SM ERR busy MAC", usb("@SM MAC"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR busy MAC", usb("@SM MAC D -"));
    for (int i = 0; i < 20000 && rig->protocol.opInProgress(); ++i)
        pass();
    TEST_ASSERT_FALSE(rig->protocol.opInProgress());
    TEST_ASSERT_EQUAL_STRING("@SM MAC D - default", usb("@SM MAC D"));
}

static void test_mac_is_a_known_verb() {
    // Old firmware answers "ERR verb MAC"; that's how a client tells it apart. This one never does.
    TEST_ASSERT_NOT_EQUAL(0, strcmp("@SM ERR verb MAC", ask("@SM MAC")));
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_get_an_empty_slot);
    RUN_TEST(test_set_get_none_and_default);
    RUN_TEST(test_build_address_shows_as_default);
    RUN_TEST(test_list_sends_four_lines);
    RUN_TEST(test_typed_variants);
    RUN_TEST(test_crc_rules);
    RUN_TEST(test_busy);
    RUN_TEST(test_mac_is_a_known_verb);
    return UNITY_END();
}
```

Note on `test_mac_is_a_known_verb`: `ask("@SM MAC")` awaits one line, the list's first; the next test's `setUp()` builds a fresh rig, so the other three don't matter.

- [ ] **Step 2: Run it to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_protocol_mac`
Expected: FAIL in `test_get_an_empty_slot`: `Expected '@SM MAC F - default' Was '@SM ERR verb MAC'`.

- [ ] **Step 3: Add the request**

In `lib/ShadowMap/src/sm_protocol.h`:

1. Add `#include "sm_mac.h"` after `#include "sm_battery.h"`.
2. Change the `Op` enum to `enum class Op : uint8_t { None, Set, Revert, Defaults, Prefix, Suffix, Dump, Sum, Mac, MacList };`.
3. After `bool sendSlotLine(uint8_t slot, ReplyPort &port);`, add:
   ```cpp
       bool handleMac(uint8_t argc, char *const *argv, ReplyPort &port);
       void sendMacLine(uint8_t index, ReplyPort &port);  // "@SM MAC <S> <address|-> <eeprom|default>"
   ```
4. After `uint32_t sumCrc_ = 0;`, add `uint8_t opMac_ = 0;  // MAC: the controller slot; MAC list: the next one`.

In `lib/ShadowMap/src/sm_protocol.cpp`:

1. In `handleLine()`, right after the `EVENTS` block (before `const bool isSet = ...`), add:
   ```cpp
       if (v0 == 'M' && strcmp_P(verb, PSTR("MAC")) == 0)
           return handleMac(argc, argv, port);
   ```
2. In `startOp()`'s `BadArg` case, change the code to name `addr` for MAC:
   ```cpp
           case MapStore::Result::BadArg:
               sendError(port,
                         op == Op::Prefix   ? PSTR("prefix")
                         : op == Op::Suffix ? PSTR("suffix")
                                            : PSTR("addr"),
                         verb, slot);
               return;
   ```
3. After `Protocol::startOp()`, add:
   ```cpp
   // MAC [<S> [<address|-|default>]]: the allowed controllers (WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md).
   // The list goes one line a pass, like DUMP; a slot's line is answered at once, like GET; a change
   // is a write, one at a time with the others.
   bool Protocol::handleMac(uint8_t argc, char *const *argv, ReplyPort &port) {
       const char *verb = argv[0];
       if (argc == 1) {
           if (op_ != Op::None) {
               sendError(port, PSTR("busy"), verb, kNoSlot);
               return true;
           }
           op_ = Op::MacList;
           opPort_ = &port;
           opMac_ = 0;
           return true;
       }
       uint8_t index = 0;
       if (argc > 3 || !parseMacSlot(argv[1], &index)) {
           sendError(port, PSTR("args"), verb, kNoSlot);
           return true;
       }
       if (argc == 2) {
           sendMacLine(index, port);  // fits: canSend(kMaxLine) was checked above
           return true;
       }
       const char *arg = argv[2];
       MapStore::MacState state = MapStore::MacState::Address;
       uint8_t mac[kMacLen] = {};
       if (strcmp(arg, "-") == 0)
           state = MapStore::MacState::None;
       else if (strcmp_P(arg, PSTR("default")) == 0)
           state = MapStore::MacState::NotSet;
       else if (!parseMac(arg, mac)) {
           sendError(port, PSTR("addr"), verb, kNoSlot);
           return true;
       }
       if (op_ != Op::None) {
           sendError(port, PSTR("busy"), verb, kNoSlot);
           return true;
       }
       const MapStore::Result r = store_.beginMac(index, state, mac);
       if (r == MapStore::Result::Ok) {
           opMac_ = index;
           if (state == MapStore::MacState::Address)
               formatMac(mac, opArg_);  // upper case, however it was typed
           else
               strcpy_P(opArg_, state == MapStore::MacState::None ? PSTR("-") : PSTR("default"));
       }
       startOp(Op::Mac, port, r, verb, kNoSlot);
       return true;
   }

   void Protocol::sendMacLine(uint8_t index, ReplyPort &port) {
       uint8_t mac[kMacLen];
       Source src = Source::Default;
       const bool has = store_.macFor(index, mac, &src);
       char slot[3];
       macSlotToken(index, slot);
       Out out;
       out.p(PSTR("@SM MAC ")).s(slot).c(' ');
       if (has) {
           char text[kMacTextBuf];
           formatMac(mac, text);
           out.s(text);
       } else {
           out.c('-');
       }
       out.c(' ').p(sourceToken(src));
       emit(out, port);
   }
   ```
   (`static_assert(kOpArgBuf >= kMacTextBuf, ...)` holds: 32 >= 18.)
4. In `Protocol::service()`, after the `Op::Dump` block, add:
   ```cpp
       if (op_ == Op::MacList) {
           if (!opPort_->canSend(kMaxLine))
               return;
           sendMacLine(opMac_, *opPort_);
           if (++opMac_ >= kMacSlots)
               op_ = Op::None;
           return;
       }
   ```
5. In `Protocol::sendOpReply()`, add `case Op::Mac: verb = PSTR("MAC"); break;` to the switch. In the failure branch, after the `Set`/`Revert` slot line, add:
   ```cpp
           if (op_ == Op::Mac) {
               char slot[3];
               macSlotToken(opMac_, slot);
               out.c(' ').s(slot);
           }
   ```
   In the success branch, before `else out.c(' ').s(opArg_);`, add:
   ```cpp
       else if (op_ == Op::Mac) {
           char slot[3];
           macSlotToken(opMac_, slot);
           out.c(' ').s(slot).c(' ').s(opArg_);
       }
   ```

- [ ] **Step 4: Run it to verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_protocol_mac`
Expected: 8 tests PASS.

- [ ] **Step 5: Run the native suite, the Python contract tests and the emulated Mega**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: every suite PASS.
Run: `python3.12 -m unittest tools/shadowmap/test_shadowmap.py tools/shadowmap/test_transcripts.py`
Expected: OK (the goldens are unchanged). Use Python 3.12 or newer: `test_cli_set_raw_text_starting_with_a_dash` fails on Python 3.11 for a reason unrelated to this work.
Run: `~/.platformio/penv/bin/pio test -e simavr --without-uploading -f test_protocol_mac`
Expected: 8 tests PASS. If it hangs (SRAM), move `test_typed_variants` and `test_busy` into `test/test_protocol_mac2/test_protocol_mac2.cpp` with the same `usb()` helper.

- [ ] **Step 6: Commit**

```bash
git add lib/ShadowMap/src/sm_protocol.h lib/ShadowMap/src/sm_protocol.cpp test/test_protocol_mac
git commit -m "@SM MAC: list, read and set the allowed controllers (still protocol v4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
```

---

### Task 4: The `REFUSED` event

**Files:**

- Modify: `lib/ShadowMap/src/sm_protocol.h` (`setConsole()`, `noteRefused()`, members, `servicePads()` returns `bool`)
- Modify: `lib/ShadowMap/src/sm_protocol.cpp`
- Test: `test/test_refused/test_refused.cpp`

**Interfaces:**

- Consumes (Task 1): `kMacLen`, `formatMac`, `kMacTextBuf`. `Ctrl` from `sm_slots.h`.
- Produces: `void Protocol::setConsole(ReplyPort &port);`, `void Protocol::noteRefused(Ctrl role, const uint8_t bdaddr[kMacLen]);`. Wire: `@SM REFUSED <F|D> <ADDRESS> *<crc>`. `service()` sends at most one event line a call: `CTL` first, else `REFUSED` (to the console first, the WCB event port on a later call).

- [ ] **Step 1: Write the failing test**

Create `test/test_refused/test_refused.cpp`:

```cpp
// @SM REFUSED: a paired controller that isn't allowed tried to connect.
#include <sm_protocol_rig.h>

namespace {

const uint8_t kBdaddrA[kMacLen] = {0x05, 0x4D, 0x3C, 0x2B, 0x1A, 0x00};  // 00:1A:2B:3C:4D:05, as Btd holds it
const uint8_t kBdaddrB[kMacLen] = {0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12};  // 12:34:56:78:9A:BC

void passes(int n) {
    for (int i = 0; i < n; ++i)
        pass();
}

const char *usbLast() {
    static char body[96];
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), body, sizeof(body)));
    return body;
}

}  // namespace

static void test_refused_goes_to_the_console_without_events() {
    rig->protocol.setConsole(rig->usb);
    rig->protocol.noteRefused(Ctrl::F, kBdaddrA);
    passes(3);
    TEST_ASSERT_EQUAL_UINT32(1, rig->usb.log.count);
    TEST_ASSERT_EQUAL_STRING("@SM REFUSED F 00:1A:2B:3C:4D:05", usbLast());
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.usb.count);  // the WCB port didn't ask for events
}

static void test_refused_goes_to_the_wcb_with_events() {
    rig->protocol.setConsole(rig->usb);
    TEST_ASSERT_EQUAL_STRING("@SM OK EVENTS 1", ask("@SM EVENTS 1"));
    rig->protocol.noteRefused(Ctrl::D, kBdaddrB);
    passes(6);
    TEST_ASSERT_EQUAL_STRING("@SM REFUSED D 12:34:56:78:9A:BC", usbLast());
    TEST_ASSERT_EQUAL_STRING("@SM REFUSED D 12:34:56:78:9A:BC", await(0));
}

static void test_newest_refusal_wins() {
    rig->protocol.setConsole(rig->usb);
    rig->protocol.noteRefused(Ctrl::F, kBdaddrA);
    rig->protocol.noteRefused(Ctrl::D, kBdaddrB);
    passes(3);
    TEST_ASSERT_EQUAL_UINT32(1, rig->usb.log.count);
    TEST_ASSERT_EQUAL_STRING("@SM REFUSED D 12:34:56:78:9A:BC", usbLast());
}

static void test_refused_waits_for_a_full_console() {
    rig->protocol.setConsole(rig->usb);
    rig->usb.open = false;  // no room on the USB port
    rig->protocol.noteRefused(Ctrl::F, kBdaddrA);
    passes(5);
    TEST_ASSERT_EQUAL_UINT32(0, rig->usb.log.count);
    rig->usb.open = true;
    passes(2);
    TEST_ASSERT_EQUAL_UINT32(1, rig->usb.log.count);
    TEST_ASSERT_EQUAL_STRING("@SM REFUSED F 00:1A:2B:3C:4D:05", usbLast());
}

static void test_refused_once_when_usb_has_events() {
    rig->protocol.setConsole(rig->usb);
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM EVENTS 1", rig->usb));  // USB is the event port
    passes(2);
    const uint32_t before = rig->usb.log.count;
    rig->protocol.noteRefused(Ctrl::F, kBdaddrA);
    passes(6);
    TEST_ASSERT_EQUAL_UINT32(before + 1, rig->usb.log.count);
}

static void test_no_console_no_events_sends_nothing() {
    rig->protocol.noteRefused(Ctrl::F, kBdaddrA);
    passes(3);
    TEST_ASSERT_EQUAL_UINT32(0, rig->usb.log.count);
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.usb.count);
}

static void test_one_event_line_a_pass() {
    rig->protocol.setConsole(rig->usb);
    TEST_ASSERT_EQUAL_STRING("@SM OK EVENTS 1", ask("@SM EVENTS 1"));
    const uint32_t wcbBefore = rig->wcb.usb.count;
    rig->protocol.setPads(true, false);  // a CTL event is due
    rig->protocol.noteRefused(Ctrl::D, kBdaddrB);
    rig->protocol.service();
    // The first service() sends the CTL only.
    TEST_ASSERT_EQUAL_UINT32(0, rig->usb.log.count);
    rig->protocol.service();
    TEST_ASSERT_EQUAL_UINT32(1, rig->usb.log.count);  // then REFUSED to the console
    passes(4);
    TEST_ASSERT_EQUAL_UINT32(wcbBefore + 2, rig->wcb.usb.count);  // CTL and REFUSED on the WCB
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_refused_goes_to_the_console_without_events);
    RUN_TEST(test_refused_goes_to_the_wcb_with_events);
    RUN_TEST(test_newest_refusal_wins);
    RUN_TEST(test_refused_waits_for_a_full_console);
    RUN_TEST(test_refused_once_when_usb_has_events);
    RUN_TEST(test_no_console_no_events_sends_nothing);
    RUN_TEST(test_one_event_line_a_pass);
    return UNITY_END();
}
```

`await(0)` returns the Pi's latest line without waiting for a new one (the rig's `await` checks `count + 0` lines arrived).

- [ ] **Step 2: Run it to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_refused`
Expected: build error, `'class sm::Protocol' has no member named 'setConsole'`.

- [ ] **Step 3: Add the event**

In `lib/ShadowMap/src/sm_protocol.h`:

1. After `void setPads(bool footConnected, bool domeConnected);`, add:
   ```cpp
       // The USB port. "@SM REFUSED" goes there without EVENTS: it's where the owner looks for it
       // (DroidNet's Serial tab). Call once from setup().
       void setConsole(ReplyPort &port) { console_ = &port; }

       // A paired controller whose address isn't allowed tried to connect as `role`. `bdaddr` is
       // least significant byte first (Btd.disc_bdaddr). service() sends "@SM REFUSED <F|D> <address>"
       // to the console and to the event port when that's another port, each when it has room. A
       // newer refusal replaces one not yet sent.
       void noteRefused(Ctrl role, const uint8_t bdaddr[kMacLen]);
   ```
2. Change `void servicePads();` to `bool servicePads();  // whether it sent a line`, and after it add `bool serviceRefused();  // the same`.
3. After `BatteryReport battery_ = {};`, add:
   ```cpp
       ReplyPort *console_ = nullptr;
       uint8_t refusedMac_[kMacLen] = {};  // most significant byte first
       char refusedRole_ = 'F';
       bool refusedToConsole_ = false;
       bool refusedToEvents_ = false;
   ```

In `lib/ShadowMap/src/sm_protocol.cpp`:

1. In `Protocol::service()`, change the first line `servicePads();` to:
   ```cpp
       // One event line a call: a CTL first, else a REFUSED.
       if (!servicePads())
           serviceRefused();
   ```
2. Change `Protocol::servicePads()` to return `bool`: `return false;` on its early return and `return true;` after `padsReported_ = ...`.
3. After `Protocol::servicePads()`, add:
   ```cpp
   void Protocol::noteRefused(Ctrl role, const uint8_t bdaddr[kMacLen]) {
       for (uint8_t i = 0; i < kMacLen; ++i)
           refusedMac_[i] = bdaddr[kMacLen - 1 - i];
       refusedRole_ = role == Ctrl::F ? 'F' : 'D';
       refusedToConsole_ = console_ != nullptr;
       refusedToEvents_ = true;  // checked against the event port when it's sent
   }

   // "@SM REFUSED <F|D> <address>": to the console first, then (on a later call) to the event port
   // if that's another port. Each waits for room; a newer refusal replaces both.
   bool Protocol::serviceRefused() {
       if (refusedToEvents_ && (eventPort_ == nullptr || eventPort_ == console_))
           refusedToEvents_ = false;  // nobody else asked for events
       ReplyPort *to = nullptr;
       if (refusedToConsole_ && console_->canSend(kMaxLine))
           to = console_;
       else if (!refusedToConsole_ && refusedToEvents_ && eventPort_->canSend(kMaxLine))
           to = eventPort_;
       if (to == nullptr)
           return false;
       char text[kMacTextBuf];
       formatMac(refusedMac_, text);
       Out out;
       out.p(PSTR("@SM REFUSED ")).c(refusedRole_).c(' ').s(text);
       emit(out, *to);
       if (to == console_)
           refusedToConsole_ = false;
       else
           refusedToEvents_ = false;
       return true;
   }
   ```

- [ ] **Step 4: Run it to verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_refused`
Expected: 7 tests PASS.

- [ ] **Step 5: Run the native suite and the emulated Mega**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: every suite PASS (`test_protocol_more`'s CTL tests are unchanged by the one-event-a-call rule: they have no refusal).
Run: `~/.platformio/penv/bin/pio test -e simavr --without-uploading -f test_refused`
Expected: 7 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/ShadowMap/src/sm_protocol.h lib/ShadowMap/src/sm_protocol.cpp test/test_refused
git commit -m "@SM REFUSED: report a controller that isn't allowed (USB always, WCB with EVENTS)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
```

---

### Task 5: Timing: every controller-address pass under 1 ms

**Files:**

- Modify: `test/support/sm_avr_timing.h` (`eeErase()` clears the records too)
- Create: `test/test_avr_timing_mac/test_avr_timing_mac.cpp`

**Interfaces:**

- Consumes (Tasks 2-4): `MapStore::beginMac`, `macAllows`, `kEepromUsed`; `Protocol::setConsole`, `noteRefused`, `handleLine`, `service`; harness `timerBegin`, `ticksOf`, `record`, `part`, `checkBudget`, `Worst`, `NullUart`, `kWorstDefaults`, `kFw11`, `eeWorst`.
- Produces: the TIMING lines for MAC and REFUSED; the suite fails at 1 ms.

- [ ] **Step 1: Make `eeErase()` clear the records**

In `test/support/sm_avr_timing.h`, in `eeErase()`, change `a < MapStore::kFootprint` to `a < MapStore::kEepromUsed`.

- [ ] **Step 2: Write the timing suite**

Create `test/test_avr_timing_mac/test_avr_timing_mac.cpp`:

```cpp
// Worst-case time of the controller-address work in one loop() pass: each @SM MAC request, every
// pass of a MAC write and of the MAC list, a REFUSED event to each port, and macAllows(). AVR only
// (simavr); see test/support/sm_avr_timing.h for the method. One MapStore and one Protocol, as in
// the firmware.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#include <sm_test_main.h>
#include <sm_avr_timing.h>

#if defined(__AVR__)

using namespace sm;
using namespace smt;

namespace {

AvrEeprom io;
MapStore store(io, kWorstDefaults, kWorstDefaultCount);
TxQueue tx;
WcbPort port(tx);
Protocol protocol(store, kFw11);
NullUart wcb;

// The USB port: takes every line, keeps nothing.
class SinkPort final : public ReplyPort {
  public:
    bool canSend(uint8_t) override { return true; }
    void send(const char *, uint8_t len) override { last = len; }
    bool requiresCrc() const override { return false; }
    uint8_t last = 0;
};
SinkPort usb;

const uint8_t kMacA[kMacLen] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x05};
const uint8_t kMacB[kMacLen] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

void drainAll() {
    while (tx.used() > 0) {
        tx.drain(wcb);
        wcb.note();
    }
}

// The reply's length as the Pi sees it (route stripped).
uint8_t replyLen() {
    const uint8_t route = static_cast<uint8_t>(strlen(port.routePrefix()));
    return wcb.last > route ? static_cast<uint8_t>(wcb.last - route) : 0;
}

char gLine[LineReader::kMaxLen + 8];

// `body` (flash) + " *<CRC32>".
const char *crcLine(PGM_P body) {
    char b[LineReader::kMaxLen + 1];
    strncpy_P(b, body, sizeof(b) - 1);
    b[sizeof(b) - 1] = '\0';
    withCrc(b, gLine, sizeof(gLine));
    return gLine;
}

uint16_t timeLine(const char *line) {
    drainAll();
    wcb.last = 0;
    bool handled = false;
    const uint16_t t = ticksOf([&] { handled = protocol.handleLine(line, port); });
    TEST_ASSERT_TRUE(handled);
    drainAll();
    return t;
}

struct OpWorst {
    Worst service, step, pass;
};

// Every pass of an operation until it has finished and replied: service(), step() and the drain,
// timed one by one, and their sum per pass.
void runOp(OpWorst &w) {
    for (uint16_t i = 0; i < 4000 && (store.busy() || protocol.opInProgress()); ++i) {
        const uint16_t a = ticksOf([] { protocol.service(); });
        const uint16_t b = ticksOf([] { store.step(); });
        const uint16_t c = ticksOf([] { tx.drain(wcb); });
        wcb.note();
        w.service.add(a);
        w.step.add(b);
        w.pass.add(static_cast<uint16_t>(a + b + c));
    }
    TEST_ASSERT_FALSE(store.busy());
    TEST_ASSERT_FALSE(protocol.opInProgress());
    drainAll();
}

// The worst data: the mapping's worst state, and every controller slot holding an address in
// EEPROM and in the build, so each lookup and list line is the longest.
void worstState() {
    eeWorst();
    store.begin();
    for (uint8_t i = 0; i < kMacSlots; ++i) {
        TEST_ASSERT_TRUE(store.beginMac(i, MapStore::MacState::Address, kMacA) == MapStore::Result::Ok);
        while (store.busy())
            store.step();
        TEST_ASSERT_TRUE(store.takeCompletion() == MapStore::Completion::Done);
        store.setBuiltInMac(i, kMacB);
    }
    timeLine(crcLine(PSTR("@SM HELLO 20")));  // the longest route
}

}  // namespace

static void test_requests() {
    timerBegin();
    worstState();
    record(PSTR("handleLine MAC F2 (an address, from EEPROM)"), timeLine(crcLine(PSTR("@SM MAC F2"))), kT1,
           replyLen());
    record(PSTR("handleLine MAC F2 with a bad address (ERR addr)"),
           timeLine(crcLine(PSTR("@SM MAC F2 00:1A:2B:3C:4D:0G"))), kT1, replyLen());
    record(PSTR("handleLine MAC F2 with a bad slot (ERR args)"), timeLine(crcLine(PSTR("@SM MAC F3 -"))), kT1,
           replyLen());

    OpWorst w;
    record(PSTR("handleLine MAC (the list starts)"), timeLine(crcLine(PSTR("@SM MAC"))), kT1);
    runOp(w);
    record(PSTR("MAC list: worst service() (a MAC line)"), w.service.ticks, kT1, replyLen());
    record(PSTR("MAC list: worst pass (service+step+drain)"), w.pass.ticks, kT1);
}

static void test_writes() {
    worstState();
    OpWorst w;
    record(PSTR("handleLine MAC F2 <address>"), timeLine(crcLine(PSTR("@SM MAC F2 12:34:56:78:9A:BC"))), kT1);
    runOp(w);
    record(PSTR("MAC <address>: worst service() (OK reply)"), w.service.ticks, kT1, replyLen());
    record(PSTR("MAC <address>: worst step() (its finish reloads the record)"), w.step.ticks, kT1);
    record(PSTR("MAC <address>: worst pass (service+step+drain)"), w.pass.ticks, kT1);

    w = OpWorst();
    record(PSTR("handleLine MAC D2 -"), timeLine(crcLine(PSTR("@SM MAC D2 -"))), kT1);
    runOp(w);
    record(PSTR("MAC -: worst pass (service+step+drain)"), w.pass.ticks, kT1, replyLen());

    w = OpWorst();
    record(PSTR("handleLine MAC D2 default"), timeLine(crcLine(PSTR("@SM MAC D2 default"))), kT1);
    runOp(w);
    record(PSTR("MAC default: worst pass (service+step+drain)"), w.pass.ticks, kT1, replyLen());
}

static void test_refused_and_allows() {
    worstState();
    protocol.setConsole(usb);
    timeLine(crcLine(PSTR("@SM EVENTS 1")));
    const uint8_t bdaddr[kMacLen] = {0xBC, 0x9A, 0x78, 0x56, 0x34, 0x13};  // in no slot: every slot is compared
    part(PSTR("noteRefused()"), ticksOf([&] { protocol.noteRefused(Ctrl::D, bdaddr); }));
    usb.last = 0;
    uint16_t t = ticksOf([] { protocol.service(); });
    TEST_ASSERT_EQUAL_UINT8(41, usb.last);
    record(PSTR("service(): REFUSED to the USB port"), t, kT1, usb.last);
    t = ticksOf([] { protocol.service(); });
    record(PSTR("service(): REFUSED to the WCB (EVENTS 1)"), t, kT1, replyLen());
    drainAll();

    bool allowed = true;
    part(PSTR("macAllows(): no match, both slots compared"),
         ticksOf([&] { allowed = store.macAllows(Ctrl::D, bdaddr); }));
    TEST_ASSERT_FALSE(allowed);
}

static void test_every_pass_is_under_1_ms() { checkBudget(); }

#else

static void test_requests() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_writes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_refused_and_allows() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_every_pass_is_under_1_ms() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_requests);
    RUN_TEST(test_writes);
    RUN_TEST(test_refused_and_allows);
    RUN_TEST(test_every_pass_is_under_1_ms);
    return UNITY_END();
}
```

The `41` in `test_refused_and_allows` is the length of `@SM REFUSED D 12:34:56:78:9A:13 *XXXXXXXX` (no terminator): `@SM ` 4 + `REFUSED ` 8 + `D ` 2 + the address 17 + ` *` 2 + the CRC 8. If the run reports another length, count the line by hand before changing the number.

- [ ] **Step 3: Run it and read the figures**

Run: `~/.platformio/penv/bin/pio test -e simavr --without-uploading -f test_avr_timing_mac -v | grep -E "TIMING|PASS|FAIL"`
Expected: every `TIMING` figure below 1000 us, all 4 tests PASS. Note the worst figure in the commit message.

- [ ] **Step 4: Run every simavr suite (the whole-pass suites include the new background work)**

Run: `~/.platformio/penv/bin/pio test -e simavr --without-uploading`
Expected: every suite PASS, including `test_avr_timing_passes`, `test_avr_timing_runtime` and `test_avr_timing_v4`.

- [ ] **Step 5: Commit**

```bash
git add test/support/sm_avr_timing.h test/test_avr_timing_mac
git commit -m "Timing: MAC requests, writes, the list and REFUSED, every pass under 1 ms (worst <N> us)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
```

(Replace `<N>` with the worst figure from Step 3.)

---

### Task 6: Firmware: use the list, report refusals, drop revoked controllers

**Files:**

- Modify: `include/config.h:51-57` (the MAC block)
- Modify: `src/config.cpp:12-27` (the MAC block)
- Modify: `src/main.cpp` (setup, `onInitPS3NavFoot`, `onInitPS3NavDome`, new `dropFoot`, `dropDome`, `checkRevoked`, `loop`)
- Modify: `include/controller_macs.example.h` (comment)

**Interfaces:**

- Consumes (Tasks 1-4): `sm::parseMac`, `sm::kMacLen`, `sm::kMacTextBuf`, `sm::kMacSlots`, `MapStore::setBuiltInMac`, `macAllows`, `macChanges`, `Protocol::setConsole`, `noteRefused`.
- Produces: `extern const char controllerMacs[4][18] PROGMEM;` (config.h). The firmware behavior in the spec's "The connect check".

`main.cpp` isn't unit-testable; this task's checks are the firmware builds (with and without `controller_macs.h`), `pio check`, and the bench test in Task 9.

- [ ] **Step 1: Make `controller_macs.h` optional**

In `include/config.h`, replace the four `extern String PS3Controller...` lines with:

```cpp
// The build's allowed controllers, in the order F (foot), D (dome), F2, D2 (backups), from
// include/controller_macs.h when it exists. Text that isn't an address (the template's
// "XX:XX:XX:XX:XX:XX", or "") means none. Addresses set with "@SM MAC" (kept in EEPROM) take
// precedence; see WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md.
extern const char controllerMacs[4][18] PROGMEM;
```

In `src/config.cpp`, replace from the comment `// The addresses live in include/controller_macs.h, ...` through the four `String PS3Controller...` lines with:

```cpp
// The addresses live in include/controller_macs.h, which is gitignored and optional: copy
// include/controller_macs.example.h to it to build your addresses in, or leave it out and allow
// your controllers with "@SM MAC" after flashing (README, "Allow your controllers").

#if __has_include("../include/controller_macs.h")
#include "../include/controller_macs.h"
#endif
#ifndef PS3_FOOT_MAC
#define PS3_FOOT_MAC ""
#endif
#ifndef PS3_DOME_MAC
#define PS3_DOME_MAC ""
#endif
#ifndef PS3_BACKUP_FOOT_MAC
#define PS3_BACKUP_FOOT_MAC ""
#endif
#ifndef PS3_BACKUP_DOME_MAC
#define PS3_BACKUP_DOME_MAC ""
#endif

const char controllerMacs[4][18] PROGMEM = {PS3_FOOT_MAC, PS3_DOME_MAC, PS3_BACKUP_FOOT_MAC, PS3_BACKUP_DOME_MAC};
```

In `include/controller_macs.example.h`, replace the paragraph starting `// Copy this file to include/controller_macs.h` (through `gitignored so your addresses stay out of the repository.`) with:

```cpp
// Optional. Copy this file to include/controller_macs.h and replace the placeholders with your
// controllers' Bluetooth addresses (uppercase, two digits a byte, colon-separated, as in
// "00:1A:2B:3C:4D:05") to build them in. controller_macs.h is gitignored so your addresses stay
// out of the repository. Without it, the firmware allows no controller until you add one with
// "@SM MAC" (README, "Allow your controllers"); an address added that way overrides this file's.
```

- [ ] **Step 2: Build to see the old uses fail**

Run: `~/.platformio/penv/bin/pio run`
Expected: FAIL: `'PS3ControllerFootMac' was not declared in this scope` in `main.cpp` (`onInitPS3NavFoot`, `onInitPS3NavDome`).

- [ ] **Step 3: Wire the list into `main.cpp`**

1. After `sm::Uart1 wcbUart;` (in the Button Mapping section), add:
   ```cpp
   // The connected controllers' addresses (Btd.disc_bdaddr order), kept so an "@SM MAC" change that
   // revokes one can drop it (checkRevoked()).
   uint8_t footBdaddr[sm::kMacLen] = {};
   uint8_t domeBdaddr[sm::kMacLen] = {};
   uint8_t macChangesSeen = 0;
   ```
2. In `setup()`, right after `mapStore.begin();`, add:
   ```cpp
       // Allowed controllers: the build's addresses (include/controller_macs.h, if any), under those
       // set with @SM MAC (EEPROM). Refusals are reported on the USB port as "@SM REFUSED".
       for (uint8_t i = 0; i < sm::kMacSlots; ++i) {
           char text[sm::kMacTextBuf];
           strncpy_P(text, controllerMacs[i], sizeof(text) - 1);
           text[sizeof(text) - 1] = '\0';
           uint8_t mac[sm::kMacLen];
           mapStore.setBuiltInMac(i, sm::parseMac(text, mac) ? mac : nullptr);
       }
       mapProtocol.setConsole(usbReplies);
   ```
3. Replace the whole of `onInitPS3NavFoot()` and `onInitPS3NavDome()` with:
   ```cpp
   // Drops the foot controller and stops the motors: a controller that isn't allowed, at connection or
   // after an @SM MAC change.
   void dropFoot() {
       ST->stop();
       SyR->stop();
       isFootMotorStopped = true;
       footDriveSpeed = 0;
       PS3NavFoot->setLedOff(LED1);
       PS3NavFoot->disconnect();
   #ifdef SHADOW_DEBUG
       printOutput();
   #endif
       isPS3NavigatonInitialized = false;
       mainControllerConnected = false;
   }

   void dropDome() {
       ST->stop();
       SyR->stop();
       isFootMotorStopped = true;
       footDriveSpeed = 0;
       PS3NavDome->setLedOff(LED1);
       PS3NavDome->disconnect();
   #ifdef SHADOW_DEBUG
       printOutput();
   #endif
       isSecondaryPS3NavigatonInitialized = false;
       domeControllerConnected = false;
   }

   void onInitPS3NavFoot() {
   #ifdef SHADOW_DEBUG
       Serial.println(F("\r\n*** onInitPS3NavFoot CALLED ***"));
   #endif

       PS3NavFoot->setLedOn(LED1);
       isPS3NavigatonInitialized = true;
       badPS3Data = 0;

   #ifdef SHADOW_DEBUG
       output += "\r\nBT Address of Last connected Device when FOOT PS3 Connected: ";
       output += getLastConnectedBtMAC();
   #endif

       if (mapStore.macAllows(sm::Ctrl::F, Btd.disc_bdaddr)) {
   #ifdef SHADOW_DEBUG
           output += "\r\nWe have our FOOT controller connected.\r\n";
   #endif
           memcpy(footBdaddr, Btd.disc_bdaddr, sm::kMacLen);
           mainControllerConnected = true;
           WaitingforReconnect = true;
       } else {
   // Prevent connection from anything but the allowed controllers
   #ifdef SHADOW_DEBUG
           output += "\r\nWe have an invalid controller trying to connect as tha FOOT controller, it will be dropped.\r\n";
   #endif
           mapProtocol.noteRefused(sm::Ctrl::F, Btd.disc_bdaddr);
           dropFoot();
       }
   }

   void onInitPS3NavDome() {
   #ifdef SHADOW_DEBUG
       Serial.println(F("\r\n*** onInitPS3NavDome CALLED ***"));
   #endif

       PS3NavDome->setLedOn(LED1);
       isSecondaryPS3NavigatonInitialized = true;
       badPS3Data = 0;

       if (mapStore.macAllows(sm::Ctrl::D, Btd.disc_bdaddr)) {
   #ifdef SHADOW_DEBUG
           output += "\r\nWe have our DOME controller connected.\r\n";
   #endif
           memcpy(domeBdaddr, Btd.disc_bdaddr, sm::kMacLen);
           domeControllerConnected = true;
           WaitingforReconnectDome = true;
       } else {
   // Prevent connection from anything but the allowed controllers
   #ifdef SHADOW_DEBUG
           output += "\r\nWe have an invalid controller trying to connect as the DOME controller, it will be dropped.\r\n";
   #endif
           mapProtocol.noteRefused(sm::Ctrl::D, Btd.disc_bdaddr);
           dropDome();
       }
   }

   // An @SM MAC change that no longer allows a connected controller drops it, as a refused connection
   // is dropped: motors stopped, controller disconnected. Once a pass, before the motors are driven.
   void checkRevoked() {
       if (mapStore.macChanges() == macChangesSeen)
           return;
       macChangesSeen = mapStore.macChanges();
       if (mainControllerConnected && !mapStore.macAllows(sm::Ctrl::F, footBdaddr))
           dropFoot();
       if (domeControllerConnected && !mapStore.macAllows(sm::Ctrl::D, domeBdaddr))
           dropDome();
   }
   ```
   Before replacing, compare the old functions line by line with these: the only intended differences are `macAllows()` in place of the `String` comparisons, `memcpy` of the address, `noteRefused()`, and the refusal steps moved into `dropFoot()`/`dropDome()` unchanged. If the old dome function sets anything else (for example `WaitingforReconnectDome`), keep it exactly as it was.
4. Add the declarations next to the existing ones at the top of `main.cpp` (`void onInitPS3NavFoot();` ...): `void dropFoot();`, `void dropDome();`, `void checkRevoked();`.
5. In `loop()`, right after the `if (!readUSB()) { ... }` block and before `footMotorDrive();`, add:
   ```cpp
       checkRevoked();
   ```

- [ ] **Step 4: Build with C2-B5's `controller_macs.h`**

Run: `~/.platformio/penv/bin/pio run`
Expected: SUCCESS. Note RAM and flash; RAM should be within about 60 bytes of the 2078 before this work (the `String` addresses are gone, the records and built-ins added).

- [ ] **Step 5: Build without it**

Run: `tmp="$(mktemp -d)" && mv include/controller_macs.h "$tmp/" && ~/.platformio/penv/bin/pio run; rc=$?; mv "$tmp/controller_macs.h" include/ && rmdir "$tmp"; test -f include/controller_macs.h && echo back; exit $rc`
Expected: SUCCESS (no `#error` any more), then `back`. The file goes outside the repository while it's away, so no copy of it can be committed by accident.

- [ ] **Step 6: Lint**

Run: `~/.platformio/penv/bin/pio check -e SHADOWC2B5 --skip-packages 2>&1 | tail -20`
Expected: no defect on a line this task changed. Defects reported elsewhere were there before: leave them, and name them in the commit message.

- [ ] **Step 7: Commit**

```bash
git add include/config.h src/config.cpp src/main.cpp include/controller_macs.example.h
git commit -m "Firmware: allowed controllers from @SM MAC over controller_macs.h (now optional); REFUSED; revocation drops

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
```

---

### Task 7: Release build and its address check

**Files:**

- Create: `scripts/check_release_hex.py`
- Create: `scripts/test_check_release_hex.py`
- Create: `scripts/build_release.sh`

**Interfaces:**

- Consumes: the firmware build (`pio run -e SHADOWC2B5`), `include/controller_macs.h` if present.
- Produces: `scripts/build_release.sh [<commit>]` writes `build/release/SHADOWC2B5_<short commit>.hex` and exits non-zero if a local address is in it. `check_release_hex.py <hex> [--macs-from <header>] [--mac <addr> ...]`: exit 0 clean, 1 found, 2 bad input.

- [ ] **Step 1: Write the failing test**

Create `scripts/test_check_release_hex.py`:

```python
"""Tests for check_release_hex.py: run with python3 -m unittest scripts/test_check_release_hex.py"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_release_hex as crh  # noqa: E402

MAC = "00:1A:2B:3C:4D:05"
MAC_BYTES = bytes.fromhex("001A2B3C4D05")


def ihex(data, base=0):
    """Intel HEX for `data` at `base`, 16 bytes a record, with a type-04 record for the upper 16 bits."""
    lines = []

    def rec(kind, addr, payload):
        raw = bytes([len(payload), (addr >> 8) & 0xFF, addr & 0xFF, kind]) + payload
        return ":" + (raw + bytes([(-sum(raw)) & 0xFF])).hex().upper()

    lines.append(rec(4, 0, bytes([(base >> 24) & 0xFF, (base >> 16) & 0xFF])))
    for off in range(0, len(data), 16):
        lines.append(rec(0, (base + off) & 0xFFFF, data[off:off + 16]))
    lines.append(rec(1, 0, b""))
    return "\n".join(lines) + "\n"


class CheckTest(unittest.TestCase):
    def write(self, text):
        f = tempfile.NamedTemporaryFile("w", suffix=".hex", delete=False)
        f.write(text)
        f.close()
        self.addCleanup(os.unlink, f.name)
        return f.name

    def test_clean_image_passes(self):
        path = self.write(ihex(b"\x00\x01SHADOW firmware\xff" * 8))
        self.assertEqual(crh.main([path, "--mac", MAC]), 0)

    def test_finds_text_and_bytes(self):
        for blob in (MAC.encode(), MAC.lower().encode(), MAC_BYTES, MAC_BYTES[::-1]):
            path = self.write(ihex(b"\x00" * 20 + blob + b"\x00" * 20, base=0x10000))
            self.assertEqual(crh.main([path, "--mac", MAC]), 1, blob)

    def test_reads_addresses_from_the_header(self):
        header = self.write(
            '#define PS3_FOOT_MAC "00:1A:2B:3C:4D:05"\n#define PS3_DOME_MAC "XX:XX:XX:XX:XX:XX"\n'
            '#define PS3_BACKUP_FOOT_MAC "00:00:00:00:00:00"\n')
        self.assertEqual(crh.macs_in_header(header), [MAC])

    def test_bad_checksum_is_bad_input(self):
        lines = ihex(b"abcd").splitlines()
        data = lines[1]  # the data record (line 0 is the type-04 record)
        lines[1] = data[:-2] + ("00" if data[-2:] != "00" else "01")
        path = self.write("\n".join(lines) + "\n")
        self.assertEqual(crh.main([path, "--mac", MAC]), 2)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 -m unittest scripts/test_check_release_hex.py`
Expected: `ModuleNotFoundError: No module named 'check_release_hex'`.

- [ ] **Step 3: Write the check**

Create `scripts/check_release_hex.py`:

```python
#!/usr/bin/env python3
"""Fails when a firmware .hex holds a controller address: as text ("00:1A:2B:3C:4D:05", either
case) or as its six bytes in either order. The release build runs it so no owner's addresses
ship (scripts/build_release.sh).

    python3 scripts/check_release_hex.py <firmware.hex> [--macs-from include/controller_macs.h] [--mac <addr> ...]

Exit 0: none found. 1: found (each is printed). 2: bad input.
"""

import argparse
import re
import sys

MAC_RE = re.compile(r"\b([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})\b")
NOT_ADDRESSES = {"00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF"}


def read_ihex(path):
    """Every data byte of an Intel HEX file (record types 00, 01, 02, 04), in address order; gaps
    read as 0xFF."""
    data = {}
    base = 0
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError(f"line {n}: not Intel HEX")
            raw = bytes.fromhex(line[1:])
            if len(raw) < 5 or len(raw) != raw[0] + 5 or sum(raw) & 0xFF:
                raise ValueError(f"line {n}: bad length or checksum")
            addr, kind, payload = (raw[1] << 8) | raw[2], raw[3], raw[4:4 + raw[0]]
            if kind == 0:
                for i, b in enumerate(payload):
                    data[base + addr + i] = b
            elif kind == 1:
                break
            elif kind == 2:
                base = ((payload[0] << 8) | payload[1]) << 4
            elif kind == 4:
                base = ((payload[0] << 8) | payload[1]) << 16
    if not data:
        return b""
    lo, hi = min(data), max(data)
    return bytes(data.get(a, 0xFF) for a in range(lo, hi + 1))


def macs_in_header(path):
    """The addresses in a controller_macs.h, upper case, placeholders and all-0/all-FF left out."""
    with open(path) as f:
        found = [m.group(1).upper() for m in MAC_RE.finditer(f.read())]
    return [m for m in found if m not in NOT_ADDRESSES]


def findings(image, macs):
    out = []
    upper = image.upper()
    for mac in macs:
        raw = bytes.fromhex(mac.replace(":", ""))
        if mac.upper().encode() in upper:
            out.append(f"{mac} as text")
        if raw in image:
            out.append(f"{mac} as bytes")
        if raw[::-1] in image:
            out.append(f"{mac} as bytes, least significant first")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("hex")
    ap.add_argument("--macs-from", action="append", default=[])
    ap.add_argument("--mac", action="append", default=[])
    args = ap.parse_args(argv)
    try:
        image = read_ihex(args.hex)
        macs = [m.upper() for m in args.mac]
        for header in args.macs_from:
            macs += macs_in_header(header)
    except (OSError, ValueError) as e:
        print(f"check_release_hex: {e}", file=sys.stderr)
        return 2
    if not macs:
        print("check_release_hex: no addresses to look for (no controller_macs.h here)")
        return 0
    found = findings(image, macs)
    for f in found:
        print(f"FOUND {f} in {args.hex}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run it to verify it passes**

Run: `python3 -m unittest scripts/test_check_release_hex.py`
Expected: `Ran 4 tests` ... `OK`.

- [ ] **Step 5: Write the release build**

Create `scripts/build_release.sh` (then `chmod +x scripts/build_release.sh`):

```bash
#!/usr/bin/env bash
# Builds the release firmware from a clean checkout of a commit, where include/controller_macs.h
# doesn't exist, and checks that none of this machine's controller addresses got into it.
#
#   scripts/build_release.sh [<commit>]        (default: HEAD)
#
# Writes build/release/SHADOWC2B5_<short commit>.hex and prints its path. Attach that file to the
# GitHub Release for the commit (see README, "Flash with DroidNet").
set -euo pipefail

root="$(git -C "$(dirname "$0")/.." rev-parse --show-toplevel)"
commit="$(git -C "$root" rev-parse --short "${1:-HEAD}")"
pio="${PIO:-$HOME/.platformio/penv/bin/pio}"
work="$(mktemp -d)"
cleanup() {
    git -C "$root" worktree remove --force "$work/tree" >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT

git -C "$root" worktree add --detach "$work/tree" "$commit" >/dev/null
if [ -e "$work/tree/include/controller_macs.h" ]; then
    echo "build_release: include/controller_macs.h is committed at $commit; refusing" >&2
    exit 1
fi
(cd "$work/tree" && "$pio" run -e SHADOWC2B5 >"$work/build.log" 2>&1) || {
    tail -30 "$work/build.log" >&2
    exit 1
}

mkdir -p "$root/build/release"
out="$root/build/release/SHADOWC2B5_${commit}.hex"
cp "$work/tree/.pio/build/SHADOWC2B5/firmware.hex" "$out"

check=(python3 "$root/scripts/check_release_hex.py" "$out")
if [ -f "$root/include/controller_macs.h" ]; then
    check+=(--macs-from "$root/include/controller_macs.h")
fi
"${check[@]}"
echo "$out"
```

- [ ] **Step 6: Run the release build**

Run: `scripts/build_release.sh`
Expected: it prints `build/release/SHADOWC2B5_<commit>.hex` and exits 0, with no `FOUND` line. Then prove the check would catch a leak: `python3 scripts/check_release_hex.py .pio/build/SHADOWC2B5/firmware.hex --macs-from include/controller_macs.h; echo "exit $?"`. The working-tree build has C2-B5's addresses compiled in, so expect `FOUND ... as text` lines and `exit 1`. Don't paste the printed addresses anywhere (commit messages, docs, chat): report only the count of findings.

- [ ] **Step 7: Commit**

```bash
git add scripts/check_release_hex.py scripts/test_check_release_hex.py scripts/build_release.sh
git commit -m "Release: build the .hex from a clean checkout and refuse one holding a local controller address

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
```

---

### Task 8: Docs

**Files:**

- Modify: `README.md` ("Turn it on")
- Modify: `WIPDocs/COMMAND_MAPPING_DESIGN.md` (protocol reference, EEPROM layout, errors)
- Modify: `WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md` (the three implementation choices)
- Modify: `CLAUDE.md`

**Interfaces:**

- Consumes: the behavior of Tasks 3, 4, 6 and 7.
- Produces: owner-facing instructions.

- [ ] **Step 1: README, "Turn it on"**

Replace the README's sections `### 1. Build and upload with your controllers' addresses`, `### 2. Pair each controller with the dongle` and `### 3. Tell SHADOW which controllers are yours` with the text below. Keep `### 4. First drive` and `### 5. Connect DroidNet`, renumbered as 4 and 5.

````markdown
### 1. Put the firmware on SHADOW

**With DroidNet (no tools on your computer):**

1. Download `SHADOWC2B5_<commit>.hex` from the newest [release](https://github.com/travisccook/DroidNet-SHADOW-C2B5/releases).
2. Turn off the motor power (or E-stop it) and put the drive wheels up: flashing resets the Mega.
3. Plug SHADOW's USB (the Mega's USB-B port) into the Signal Booster.
4. In DroidNet's **Firmware** tab, pick the Mega as the device, choose the `.hex`, set Board Type to **Arduino Mega**, and click **Flash Firmware**. Don't use Auto-detect: it probes other serial ports on the Booster. DroidNet keeps SHADOW's EEPROM, so a button map and controller list already there survive.

**Or build it yourself** with PlatformIO (VS Code, or `pio` from the repository root): `pio run -t upload`. To build your controllers' addresses in, copy `include/controller_macs.example.h` to `include/controller_macs.h` and fill it in; without it, you allow them in step 3 like everyone else.

### 2. Pair each controller with the dongle

SHADOW pairs controllers the way SHADOW always has, through the USB Host Shield 2.0 library ([its instructions](https://github.com/felis/USB_Host_Shield_2.0#ps3-library)). Do this once for each controller:

1. With the dongle in the Mega's USB host port, power up or reset the Mega and wait a few seconds. The library reads the dongle's address.
2. **Without resetting the Mega**, unplug the dongle and plug the Navigation controller into the same port with a USB cable. Wait until its light flashes: the library has written the dongle's address into the controller.
3. Unplug the controller and plug the dongle back in.

### 3. Allow your controllers

SHADOW drops any controller it doesn't know and stops the motors. To allow one:

1. In DroidNet's **Serial** tab, pick SHADOW's USB port (the Mega), set 115200 baud and click **Connect**. Connecting resets SHADOW; that's expected.
2. Press PS on the foot controller. SHADOW refuses it and shows its address:
   ```
   @SM REFUSED F 00:1A:2B:3C:4D:05 *…
   ```
3. Type the same address into an `@SM MAC` command and press Enter:
   ```
   @SM MAC F 00:1A:2B:3C:4D:05
   ```
   SHADOW answers `@SM OK MAC F 00:1A:2B:3C:4D:05`.
4. Press PS again: the controller connects and its light stays on.
5. Then the dome controller, the same way with `D`. **Connect the foot controller first:** SHADOW gives the first controller that connects the foot's place, so a dome controller connecting first is refused as `F`.

The slots are `F` (foot), `D` (dome), `F2` and `D2` (a backup for each). `@SM MAC` lists all four, `@SM MAC D2 -` empties a slot, and `@SM MAC F default` goes back to the address built into the firmware (none in the release). Taking away the address of a controller that's connected drops it at once and stops the motors. The list lives in SHADOW's EEPROM: it survives reboots and reflashing. The same commands work through the WCB with the protocol's checksum (see `WIPDocs/COMMAND_MAPPING_DESIGN.md`).
````

In the README's "What it does" list, change the last bullet to: `- **Stops the motors** when a controller drops or sends bad data, and accepts only the controllers you've allowed (from DroidNet's Serial tab, or built in).`

In the README's "Safety" list, change the first bullet's end from `when a controller that isn't in controller_macs.h tries to connect.` to `when a controller you haven't allowed tries to connect, or you take away the address of one that's connected.`

- [ ] **Step 2: Protocol reference**

In `WIPDocs/COMMAND_MAPPING_DESIGN.md`:

1. In "EEPROM layout", change "The layout uses 3952 of the 4096 bytes. Nothing else uses EEPROM." to "The mapping uses 3952 of the 4096 bytes; the controller addresses take the next 36 (see `WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md`). Nothing else uses EEPROM.", and in the layout block change `0xF70  free` to:
   ```
   0xF70  4 x 9 B          controller addresses F, D, F2, D2: state, address, CRC16
   0xF94  free
   ```
2. In the "Requests" table, add after the `BAT` row:
   ```markdown
   | `MAC` | Four lines, F, D, F2, D2, one a pass: `MAC <S> <address\|-> <eeprom\|default>`. Refused with `ERR busy` while another operation runs, like DUMP. |
   | `MAC <S>` | `MAC <S> <address\|-> <eeprom\|default>`. Always answered, like GET. `S` is `F`, `D`, `F2` or `D2`. |
   | `MAC <S> <address\|-\|default>` | `OK MAC <S> <ADDRESS\|-\|default>` once written and verified: an address (`HH:HH:HH:HH:HH:HH`, either case; echoed upper case), `-` for no controller, `default` for the build's. A write: one at a time with SET and the others. |
   ```
3. In "One write-type operation at a time", add `MAC` (its list and its changes) to the list of refused-while-busy operations.
4. In the "Events" table, add:
   ```markdown
   | `REFUSED <F\|D> <address>` | A paired controller that isn't allowed tried to connect, as the foot or the dome. Also sent to the USB port without EVENTS. |
   ```
5. In "Errors", add the row `| \`addr\` | Not a usable address (\`HH:HH:HH:HH:HH:HH\`, not all 0 or all FF) |`.
6. Under "Protocol reference (v4)", add one sentence: "`MAC` and `REFUSED` came later without a version change: a client tells firmware without them by `ERR verb` to `MAC`."

- [ ] **Step 3: The spec's three implementation choices**

In `WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md`:

1. In "The connect check", replace the first bullet's "A small `ControllerList` in `lib/ShadowMap` (...) holds" with "`MapStore` (in `lib/ShadowMap`, no Arduino dependency, so it tests on the host) holds, through `setBuiltInMac()`, `macFor()` and `macAllows()`," and change "`allows(role, bdaddr)`" to "`macAllows(role, bdaddr)`".
2. Replace "parsed once at boot by `parseBtAddress()` (beside `formatBtAddress()` in `include/bt_address.h`)" with "parsed once at boot by `sm::parseMac()` (`lib/ShadowMap/src/sm_mac.h`; the protocol uses it too, and the library can't include the project's `include/`)".
3. In the `REFUSED` paragraph, replace "sent only to the port that sent `EVENTS 1`, dropped rather than queued when there's no room, never while a press waits." with "sent only to the port that sent `EVENTS 1`, and like `CTL` it waits for room (never while a press waits); a newer refusal replaces it."
4. In "Testing", change "`parseBtAddress()`" to "`sm::parseMac()`" and "`ControllerList`" to "`MapStore::macFor()` / `macAllows()`".

- [ ] **Step 4: CLAUDE.md**

1. In "Critical Development Notes" item 3, change "EEPROM holds the mapping (3952 bytes from address 0: the single presses, their double taps, then their long presses); anything else that uses EEPROM must start after that." to "EEPROM holds the mapping (3952 bytes from address 0: the single presses, their double taps, then their long presses), then the four allowed-controller addresses (36 bytes, to 3987); anything else that uses EEPROM must start after that."
2. Replace the `include/controller_macs.h` bullet in "Architecture" with: "`include/controller_macs.h`: optional built-in PS3 controller addresses (gitignored; `include/controller_macs.example.h` is the template). The allowed controllers are `MapStore::macAllows()`: an address set with `@SM MAC` (EEPROM) wins over the built-in one. This is the only check on who can connect and drive. Never commit real MAC addresses (or the Bluetooth dongle's address) anywhere in this repo, and build releases with `scripts/build_release.sh`, which refuses a `.hex` holding a local address."
3. In "Common Development Commands", add `scripts/build_release.sh                 # Release .hex from a clean checkout, checked for local addresses` and `python3 -m unittest scripts/test_check_release_hex.py`.

- [ ] **Step 5: Check the docs' links and commit**

Run: `grep -o -E '\]\(([^)#]+)\)' README.md | sed -E 's/^\]\(//; s/\)$//' | grep -v -E '^https?://' | while read p; do [ -e "$p" ] || echo "MISSING $p"; done`
Expected: no output.

```bash
git add README.md WIPDocs/COMMAND_MAPPING_DESIGN.md WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md CLAUDE.md
git commit -m "Docs: flash with DroidNet, allow controllers with @SM MAC; protocol reference and spec updated

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
```

---

### Task 9: Bench test on C2-B5, then the release

This task needs the owner at the droid for every physical step (pairing, pressing PS, watching the droid stop). Coordinate with the DroidNet session before touching c2-body (one device on c2-body at a time).

**Files:**

- Modify: `WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md` (append a "Bench test" record)

- [ ] **Step 1: Build the candidate**

Run: `scripts/build_release.sh`
Expected: `build/release/SHADOWC2B5_<commit>.hex`, no `FOUND`. This is the exact file the release will carry.

- [ ] **Step 2: Rollback image first**

Follow `WIPDocs/HANDOFF_2026-09-23_shadow_wdp_device_announce.md`, "Flashing SHADOW", steps 2, 3 and 5: motor power off and wheels up, the USB power check, then the `avrdude` dump over SSH on c2-body to `/root/shadow-backups/shadow-pre-macs-<date>.hex` with its `sha256sum`. Copy the candidate to c2-body with `scp -O`.

- [ ] **Step 2b: Reproduce the address race on the current firmware (RED for the final review's fix)**

With the firmware C2-B5 runs now, wheels up: turn both controllers off, then press PS on the dome controller and,
within about a second, on the foot controller. Note which controller drives the feet and which is dropped. The
final review found that the connect check read `Btd.disc_bdaddr`, which the second controller's connection
request overwrites before the first one's `onInit` runs, so the dome controller can end up driving the feet.
Repeat a few times; record what was seen.

- [ ] **Step 3: Flash through DroidNet's Firmware tab**

On the Signal Booster's web page: Firmware tab, the Mega ADK device, the candidate `.hex`, Board Type **Arduino Mega**, Flash. Record the console's last lines (success and verify).

- [ ] **Step 4: The checks, in order (owner at the droid, wheels up)**

1. C2-B5's own addresses are no longer built in. Open DroidNet's Serial tab on the Mega at 115200, and with each controller connecting, expect `@SM REFUSED F ...` (foot) and then the dome as `F` too: the foot isn't allowed yet.
2. Allow the foot with `@SM MAC F <address from REFUSED>`, expect `@SM OK MAC F ...`, press PS: it connects.
3. Press PS on the dome: `@SM REFUSED D ...`. Allow it with `@SM MAC D ...`, press PS: it connects.
4. `@SM MAC` lists F and D with `eeprom`, F2 and D2 `- default`.
5. Revoke while connected: `@SM MAC D -`. Expect the dome controller to drop and the motors to stop (wheels up). `@SM MAC D <address>` again, press PS: back.
6. Close the Serial tab. In the SHADOW tab, check the link is Live and the map is in sync (the button map is untouched).
7. Drive check, wheels up: foot drive, dome, a few mapped buttons.
8. Add the backups if the owner has them (`F2`, `D2`), the same way.
9. The race from Step 2b (GREEN): both controllers off, PS on the dome, then the foot within about a second. Expect the
   dome controller refused as `F` (`REFUSED F <its own address>`) and only the foot controller driving the feet; then
   the dome connects as the dome on its next PS.
10. Revoke a controller that is off: `@SM MAC D -` with the dome controller off, then press PS on it: refused. Put it
    back. Expect no effect on the foot controller.
11. Revoke while driving (wheels up, foot stick held): `@SM MAC F -`. Expect the feet to stop at once and the foot
    controller dropped. Put it back.

If any check fails, roll back over SSH with the Step 2 dump (`avrdude ... -D -U flash:w:<dump>:i`) and stop.

- [ ] **Step 5: Record and commit**

Append to `WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md` a section `## Bench test (<date>)` with the commit, the candidate's sha256, and one line per check above (pass/fail and what was seen; no addresses).

```bash
git add WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md
git commit -m "Bench test on C2-B5: release candidate <commit>, controllers allowed with @SM MAC

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin main
```

- [ ] **Step 6: The GitHub Release (owner's go-ahead first)**

Ask the owner for the tag name (for example `v1.0.0`) and whether to publish now; the repository is still private, so a release is visible only to the owner until it's made public. With a yes:

```bash
gh release create <tag> build/release/SHADOWC2B5_<commit>.hex --target <full commit sha> \
  --title "SHADOW <tag>" --notes "Flash with DroidNet's Firmware tab (Board Type Arduino Mega), then allow your controllers with @SM MAC: see the README's Turn it on."
```

- [ ] **Step 7: Tell the DroidNet session**

Send the DroidNet session (via SendMessage) the new wire formats (`MAC` requests and replies, `REFUSED`, `ERR addr`), the commit, and that DroidNet needs no change; a Controllers panel in the SHADOW tab can come later using these over the WCB.
