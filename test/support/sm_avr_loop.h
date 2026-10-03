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

// The most a pass reads: one full read budget, shared by both ports. Labels say "32 bytes".
static_assert(Runtime::kMaxBytesPerPass == 32, "the timing labels say a full read budget is 32 bytes");

// A request (with its CRC) arranged so that a full read budget ends with its CR: the most a pass
// reads, completing the line at its last byte. `lead` is what has to arrive, and be read, in an
// earlier pass (the line's first bytes, when the line and its CR are longer than the budget);
// `budget` is the Runtime::kMaxBytesPerPass bytes after it (other traffic, ended by a CR, first
// when the line is shorter).
struct BudgetLine {
    char lead[LineReader::kMaxLen + 2];
    char budget[Runtime::kMaxBytesPerPass + 1];
};
void budgetLine(PGM_P body, BudgetLine &b) {
    crcLine(body);
    const uint8_t n = static_cast<uint8_t>(strlen(gLine) + 1);  // with the CR
    constexpr uint8_t B = Runtime::kMaxBytesPerPass;
    if (n >= B) {
        const uint8_t head = static_cast<uint8_t>(n - B);
        memcpy(b.lead, gLine, head);
        b.lead[head] = '\0';
        memcpy(b.budget, gLine + head, B - 1);
    } else {
        b.lead[0] = '\0';
        memset(b.budget, 'j', B - n);
        b.budget[B - n - 1] = '\r';
        memcpy(b.budget + (B - n), gLine, n - 1);
    }
    b.budget[B - 1] = '\r';
    b.budget[B] = '\0';
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

// One loop() pass of the mapping, in main.cpp's order (after the motors): buttons(), batteries(),
// then afterButtons() (one request or the background unit, by the pass rule, then the EEPROM step
// and the drain), with Serial1's TX ring empty first (the UART has sent everything: the most the
// drain can write in a pass). Each part is timed on its own; the pass is their sum.
struct PassParts {
    uint16_t buttons, batteries, after;
    uint16_t total() const { return static_cast<uint16_t>(buttons + batteries + after); }
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
    // Faulted (`mayAnnounce` false): buttons() isn't called, as in main.cpp's fault path.
    if (mayAnnounce) {
        gLast.buttons = ticksOf([&] { gAny = runtime.buttons(now, foot, dome, &gFired); });
    } else {
        gAny = false;
        gLast.buttons = 0;
    }
    gLast.batteries = ticksOf([&] { runtime.batteries(now, foot.connected, footBatt, dome.connected, domeBatt); });
    gLast.after = ticksOf([&] {
        runtime.afterButtons(now, gAny, !mayAnnounce, Runtime::Link<Uart1>{SMT_WCB_UART, wcbLines, port},
                             Runtime::Link<HardwareSerial>{Serial, usbLines, usbPort}, SMT_WCB_UART);
    });
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
    say(PSTR("  parts: buttons %u.%u, batteries %u.%u, after %u.%u us (%u passes)"), p.buttons / 2,
        (p.buttons & 1) * 5, p.batteries / 2, (p.batteries & 1) * 5, p.after / 2, (p.after & 1) * 5, w.passes);
}

// A SET of `slot` to kKey15 ("zyxwvutsrqponml") was taken from Serial1 and
// written: nothing left in the RX buffer or the line reader, and the slot reads back the new key
// from EEPROM.
void assertSetWritten(uint8_t slot) {
    TEST_ASSERT_FALSE(wcbLines.ready());
    TEST_ASSERT_EQUAL_INT(0, Serial1.available());
    TEST_ASSERT_FALSE(store.busy());
    char key[kKeyBuf];
    Source src = Source::Default;
    TEST_ASSERT_TRUE(store.keyForSlot(slot, key, &src));
    TEST_ASSERT_TRUE(src == Source::Eeprom);
    TEST_ASSERT_EQUAL_INT(0, strcmp(key, kKey15));
}

// Passes with nothing held until the mapping has nothing left to do (no request waiting, no write,
// no operation, nothing queued for the UART, no press waiting); their worst goes into `w`.
void settle(uint32_t &now, WorstPass *w = nullptr, uint16_t maxPasses = 3000) {
    for (uint16_t i = 0; i < maxPasses; ++i) {
        if (!wcbLines.ready() && SMT_WCB_UART.available() == 0 && !store.busy() && !protocol.opInProgress() &&
            tx.used() == 0 && !runtime.tapPending() && runtime.deferredCount() == 0)
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
