// Host tests for wdp::AnnounceTimer. Run: pio test -e native
#include <unity.h>

#include "../../lib/WdpAnnounce/WdpAnnounce.h"

namespace {
constexpr size_t kLen = 39;        // "@WDP1 {\"type\":\"Shadow\",\"fw\":\"a51a459\"}\r"
constexpr int kRoomy = 63;         // empty Serial1 TX ring
wdp::AnnounceTimer timer;
}  // namespace

void setUp(void) {
    timer = wdp::AnnounceTimer();
}

void tearDown(void) {}

void test_not_ready_before_start(void) {
    TEST_ASSERT_FALSE(timer.ready(0, kRoomy, kLen));
    TEST_ASSERT_FALSE(timer.ready(100000, kRoomy, kLen));
}

void test_first_delay(void) {
    timer.start(1000, 2000);
    TEST_ASSERT_FALSE(timer.ready(1000, kRoomy, kLen));
    TEST_ASSERT_FALSE(timer.ready(2999, kRoomy, kLen));
    TEST_ASSERT_TRUE(timer.ready(3000, kRoomy, kLen));
    TEST_ASSERT_TRUE(timer.ready(3001, kRoomy, kLen));
}

void test_waits_for_room_without_rescheduling(void) {
    timer.start(0, 500);
    TEST_ASSERT_FALSE(timer.ready(600, 10, kLen));   // due, but Serial1 is busy
    TEST_ASSERT_FALSE(timer.ready(700, 38, kLen));   // still one byte short
    TEST_ASSERT_TRUE(timer.ready(800, 39, kLen));    // room now, still due
}

void test_room_boundary(void) {
    timer.start(0, 500);
    TEST_ASSERT_TRUE(timer.ready(500, static_cast<int>(kLen), kLen));
    TEST_ASSERT_FALSE(timer.ready(500, static_cast<int>(kLen) - 1, kLen));
    TEST_ASSERT_FALSE(timer.ready(500, 0, kLen));
    TEST_ASSERT_FALSE(timer.ready(500, -1, kLen));
}

void test_interval_after_send(void) {
    timer.start(0, 500);
    timer.markSent(500, 27000);
    TEST_ASSERT_FALSE(timer.ready(501, kRoomy, kLen));
    TEST_ASSERT_FALSE(timer.ready(27499, kRoomy, kLen));
    TEST_ASSERT_TRUE(timer.ready(27500, kRoomy, kLen));
}

void test_not_ready_same_ms_after_send(void) {
    timer.start(0, 500);
    TEST_ASSERT_TRUE(timer.ready(500, kRoomy, kLen));
    timer.markSent(500, 25000);
    TEST_ASSERT_FALSE(timer.ready(500, kRoomy, kLen));
}

void test_ready_long_after_due(void) {
    timer.start(0, 500);
    timer.markSent(500, 30000);
    TEST_ASSERT_TRUE(timer.ready(500 + 10UL * 60UL * 1000UL, kRoomy, kLen));  // fault cleared after 10 min
}

void test_millis_rollover(void) {
    const uint32_t start = 0xFFFFF000UL;  // 4096 ms before millis() wraps
    timer.start(start, 30000);
    TEST_ASSERT_FALSE(timer.ready(start + 256U, kRoomy, kLen));  // before the wrap; the deadline has wrapped
    TEST_ASSERT_FALSE(timer.ready(0x00000010U, kRoomy, kLen));  // just after the wrap: 4112 ms elapsed
    TEST_ASSERT_FALSE(timer.ready(static_cast<uint32_t>(start + 29999U), kRoomy, kLen));  // wraps to 0x6D2F
    TEST_ASSERT_TRUE(timer.ready(static_cast<uint32_t>(start + 30000U), kRoomy, kLen));
}

void test_constants(void) {
    TEST_ASSERT_EQUAL_UINT32(500, wdp::kFirstDelayMinMs);
    TEST_ASSERT_EQUAL_UINT32(3000, wdp::kFirstDelayMaxMs);
    TEST_ASSERT_EQUAL_UINT32(25000, wdp::kIntervalMinMs);
    TEST_ASSERT_EQUAL_UINT32(30000, wdp::kIntervalMaxMs);
    TEST_ASSERT_EQUAL_UINT32(63, wdp::kMaxLineLen);
    TEST_ASSERT_EQUAL_UINT32(14, wdp::kMaxFwLen);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_not_ready_before_start);
    RUN_TEST(test_first_delay);
    RUN_TEST(test_waits_for_room_without_rescheduling);
    RUN_TEST(test_room_boundary);
    RUN_TEST(test_interval_after_send);
    RUN_TEST(test_not_ready_same_ms_after_send);
    RUN_TEST(test_ready_long_after_due);
    RUN_TEST(test_millis_rollover);
    RUN_TEST(test_constants);
    return UNITY_END();
}
