// The @SM protocol, part 2: PREFIX/SUFFIX, events, malformed input, REVERT, SUM and
// controller state. The rig is in test/support/sm_protocol_rig.h.
#include <unity.h>

#include <sm_protocol_rig.h>

static void test_prefix_and_suffix_travel_as_hex() {
    TEST_ASSERT_EQUAL_STRING("@SM CFG pre=3B57313B43 suf=-", ask("@SM CFG"));
    TEST_ASSERT_EQUAL_STRING("@SM OK PREFIX 3B57323B43", ask("@SM PREFIX 3B57323B43"));  // ;W2;C
    TEST_ASSERT_EQUAL_STRING("@SM OK SUFFIX 2C4C", ask("@SM SUFFIX 2C4C"));              // ,L
    TEST_ASSERT_EQUAL_STRING("@SM CFG pre=3B57323B43 suf=2C4C", ask("@SM CFG"));
    TEST_ASSERT_EQUAL_STRING(";W2;C", rig->store.prefix());
    TEST_ASSERT_EQUAL_STRING(",L", rig->store.suffix());
    TEST_ASSERT_EQUAL_STRING("@SM OK SUFFIX -", ask("@SM SUFFIX -"));
    TEST_ASSERT_EQUAL_STRING("", rig->store.suffix());

    TEST_ASSERT_EQUAL_STRING("@SM ERR prefix PREFIX", ask("@SM PREFIX 5731"));        // "W1": no leading ';'
    TEST_ASSERT_EQUAL_STRING("@SM ERR prefix PREFIX", ask("@SM PREFIX 3B57315E43"));  // contains '^'
    TEST_ASSERT_EQUAL_STRING("@SM ERR args PREFIX", ask("@SM PREFIX 3B5"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args PREFIX", ask("@SM PREFIX -"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args PREFIX", ask("@SM PREFIX 3B00"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args SUFFIX", ask("@SM SUFFIX 2C4C2C4C2C4C2C4C"));
}

static void test_events_report_presses() {
    rig->protocol.notifyPress(kSlotFootL1Up, "extremehappysou", Source::Default, false);
    for (int i = 0; i < 50; ++i)
        pass();
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.usb.count);  // off by default
    TEST_ASSERT_EQUAL_STRING("@SM OK EVENTS 1", ask("@SM EVENTS 1"));
    rig->protocol.notifyPress(kSlotFootL1Up, "extremehappysou", Source::Default, false);
    TEST_ASSERT_EQUAL_STRING("@SM PRESS F L1 UP extremehappysou default", await());
    rig->protocol.notifyPress(slotIndex(Ctrl::F, Mod::L1, Btn::L3), "", Source::Reserved, false);
    TEST_ASSERT_EQUAL_STRING("@SM PRESS F L1 L3 - reserved res=overspeed", await());
    TEST_ASSERT_EQUAL_STRING("@SM OK EVENTS 0", ask("@SM EVENTS 0"));
    const uint32_t before = rig->wcb.usb.count;
    rig->protocol.notifyPress(kSlotFootL1Up, "x", Source::Eeprom, false);
    for (int i = 0; i < 50; ++i)
        pass();
    TEST_ASSERT_EQUAL_UINT32(before, rig->wcb.usb.count);
    TEST_ASSERT_EQUAL_STRING("@SM ERR args EVENTS", ask("@SM EVENTS 2"));
}

static void test_malformed_requests() {
    TEST_ASSERT_EQUAL_STRING("@SM ERR verb FOO", ask("@SM FOO"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR verb", ask("@SM foo^bar"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args", ask("@SM "));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args GET", ask("@SM GET  F L1 UP"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args GET", ask("@SM GET F L1 UP "));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args A", ask("@SM A B C D E F G H"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args DUMP", ask("@SM DUMP now"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR args CFG", ask("@SM CFG x"));
}

static void test_refuses_requests_when_it_cannot_reply() {
    // Fill the queue so there's no room for a reply plus the command headroom.
    const char *filler[] = {"; filler filler filler filler filler filler filler filler"};
    while (rig->tx.pushLine(filler, 1)) {
    }
    char line[64];
    withCrc("@SM HELLO", line, sizeof(line));
    TEST_ASSERT_FALSE(rig->protocol.handleLine(line, rig->wcbPort));
    for (int i = 0; i < 20; ++i)
        pass();
    TEST_ASSERT_TRUE(rig->protocol.handleLine(line, rig->wcbPort));
}

// Bytes as they'd arrive on Serial1: broadcast chatter, noise, a request split across reads.
static void test_line_reader_to_protocol() {
    char request[64];
    withCrc("@SM GET D CROSS LEFT", request, sizeof(request));
    char stream[200];
    snprintf(stream, sizeof(stream), "@Atrigger\r{\"json\":1}\n\x01\x02garbage\r\n%s\r\n@SM", request);
    uint8_t handled = 0;
    for (const char *p = stream; *p != '\0'; ++p) {
        if (rig->reader.feed(*p)) {
            TEST_ASSERT_TRUE(rig->protocol.handleLine(rig->reader.line(), rig->wcbPort));
            rig->reader.consume();
            ++handled;
        }
    }
    TEST_ASSERT_EQUAL_UINT8(1, handled);
    TEST_ASSERT_EQUAL_STRING("@SM SLOT D CROSS LEFT scanning default def=scanning", await());
}


static void test_revert_returns_a_slot_to_its_default() {
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F L1 UP custom", ask("@SM SET F L1 UP custom"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP custom eeprom def=extremehappysou", ask("@SM GET F L1 UP"));
    TEST_ASSERT_EQUAL_STRING("@SM OK REVERT F L1 UP", ask("@SM REVERT F L1 UP"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default def=extremehappysou", ask("@SM GET F L1 UP"));
    TEST_ASSERT_EQUAL_STRING("@SM OK REVERT D L2 RIGHT", ask("@SM REVERT D L2 RIGHT"));  // already default
    TEST_ASSERT_EQUAL_STRING("@SM ERR reserved REVERT F L1 L3", ask("@SM REVERT F L1 L3"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR slot REVERT", ask("@SM REVERT F L1"));
}

// SUM is the CRC32 of the DUMP bodies, so the Pi can check its cached copy without a DUMP.
static void test_sum_matches_the_dump() {
    ask("@SM SET D CROSS LEFT custom");
    ask("@SM SET F NONE UP -");
    ask("@SM SET F L2 PS2 twice");
    send("@SM DUMP");
    uint32_t crc = kCrc32Init;
    char body[96];
    uint32_t seen = 0;
    for (int i = 0; i < 40000 && seen < sm::kSlotCount + 1; ++i) {
        const uint32_t before = rig->wcb.usb.count;
        pass();
        for (uint32_t k = rig->wcb.usb.count - before; k > 0; --k) {
            TEST_ASSERT_TRUE(stripCrc(rig->wcb.usb.last(static_cast<uint8_t>(k - 1)), body, sizeof(body)));
            if (++seen <= sm::kSlotCount) {
                for (const char *c = body + 9; *c != '\0'; ++c)  // after "@SM SLOT "
                    crc = crc32Update(crc, static_cast<uint8_t>(*c));
                crc = crc32Update(crc, '\n');
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(sm::kSlotCount + 1, seen);
    char want[40];
    snprintf(want, sizeof(want), "@SM SUM %08lX slots=216", static_cast<unsigned long>(crc32Final(crc)));
    TEST_ASSERT_EQUAL_STRING(want, ask("@SM SUM"));
    // Any change moves it.
    ask("@SM SET D CROSS LEFT other");
    TEST_ASSERT_FALSE(strcmp(want, ask("@SM SUM")) == 0);
    TEST_ASSERT_EQUAL_STRING("@SM ERR args SUM", ask("@SM SUM now"));
}

static void test_press_reports_modifier_from_other_controller() {
    ask("@SM EVENTS 1");
    rig->protocol.notifyPress(slotIndex(Ctrl::F, Mod::CROSS, Btn::UP), "leia", Source::Default, true);
    TEST_ASSERT_EQUAL_STRING("@SM PRESS F CROSS UP leia default from=D", await());
    rig->protocol.notifyPress(slotIndex(Ctrl::D, Mod::PS, Btn::LEFT), "", Source::Default, true);
    TEST_ASSERT_EQUAL_STRING("@SM PRESS D PS LEFT - default from=F", await());
}

static void test_controller_state_in_ready_and_events() {
    rig->protocol.setPads(true, false);
    TEST_ASSERT_EQUAL_STRING("@SM READY v4 fw=test slots=216 gen=0 fmt=0 pads=F", ask("@SM HELLO"));
    ask("@SM EVENTS 1");
    for (int i = 0; i < 30; ++i)
        pass();
    const uint32_t quiet = rig->wcb.usb.count;
    TEST_ASSERT_EQUAL_UINT32(quiet, rig->wcb.usb.count);  // no event for the state it started with
    rig->protocol.setPads(true, true);
    TEST_ASSERT_EQUAL_STRING("@SM CTL D 1", await());
    rig->protocol.setPads(false, false);
    TEST_ASSERT_EQUAL_STRING("@SM CTL F 0", await());
    TEST_ASSERT_EQUAL_STRING("@SM CTL D 0", await());
    TEST_ASSERT_EQUAL_STRING("@SM READY v4 fw=test slots=216 gen=0 fmt=0 pads=-", ask("@SM HELLO"));
    rig->protocol.setPads(false, true);
    TEST_ASSERT_EQUAL_STRING("@SM READY v4 fw=test slots=216 gen=0 fmt=0 pads=D", ask("@SM HELLO"));
}

static void test_long_press_slots_v3() {
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UPL - default def=-", ask("@SM GET F L1 UPL"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE L3L - reserved res=stick", ask("@SM GET F NONE L3L"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 L3L - reserved res=overspeed", ask("@SM GET F L1 L3L"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR reserved SET F NONE L3L", ask("@SM SET F NONE L3L quiet"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR slot GET", ask("@SM GET F L1 UP3"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F L1 UPL quiet", ask("@SM SET F L1 UPL quiet"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UPL quiet eeprom def=-", ask("@SM GET F L1 UPL"));
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_prefix_and_suffix_travel_as_hex);
    RUN_TEST(test_events_report_presses);
    RUN_TEST(test_malformed_requests);
    RUN_TEST(test_refuses_requests_when_it_cannot_reply);
    RUN_TEST(test_line_reader_to_protocol);
    RUN_TEST(test_revert_returns_a_slot_to_its_default);
    RUN_TEST(test_sum_matches_the_dump);
    RUN_TEST(test_press_reports_modifier_from_other_controller);
    RUN_TEST(test_controller_state_in_ready_and_events);
    RUN_TEST(test_long_press_slots_v3);
    return UNITY_END();
}
