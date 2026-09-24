// The @SM protocol end to end: requests arrive as SHADOW would get them from the WCB,
// replies leave through the TX queue and a simulated WCB, and the checks are on what the Pi
// would read on the WCB's USB port.
#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#define SM_TEST_OWN_SETUP
#include <sm_test_main.h>
#include <new>
#include <stdio.h>

using namespace sm;

SM_DEFINE_TEST_DEFAULTS

namespace {

struct Rig {
    FakeEeprom eeprom;
    MapStore store{eeprom, kTestDefaults, kTestDefaultCount};
    TxQueue tx;
    WcbPort wcbPort{tx};
    Protocol protocol{store, "test"};
    WcbSim wcb;
    CapturePort usb;
    LineReader reader;
};

Rig *rig = nullptr;

// One main-loop pass, in the order main.cpp uses.
void pass(int uartBytes = 63) {
    rig->protocol.service();
    rig->store.step();
    rig->eeprom.tick();
    rig->tx.drain(rig->wcb);
    rig->wcb.refill(uartBytes);
}

// Sends a request line (CRC added) to the WCB port, retrying while it's refused.
void send(const char *body) {
    char line[96];
    withCrc(body, line, sizeof(line));
    for (int i = 0; i < 200; ++i) {
        if (rig->protocol.handleLine(line, rig->wcbPort))
            return;
        pass();
    }
    TEST_FAIL_MESSAGE("request never accepted");
}

// Pumps until the Pi has received `count` more lines. Returns the last one, CRC checked and
// stripped.
const char *await(uint32_t count = 1, int maxPasses = 2000) {
    static char body[96];
    const uint32_t target = rig->wcb.usb.count + count;
    for (int i = 0; i < maxPasses && rig->wcb.usb.count < target; ++i)
        pass();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(target, rig->wcb.usb.count, "no reply");
    TEST_ASSERT_TRUE_MESSAGE(stripCrc(rig->wcb.usb.last(), body, sizeof(body)), rig->wcb.usb.last());
    return body;
}

const char *ask(const char *body) {
    send(body);
    return await();
}

}  // namespace

void setUp() {
    static Rig instance;
    instance.~Rig();
    new (&instance) Rig();
    rig = &instance;
    rig->store.begin();
}

void tearDown() {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.violations, "SHADOW broke a WCB rule");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->wcb.overruns, "wrote past the UART buffer");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rig->eeprom.violations, "EEPROM touched while busy");
}

static void test_hello_reports_version_and_state() {
    TEST_ASSERT_EQUAL_STRING("@SM READY v1 fw=test slots=72 gen=0 fmt=0", ask("@SM HELLO"));
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
    TEST_ASSERT_EQUAL_STRING("@SM READY v1 fw=test slots=72 gen=0 fmt=0", await());
}

static void test_usb_port_accepts_lines_without_crc() {
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM GET F L1 UP", rig->usb));
    char body[96];
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default", body);
    // A CRC that is present must still be right.
    TEST_ASSERT_TRUE(rig->protocol.handleLine("@SM GET F L1 UP *DEADBEEF", rig->usb));
    TEST_ASSERT_TRUE(stripCrc(rig->usb.log.last(), body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("@SM ERR crc", body);
}

static void test_get_reports_source_and_reserved_reason() {
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default", ask("@SM GET F L1 UP"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT D L2 RIGHT - default", ask("@SM GET D L2 RIGHT"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE PS - reserved psgate", ask("@SM GET F NONE PS"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 L3 - reserved overspeed", ask("@SM GET F L1 L3"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR slot GET", ask("@SM GET F L1"));
    TEST_ASSERT_EQUAL_STRING("@SM ERR slot GET", ask("@SM GET X L1 UP"));
}

static void test_set_replies_after_the_write_lands() {
    send("@SM SET F NONE DOWN scream");
    TEST_ASSERT_TRUE(rig->store.busy());
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN scream", await());
    TEST_ASSERT_FALSE(rig->store.busy());
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE DOWN scream eeprom", ask("@SM GET F NONE DOWN"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET F NONE DOWN -", ask("@SM SET F NONE DOWN -"));
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F NONE DOWN - eeprom", ask("@SM GET F NONE DOWN"));
    TEST_ASSERT_EQUAL_STRING("@SM READY v1 fw=test slots=72 gen=0 fmt=1", ask("@SM HELLO"));
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
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default", ask("@SM GET F L1 UP"));
    TEST_ASSERT_EQUAL_STRING("@SM OK SET D L2 RIGHT longkeyvalue12", await());
}

static void test_dump_streams_every_slot_then_end() {
    ask("@SM SET D CROSS LEFT custom");
    send("@SM DUMP");
    char body[96];
    uint32_t seen = 0;
    bool sawCustom = false, sawReserved = false;
    for (int i = 0; i < 20000 && seen < 73; ++i) {
        const uint32_t before = rig->wcb.usb.count;
        pass(4);  // a slow UART: about what 9600 baud drains between passes
        const uint32_t added = rig->wcb.usb.count - before;
        TEST_ASSERT_TRUE(added <= LineLog::kLines);
        for (uint32_t k = added; k > 0; --k) {
            TEST_ASSERT_TRUE_MESSAGE(stripCrc(rig->wcb.usb.last(static_cast<uint8_t>(k - 1)), body, sizeof(body)),
                                     rig->wcb.usb.last(static_cast<uint8_t>(k - 1)));
            ++seen;
            if (seen <= 72) {
                TEST_ASSERT_EQUAL_INT(0, strncmp(body, "@SM SLOT ", 9));
                if (strcmp(body, "@SM SLOT D CROSS LEFT custom eeprom") == 0)
                    sawCustom = true;
                if (strcmp(body, "@SM SLOT D NONE PS - reserved psgate") == 0)
                    sawReserved = true;
            } else {
                TEST_ASSERT_EQUAL_STRING("@SM END DUMP 72", body);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(73, seen);
    TEST_ASSERT_TRUE(sawCustom);
    TEST_ASSERT_TRUE(sawReserved);
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
    TEST_ASSERT_EQUAL_STRING("@SM SLOT F L1 UP extremehappysou default", ask("@SM GET F L1 UP"));
}

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
    rig->protocol.notifyPress(kSlotFootL1Up, "extremehappysou", Source::Default);
    for (int i = 0; i < 50; ++i)
        pass();
    TEST_ASSERT_EQUAL_UINT32(0, rig->wcb.usb.count);  // off by default
    TEST_ASSERT_EQUAL_STRING("@SM OK EVENTS 1", ask("@SM EVENTS 1"));
    rig->protocol.notifyPress(kSlotFootL1Up, "extremehappysou", Source::Default);
    TEST_ASSERT_EQUAL_STRING("@SM PRESS F L1 UP extremehappysou default", await());
    rig->protocol.notifyPress(slotIndex(Ctrl::F, Mod::L1, Btn::L3), "", Source::Reserved);
    TEST_ASSERT_EQUAL_STRING("@SM PRESS F L1 L3 - reserved", await());
    TEST_ASSERT_EQUAL_STRING("@SM OK EVENTS 0", ask("@SM EVENTS 0"));
    const uint32_t before = rig->wcb.usb.count;
    rig->protocol.notifyPress(kSlotFootL1Up, "x", Source::Eeprom);
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
    TEST_ASSERT_EQUAL_STRING("@SM SLOT D CROSS LEFT scanning default", await());
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
    RUN_TEST(test_prefix_and_suffix_travel_as_hex);
    RUN_TEST(test_events_report_presses);
    RUN_TEST(test_malformed_requests);
    RUN_TEST(test_refuses_requests_when_it_cannot_reply);
    RUN_TEST(test_line_reader_to_protocol);
    return UNITY_END();
}
