// The @SM protocol end to end: requests arrive as SHADOW would get them from the WCB,
// replies leave through the TX queue and a simulated WCB, and the checks are on what the Pi
// would read on the WCB's USB port. Part 1: HELLO, GET, SET, DUMP and flow control.
#include <unity.h>

#include <sm_protocol_rig.h>

static void test_hello_reports_version_and_state() {
    TEST_ASSERT_EQUAL_STRING("@SM READY v3 fw=test slots=216 gen=0 fmt=0 pads=-", ask("@SM HELLO"));
    TEST_ASSERT_EQUAL_UINT8(0, rig->wcb.lastRoute);
}

static void test_hello_sets_reply_route() {
    ask("@SM HELLO 3");
    TEST_ASSERT_EQUAL_UINT8(3, rig->wcb.lastRoute);
    TEST_ASSERT_EQUAL_STRING(";W3;S0", rig->wcbPort.routePrefix());
    ask("@SM HELLO 12");
    TEST_ASSERT_EQUAL_UINT8(12, rig->wcb.lastRoute);
    TEST_ASSERT_EQUAL_STRING(";W12,;S0", rig->wcbPort.routePrefix());
    TEST_ASSERT_EQUAL_STRING("@SM ERR args HELLO", ask("@SM HELLO 21"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args HELLO", ask("@SM HELLO x"));
    ask("@SM HELLO 0");
    TEST_ASSERT_EQUAL_UINT8(0, rig->wcb.lastRoute);
}

static void test_crc_is_required_on_the_wcb_port() {
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM HELLO", rig->wcbPort));
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", await());
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM HELLO *00000000", rig->wcbPort));
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", await());
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM HELLO *1234", rig->wcbPort));
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", await());
    // Lower-case hex is fine.
    char line[64];
    withCrc("@SM HELLO", line, sizeof(line));
    for (char *p = strstr(line, " *"); *p != '\0'; ++p) {
        if (*p >= 'A' && *p <= 'F')
            *p = static_cast<char>(*p - 'A' + 'a');
    }
    TEST_ASSERT_TRUE(rig->protocol.handleLine(line, rig->wcbPort));
    TEST_ASSERT_EQUAL_STRING("@SM READY v3 fw=test slots=216 gen=0 fmt=0 pads=-", await());
}

static void test_usb_port_accepts_lines_without_crc() {
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM GET F L1 UP", rig->usb));
    TEST_ASSERT_EQUAL_UINT32(0, rig->usb.log.count);
    rig->protocol.service();  // a SLOT reply goes out on a later pass than its request
    char body[96];
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default def=extremehappysou", body);
    // A CRC that is present must still be right.
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM GET F L1 UP *DEADBEEF", rig->usb));
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", body);
}

static void test_get_reports_source_and_reserved_reason() {
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default def=extremehappysou", ask("@SM GET F L1 UP"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT D L2 RIGHT - default def=-", ask("@SM GET D L2 RIGHT"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE PS - reserved res=psgate", ask("@SM GET F NONE PS"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 L3 - reserved res=overspeed", ask("@SM GET F L1 L3"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR slot GET", ask("@SM GET F L1"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR slot GET", ask("@SM GET X L1 UP"));
}

static void test_set_replies_after_the_write_lands() {
    send("@SM SET F NONE DOWN scream");
    TEST_ASSERT_TRUE(rig->store.busy());
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN scream", await());
    TEST_ASSERT_FALSE(rig->store.busy());
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE DOWN scream eeprom def=angrysound", ask("@SM GET F NONE DOWN"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN -", ask("@SM SET F NONE DOWN -"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE DOWN - eeprom def=angrysound", ask("@SM GET F NONE DOWN"));
    TEST_ASSERT_EQUAL_STRING("@SM READY v3 fw=test slots=216 gen=0 fmt=1 pads=-", ask("@SM HELLO"));
}

static void test_set_errors() {
    TEST_ASSERT_EQUAL_STRING("@SM ERR reserved SET F L1 L3", ask("@SM SET F L1 L3 x"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR key SET F NONE UP", ask("@SM SET F NONE UP extremehappysoun"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR key SET F NONE UP", ask("@SM SET F NONE UP bad-key"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR slot SET", ask("@SM SET F NONE UP"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR slot SET", ask("@SM SET F NOPE UP x"));
}

static void test_one_write_at_a_time() {
    send("@SM SET F NONE DOWN one");
    TEST_ASSERT_EQUAL_STRING("@SM ERR busy SET F NONE UP", ask("@SM SET F NONE UP two"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR busy DEFAULTS", ask("@SM DEFAULTS"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR busy DUMP", ask("@SM DUMP"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN one", await());
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE UP two", ask("@SM SET F NONE UP two"));
}

static void test_get_during_a_write_is_answered() {
    rig->eeprom.writeTicks = 20;
    send("@SM SET D L2 RIGHT longkeyvalue12");
    for (int i = 0; i < 3; ++i)
        pass();
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default def=extremehappysou", ask("@SM GET F L1 UP"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET D L2 RIGHT longkeyvalue12", await());
}

static void test_dump_streams_every_slot_then_end() {
    ask("@SM SET D CROSS LEFT custom");
    ask("@SM SET D CROSS LEFT2 twice");
    send("@SM DUMP");
    char body[96];
    uint32_t seen = 0;
    bool sawCustom = false, sawReserved = false, sawDouble = false, sawStick = false;
    for (int i = 0; i < 20000 && seen < sm::kSlotCount + 1; ++i) {
        const uint32_t before = rig->wcb.usb.count;
        pass(4);  // a slow UART: about what 9600 baud drains between passes
        const uint32_t added = rig->wcb.usb.count - before;
        TEST_ASSERT_TRUE(added <= LineLog::kLines);
        for (uint32_t k = added; k > 0; --k) {
            TEST_ASSERT_TRUE_MESSAGE(stripCrc(rig->wcb.usb.last(static_cast<uint8_t>(k - 1)), body, sizeof(body)),
                                     rig->wcb.usb.last(static_cast<uint8_t>(k - 1)));
            ++seen;
            if (seen <= sm::kSlotCount) {
                TEST_ASSERT_EQUAL_INT(0, strncmp(body, "@SM SLOT ", 9));
                if (strcmp(body, "@SM SLOT D CROSS LEFT custom eeprom def=scanning") == 0)
                    sawCustom = true;
                if (strcmp(body, "@SM SLOT D NONE PS - reserved res=psgate") == 0)
                    sawReserved = true;
                if (strcmp(body, "@SM SLOT D CROSS LEFT2 twice eeprom def=-") == 0)
                    sawDouble = true;
                if (strcmp(body, "@SM SLOT F NONE L32 - reserved res=stick") == 0)
                    sawStick = true;
            } else {
                TEST_ASSERT_EQUAL_STRING("@SM END DUMP 216", body);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(sm::kSlotCount + 1, seen);
    TEST_ASSERT_TRUE(sawCustom);
    TEST_ASSERT_TRUE(sawReserved);
    TEST_ASSERT_TRUE(sawDouble);
    TEST_ASSERT_TRUE(sawStick);
    TEST_ASSERT_FALSE(rig->protocol.opInProgress());
}

static void test_button_commands_always_fit_during_a_dump() {
    send("@SM DUMP");
    int commands = 0;
    for (int i = 0; i < 20000 && rig->protocol.opInProgress(); ++i) {
        if (i % 7 == 0) {
            TEST_ASSERT_TRUE_MESSAGE(pushCommand(rig->tx, rig->store.prefix(), "extremehappysou", rig->store.suffix()),
                                     "button command dropped");
            ++commands;
        }
        pass(3);
    }
    for (int i = 0; i < 200; ++i)
        pass();
    TEST_ASSERT_TRUE(commands > 20);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(commands), rig->wcb.commands.count);
    TEST_ASSERT_EQUAL_STRING(";W1;Cextremehappysou", rig->wcb.commands.last());
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.overlong);
}

static void test_defaults_resets_everything() {
    ask("@SM SET F L1 UP custom");
    TEST_ASSERT_EQUAL_STRING("@SM OK DEFAULTS gen=1", ask("@SM DEFAULTS"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default def=extremehappysou", ask("@SM GET F L1 UP"));
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_hello_reports_version_and_state);
    RUN_TEST(test_hello_sets_reply_route);
    RUN_TEST(test_crc_is_required_on_the_wcb_port);
    RUN_TEST(test_usb_port_accepts_lines_without_crc);
    RUN_TEST(test_get_reports_source_and_reserved_reason);
    RUN_TEST(test_set_replies_after_the_write_lands);
    RUN_TEST(test_set_errors);
    RUN_TEST(test_one_write_at_a_time);
    RUN_TEST(test_get_during_a_write_is_answered);
    RUN_TEST(test_dump_streams_every_slot_then_end);
    RUN_TEST(test_button_commands_always_fit_during_a_dump);
    RUN_TEST(test_defaults_resets_everything);
    return UNITY_END();
}
