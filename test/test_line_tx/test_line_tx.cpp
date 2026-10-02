#include <unity.h>

#include <ShadowMap.h>
#include <sm_fakes.h>
#include <sm_test_main.h>

using namespace sm;

namespace {

// Feeds a string; returns how many lines became ready (each is consumed immediately).
uint8_t feedAll(LineReader &reader, const char *s, char *lastLine = nullptr) {
    uint8_t ready = 0;
    for (; *s != '\0'; ++s) {
        if (reader.feed(*s)) {
            ++ready;
            if (lastLine != nullptr)
                strcpy(lastLine, reader.line());
            reader.consume();
        }
    }
    return ready;
}

// Same, for bytes that may include '\0'.
uint8_t feedBytes(LineReader &reader, const char *s, size_t n, char *lastLine = nullptr) {
    uint8_t ready = 0;
    for (size_t i = 0; i < n; ++i) {
        if (reader.feed(s[i])) {
            ++ready;
            if (lastLine != nullptr)
                strcpy(lastLine, reader.line());
            reader.consume();
        }
    }
    return ready;
}

}  // namespace

static void test_reader_keeps_only_sm_lines() {
    LineReader r;
    char line[LineReader::kMaxLen + 1] = {};
    TEST_ASSERT_EQUAL_UINT8(1, feedAll(r, "@SM HELLO\r", line));
    TEST_ASSERT_EQUAL_STRING("@SM HELLO", line);
    TEST_ASSERT_EQUAL_UINT8(1, feedAll(r, "garbage\r@SM X\n", line));
    TEST_ASSERT_EQUAL_STRING("@SM X", line);
    TEST_ASSERT_EQUAL_UINT8(0, feedAll(r, "@SX HELLO\r@S\r@SM\r\r\n\n"));
    TEST_ASSERT_EQUAL_UINT8(0, feedAll(r, ";S0@SM HELLO\r"));  // must start the line
    TEST_ASSERT_EQUAL_UINT8(2, feedAll(r, "@SM A\r\n@SM B\n\r", line));
    TEST_ASSERT_EQUAL_STRING("@SM B", line);
}

static void test_reader_drops_noise_and_long_lines() {
    LineReader r;
    char line[LineReader::kMaxLen + 1] = {};
    TEST_ASSERT_EQUAL_UINT8(0, feedAll(r, "@SM HEL\x01LO\r"));
    TEST_ASSERT_EQUAL_UINT8(0, feedAll(r, "@SM HEL\xC3LO\r"));
    char longLine[LineReader::kMaxLen + 8];
    memset(longLine, 'A', sizeof(longLine));
    memcpy(longLine, "@SM ", 4);
    longLine[LineReader::kMaxLen + 1] = '\r';
    longLine[LineReader::kMaxLen + 2] = '\0';
    TEST_ASSERT_EQUAL_UINT8(0, feedAll(r, longLine));
    // Exactly kMaxLen characters still fits.
    longLine[LineReader::kMaxLen] = '\r';
    longLine[LineReader::kMaxLen + 1] = '\0';
    TEST_ASSERT_EQUAL_UINT8(1, feedAll(r, longLine, line));
    TEST_ASSERT_EQUAL_UINT8(LineReader::kMaxLen, strlen(line));
    // And the reader recovers for the next line.
    TEST_ASSERT_EQUAL_UINT8(1, feedAll(r, "@SM OK\r", line));
    TEST_ASSERT_EQUAL_STRING("@SM OK", line);
}

// A glitch byte on the wire before a line starts (seen when WCB 1 resets at power-up) must not
// cost the next request. Noise inside a line, or printable junk before "@SM ", still drops it.
static void test_reader_ignores_noise_before_a_line_starts() {
    LineReader r;
    char line[LineReader::kMaxLen + 1] = {};
    TEST_ASSERT_EQUAL_UINT8(1, feedAll(r, "\xFF@SM HELLO\r", line));
    TEST_ASSERT_EQUAL_STRING("@SM HELLO", line);
    const char nul[] = "\x00@SM A\r";
    TEST_ASSERT_EQUAL_UINT8(1, feedBytes(r, nul, sizeof(nul) - 1, line));
    TEST_ASSERT_EQUAL_STRING("@SM A", line);
    const char several[] = "\xF0\x00\x80\x1B@SM B\r";
    TEST_ASSERT_EQUAL_UINT8(1, feedBytes(r, several, sizeof(several) - 1, line));
    TEST_ASSERT_EQUAL_STRING("@SM B", line);
    // Still dropped: printable junk first, noise after the line has started, noise after junk.
    TEST_ASSERT_EQUAL_UINT8(0, feedAll(r, "x@SM C\r"));
    TEST_ASSERT_EQUAL_UINT8(0, feedAll(r, "@\xFFSM D\r"));
    TEST_ASSERT_EQUAL_UINT8(0, feedAll(r, "\xFFx@SM E\r"));
    TEST_ASSERT_EQUAL_UINT8(0, feedAll(r, "junk\xFF@SM F\r"));
    // And the next clean line after all of that is kept.
    TEST_ASSERT_EQUAL_UINT8(1, feedAll(r, "@SM G\r", line));
    TEST_ASSERT_EQUAL_STRING("@SM G", line);
}

static void test_ready_line_is_held_until_consumed() {
    LineReader r;
    const char *s = "@SM ONE\r@SM TWO\r";
    const char *p = s;
    while (!r.feed(*p))
        ++p;
    ++p;
    TEST_ASSERT_TRUE(r.ready());
    for (const char *q = p; *q != '\0'; ++q)
        TEST_ASSERT_FALSE(r.feed(*q));  // refused, not overwritten
    TEST_ASSERT_EQUAL_STRING("@SM ONE", r.line());
    TEST_ASSERT_EQUAL_UINT8(7, r.length());
    r.consume();
    TEST_ASSERT_FALSE(r.ready());
}

static void test_discard_while_pending_resyncs_to_the_next_line() {
    LineReader r;
    const char *first = "@SM ONE\r";
    for (const char *p = first; *p != '\0'; ++p)
        r.feed(*p);
    TEST_ASSERT_TRUE(r.ready());
    // Stop discarding mid-line: the tail of that line must not become a line of its own.
    for (const char *p = "@SM TW"; *p != '\0'; ++p)
        r.discard(*p);
    r.consume();
    char line[LineReader::kMaxLen + 1] = {};
    TEST_ASSERT_EQUAL_UINT8(1, feedAll(r, "O\r@SM THREE\r", line));
    TEST_ASSERT_EQUAL_STRING("@SM THREE", line);
    // Stop discarding right after a line end: the next line is read normally.
    for (const char *p = first; *p != '\0'; ++p)
        r.feed(*p);
    for (const char *p = "noise\r"; *p != '\0'; ++p)
        r.discard(*p);
    r.consume();
    TEST_ASSERT_EQUAL_UINT8(1, feedAll(r, "@SM FOUR\r", line));
    TEST_ASSERT_EQUAL_STRING("@SM FOUR", line);
}

static void test_queue_is_all_or_nothing() {
    TxQueue q;
    char big[TxQueue::kCapacity + 1];
    memset(big, 'x', sizeof(big));
    big[TxQueue::kCapacity - 1] = '\0';  // kCapacity-1 chars + '\r' fills it exactly
    const char *parts[] = {big};
    TEST_ASSERT_TRUE(q.pushLine(parts, 1));
    TEST_ASSERT_EQUAL_UINT8(0, q.freeSpace());
    const char *tiny[] = {""};
    TEST_ASSERT_FALSE(q.pushLine(tiny, 1));
    TEST_ASSERT_EQUAL_UINT8(TxQueue::kCapacity, q.used());
}

static void test_queue_wraps_and_drains_at_uart_speed() {
    TxQueue q;
    WcbSim wcb;
    wcb.room = 0;
    char key[8];
    for (uint16_t i = 0; i < 300; ++i) {
        snprintf(key, sizeof(key), "k%u", static_cast<unsigned>(i));
        while (!pushCommand(q, ";W1;C", key, "")) {
            wcb.refill(5);
            q.drain(wcb);
        }
        wcb.refill(3);
        q.drain(wcb);
        TEST_ASSERT_TRUE(wcb.room >= 0);
    }
    while (q.used() > 0) {
        wcb.refill(5);
        q.drain(wcb);
    }
    TEST_ASSERT_EQUAL_UINT32(300, wcb.commands.count);
    TEST_ASSERT_EQUAL_STRING(";W1;Ck299", wcb.commands.last());
    TEST_ASSERT_EQUAL_STRING(";W1;Ck298", wcb.commands.last(1));
    TEST_ASSERT_EQUAL_UINT32(0, wcb.overruns);
    TEST_ASSERT_EQUAL_UINT32(0, wcb.violations);
}

static void test_command_headroom_fits_the_longest_command() {
    TxQueue q;
    char key[kKeyBuf], prefix[kPrefixMax + 1], suffix[kSuffixMax + 1];
    memset(key, 'k', kKeyMax);
    key[kKeyMax] = '\0';
    memset(prefix, 'p', kPrefixMax);
    prefix[0] = ';';
    prefix[kPrefixMax] = '\0';
    memset(suffix, 's', kSuffixMax);
    suffix[kSuffixMax] = '\0';
    // Leave exactly the headroom free.
    char fill[TxQueue::kCapacity];
    const uint8_t fillLen = TxQueue::kCapacity - kCommandHeadroom - 1;
    memset(fill, 'f', fillLen);
    fill[fillLen] = '\0';
    const char *parts[] = {fill};
    TEST_ASSERT_TRUE(q.pushLine(parts, 1));
    TEST_ASSERT_EQUAL_UINT8(kCommandHeadroom, q.freeSpace());
    TEST_ASSERT_TRUE(pushCommand(q, prefix, key, suffix));
    TEST_ASSERT_EQUAL_UINT8(0, q.freeSpace());
}

static void test_command_headroom_fits_the_longest_raw_command() {
    TxQueue q;
    char fill[TxQueue::kCapacity];
    const uint8_t fillLen = TxQueue::kCapacity - kCommandHeadroom - 1;
    memset(fill, 'f', fillLen);
    fill[fillLen] = '\0';
    const char *parts[] = {fill};
    TEST_ASSERT_TRUE(q.pushLine(parts, 1));
    TEST_ASSERT_TRUE(pushRaw(q, "~~~~~~~~~~~~~~~"));  // 15 characters and '\r'
    TEST_ASSERT_EQUAL_UINT8(kCommandHeadroom - 16, q.freeSpace());
}

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_reader_keeps_only_sm_lines);
    RUN_TEST(test_reader_drops_noise_and_long_lines);
    RUN_TEST(test_reader_ignores_noise_before_a_line_starts);
    RUN_TEST(test_ready_line_is_held_until_consumed);
    RUN_TEST(test_discard_while_pending_resyncs_to_the_next_line);
    RUN_TEST(test_queue_is_all_or_nothing);
    RUN_TEST(test_queue_wraps_and_drains_at_uart_speed);
    RUN_TEST(test_command_headroom_fits_the_longest_command);
    RUN_TEST(test_command_headroom_fits_the_longest_raw_command);
    return UNITY_END();
}
