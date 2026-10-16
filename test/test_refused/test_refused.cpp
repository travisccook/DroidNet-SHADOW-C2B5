// @SM REFUSED: a paired controller that isn't allowed tried to connect.
#include <unity.h>

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
