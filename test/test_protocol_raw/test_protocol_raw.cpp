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

// F CIRCLE RIGHTL with a 15-character default (no real firmware has one there), for the
// longest SLOT line.
const DefaultMapping kWideDefaults[] PROGMEM = {
    {slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT, Tap::LONG), "abcdefghijklmno"},
};

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

static void test_dump_and_sum_carry_raw_slots() {
    char req[64], want[96];
    snprintf(req, sizeof(req), "@SM SET D CROSS LEFT !%s", kRawHex);
    ask(req);
    ask("@SM SET F L2 PS2 !2A");
    ask("@SM SET F L1 UPL !20");
    snprintf(want, sizeof(want), "@SM SLOT D CROSS LEFT !%s eeprom def=scanning", kRawHex);
    send("@SM DUMP");
    uint32_t crc = kCrc32Init;
    char body[96];
    bool sawRaw = false, sawStar = false, sawSpace = false;
    uint32_t seen = 0;
    for (int i = 0; i < 40000 && seen < sm::kSlotCount + 1; ++i) {
        const uint32_t before = rig->wcb.usb.count;
        pass();
        for (uint32_t k = rig->wcb.usb.count - before; k > 0; --k) {
            TEST_ASSERT_TRUE(stripCrc(rig->wcb.usb.last(static_cast<uint8_t>(k - 1)), body, sizeof(body)));
            if (++seen <= sm::kSlotCount) {
                sawRaw = sawRaw || strcmp(body, want) == 0;
                sawStar = sawStar || strcmp(body, "@SM SLOT F L2 PS2 !2A eeprom def=-") == 0;
                sawSpace = sawSpace || strcmp(body, "@SM SLOT F L1 UPL !20 eeprom def=-") == 0;
                for (const char *c = body + 9; *c != '\0'; ++c)  // after "@SM SLOT "
                    crc = crc32Update(crc, static_cast<uint8_t>(*c));
                crc = crc32Update(crc, '\n');
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(sm::kSlotCount + 1, seen);
    TEST_ASSERT_TRUE(sawRaw);
    TEST_ASSERT_TRUE(sawStar);
    TEST_ASSERT_TRUE(sawSpace);
    char sum[40];
    snprintf(sum, sizeof(sum), "@SM SUM %08lX slots=216", static_cast<unsigned long>(crc32Final(crc)));
    TEST_ASSERT_EQUAL_STRING(sum, ask("@SM SUM"));
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

// Protocol::kMaxLine: the longest reply is a SLOT with a 15-character raw command and a
// 15-character default on F CIRCLE RIGHTL (93 with its CRC); a PRESS with a raw command and
// from= is 81; the longest request, a SET with 15 raw bytes, is 65 (LineReader takes 72).
static void test_the_longest_lines_fit() {
    const uint8_t slot = slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT, Tap::LONG);
    MapStore wide(rig->eeprom, kWideDefaults, 1);
    wide.begin();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(wide.beginSet(slot, kRaw, true)));
    while (wide.busy()) {
        wide.step();
        rig->eeprom.tick();
    }
    Protocol wideProtocol(wide, "test");
    TEST_ASSERT_TRUE(wideProtocol.handleLine("@SM GET F CIRCLE RIGHTL", rig->usb));
    char want[100], body[100];
    snprintf(want, sizeof(want), "@SM SLOT F CIRCLE RIGHTL !%s eeprom def=abcdefghijklmno", kRawHex);
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING(want, body);
    TEST_ASSERT_EQUAL_UINT32(93, strlen(rig->usb.log.last()));
    TEST_ASSERT_TRUE(strlen(rig->usb.log.last()) <= Protocol::kMaxLine);

    ask("@SM EVENTS 1");
    rig->protocol.notifyPress(slot, kRaw, Source::Eeprom, true, true);
    await();
    TEST_ASSERT_EQUAL_UINT32(81, strlen(rig->wcb.usb.last()));

    char req[80], line[96];
    snprintf(req, sizeof(req), "@SM SET F CIRCLE RIGHTL !%s", kRawHex);
    withCrc(req, line, sizeof(line));
    TEST_ASSERT_EQUAL_UINT32(65, strlen(line));
    TEST_ASSERT_TRUE(strlen(line) <= LineReader::kMaxLen);
    for (const char *p = line; *p != '\0'; ++p)
        rig->reader.feed(*p);
    TEST_ASSERT_TRUE(rig->reader.feed('\r'));
    TEST_ASSERT_TRUE(rig->protocol.handleLine(rig->reader.line(), rig->wcbPort));
    rig->reader.consume();
    char ok[96];
    snprintf(ok, sizeof(ok), "@SM OK SET F CIRCLE RIGHTL !%s", kRawHex);
    TEST_ASSERT_EQUAL_STRING(ok, await());  // all 30 digits: the echo fits opArg_
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_set_raw_round_trips_as_upper_case_hex);
    RUN_TEST(test_set_raw_refuses_bad_hex);
    RUN_TEST(test_get_during_a_raw_write_answers_the_text);
    RUN_TEST(test_dump_and_sum_carry_raw_slots);
    RUN_TEST(test_press_carries_the_raw_token);
    RUN_TEST(test_a_long_command_keeps_its_press_event);
    RUN_TEST(test_the_longest_lines_fit);
    return UNITY_END();
}
