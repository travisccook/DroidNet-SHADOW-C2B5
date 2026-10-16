// @SM MAC: the controller-address requests (WIPDocs/CONTROLLER_ADDRESSES_DESIGN.md).
#include <unity.h>

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
