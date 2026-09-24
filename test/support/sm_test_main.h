// Shared Unity entry point: runs on the host (main) and on the emulated ATmega2560
// (setup, then sleep with interrupts off, which ends the simavr run).
#pragma once

#include <unity.h>

int runAllTests();

// Define SM_TEST_OWN_SETUP before including this to supply your own setUp() and tearDown().
#ifndef SM_TEST_OWN_SETUP
void setUp() {}
void tearDown() {}
#endif

#ifdef ARDUINO
#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

void setup() {
    runAllTests();
    Serial.flush();
    cli();
    sleep_enable();
    sleep_cpu();
}
void loop() {}
#else
int main() { return runAllTests(); }
#endif
