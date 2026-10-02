// The mapping's side of loop() on the emulated ATmega2560, for the whole-pass timing suites: one
// MapStore, Protocol and Runtime, as in the firmware, the WCB on the real Serial1 and the USB port
// on the real Serial. Include it once per test program, after sm_avr_timing.h. AVR only.
#pragma once

#if defined(__AVR__)

#include <new>

#include <sm_avr_timing.h>
#include <sm_uart_avr.h>

namespace smt {

// The firmware's WCB UART as loop() reaches it (main.cpp's wcbUart).
sm::Uart1 gUart1;
#define SMT_WCB_UART gUart1

PacedEeprom io;
MapStore store(io, kWorstDefaults, kWorstDefaultCount);
TxQueue tx;
WcbPort port(tx);
Protocol protocol(store, kFw11);
LineReader wcbLines;
LineReader usbLines;
NullUart null;

alignas(Runtime) uint8_t runtimeBytes[sizeof(Runtime)];
Runtime &runtime = *reinterpret_cast<Runtime *>(runtimeBytes);

// The USB side: replies are taken and dropped (main.cpp's UsbReplyPort waits for the debug
// output to empty; that costs nothing here).
class IdlePort final : public ReplyPort {
  public:
    bool canSend(uint8_t) override { return true; }
    void send(const char *, uint8_t) override {}
    bool requiresCrc() const override { return false; }
};
IdlePort usbPort;

constexpr PadState kNone = {true, 0};
// F CIRCLE RIGHT with CIRCLE held on the dome controller ("from=D"); its long press is
// F CIRCLE RIGHTL (kWide).
constexpr PadState kFootRight = {true, kHeldRight};
constexpr PadState kDomeCircle = {true, kHeldCircle};
// D CIRCLE RIGHT with CIRCLE held on the foot controller ("from=F"): kWideSingle, a single press.
constexpr PadState kDomeRight = {true, kHeldRight};
constexpr PadState kFootCircle = {true, kHeldCircle};

void freshRuntime() {
    new (runtimeBytes) Runtime(store, protocol, tx, kFw11);
    wcbLines.consume();
    usbLines.consume();
}

void drainAll() {
    while (tx.used() > 0) {
        tx.drain(null);
        null.note();
    }
}

char gLine[LineReader::kMaxLen + 8];
const char *crcLine(PGM_P body) {
    char b[LineReader::kMaxLen + 1];
    strncpy_P(b, body, sizeof(b) - 1);
    b[sizeof(b) - 1] = '\0';
    withCrc(b, gLine, sizeof(gLine));
    return gLine;
}

// Handles a request directly (not timed), with everything it sends drained.
void request(PGM_P body) {
    drainAll();
    TEST_ASSERT_TRUE(protocol.handleLine(crcLine(body), port));
    drainAll();
}

// A request and its CR in Serial1's RX buffer, as if the WCB had sent it.
void injectLine(PGM_P body) {
    crcLine(body);
    SerialPoke::inject(Serial1, gLine);
    SerialPoke::inject(Serial1, "\r");
}

// The worst state, the reply route at its longest (HELLO 20) and EVENTS on, so every press also
// sends a PRESS line.
void worstState(bool events = true) {
    io.busyPasses = 0;
    io.corrupt = false;
    eeWorst();
    store.begin();
    freshRuntime();
    request(PSTR("@SM HELLO 20"));
    if (events)
        request(PSTR("@SM EVENTS 1"));
    else
        request(PSTR("@SM EVENTS 0"));
    SerialPoke::clearRx(Serial1);
    SerialPoke::clearRx(Serial);
    protocol.setPads(true, true);
}

// One loop() pass of the mapping, in main.cpp's order (mappedButtons(), then serviceMapping():
// pump, pump, batteries, service), with Serial1's TX ring empty first (the UART has sent
// everything: the most the drain can write in a pass). Each part is timed on its own; the pass is
// their sum.
struct PassParts {
    uint16_t wcbPump, usbPump, buttons, batteries, service;
    uint16_t total() const { return static_cast<uint16_t>(wcbPump + usbPump + buttons + batteries + service); }
};
struct WorstPass {
    PassParts parts = {};
    uint16_t passes = 0;
    void add(const PassParts &p) {
        if (p.total() > parts.total())
            parts = p;
        ++passes;
    }
    void add(const WorstPass &w) {
        add(w.parts);
        passes = static_cast<uint16_t>(passes + w.passes - 1);
    }
    uint16_t ticks() const { return parts.total(); }
};

FiredPress gFired;
bool gAny = false;        // a press fired in the last pass
bool gFinishing = false;  // the EEPROM writer was due to finish its job in the last pass
PassParts gLast;
uint16_t loopPass(uint32_t now, const PadState &foot, const PadState &dome, uint8_t footBatt = 0, uint8_t domeBatt = 0,
                  bool mayAnnounce = true) {
    SerialPoke::dropTx1();
    io.tick();
    gFinishing = store.finishDue();
    gLast.buttons = ticksOf([&] { gAny = runtime.buttons(now, foot, dome, &gFired); });
    gLast.wcbPump = ticksOf([] { runtime.pump(SMT_WCB_UART, wcbLines, port); });
    gLast.usbPump = ticksOf([] { runtime.pump(Serial, usbLines, usbPort); });
    gLast.batteries = ticksOf([&] { runtime.batteries(now, foot.connected, footBatt, dome.connected, domeBatt); });
    gLast.service = ticksOf([&] { runtime.service(now, SMT_WCB_UART, mayAnnounce); });
    return gLast.total();
}

// The worst passes of a scenario with at most one press a pass, by class: a press fired while the
// EEPROM writer finished a job is T2, any other pass T1.
struct ClassWorst {
    WorstPass t1, t2;
    void add() {
        if (gAny && gFinishing)
            t2.add(gLast);
        else
            t1.add(gLast);
    }
};

void recordPass(PGM_P what, const WorstPass &w, uint8_t cls = kT1) {
    record(what, w.ticks(), cls);
    const PassParts &p = w.parts;
    say(PSTR("  parts: buttons %u.%u, WCB pump %u.%u, USB pump %u.%u, batteries %u.%u, service %u.%u us (%u passes)"),
        p.buttons / 2, (p.buttons & 1) * 5, p.wcbPump / 2, (p.wcbPump & 1) * 5, p.usbPump / 2, (p.usbPump & 1) * 5,
        p.batteries / 2, (p.batteries & 1) * 5, p.service / 2, (p.service & 1) * 5, w.passes);
}

// Passes with nothing held until the mapping has nothing left to do (no request waiting, no write,
// no operation, nothing queued for the UART); their worst goes into `w`.
void settle(uint32_t &now, WorstPass *w = nullptr, uint16_t maxPasses = 3000) {
    for (uint16_t i = 0; i < maxPasses; ++i) {
        if (!wcbLines.ready() && SMT_WCB_UART.available() == 0 && !store.busy() && !protocol.opInProgress() &&
            tx.used() == 0 && !runtime.tapPending())
            return;
        now += 2;
        loopPass(now, kNone, kNone);
        if (w != nullptr)
            w->add(gLast);
    }
    TEST_FAIL_MESSAGE("the mapping never settled");
}

}  // namespace smt

#endif  // __AVR__
