// =======================================================================================
//        SHADOW+MD+C2B5:  Small Handheld Arduino Droid Operating Wand + MarcDuino + C2-B5 Modifications
// =======================================================================================
//                                  SHADOW+MD
//                         Last Revised Date: 08/23/2015
//                             Revised By: vint43
//                Inspired by the PADAWAN / KnightShade SHADOW effort
//
//         This program is free software: you can redistribute it and/or modify it for
//         your personal use and the personal use of other astromech club members.
//
//         This program is distributed in the hope that it will be useful
//         as a courtesy to fellow astromech club members wanting to develop
//         their own droid control system.
//
//         IT IS OFFERED WITHOUT ANY WARRANTY; without even the implied warranty of
//         MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
//         You are using this software at your own risk and, as a fellow club member, it is
//         expected you will have the proper experience / background to handle and manage that
//         risk appropriately.  It is completely up to you to insure the safe operation of
//         your droid and to validate and test all aspects of your droid control system.
// =======================================================================================
//                                    +C2B5
//                        Modified By: Travis Cook (travisccook)
//                 Repository: https://github.com/travisccook/DroidNet-SHADOW-C2B5
//
//         This code is a heavily modified version of the SHADOW+MD system. As my own
//         droid, C2-B5, evolved it became clear that all I really needed SHADOW to do
//         for me was serve as a mapping of buttons (and sticks) to serial commands.
//         With the wealth of community-driven boards out there offloading various functions,
//         serial commands rule the world.
//
//         I also realized that, once I implemented the HCR vocalizer and Pololu Maestros,
//         I no longer had a need for Marcduino in my system. I stuck with SHADOW+MD as
//         my base, though, because it offered the simplest button mapping and I had
//         hundreds of hours of problem-free drive time with it's drive logic.
//
//         Since then changes have been to rip out the things I don't personally use for
//         easier to read and simpler code and quest to make modifying button mappings as
//         simple as possible.
//
//         This fork is not intended to be a drop-in replacement for anyone. It very
//         intentionally does not cover multiple solutions for various things. The
//         original SHADOW is far more comprehensive and offers a wide range of options
//         - I suggest starting there if you are looking for that level of functionality.
//
//         However, I make it available to the community share and share alike as the
//         community has so freely shared with me. If you, too, are just looking for a
//         button-to-command mapping solution then this may be your starting point.
//         Happy driving, and may the force be with you!
//
//         -Travis, 2025-06-02
//
//   Note: This project is designed to support the VSCode+PlatformIO IDE. It is not
//   designed to operate in the standard Arduino IDE and you may get some errors if you try.
//
//   If you haven't tried VSCode with the PlatformIO extension, give it a shot. I've
//   really enjoyed using it.
//
// =======================================================================================
//   Note: You will need a Arduino Mega ADK rev3 to run this sketch,
//   as a normal Arduino (Uno, Duemilanove etc.) doesn't have enough SRAM and FLASH
//
//   This is written to be a SPECIFIC Sketch - supporting only one type of controller
//      - PS3 Move Navigation (One for foot, optional additional for dome)
//
//   PS3 Bluetooth library - developed by Kristian Lauszus (kristianl@tkjelectronics.com)
//   For more information visit my blog: http://blog.tkjelectronics.dk/ or
//
//   Sabertooth (Foot Drive):
//         Set Sabertooth 2x32 or 2x25 Dip Switches: 1 and 2 Down, All Others Up
//
//   SyRen 10 Dome Drive:
//         For SyRen packetized Serial Set Switches: 1, 2 and 4 Down, All Others Up
//         NOTE:  Support for SyRen Simple Serial has been removed, due to problems.
//         Please contact DimensionEngineering to get an RMA to flash your firmware
//         Some place a 10K ohm resistor between S1 & GND on the SyRen 10 itself
//
// =======================================================================================
//

// ---------------------------------------------------------------------------------------
//                          Libraries
// ---------------------------------------------------------------------------------------
#include <Arduino.h>

#include <PS3BT.h>
#include <usbhub.h>
#include "../lib/Sabertooth/Sabertooth.h"
#include "../include/config.h"
#include "../include/default_mappings.h"
#include "../include/sm_uart_avr.h"
#include <ShadowMap.h>

// ---------------------------------------------------------------------------------------
//                        General User Settings
// ---------------------------------------------------------------------------------------

// Configuration variables moved to config.h/config.cpp

// ---------------------------------------------------------------------------------------
//                          Function Declaration // .cpp compatibility conversion
// ---------------------------------------------------------------------------------------

void onInitPS3NavFoot();
void onInitPS3NavDome();
String getLastConnectedBtMAC();
boolean criticalFaultDetect();
boolean readUSB();
void printOutput();

// ---------------------------------------------------------------------------------------
//                          Variables
// ---------------------------------------------------------------------------------------

unsigned long previousDomeMillis = millis();
unsigned long previousFootMillis = millis();
unsigned long previousDomeToggleMillis = millis();
unsigned long previousSpeedToggleMillis = millis();
unsigned long currentMillis = millis();

// Connection timeout monitoring to prevent infinite hangs
unsigned long lastConnectionAttemptTime = 0;
unsigned long connectionTimeoutDuration = 30000;  // 30 second timeout for connections
boolean connectionInProgress = false;

int speedToggleButtonCounter = 0;
int domeToggleButtonCounter = 0;

Sabertooth *ST = new Sabertooth(SABERTOOTH_ADDR, Serial2);
Sabertooth *SyR = new Sabertooth(SYREN_ADDR, Serial2);

///////Setup for USB and Bluetooth Devices////////////////////////////
USB Usb;
BTD Btd(&Usb);
PS3BT *PS3NavFoot = new PS3BT(&Btd);
PS3BT *PS3NavDome = new PS3BT(&Btd);

// Used for PS3 Fault Detection
uint32_t msgLagTime = 0;
uint32_t lastMsgTime = 0;
uint32_t currentTime = 0;
uint32_t lastLoopTime = 0;
int badPS3Data = 0;
int badPS3DataDome = 0;

boolean firstMessage = true;
String output = "";

boolean isFootMotorStopped = true;
boolean isDomeMotorStopped = true;

boolean overSpeedSelected = false;

boolean isPS3NavigatonInitialized = false;
boolean isSecondaryPS3NavigatonInitialized = false;

boolean isStickEnabled = true;
boolean isDomeStickEnabled = true;

boolean WaitingforReconnect = false;
boolean WaitingforReconnectDome = false;

boolean mainControllerConnected = false;
boolean domeControllerConnected = false;

// Dome Automation Variables
boolean domeAutomation = false;
int domeTurnDirection = 1;            // 1 = positive turn, -1 negative turn
float domeTargetPosition = 0;         // (0 - 359) - degrees in a circle, 0 = home
unsigned long domeStopTurnTime = 0;   // millis() when next turn should stop
unsigned long domeStartTurnTime = 0;  // millis() when next turn should start
int domeStatus = 0;                   // 0 = stopped, 1 = prepare to turn, 2 = turning

byte action = 0;
unsigned long DriveMillis = 0;

int footDriveSpeed = 0;

// ---------------------------------------------------------------------------------------
//                          Button Mapping
// ---------------------------------------------------------------------------------------
//
// Each controller button combo has a slot that names a WCB stored command. The built-in
// defaults are in src/default_mappings.cpp; DroidNet can change any slot at runtime over the
// WCB link (the "@SM" protocol, see lib/ShadowMap and WIPDocs/COMMAND_MAPPING_DESIGN.md) and
// the change is kept in EEPROM. None of it blocks: EEPROM writes happen one byte per pass in
// the background and everything bound for the WCB goes through one non-blocking queue.

sm::AvrEeprom mapEeprom;
sm::MapStore mapStore(mapEeprom, defaultButtonMappings, defaultButtonMappingsCount);
sm::TxQueue wcbTx;  // everything SHADOW sends the WCB on Serial1 after setup()
sm::WcbPort wcbReplies(wcbTx);
sm::Protocol mapProtocol(mapStore, SHADOW_FW_VERSION);
sm::LineReader wcbLines;
sm::LineReader usbLines;
// Serial1 as the mapping's per-pass work reaches it (include/sm_uart_avr.h): HardwareSerial's own
// buffers without a virtual call per byte. setup()'s direct Serial1 prints are unchanged.
sm::Uart1 wcbUart;

// Replies to "@SM" requests typed on the USB port go out with the debug output. A reply waits
// until `output` is nearly empty, so printOutput()'s write stays short (the UART buffer holds
// 64 bytes; 128 bytes at 115200 is under 6 ms).
class UsbReplyPort final : public sm::ReplyPort {
  public:
    static constexpr unsigned kBudget = 128;
    bool canSend(uint8_t len) override { return output.length() + len + 4 <= kBudget; }
    void send(const char *line, uint8_t len) override {
        (void)len;
        output += "\r\n";  // start a fresh line even if debug text didn't end one
        output += line;
        output += "\r\n";
    }
    bool requiresCrc() const override { return false; }
};
UsbReplyPort usbReplies;

// SHADOW_FW_VERSION goes in @SM READY and in the announce; ?WDP,DA on the WCB shows 14 characters.
static_assert(sizeof(SHADOW_FW_VERSION) <= 12, "SHADOW_FW_VERSION is longer than 11 characters");
#ifdef SHADOW_WDP_ANNOUNCE
// The announce line has to fit the 64-byte Serial1 TX buffer in one go.
static_assert(sm::Runtime::kAnnounceOverhead + sizeof(SHADOW_FW_VERSION) - 1 <= 63, "WCB announce line too long");
sm::Runtime mapRuntime(mapStore, mapProtocol, wcbTx, SHADOW_FW_VERSION, SHADOW_DOUBLE_TAP_MS,
                       SHADOW_LONG_PRESS_MS);  // announces every 25-30 s
#else
sm::Runtime mapRuntime(mapStore, mapProtocol, wcbTx, nullptr, SHADOW_DOUBLE_TAP_MS, SHADOW_LONG_PRESS_MS);
#endif

// The buttons that matter for the mapping, as a bitmask.
uint16_t heldButtons(PS3BT *pad) {
    uint16_t held = 0;
    if (pad->getButtonPress(UP))
        held |= sm::kHeldUp;
    if (pad->getButtonPress(RIGHT))
        held |= sm::kHeldRight;
    if (pad->getButtonPress(DOWN))
        held |= sm::kHeldDown;
    if (pad->getButtonPress(LEFT))
        held |= sm::kHeldLeft;
    if (pad->getButtonPress(L3))
        held |= sm::kHeldL3;
    if (pad->getButtonPress(PS))
        held |= sm::kHeldPs;
    if (pad->getButtonPress(L1))
        held |= sm::kHeldL1;
    if (pad->getButtonPress(L2))
        held |= sm::kHeldL2;
    if (pad->getButtonPress(CIRCLE))
        held |= sm::kHeldCircle;
    if (pad->getButtonPress(CROSS))
        held |= sm::kHeldCross;
    return held;
}

sm::PadState padState(PS3BT *pad) {
    const bool connected = pad->PS3NavigationConnected;
    return {connected, connected ? heldButtons(pad) : static_cast<uint16_t>(0)};
}

// Button combos to WCB stored commands (see sm::Runtime for the repeat and double-tap timing).
void mappedButtons() {
    sm::FiredPress fired;
    if (!mapRuntime.buttons(millis(), padState(PS3NavFoot), padState(PS3NavDome), &fired))
        return;

#ifdef SHADOW_VERBOSE
    char name[sm::kSlotTextBuf];
    sm::formatSlot(name, sizeof(name), fired.slot);
    output += "ACTIVATED: ";
    output += name;
    output += " -> ";
    output += fired.key[0] != '\0' ? fired.key : "(unmapped)";
    if (fired.modFromOther)
        output += " (modifier held on the other controller)";
    if (fired.key[0] != '\0' && !fired.queued)
        output += " [WCB queue full, dropped]";
    output += "\r\n";
#endif
}

#ifdef SHADOW_VERBOSE
const char *battLevelName(sm::BattLevel level) {
    switch (level) {
        case sm::BattLevel::Ok:
            return "OK";
        case sm::BattLevel::Low:
            return "LOW";
        case sm::BattLevel::Crit:
            return "CRIT";
        default:
            return "-";
    }
}
#endif

// The controllers' battery levels, for "@SM BAT". Every pass, faults included.
void watchBatteries() {
    const bool changed =
        mapRuntime.batteries(millis(), PS3NavFoot->PS3NavigationConnected, PS3NavFoot->getBatteryStatus(),
                             PS3NavDome->PS3NavigationConnected, PS3NavDome->getBatteryStatus());
#ifdef SHADOW_VERBOSE
    if (changed) {
        output += "Controller battery: foot ";
        output += battLevelName(mapRuntime.footBattery().level());
        output += ", dome ";
        output += battLevelName(mapRuntime.domeBattery().level());
        output += "\r\n";
    }
#else
    (void)changed;
#endif
}

// Everything the mapping does besides firing presses: @SM requests from DroidNet (via the WCB)
// or the USB port, battery levels, then the protocol's background work, the EEPROM writer and
// the WCB queue (sm::Runtime::pump and service: at most one heavy thing a pass besides presses).
// Runs every pass, after mappedButtons(), faults included, but SHADOW doesn't announce itself to
// the WCB while a controller fault is active. Never blocks.
void serviceMapping(bool faulted) {
    mapRuntime.pump(wcbUart, wcbLines, wcbReplies);
    mapRuntime.pump(Serial, usbLines, usbReplies);
    watchBatteries();
    mapRuntime.service(millis(), wcbUart, !faulted);
}

// =======================================================================================
//                          Main Program
// =======================================================================================

// =======================================================================================
//                          Initialize - Setup Function
// =======================================================================================
void setup() {
    // Debug Serial for use with USB Debugging
    Serial.begin(115200);
    while (!Serial)
        ;

    if (Usb.Init() == -1) {
        Serial.print(F("\r\nOSC did not start"));
        while (1)
            ;  // halt
    }

    Serial.print(F("\r\nBluetooth Library Started"));

#ifdef SHADOW_DEBUG
    Serial.print(F("\r\nSHADOW_DEBUG is enabled"));
#endif

    output.reserve(200);  // Reserve 200 bytes for the output string

    // Setup for PS3
    PS3NavFoot->attachOnInit(onInitPS3NavFoot);  // onInitPS3NavFoot is called upon a new connection
    PS3NavDome->attachOnInit(onInitPS3NavDome);

    // Setup for Serial2:: Motor Controllers - Sabertooth (Feet)
    Serial2.begin(motorControllerBaudRate);
    ST->autobaud();      // Send the autobaud command to the Sabertooth controller(s).
    ST->setTimeout(10);  // DMB:  How low can we go for safety reasons?  multiples of 100ms
    ST->setDeadband(driveDeadBandRange);
    ST->stop();
    SyR->autobaud();
    SyR->setTimeout(20);  // DMB:  How low can we go for safety reasons?  multiples of 100ms
    SyR->stop();

    // Setup for Serial1:: MarcDuino Dome Control Board
    Serial1.begin(marcDuinoBaudRate);

    // Setup for HCR Vocalizer
    Serial1.print("<PVV100,PVA100,PVB100>\r");  // Start volumes at 100

    randomSeed(analogRead(0));  // random number seed for dome automation

    Serial1.print("P1\r");  // Set Magic Panel to Always On

    // Button mapping: reads the EEPROM headers once; everything after this is non-blocking.
    mapStore.begin();
#ifdef SHADOW_VERBOSE
    output += "\r\nButton mapping: ";
    if (mapStore.formatted()) {
        output += mapStore.validEepromSlots();
        output += " slot(s) from EEPROM, the rest built-in";
    } else {
        output += "built-in defaults";
    }
    output += ", prefix ";
    output += mapStore.prefix();
    output += "\r\n";
    output += "Firmware ";
    output += SHADOW_FW_VERSION;
    output += "\r\n";
#endif

    // First WCB device announce in 0.5-3 s (if SHADOW_WDP_ANNOUNCE is on).
    mapRuntime.startAnnounce(millis(), static_cast<uint32_t>(random()));
#ifdef SHADOW_BATTERY_REPORT
    // First controller battery report in 5 s, then on every change and every 30 s.
    mapRuntime.startBattery(wcbReplies, millis());
#endif
}

// =======================================================================================
//           footDrive Motor Control Section
// =======================================================================================

boolean ps3FootMotorDrive(PS3BT *myPS3 = PS3NavFoot) {
    int stickSpeed = 0;
    int turnnum = 0;


    if (isPS3NavigatonInitialized) {
        // CRITICAL SAFETY CHECK: Do NOT send commands if we're waiting for reconnect due to communication issues
        if (WaitingforReconnect) {
            if (!isFootMotorStopped) {
                ST->stop();
                isFootMotorStopped = true;
                footDriveSpeed = 0;

#ifdef SHADOW_VERBOSE
                output += "\r\n***Foot Motor STOPPED - Waiting for reconnect***\r\n";
#endif
            }
            return false;
        }

        // Additional fault control.  Do NOT send additional commands to Sabertooth if no controllers have initialized.
        if (!isStickEnabled) {
#ifdef SHADOW_VERBOSE
            if (abs(myPS3->getAnalogHat(LeftHatY) - 128) > joystickFootDeadZoneRange) {
                output += "Drive Stick is disabled\r\n";
            }
#endif

            if (!isFootMotorStopped) {
                ST->stop();
                isFootMotorStopped = true;
                footDriveSpeed = 0;

#ifdef SHADOW_VERBOSE
                output += "\r\n***Foot Motor STOPPED***\r\n";
#endif
            }

            return false;
        } else if (!myPS3->PS3NavigationConnected) {
            if (!isFootMotorStopped) {
                ST->stop();
                isFootMotorStopped = true;
                footDriveSpeed = 0;

#ifdef SHADOW_VERBOSE
                output += "\r\n***Foot Motor STOPPED***\r\n";
#endif
            }

            return false;
        } else if (myPS3->getButtonPress(L2) || myPS3->getButtonPress(L1)) {
            if (!isFootMotorStopped) {
                ST->stop();
                isFootMotorStopped = true;
                footDriveSpeed = 0;

#ifdef SHADOW_VERBOSE
                output += "\r\n***Foot Motor STOPPED***\r\n";
#endif
            }

            return false;
        } else {
            int joystickPosition = myPS3->getAnalogHat(LeftHatY);

            if (overSpeedSelected)  // Over throttle is selected
            {
                stickSpeed = (map(joystickPosition, 0, 255, -drivespeed2, drivespeed2));
            } else {
                stickSpeed = (map(joystickPosition, 0, 255, -drivespeed1, drivespeed1));
            }

            if (abs(joystickPosition - 128) < joystickFootDeadZoneRange) {
                // This is RAMP DOWN code when stick is now at ZERO but prior FootSpeed > 20

                if (abs(footDriveSpeed) > 50) {
                    if (footDriveSpeed > 0) {
                        footDriveSpeed -= 3;
                    } else {
                        footDriveSpeed += 3;
                    }

#ifdef SHADOW_VERBOSE
                    output += "ZERO FAST RAMP: footSpeed: ";
                    output += footDriveSpeed;
                    output += "\nStick Speed: ";
                    output += stickSpeed;
                    output += "\n\r";
#endif
                } else if (abs(footDriveSpeed) > 20) {
                    if (footDriveSpeed > 0) {
                        footDriveSpeed -= 2;
                    } else {
                        footDriveSpeed += 2;
                    }

#ifdef SHADOW_VERBOSE
                    output += "ZERO MID RAMP: footSpeed: ";
                    output += footDriveSpeed;
                    output += "\nStick Speed: ";
                    output += stickSpeed;
                    output += "\n\r";
#endif
                } else {
                    footDriveSpeed = 0;
                }
            } else {
                isFootMotorStopped = false;

                if (footDriveSpeed < stickSpeed) {
                    if ((stickSpeed - footDriveSpeed) > (ramping + 1)) {
                        footDriveSpeed += ramping;

#ifdef SHADOW_VERBOSE
                        output += "RAMPING UP: footSpeed: ";
                        output += footDriveSpeed;
                        output += "\nStick Speed: ";
                        output += stickSpeed;
                        output += "\n\r";
#endif
                    } else
                        footDriveSpeed = stickSpeed;
                } else if (footDriveSpeed > stickSpeed) {
                    if ((footDriveSpeed - stickSpeed) > (ramping + 1)) {
                        footDriveSpeed -= ramping;

#ifdef SHADOW_VERBOSE
                        output += "RAMPING DOWN: footSpeed: ";
                        output += footDriveSpeed;
                        output += "\nStick Speed: ";
                        output += stickSpeed;
                        output += "\n\r";
#endif
                    } else
                        footDriveSpeed = stickSpeed;
                } else {
                    footDriveSpeed = stickSpeed;
                }
            }

            turnnum = (myPS3->getAnalogHat(LeftHatX));

            // TODO:  Is there a better algorithm here?
            if (abs(footDriveSpeed) > 50)
                turnnum = (map(myPS3->getAnalogHat(LeftHatX), 54, 200, -(turnspeed / 4), (turnspeed / 4)));
            else if (turnnum <= 200 && turnnum >= 54)
                turnnum = (map(myPS3->getAnalogHat(LeftHatX), 54, 200, -(turnspeed / 3), (turnspeed / 3)));
            else if (turnnum > 200)
                turnnum = (map(myPS3->getAnalogHat(LeftHatX), 201, 255, turnspeed / 3, turnspeed));
            else if (turnnum < 54)
                turnnum = (map(myPS3->getAnalogHat(LeftHatX), 0, 53, -turnspeed, -(turnspeed / 3)));

            if (abs(turnnum) > 5) {
                isFootMotorStopped = false;
            }

            currentMillis = millis();

            if ((currentMillis - previousFootMillis) > SerialLatency) {
                if (footDriveSpeed != 0 || abs(turnnum) > 5) {
                    // FINAL SAFETY CHECK: Ensure we're not waiting for reconnect before sending motor commands
                    if (WaitingforReconnect) {
#ifdef SHADOW_VERBOSE
                        output += "SAFETY BLOCK: Motor commands blocked due to WaitingforReconnect\r\n";
#endif
                        ST->stop();
                        isFootMotorStopped = true;
                        footDriveSpeed = 0;
                        return false;
                    }

#ifdef SHADOW_VERBOSE
                    output += "Motor: FootSpeed: ";
                    output += footDriveSpeed;
                    output += "\nTurnnum: ";
                    output += turnnum;
                    output += "\nTime of command: ";
                    output += millis();
#endif

                    ST->turn(turnnum * invertTurnDirection);
                    ST->drive(footDriveSpeed);
                } else {
                    if (!isFootMotorStopped) {
                        ST->stop();
                        isFootMotorStopped = true;
                        footDriveSpeed = 0;

#ifdef SHADOW_VERBOSE
                        output += "\r\n***Foot Motor STOPPED***\r\n";
#endif
                    }
                }

                // The Sabertooth won't act on mixed mode packet Serial commands until
                // it has received power levels for BOTH throttle and turning, since it
                // mixes the two together to get diff-drive power levels for both motors.

                previousFootMillis = currentMillis;
                return true;  // we sent a foot command
            }
        }
    }
    return false;
}

void footMotorDrive() {
    // Flood control prevention
    if ((millis() - previousFootMillis) < SerialLatency)
        return;

    if (PS3NavFoot->PS3NavigationConnected)
        ps3FootMotorDrive(PS3NavFoot);
}

// =======================================================================================
//           domeDrive Motor Control Section
// =======================================================================================

int ps3DomeDrive(PS3BT *myPS3 = PS3NavDome) {
    int domeRotationSpeed = 0;

    int joystickPosition = myPS3->getAnalogHat(LeftHatX);

    domeRotationSpeed = (map(joystickPosition, 0, 255, -domespeed, domespeed));

    if (abs(joystickPosition - 128) < joystickDomeDeadZoneRange)
        domeRotationSpeed = 0;

    if (domeRotationSpeed != 0 && domeAutomation == true)  // Turn off dome automation if manually moved
    {
        domeAutomation = false;
        domeStatus = 0;
        domeTargetPosition = 0;

#ifdef SHADOW_VERBOSE
        output += "Dome Automation OFF\r\n";
#endif
    }

    return domeRotationSpeed;
}

void rotateDome(int domeRotationSpeed, String mesg) {
    // Constantly sending commands to the SyRen (Dome) is causing foot motor delay.
    // Lets reduce that chatter by trying 3 things:
    //  1.) Eliminate a constant stream of "don't spin" messages (isDomeMotorStopped flag)
    //  2.) Add a delay between commands sent to the SyRen (previousDomeMillis timer)
    //  3.) Switch to real UART on the MEGA (Likely the *CORE* issue and solution)
    //  4.) Reduce the timout of the SyRen - just better for safety!

    // CRITICAL SAFETY CHECK: Do NOT send dome commands if waiting for reconnect
    if (WaitingforReconnectDome || WaitingforReconnect) {
        if (!isDomeMotorStopped) {
            SyR->stop();
            isDomeMotorStopped = true;
#ifdef SHADOW_VERBOSE
            output += "\r\n***Dome Motor STOPPED - Waiting for reconnect***\r\n";
#endif
        }
        return;
    }

    currentMillis = millis();
    if ((!isDomeMotorStopped || domeRotationSpeed != 0) &&
        ((currentMillis - previousDomeMillis) > (2 * SerialLatency))) {
        if (domeRotationSpeed != 0) {
            isDomeMotorStopped = false;

#ifdef SHADOW_VERBOSE
            output += "Dome rotation speed: ";
            output += domeRotationSpeed;
#endif

            SyR->motor(domeRotationSpeed);
        } else {
            isDomeMotorStopped = true;

#ifdef SHADOW_VERBOSE
            output += "\n\r***Dome motor is STOPPED***\n\r";
#endif

            SyR->stop();
        }

        previousDomeMillis = currentMillis;
    }
}

void domeDrive() {
    // Flood control prevention
    // This is intentionally set to double the rate of the Dome Motor Latency
    if ((millis() - previousDomeMillis) < (2 * SerialLatency))
        return;

    int domeRotationSpeed = 0;
    int ps3NavControlSpeed = 0;

    if (PS3NavDome->PS3NavigationConnected && isDomeStickEnabled) {
        ps3NavControlSpeed = ps3DomeDrive(PS3NavDome);

        domeRotationSpeed = ps3NavControlSpeed;

        rotateDome(domeRotationSpeed, "Controller Move");
    } else if (PS3NavFoot->PS3NavigationConnected && PS3NavFoot->getButtonPress(L2)) {
        ps3NavControlSpeed = ps3DomeDrive(PS3NavFoot);

        domeRotationSpeed = ps3NavControlSpeed;

        rotateDome(domeRotationSpeed, "Controller Move");
    } else {
        if (!isDomeMotorStopped) {
            SyR->stop();
            isDomeMotorStopped = true;
        }
    }
}

// =======================================================================================
//                               Toggle Control Section
// =======================================================================================

void ps3ToggleSettings(PS3BT *myPS3 = PS3NavFoot) {
    // enable / disable drive stick
    if (myPS3->getButtonPress(PS) && myPS3->getButtonClick(CROSS)) {
        if (myPS3 == PS3NavFoot) {
#ifdef SHADOW_VERBOSE
            output += "Disabling the Foot DriveStick\r\n";
            output += "Stopping Motors";
#endif
            ST->stop();
            isFootMotorStopped = true;
            isStickEnabled = false;
            footDriveSpeed = 0;
        } else if (myPS3 == PS3NavDome) {
#ifdef SHADOW_VERBOSE
            output += "Disabling the Dome Stick\r\n";
            output += "Stopping Dome";
#endif
            SyR->stop();
            isDomeMotorStopped = true;
            isDomeStickEnabled = false;
        }
    }

    if (myPS3->getButtonPress(PS) && myPS3->getButtonClick(CIRCLE)) {
        if (myPS3 == PS3NavFoot) {
#ifdef SHADOW_VERBOSE
            output += "Enabling the Foot DriveStick\r\n";
#endif
            isStickEnabled = true;
        } else if (myPS3 == PS3NavDome) {
#ifdef SHADOW_VERBOSE
            output += "Enabling the Dome Stick\r\n";
#endif
            isDomeStickEnabled = true;
        }
    }

    // Enable and Disable Overspeed (foot controller only: dome L3+L1 is a mapped combo)
    if (myPS3 == PS3NavFoot && myPS3->getButtonPress(L3) && myPS3->getButtonPress(L1) && isStickEnabled) {
        if ((millis() - previousSpeedToggleMillis) > 1000) {
            speedToggleButtonCounter = 0;
            previousSpeedToggleMillis = millis();
        }

        speedToggleButtonCounter += 1;

        if (speedToggleButtonCounter == 1) {
            if (!overSpeedSelected) {
                overSpeedSelected = true;

#ifdef SHADOW_VERBOSE
                output += "Over Speed is now: ON";
#endif
            } else {
                overSpeedSelected = false;

#ifdef SHADOW_VERBOSE
                output += "Over Speed is now: OFF";
#endif
            }
        }
    }

    // Enable Disable Dome Automation
    if (myPS3->getButtonPress(L2) && myPS3->getButtonClick(CROSS)) {
        domeAutomation = false;
        domeStatus = 0;
        domeTargetPosition = 0;
        SyR->stop();
        isDomeMotorStopped = true;

#ifdef SHADOW_VERBOSE
        output += "Dome Automation OFF\r\n";
#endif
    }

    if (myPS3->getButtonPress(L2) && myPS3->getButtonClick(CIRCLE)) {
        domeAutomation = true;

#ifdef SHADOW_VERBOSE
        output += "Dome Automation ON\r\n";
#endif
    }
}

void toggleSettings() {
    if (PS3NavFoot->PS3NavigationConnected)
        ps3ToggleSettings(PS3NavFoot);
    if (PS3NavDome->PS3NavigationConnected)
        ps3ToggleSettings(PS3NavDome);
}

// =======================================================================================
//                             Dome Automation Function
//
//    Features toggles 'on' via L2 + CIRCLE.  'off' via L2 + CROSS.  Default is 'off'.
//
//    This routines randomly turns the dome motor in both directions.  It assumes the
//    dome is in the 'home' position when the auto dome feature is toggled on.  From
//    there it turns the dome in a random direction.  Stops for a random length of
//    of time.  Then returns the dome to the home position.  This randomly repeats.
//
//    It is driven off the user variable - time360DomeTurn.  This records how long
//    it takes the dome to do a 360 degree turn at the given auto dome speed.  Tweaking
//    this parameter to be close provides the best results.
//
//    Activating the dome controller manually immediately cancels the auto dome feature
//    or you can toggle the feature off by pressing L2 + CROSS.
// =======================================================================================
void autoDome() {
    long rndNum;
    int domeSpeed;

    if (domeStatus == 0)  // Dome is currently stopped - prepare for a future turn
    {
        if (domeTargetPosition == 0)  // Dome is currently in the home position - prepare to turn away
        {
            domeStartTurnTime = millis() + (random(25, 50) * 1000);

            rndNum = random(5, 354);

            domeTargetPosition =
                rndNum;  // set the target position to a random degree of a 360 circle - shaving off the first and last 5 degrees

            if (domeTargetPosition < 180)  // Turn the dome in the positive direction
            {
                domeTurnDirection = 1;

                domeStopTurnTime = domeStartTurnTime + ((domeTargetPosition / 360) * time360DomeTurn);
            } else  // Turn the dome in the negative direction
            {
                domeTurnDirection = -1;

                domeStopTurnTime = domeStartTurnTime + (((360 - domeTargetPosition) / 360) * time360DomeTurn);
            }
        } else  // Dome is not in the home position - send it back to home
        {
            domeStartTurnTime = millis() + (random(15, 35) * 1000);

            if (domeTargetPosition < 180) {
                domeTurnDirection = -1;

                domeStopTurnTime = domeStartTurnTime + ((domeTargetPosition / 360) * time360DomeTurn);
            } else {
                domeTurnDirection = 1;

                domeStopTurnTime = domeStartTurnTime + (((360 - domeTargetPosition) / 360) * time360DomeTurn);
            }

            domeTargetPosition = 0;
        }

        domeStatus = 1;  // Set dome status to preparing for a future turn

#ifdef SHADOW_DEBUG
        output += "Dome Automation: Initial Turn Set\r\n";
        output += "Current Time: ";
        output += millis();
        output += "\r\n Next Start Time: ";
        output += domeStartTurnTime;
        output += "\r\n";
        output += "Next Stop Time: ";
        output += domeStopTurnTime;
        output += "\r\n";
        output += "Dome Target Position: ";
        output += domeTargetPosition;
        output += "\r\n";
#endif
    }

    if (domeStatus == 1)  // Dome is prepared for a future move - start the turn when ready
    {
        if (domeStartTurnTime < millis()) {
            domeStatus = 2;

#ifdef SHADOW_DEBUG
            output += "Dome Automation: Ready To Start Turn\r\n";
#endif
        }
    }

    if (domeStatus == 2)  // Dome is now actively turning until it reaches its stop time
    {
        if (domeStopTurnTime > millis()) {
            domeSpeed = domeAutoSpeed * domeTurnDirection;

            SyR->motor(domeSpeed);

#ifdef SHADOW_DEBUG
            output += "Turning Now!!\r\n";
#endif
        } else  // turn completed - stop the motor
        {
            domeStatus = 0;
            SyR->stop();

#ifdef SHADOW_DEBUG
            output += "STOP TURN!!\r\n";
#endif
        }
    }
}

// =======================================================================================
//           Program Utility Functions - Called from various locations
// =======================================================================================

// =======================================================================================
//           PPS3 Controller Device Mgt Functions
// =======================================================================================

void onInitPS3NavFoot() {
#ifdef SHADOW_DEBUG
    Serial.println(F("\r\n*** onInitPS3NavFoot CALLED ***"));
#endif

    String btAddress = getLastConnectedBtMAC();
    PS3NavFoot->setLedOn(LED1);
    isPS3NavigatonInitialized = true;
    badPS3Data = 0;

#ifdef SHADOW_DEBUG
    output += "\r\nBT Address of Last connected Device when FOOT PS3 Connected: ";
    output += btAddress;
#endif

    if (btAddress == PS3ControllerFootMac || btAddress == PS3ControllerBackupFootMac) {
#ifdef SHADOW_DEBUG
        output += "\r\nWe have our FOOT controller connected.\r\n";
#endif

        mainControllerConnected = true;
        WaitingforReconnect = true;
    } else {
// Prevent connection from anything but the MAIN controllers
#ifdef SHADOW_DEBUG
        output += "\r\nWe have an invalid controller trying to connect as tha FOOT controller, it will be dropped.\r\n";
#endif

        ST->stop();
        SyR->stop();
        isFootMotorStopped = true;
        footDriveSpeed = 0;
        PS3NavFoot->setLedOff(LED1);
        PS3NavFoot->disconnect();
#ifdef SHADOW_DEBUG
        printOutput();
#endif

        isPS3NavigatonInitialized = false;
        mainControllerConnected = false;
    }
}

void onInitPS3NavDome() {
#ifdef SHADOW_DEBUG
    Serial.println(F("\r\n*** onInitPS3NavDome CALLED ***"));
#endif

    String btAddress = getLastConnectedBtMAC();
    PS3NavDome->setLedOn(LED1);
    isSecondaryPS3NavigatonInitialized = true;
    badPS3Data = 0;

    if (btAddress == PS3ControllerDomeMAC || btAddress == PS3ControllerBackupDomeMAC) {
#ifdef SHADOW_DEBUG
        output += "\r\nWe have our DOME controller connected.\r\n";
#endif

        domeControllerConnected = true;
        WaitingforReconnectDome = true;
    } else {
// Prevent connection from anything but the DOME controllers
#ifdef SHADOW_DEBUG
        output += "\r\nWe have an invalid controller trying to connect as the DOME controller, it will be dropped.\r\n";
#endif

        ST->stop();
        SyR->stop();
        isFootMotorStopped = true;
        footDriveSpeed = 0;
        PS3NavDome->setLedOff(LED1);
        PS3NavDome->disconnect();
#ifdef SHADOW_DEBUG
        printOutput();
#endif

        isSecondaryPS3NavigatonInitialized = false;
        domeControllerConnected = false;
    }
}

String getLastConnectedBtMAC() {
    String btAddress = "";
    for (int8_t i = 5; i > 0; i--) {
        if (Btd.disc_bdaddr[i] < 0x10) {
            btAddress += "0";
        }
        btAddress += String(Btd.disc_bdaddr[i], HEX);
        btAddress += (":");
    }
    btAddress += String(Btd.disc_bdaddr[0], HEX);
    btAddress.toUpperCase();
    return btAddress;
}

boolean criticalFaultDetect() {
    if (PS3NavFoot->PS3NavigationConnected || PS3NavFoot->PS3Connected) {
        currentTime = millis();
        lastMsgTime = PS3NavFoot->getLastMessageTime();

        // Handle case where getLastMessageTime() returns 0 (no messages received yet)
        // This is normal for newly connected controllers that haven't sent data yet
        if (lastMsgTime == 0) {
            // If we just connected and haven't received any messages yet, don't timeout
            static unsigned long connectionStartTime = 0;
            if (connectionStartTime == 0 || !isPS3NavigatonInitialized) {
                connectionStartTime = currentTime;
            }

            // Give the controller up to 30 seconds to start sending data after connection
            if ((currentTime - connectionStartTime) > 30000) {
#ifdef SHADOW_DEBUG
                output += "No messages received from PS3 controller in 30s after connection\r\n";
                output += "Disconnecting the Foot controller.\r\n";
#endif
                ST->stop();
                isFootMotorStopped = true;
                footDriveSpeed = 0;
                PS3NavFoot->disconnect();
                WaitingforReconnect = true;
                connectionStartTime = 0;  // Reset for next connection
                return true;
            }

            // Don't perform timeout checks if we haven't received any messages yet
            return false;
        }

        msgLagTime = currentTime - lastMsgTime;

        if (WaitingforReconnect) {
            if (msgLagTime < 200) {
                WaitingforReconnect = false;
            }

            lastMsgTime = currentTime;
        }

        if (currentTime >= lastMsgTime) {
            msgLagTime = currentTime - lastMsgTime;
        } else {
            msgLagTime = 0;
        }

        if (msgLagTime > 300 && !isFootMotorStopped) {
#ifdef SHADOW_DEBUG
            output += "It has been 300ms since we heard from the PS3 Foot Controller\r\n";
            output += "Shut downing motors, and watching for a new PS3 Foot message\r\n";
#endif
            ST->stop();
            isFootMotorStopped = true;
            footDriveSpeed = 0;
            // Clear any remaining motor state to prevent phantom commands
            WaitingforReconnect = true;
        }

        if (msgLagTime > 10000) {
#ifdef SHADOW_DEBUG
            output += "It has been 10s since we heard from the PS3 Foot Controller\r\n";
            output += "msgLagTime:";
            output += msgLagTime;
            output += "  lastMsgTime:";
            output += lastMsgTime;
            output += "  millis:";
            output += millis();
            output += "\r\nDisconnecting the Foot controller.\r\n";
#endif
            ST->stop();
            isFootMotorStopped = true;
            footDriveSpeed = 0;
            PS3NavFoot->disconnect();
            WaitingforReconnect = true;
            return true;
        }

        // Check PS3 Signal Data only if we've received some messages or been connected for a while
        // Skip this check for newly connected controllers to prevent freezing during initialization
        static unsigned long lastDataCheckTime = 0;
        if (lastMsgTime > 0 || (currentTime - lastDataCheckTime) > 5000) {
            lastDataCheckTime = currentTime;

            if (!PS3NavFoot->getStatus(Plugged) && !PS3NavFoot->getStatus(Unplugged)) {
                // We don't have good data from the controller.
                // Wait 15ms if no second controller - 100ms if some controller connected, Update USB, and try again
                if (PS3NavDome->PS3NavigationConnected) {
                    delay(100);
                } else {
                    delay(15);
                }

                Usb.Task();
                lastMsgTime = PS3NavFoot->getLastMessageTime();

                if (!PS3NavFoot->getStatus(Plugged) && !PS3NavFoot->getStatus(Unplugged)) {
                    badPS3Data++;
#ifdef SHADOW_DEBUG
                    output += "\r\n**Invalid data from PS3 FOOT Controller. - Resetting Data**\r\n";
#endif
                    return true;
                }
            } else if (badPS3Data > 0) {
                badPS3Data = 0;
            }

            if (badPS3Data > 10) {
#ifdef SHADOW_DEBUG
                output += "Too much bad data coming from the PS3 FOOT Controller\r\n";
                output += "Disconnecting the controller and stop motors.\r\n";
#endif
                ST->stop();
                isFootMotorStopped = true;
                footDriveSpeed = 0;
                PS3NavFoot->disconnect();
                WaitingforReconnect = true;
                return true;
            }
        }
    } else if (!isFootMotorStopped) {
#ifdef SHADOW_DEBUG
        output += "No foot controller was found\r\n";
        output += "Shuting down motors and watching for a new PS3 foot message\r\n";
#endif
        ST->stop();
        isFootMotorStopped = true;
        footDriveSpeed = 0;
        WaitingforReconnect = true;
        return true;
    }

    return false;
}

boolean criticalFaultDetectDome() {
    if (PS3NavDome->PS3NavigationConnected || PS3NavDome->PS3Connected) {
        currentTime = millis();
        lastMsgTime = PS3NavDome->getLastMessageTime();

        // Handle case where getLastMessageTime() returns 0 (no messages received yet)
        // This is normal for newly connected controllers that haven't sent data yet
        if (lastMsgTime == 0) {
            // If we just connected and haven't received any messages yet, don't timeout
            static unsigned long domeConnectionStartTime = 0;
            if (domeConnectionStartTime == 0 || !isSecondaryPS3NavigatonInitialized) {
                domeConnectionStartTime = currentTime;
            }

            // Give the controller up to 30 seconds to start sending data after connection
            if ((currentTime - domeConnectionStartTime) > 30000) {
#ifdef SHADOW_DEBUG
                output += "No messages received from PS3 dome controller in 30s after connection\r\n";
                output += "Disconnecting the Dome controller.\r\n";
#endif
                SyR->stop();
                PS3NavDome->disconnect();
                WaitingforReconnectDome = true;
                domeConnectionStartTime = 0;  // Reset for next connection
                return true;
            }

            // Don't perform timeout checks if we haven't received any messages yet
            return false;
        }

        msgLagTime = currentTime - lastMsgTime;

        if (WaitingforReconnectDome) {
            if (msgLagTime < 200) {
                WaitingforReconnectDome = false;
            }

            lastMsgTime = currentTime;
        }

        if (currentTime >= lastMsgTime) {
            msgLagTime = currentTime - lastMsgTime;
        } else {
            msgLagTime = 0;
        }

        if (msgLagTime > 10000) {
#ifdef SHADOW_DEBUG
            output += "It has been 10s since we heard from the PS3 Dome Controller\r\n";
            output += "msgLagTime:";
            output += msgLagTime;
            output += "  lastMsgTime:";
            output += lastMsgTime;
            output += "  millis:";
            output += millis();
            output += "\r\nDisconnecting the Dome controller.\r\n";
#endif

            SyR->stop();
            PS3NavDome->disconnect();
            WaitingforReconnectDome = true;
            return true;
        }

        // Check PS3 Signal Data only if we've received some messages or been connected for a while
        // Skip this check for newly connected controllers to prevent freezing during initialization
        static unsigned long lastDomeDataCheckTime = 0;
        if (lastMsgTime > 0 || (currentTime - lastDomeDataCheckTime) > 5000) {
            lastDomeDataCheckTime = currentTime;

            if (!PS3NavDome->getStatus(Plugged) && !PS3NavDome->getStatus(Unplugged)) {
                // We don't have good data from the controller.
                // Wait 100ms, Update USB, and try again
                delay(100);

                Usb.Task();
                lastMsgTime = PS3NavDome->getLastMessageTime();

                if (!PS3NavDome->getStatus(Plugged) && !PS3NavDome->getStatus(Unplugged)) {
                    badPS3DataDome++;
#ifdef SHADOW_DEBUG
                    output += "\r\n**Invalid data from PS3 Dome Controller. - Resetting Data**\r\n";
#endif
                    return true;
                }
            } else if (badPS3DataDome > 0) {
                badPS3DataDome = 0;
            }

            if (badPS3DataDome > 10) {
#ifdef SHADOW_DEBUG
                output += "Too much bad data coming from the PS3 DOME Controller\r\n";
                output += "Disconnecting the controller and stop motors.\r\n";
#endif
                SyR->stop();
                PS3NavDome->disconnect();
                WaitingforReconnectDome = true;
                return true;
            }
        }
    }

    return false;
}

// =======================================================================================
//           USB Read Function - Supports Main Program Loop
// =======================================================================================

boolean readUSB() {
#ifdef SHADOW_DEBUG
    static unsigned long lastReadUSBDebug = 0;
    if (millis() - lastReadUSBDebug > 3000) {  // Output every 3 seconds
        lastReadUSBDebug = millis();
        output += "readUSB() executing...\r\n";
    }
#endif

    Usb.Task();

#ifdef SHADOW_DEBUG
    static unsigned long lastUSBTaskDebug = 0;
    if (millis() - lastUSBTaskDebug > 3000) {  // Output every 3 seconds
        lastUSBTaskDebug = millis();
        output += "Usb.Task() completed\r\n";
    }
#endif

    // The more devices we have connected to the USB or BlueTooth, the more often Usb.Task need to be called to eliminate latency.
    if (PS3NavFoot->PS3NavigationConnected) {
#ifdef SHADOW_DEBUG
        static unsigned long lastFootConnectedDebug = 0;
        if (millis() - lastFootConnectedDebug > 5000) {  // Output every 5 seconds
            lastFootConnectedDebug = millis();
            output += "PS3NavFoot connected, running criticalFaultDetect()\r\n";
        }
#endif

        if (criticalFaultDetect()) {
            // We have a fault condition that we want to ensure that we do NOT process any controller data!!!
#ifdef SHADOW_DEBUG
            output += "criticalFaultDetect() returned true, exiting readUSB()\r\n";
#endif
            printOutput();
            return false;
        }

#ifdef SHADOW_DEBUG
        static unsigned long lastFaultCheckDebug = 0;
        if (millis() - lastFaultCheckDebug > 5000) {  // Output every 5 seconds
            lastFaultCheckDebug = millis();
            output += "criticalFaultDetect() completed successfully\r\n";
        }
#endif
    } else if (!isFootMotorStopped) {
#ifdef SHADOW_DEBUG
        output += "No foot controller was found\r\n";
        output += "Shuting down motors, and watching for a new PS3 foot message\r\n";
#endif
        ST->stop();
        isFootMotorStopped = true;
        footDriveSpeed = 0;
        WaitingforReconnect = true;
    }

    if (PS3NavDome->PS3NavigationConnected) {
        if (criticalFaultDetectDome()) {
            // We have a fault condition that we want to ensure that we do NOT process any controller data!!!
            printOutput();
            return false;
        }
    }

#ifdef SHADOW_DEBUG
    static unsigned long lastReadUSBExitDebug = 0;
    if (millis() - lastReadUSBExitDebug > 3000) {  // Output every 3 seconds
        lastReadUSBExitDebug = millis();
        output += "readUSB() returning true\r\n";
    }
#endif

    return true;
}

// =======================================================================================
//          Print Output Function
// =======================================================================================

void printOutput() {
    if (output != "") {
        if (Serial)
            Serial.println(output);
        output = "";  // Reset output string
    }
}

// =======================================================================================
//           Main Program Loop - This is the recurring check loop for entire sketch
// =======================================================================================

void loop() {
// Useful to enable with Serial console when having controller issues.
#ifdef TEST_CONROLLER
    testPS3Controller();
#endif

#ifdef SHADOW_DEBUG
    // DEBUG: Confirm loop is still running
    static unsigned long lastLoopAlive = 0;
    if (millis() - lastLoopAlive > 1000) {  // Output every 1 second
        lastLoopAlive = millis();
        Serial.print(".");  // Simple heartbeat
    }

    // DEBUG: Check PS3 Navigation controller status
    static unsigned long lastDebugOutput = 0;
    if (millis() - lastDebugOutput > 2000) {  // Output every 2 seconds
        lastDebugOutput = millis();

        output += "FOOT: ";
        output += PS3NavFoot->PS3NavigationConnected ? "YES" : "NO";
        output += " | DOME: ";
        output += PS3NavDome->PS3NavigationConnected ? "YES" : "NO";
        output += " | FootInit: ";
        output += isPS3NavigatonInitialized ? "YES" : "NO";
        output += " | DomeInit: ";
        output += isSecondaryPS3NavigatonInitialized ? "YES" : "NO";
        output += "\r\n";

        if (PS3NavFoot->PS3NavigationConnected || PS3NavDome->PS3NavigationConnected) {
            output += "Last BT MAC: ";
            output += getLastConnectedBtMAC();
            output += "\r\n";
        }
    }
#endif

    // LOOP through functions from highest to lowest priority.

    if (!readUSB()) {
        // We have a fault condition that we want to ensure that we do NOT process any controller data!!!
        serviceMapping(true);
        printOutput();
        return;
    }

    footMotorDrive();
    domeDrive();
    mappedButtons();
    toggleSettings();
    serviceMapping(false);
    printOutput();

    // If dome automation is enabled - Call function
    if (domeAutomation && time360DomeTurn > 1999 && time360DomeTurn < 8001 && domeAutoSpeed > 49 &&
        domeAutoSpeed < 101) {
        autoDome();
    }
}