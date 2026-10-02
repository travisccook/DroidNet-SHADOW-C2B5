// The @SM protocol, part 3: raw commands (v4). A raw command travels as "!" and its bytes in
// upper-case hex, so the WCB never sees its own ';', '^' or ";T" in an @SM line (the simulated
// WCB counts any of them as a violation, and tearDown fails on one). The rig is in
// test/support/sm_protocol_rig.h.
#include <unity.h>

#include <sm_protocol_rig.h>

namespace {

// 15 characters with the WCB's own '^' and ";T", and its hex.
const char kRaw[] = ";W2;S4PLAY:3^;T";
const char kRawHex[] = "3B57323B5334504C41593A335E3B54";


}  // namespace

static void test_set_raw_round_trips_as_upper_case_hex() {
    char req[64], want[96];
    snprintf(req, sizeof(req), "@SM SET F NONE DOWN !%s", kRawHex);
    snprintf(want, sizeof(want), "@SM OK SET F NONE DOWN !%s", kRawHex);
    TEST_ASSERT_EQUAL_STRING(want, ask(req));
    snprintf(want, sizeof(want), "@SM SLOT F NONE DOWN !%s eeprom def=angrysound", kRawHex);
    TEST_ASSERT_EQUAL_STRING(want, ask("@SM GET F NONE DOWN"));
    char key[kKeyBuf];
    bool raw = false;
    TEST_ASSERT_TRUE(rig->store.keyForSlot(kSlotFootNoneDown, key, nullptr, &raw));
    TEST_ASSERT_EQUAL_STRING(kRaw, key);
    TEST_ASSERT_TRUE(raw);
    // Lower case is accepted; the echo is always upper case.
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN !3B5732", ask("@SM SET F NONE DOWN !3b5732"));
    // "*" and " - " are text here: not DroidNet's "back to default", not the wire's "unmapped".
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN !2A", ask("@SM SET F NONE DOWN !2A"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE DOWN !2A eeprom def=angrysound", ask("@SM GET F NONE DOWN"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN !202D20", ask("@SM SET F NONE DOWN !202D20"));
    // Back to a key: not raw.
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN scream", ask("@SM SET F NONE DOWN scream"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE DOWN scream eeprom def=angrysound", ask("@SM GET F NONE DOWN"));
}

static void test_set_raw_refuses_bad_hex() {
    // "!" alone, "!-" (hex decoding reads "-" as nothing), odd length, a bad digit, NUL, a
    // control byte, DEL, non-ASCII, and 16 bytes.
    const char *const bad[] = {"!", "!-", "!3", "!3G", "!00", "!1F", "!7F", "!C3A9",
                               "!4142434445464748494A4B4C4D4E4F50"};
    char req[64];
    for (const char *arg : bad) {
        snprintf(req, sizeof(req), "@SM SET F NONE DOWN %s", arg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("@SM ERR key SET F NONE DOWN", ask(req), arg);
    }
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE DOWN angrysound default def=angrysound", ask("@SM GET F NONE DOWN"));
}

// Review Focus 2, on the wire: a GET of a raw slot while it waits behind the first format, and
// while its own bytes are going in, answers the text's hex, never the flagged byte's.
static void test_get_during_a_raw_write_answers_the_text() {
    rig->eeprom.writeTicks = 20;
    char req[64], want[96], ok[96];
    snprintf(req, sizeof(req), "@SM SET D L2 RIGHT !%s", kRawHex);
    snprintf(want, sizeof(want), "@SM SLOT D L2 RIGHT !%s eeprom def=-", kRawHex);
    snprintf(ok, sizeof(ok), "@SM OK SET D L2 RIGHT !%s", kRawHex);
    send(req);  // the first write: the header goes in first, the slot waits in RAM
    TEST_ASSERT_EQUAL_STRING(want, ask("@SM GET D L2 RIGHT"));
    TEST_ASSERT_EQUAL_STRING(ok, await());
    send("@SM SET D L2 RIGHT !3F");  // "?"
    for (int i = 0; i < 3; ++i)
        pass();
    TEST_ASSERT_TRUE(rig->store.busy());
    TEST_ASSERT_EQUAL_STRING("@SM SLOT D L2 RIGHT !3F eeprom def=-", ask("@SM GET D L2 RIGHT"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET D L2 RIGHT !3F", await());
}

static void test_press_carries_the_raw_token() {
    ask("@SM EVENTS 1");
    rig->protocol.notifyPress(kSlotFootNoneDown, kRaw, Source::Eeprom, false, true);
    char want[96];
    snprintf(want, sizeof(want), "@SM PRESS F NONE DOWN !%s eeprom", kRawHex);
    TEST_ASSERT_EQUAL_STRING(want, await());
    rig->protocol.notifyPress(kSlotFootNoneDown, "scream", Source::Eeprom, false);
    TEST_ASSERT_EQUAL_STRING("@SM PRESS F NONE DOWN scream eeprom", await());
}

// Review Focus 1: the press's own command is already queued when its event is built. Checked
// against kMaxLine (96), a 22-34 byte command leaves too little room (8 + 96 + 1 + 34 = 139 of
// 160) and every such event is dropped. Checked against its own length, it fits.
static void test_a_long_command_keeps_its_press_event() {
    ask("@SM EVENTS 1");
    TEST_ASSERT_EQUAL_UINT8(0, rig->tx.used());
    TEST_ASSERT_TRUE(pushCommand(rig->tx, ";SEQPREFIX1", "abcdefghijklmno", ",SUFFIX"));  // 34 bytes, the longest
    rig->protocol.notifyPress(kSlotFootNoneDown, "abcdefghijklmno", Source::Eeprom, false);
    TEST_ASSERT_EQUAL_STRING("@SM PRESS F NONE DOWN abcdefghijklmno eeprom", await());
    TEST_ASSERT_EQUAL_STRING(";SEQPREFIX1abcdefghijklmno,SUFFIX", rig->wcb.commands.last());
}

static void test_a_slot_reply_takes_two_passes() {
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM GET F L1 UP", rig->usb));
    TEST_ASSERT_EQUAL_UINT32(0, rig->usb.log.count);
    TEST_ASSERT_TRUE(rig->protocol.opInProgress());  // so the simulator and test_avr keep pumping
    TEST_ASSERT_FALSE(rig->protocol.handleLine("@SM GET D L2 RIGHT", rig->usb));  // one at a time: next pass
    rig->protocol.service();
    TEST_ASSERT_EQUAL_UINT32(1, rig->usb.log.count);
    char body[96];
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default def=extremehappysou", body);
    TEST_ASSERT_FALSE(rig->protocol.opInProgress());
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM GET D L2 RIGHT", rig->usb));
}

// A GET during a DUMP waits for the DUMP's line in hand, then goes out between two DUMP lines,
// long before the DUMP reaches D CROSS LEFT itself (slot 69); the DUMP still sends all 216.
static void test_a_get_during_a_dump_is_answered_between_its_lines() {
    send("@SM DUMP");
    char get[64], body[96];
    withCrc("@SM GET D CROSS LEFT", get, sizeof(get));
    bool asked = false;
    uint32_t slots = 0, crossLeft = 0, ends = 0, firstCrossLeftAt = 0;
    for (int i = 0; i < 30000 && ends == 0; ++i) {
        if (i >= 40 && !asked)
            asked = rig->protocol.handleLine(get, rig->wcbPort);
        const uint32_t before = rig->wcb.usb.count;
        pass(4);
        for (uint32_t k = rig->wcb.usb.count - before; k > 0; --k) {
            TEST_ASSERT_TRUE(stripCrc(rig->wcb.usb.last(static_cast<uint8_t>(k - 1)), body, sizeof(body)));
            if (strncmp(body, "@SM SLOT ", 9) == 0)
                ++slots;
            if (strcmp(body, "@SM SLOT D CROSS LEFT scanning default def=scanning") == 0 && ++crossLeft == 1)
                firstCrossLeftAt = slots;  // SLOT lines so far, this one included
            if (strcmp(body, "@SM END DUMP 216") == 0)
                ++ends;
        }
    }
    TEST_ASSERT_TRUE(asked);
    TEST_ASSERT_EQUAL_UINT32(1, ends);
    TEST_ASSERT_EQUAL_UINT32(sm::kSlotCount + 1, slots);  // the DUMP's 216 and the GET's line
    TEST_ASSERT_EQUAL_UINT32(2, crossLeft);
    // The DUMP's own D CROSS LEFT line is its 70th; the GET's came first, between earlier lines.
    TEST_ASSERT_TRUE_MESSAGE(firstCrossLeftAt <= kSlotDomeCrossLeft, "the GET waited out the DUMP");
    TEST_ASSERT_FALSE(rig->protocol.opInProgress());
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_set_raw_round_trips_as_upper_case_hex);
    RUN_TEST(test_set_raw_refuses_bad_hex);
    RUN_TEST(test_get_during_a_raw_write_answers_the_text);
    RUN_TEST(test_press_carries_the_raw_token);
    RUN_TEST(test_a_long_command_keeps_its_press_event);
    RUN_TEST(test_a_slot_reply_takes_two_passes);
    RUN_TEST(test_a_get_during_a_dump_is_answered_between_its_lines);
    return UNITY_END();
}
