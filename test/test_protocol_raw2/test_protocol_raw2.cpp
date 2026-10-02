// The @SM protocol, part 4: raw commands (v4) in DUMP and SUM, and the longest lines. Split from
// test_protocol_raw so each program fits the emulated Mega's SRAM. A raw command travels as "!" and its bytes in
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


// Protocol::kMaxLine: the longest reply is a SLOT with a 15-character raw command and a
// 15-character default on F CIRCLE RIGHTL (93 with its CRC); a PRESS with a raw command and
// from= is 81; the longest request, a SET with 15 raw bytes, is 65 (LineReader takes 72).
static void test_the_longest_lines_fit() {
    const uint8_t slot = slotIndex(Ctrl::F, Mod::CIRCLE, Btn::RIGHT, Tap::LONG);
    static FakeEeprom wideEeprom;  // its own EEPROM: the rig's store keeps its own records
    wideEeprom.reset();
    MapStore wide(wideEeprom, kWideDefaults, 1);
    wide.begin();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MapStore::Result::Ok),
                            static_cast<uint8_t>(wide.beginSet(slot, kRaw, true)));
    while (wide.busy()) {
        wide.step();
        wideEeprom.tick();
    }
    Protocol wideProtocol(wide, "test");
    TEST_ASSERT_TRUE(wideProtocol.handleLine("@SM GET F CIRCLE RIGHTL", rig->usb));
    wideProtocol.service();  // the reply goes out on a later pass than the request
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

// A SLOT reply takes two loop passes (sm_protocol.h): the request's pass reads the slot, a later
// one sends the line.

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_dump_and_sum_carry_raw_slots);
    RUN_TEST(test_the_longest_lines_fit);
    return UNITY_END();
}
