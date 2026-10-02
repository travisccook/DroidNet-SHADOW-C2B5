// Serial1 for the mapping's per-pass work (Runtime::pump and Runtime::service): the same ring
// buffers and the same rules as HardwareSerial, without a virtual call per byte. Through
// HardwareSerial, reading 63 bytes took 266 us and handing 64 to an idle UART 451 us
// (Print::write calls the virtual write(c) once per byte). Firmware and simavr only.
//
// Byte for byte it does what HardwareSerial does in the Arduino AVR core that
// platformio/atmelavr@5.1.0 pins (framework-arduino-avr 5.1.0, cores/arduino/HardwareSerial.cpp):
//   available()/read(): the same ring arithmetic; read() reads the volatile head each call.
//   availableForWrite(): the same (8-bit indices, so no atomic block, as TX_BUFFER_ATOMIC).
//   write(): each leading byte that finds the ring empty and UDR1 free goes straight to UDR1,
//     with TXC1 cleared in the same atomic block, as write(c)'s shortcut does; the rest are
//     copied into the ring and published with one atomic head update and UDRIE1 set, as
//     write(c) does a byte at a time. The UART sends the same bytes in the same order, and a
//     drain hands it the same number of them (64 to an idle UART).
// The core's members are protected; they are reached through a HardwareSerial-derived accessor
// with pointers to members, as test/support/sm_avr_timing.h's SerialPoke does. Re-check this
// file against the core whenever the platform pin in platformio.ini changes.
#pragma once

#if defined(ARDUINO_ARCH_AVR)

#include <Arduino.h>
#include <string.h>
#include <util/atomic.h>

#if defined(UBRR1H)

namespace sm {

class Uart1 {
  public:
    __attribute__((always_inline)) int available() const {
        return static_cast<uint8_t>(A::rxHead() - A::rxTail()) & static_cast<uint8_t>(SERIAL_RX_BUFFER_SIZE - 1);
    }
    __attribute__((always_inline)) int read() {
        const rx_buffer_index_t tail = A::rxTail();
        if (A::rxHead() == tail)
            return -1;
        const unsigned char c = A::rxBuf()[tail];
        A::rxTail() = static_cast<rx_buffer_index_t>((tail + 1) & (SERIAL_RX_BUFFER_SIZE - 1));
        return c;
    }
    int availableForWrite() const {
        const tx_buffer_index_t head = A::txHead();
        const tx_buffer_index_t tail = A::txTail();
        if (head >= tail)
            return SERIAL_TX_BUFFER_SIZE - 1 - head + tail;
        return tail - head - 1;
    }
    // `n` must not be more than availableForWrite() returned (TxQueue::drain never asks more).
    size_t write(const uint8_t *data, size_t n) {
        if (n == 0)
            return 0;
        A::written() = true;
        size_t i = 0;
        while (i < n && A::txHead() == A::txTail() && bit_is_set(UCSR1A, UDRE1)) {
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                UDR1 = data[i];
                UCSR1A = static_cast<uint8_t>((UCSR1A & (_BV(U2X1) | _BV(MPCM1))) | _BV(TXC1));
            }
            ++i;
        }
        if (i == n)
            return n;
        unsigned char *buf = A::txBuf();
        uint8_t head = A::txHead();  // only this side moves the head
        const uint8_t rest = static_cast<uint8_t>(n - i);
        const uint8_t toEnd = static_cast<uint8_t>(SERIAL_TX_BUFFER_SIZE - head);
        const uint8_t first = rest < toEnd ? rest : toEnd;
        memcpy(buf + head, data + i, first);
        memcpy(buf, data + i + first, static_cast<uint8_t>(rest - first));
        head = static_cast<uint8_t>((head + rest) & (SERIAL_TX_BUFFER_SIZE - 1));
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            A::txHead() = head;
            UCSR1B = static_cast<uint8_t>(UCSR1B | _BV(UDRIE1));
        }
        return n;
    }

  private:
    static_assert(SERIAL_TX_BUFFER_SIZE == 64 && SERIAL_RX_BUFFER_SIZE == 64, "Uart1 assumes the core's 64-byte rings");
    static_assert(sizeof(tx_buffer_index_t) == 1 && sizeof(rx_buffer_index_t) == 1, "Uart1 assumes 8-bit ring indices");

    struct A : HardwareSerial {
        static volatile rx_buffer_index_t &rxHead() { return Serial1.*(&A::_rx_buffer_head); }
        static volatile rx_buffer_index_t &rxTail() { return Serial1.*(&A::_rx_buffer_tail); }
        static volatile tx_buffer_index_t &txHead() { return Serial1.*(&A::_tx_buffer_head); }
        static volatile tx_buffer_index_t &txTail() { return Serial1.*(&A::_tx_buffer_tail); }
        static unsigned char *rxBuf() { return Serial1.*(&A::_rx_buffer); }
        static unsigned char *txBuf() { return Serial1.*(&A::_tx_buffer); }
        static bool &written() { return Serial1.*(&A::_written); }
    };
};

}  // namespace sm

#endif  // UBRR1H
#endif  // ARDUINO_ARCH_AVR
