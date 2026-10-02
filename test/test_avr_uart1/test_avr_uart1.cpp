// sm::Uart1 (include/sm_uart_avr.h) against HardwareSerial on the emulated ATmega2560's real
// Serial1: from the same ring state, a write leaves the same ring bytes, indices, UDRIE1 and
// _written and accepts the same count, at every ring offset (the wrap included), with the UART
// idle or busy; reads give the same bytes; and a TxQueue drains the same way into both.
// AVR only (simavr).
#include <unity.h>

#include <ShadowMap.h>
#include <sm_test_main.h>
#include <stdio.h>

#if defined(__AVR__)

#include <sm_uart_avr.h>

namespace {

sm::Uart1 uart1;

struct Peek : HardwareSerial {
    static volatile tx_buffer_index_t &txHead() { return Serial1.*(&Peek::_tx_buffer_head); }
    static volatile tx_buffer_index_t &txTail() { return Serial1.*(&Peek::_tx_buffer_tail); }
    static volatile rx_buffer_index_t &rxHead() { return Serial1.*(&Peek::_rx_buffer_head); }
    static volatile rx_buffer_index_t &rxTail() { return Serial1.*(&Peek::_rx_buffer_tail); }
    static unsigned char *txBuf() { return Serial1.*(&Peek::_tx_buffer); }
    static unsigned char *rxBuf() { return Serial1.*(&Peek::_rx_buffer); }
    static bool &written() { return Serial1.*(&Peek::_written); }
};

struct State {
    uint8_t head, tail, udrie, written;
    unsigned char ring[SERIAL_TX_BUFFER_SIZE];
};

void idle() { loop_until_bit_is_set(UCSR1A, UDRE1); }

// The TX ring with `queued` bytes from `offset`, the UART idle (or, with `busy`, just handed a
// byte), interrupts off so nothing moves while the two are compared.
void setRing(uint8_t offset, uint8_t queued, bool busy) {
    idle();
    UCSR1B = static_cast<uint8_t>(UCSR1B & ~_BV(UDRIE1));
    Peek::txTail() = offset;
    Peek::txHead() = static_cast<tx_buffer_index_t>((offset + queued) % SERIAL_TX_BUFFER_SIZE);
    for (uint8_t i = 0; i < SERIAL_TX_BUFFER_SIZE; ++i)
        Peek::txBuf()[i] = static_cast<unsigned char>(0xA0 + i);
    Peek::written() = false;
    if (busy)
        UDR1 = '\n';  // simavr then holds UDR1 full for a byte time (checked below)
}

// Also turns UDRIE1 back off, so no interrupt waits while they're off.
State snapshot() {
    State s;
    s.head = Peek::txHead();
    s.tail = Peek::txTail();
    s.udrie = static_cast<uint8_t>(bit_is_set(UCSR1B, UDRIE1) ? 1 : 0);
    UCSR1B = static_cast<uint8_t>(UCSR1B & ~_BV(UDRIE1));
    s.written = Peek::written() ? 1 : 0;
    memcpy(s.ring, Peek::txBuf(), sizeof(s.ring));
    return s;
}

}  // namespace

static void test_writes_leave_the_same_state() {
    Serial1.begin(9600);
    // Line ends throughout: simavr prints what Serial1 sends a line at a time, and tool-simavr
    // 1.10800.0 overruns its 256-byte line buffer by one byte (and can crash as it exits) when
    // 256 arrive without one (see SerialPoke::dropTx1 in sm_avr_timing.h).
    uint8_t data[64];
    for (uint8_t i = 0; i < sizeof(data); ++i)
        data[i] = static_cast<uint8_t>(i % 8 == 7 ? '\n' : 'a' + i % 26);
    const uint8_t offsets[] = {0, 1, 2, 31, 32, 61, 62, 63};
    const uint8_t queuedList[] = {0, 1, 10, 62};
    uint16_t cases = 0;
    for (uint8_t offset : offsets) {
        for (uint8_t queued : queuedList) {
            for (uint8_t busy = 0; busy < 2; ++busy) {
                cli();
                setRing(offset, queued, busy != 0);
                const bool full = bit_is_clear(UCSR1A, UDRE1);
                const int room = Serial1.availableForWrite();
                const int roomB = uart1.availableForWrite();
                setRing(0, 0, false);
                sei();
                TEST_ASSERT_EQUAL_INT(room, roomB);
                TEST_ASSERT_TRUE(full == (busy != 0));
                const int counts[] = {0, 1, 2, room / 2, room - 1, room};
                for (int n : counts) {
                    if (n < 0 || n > room)
                        continue;
                    // Interrupts off from the ring's setup to its snapshot, so nothing moves.
                    cli();
                    setRing(offset, queued, busy != 0);
                    const size_t a = Serial1.write(data, static_cast<size_t>(n));
                    const State want = snapshot();
                    const int roomAfterA = Serial1.availableForWrite();
                    setRing(offset, queued, busy != 0);
                    const size_t b = uart1.write(data, static_cast<size_t>(n));
                    const State got = snapshot();
                    const int roomAfterB = uart1.availableForWrite();
                    setRing(0, 0, false);
                    sei();
                    char msg[48];
                    snprintf(msg, sizeof(msg), "offset %u queued %u busy %u n %d", offset, queued, busy, n);
                    TEST_ASSERT_EQUAL_UINT_MESSAGE(a, b, msg);
                    TEST_ASSERT_EQUAL_UINT8_MESSAGE(want.head, got.head, msg);
                    TEST_ASSERT_EQUAL_UINT8_MESSAGE(want.tail, got.tail, msg);
                    TEST_ASSERT_EQUAL_UINT8_MESSAGE(want.udrie, got.udrie, msg);
                    TEST_ASSERT_EQUAL_UINT8_MESSAGE(want.written, got.written, msg);
                    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want.ring, got.ring, sizeof(want.ring), msg);
                    TEST_ASSERT_EQUAL_INT_MESSAGE(roomAfterA, roomAfterB, msg);
                    ++cases;
                }
            }
        }
    }
    TEST_ASSERT_TRUE(cases > 300);
}

// A TxQueue holding a long line drains the same bytes, in the same chunks, into both.
static void test_a_drain_moves_the_same_bytes() {
    Serial1.begin(9600);
    for (uint8_t offset = 0; offset < SERIAL_TX_BUFFER_SIZE; offset = static_cast<uint8_t>(offset + 9)) {
        char line[100];
        for (uint8_t i = 0; i < 90; ++i)
            line[i] = static_cast<char>(i % 8 == 7 ? '\n' : 'A' + i % 26);
        line[90] = '\0';
        const char *parts[] = {line};
        sm::TxQueue qa, qb;
        TEST_ASSERT_TRUE(qa.pushLine(parts, 1));
        TEST_ASSERT_TRUE(qb.pushLine(parts, 1));
        cli();
        setRing(offset, 0, false);
        qa.drain(Serial1);
        const State want = snapshot();
        setRing(offset, 0, false);
        qb.drain(uart1);
        const State got = snapshot();
        setRing(0, 0, false);
        sei();
        TEST_ASSERT_EQUAL_UINT8(qa.used(), qb.used());
        TEST_ASSERT_EQUAL_UINT8(91 - 64, qb.used());  // an idle UART takes 64: one into UDR1, 63 into the ring
        TEST_ASSERT_EQUAL_UINT8(want.head, got.head);
        TEST_ASSERT_EQUAL_MEMORY(want.ring, got.ring, sizeof(want.ring));
    }
}

static void test_reads_give_the_same_bytes() {
    Serial1.begin(9600);
    const uint8_t offsets[] = {0, 1, 40, 62, 63};
    for (uint8_t offset : offsets) {
        for (uint8_t count = 0; count < SERIAL_RX_BUFFER_SIZE; count = static_cast<uint8_t>(count + 7)) {
            int a[70], b[70];
            for (uint8_t pass = 0; pass < 2; ++pass) {
                cli();
                Peek::rxTail() = offset;
                for (uint8_t i = 0; i < count; ++i)
                    Peek::rxBuf()[(offset + i) % SERIAL_RX_BUFFER_SIZE] = static_cast<unsigned char>(0x30 + i);
                Peek::rxHead() = static_cast<rx_buffer_index_t>((offset + count) % SERIAL_RX_BUFFER_SIZE);
                int *out = pass == 0 ? a : b;
                uint8_t k = 0;
                out[k++] = pass == 0 ? Serial1.available() : uart1.available();
                for (uint8_t i = 0; i < count + 1; ++i)
                    out[k++] = pass == 0 ? Serial1.read() : uart1.read();
                out[k++] = pass == 0 ? Serial1.available() : uart1.available();
                sei();
            }
            TEST_ASSERT_EQUAL_INT_ARRAY(a, b, count + 3);
            TEST_ASSERT_EQUAL_INT(count, a[0]);
            TEST_ASSERT_EQUAL_INT(-1, a[count + 1]);
        }
    }
}

#else

static void test_writes_leave_the_same_state() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_a_drain_moves_the_same_bytes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }
static void test_reads_give_the_same_bytes() { TEST_IGNORE_MESSAGE("AVR only (pio test -e simavr)"); }

#endif

int runAllTests() {
    UNITY_BEGIN();
    RUN_TEST(test_writes_leave_the_same_state);
    RUN_TEST(test_a_drain_moves_the_same_bytes);
    RUN_TEST(test_reads_give_the_same_bytes);
    return UNITY_END();
}
