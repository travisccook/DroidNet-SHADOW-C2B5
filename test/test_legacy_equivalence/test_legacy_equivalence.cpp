// Replays every button combination through the old table-driven code (frozen in
// legacy_tables.h) and through the new resolver + built-in defaults, and checks that both
// send the same bytes to the WCB. The only expected difference is volume and brightness
// (PS + direction), removed on purpose: now nothing is sent for them.
#include <unity.h>

#include <ShadowMap.h>
#include <default_mappings.h>
#include <sm_test_main.h>
#include <stdio.h>

#include "legacy_tables.h"

using namespace sm;

namespace {

bool legacyModifierHeld(uint16_t mod, const PadState &self, const PadState &other) {
    const bool crossCapable = mod == kHeldCross || mod == kHeldCircle || mod == kHeldPs;
    if (crossCapable && other.connected)
        return (other.held & mod) || (self.held & mod);
    return self.held & mod;
}

// The old processSimpleButtonMappings: first table entry whose combo is pressed wins.
// Returns the bytes sent, "" for a matched UNUSED entry or no match, nullptr for a removed
// stateful entry.
const char *legacySend(const LegacyMapping *table, size_t count, const PadState &self, const PadState &other) {
    for (size_t i = 0; i < count; ++i) {
        const LegacyMapping &m = table[i];
        const bool primary = self.held & m.primary;
        const bool modifier = m.modifier == 0 || legacyModifierHeld(m.modifier, self, other);
        if (primary && modifier)
            return m.sent;
    }
    return "";
}

class NoEeprom final : public EepromIo {
  public:
    bool ready() override { return true; }
    uint8_t read(uint16_t) override { return 0xFF; }
    void write(uint16_t, uint8_t) override {}
};

void describe(char *out, size_t cap, Ctrl ctrl, uint16_t self, const PadState &other) {
    snprintf(out, cap, "%s held=0x%03X other=%s/0x%03X", ctrl == Ctrl::F ? "FOOT" : "DOME", self,
             other.connected ? "on" : "off", other.held);
}

}  // namespace

static void test_new_mapping_matches_old_for_every_combo() {
    NoEeprom io;
    MapStore store(io, defaultButtonMappings, defaultButtonMappingsCount);
    store.begin();

    const uint16_t crossBits[] = {kHeldCross, kHeldCircle, kHeldPs};
    uint32_t compared = 0, same = 0, removed = 0;
    char newSent[40];
    char msg[120];

    for (uint8_t c = 0; c < 2; ++c) {
        const Ctrl ctrl = static_cast<Ctrl>(c);
        const LegacyMapping *table = ctrl == Ctrl::F ? kLegacyFoot : kLegacyDome;
        const size_t count = ctrl == Ctrl::F ? sizeof(kLegacyFoot) / sizeof(kLegacyFoot[0])
                                             : sizeof(kLegacyDome) / sizeof(kLegacyDome[0]);
        for (uint16_t held = 0; held < 1024; ++held) {
            if (!gateOpen(held))
                continue;  // the old code returned before looking at the table
            for (uint8_t o = 0; o < 16; ++o) {
                PadState other = {(o & 8) != 0, 0};
                for (uint8_t b = 0; b < 3; ++b) {
                    if (o & (1 << b))
                        other.held = static_cast<uint16_t>(other.held | crossBits[b]);
                }
                const PadState self = {true, held};
                const char *old = legacySend(table, count, self, other);

                const uint8_t slot = resolveSlot(ctrl, self, other);
                TEST_ASSERT_NOT_EQUAL(kNoSlot, slot);
                char key[kKeyBuf];
                Source src;
                TEST_ASSERT_TRUE(store.keyForSlot(slot, key, &src));
                newSent[0] = '\0';
                if (key[0] != '\0')
                    snprintf(newSent, sizeof(newSent), "%s%s%s\r", store.prefix(), key, store.suffix());

                ++compared;
                if (old == nullptr) {
                    describe(msg, sizeof(msg), ctrl, held, other);
                    TEST_ASSERT_EQUAL_STRING_MESSAGE("", newSent, msg);
                    ++removed;
                    continue;
                }
                if (strcmp(old, newSent) == 0) {
                    ++same;
                    continue;
                }
                describe(msg, sizeof(msg), ctrl, held, other);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
    char summary[100];
    snprintf(summary, sizeof(summary), "compared=%lu same=%lu removed=%lu", static_cast<unsigned long>(compared),
             static_cast<unsigned long>(same), static_cast<unsigned long>(removed));
    TEST_MESSAGE(summary);
    TEST_ASSERT_TRUE(removed > 0);
    TEST_ASSERT_EQUAL_UINT32(compared, same + removed);
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_new_mapping_matches_old_for_every_combo);
    return UNITY_END();
}
