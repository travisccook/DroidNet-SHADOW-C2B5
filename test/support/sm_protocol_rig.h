// Shared by the protocol test programs, which are split in two so each fits the
// ATmega2560's SRAM (on AVR every expected-string literal lives in RAM). Include it once per
// test program: it defines setUp() and tearDown().
#pragma once

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
