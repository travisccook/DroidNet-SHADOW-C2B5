#include "../include/config.h"

// =======================================================================================
//                          SHADOW+MD+C2B5 Configuration Values
// =======================================================================================
//
// This file contains the actual values for all configuration variables.
// These values are declared in config.h and used throughout the project.
//
// =======================================================================================

// ---------------------------------------------------------------------------------------
//                          PS3 Controller MAC Addresses
// ---------------------------------------------------------------------------------------
// The addresses live in include/controller_macs.h, which is gitignored. Copy
// include/controller_macs.example.h to include/controller_macs.h and set your own.

#if __has_include("../include/controller_macs.h")
#include "../include/controller_macs.h"
#else
#error "Missing include/controller_macs.h: copy include/controller_macs.example.h to include/controller_macs.h and set your PS3 controller MAC addresses."
#endif

String PS3ControllerFootMac = PS3_FOOT_MAC;               // FOOT PS3 controller MAC address
String PS3ControllerDomeMAC = PS3_DOME_MAC;               // Secondary DOME PS3 controller MAC address (Optional)
String PS3ControllerBackupFootMac = PS3_BACKUP_FOOT_MAC;  // BACKUP FOOT controller MAC address (Optional)
String PS3ControllerBackupDomeMAC = PS3_BACKUP_DOME_MAC;  // BACKUP DOME controller MAC address (Optional)

// ---------------------------------------------------------------------------------------
//                          Drive System Configuration
// ---------------------------------------------------------------------------------------

byte drivespeed1 =
    70;  // For Speed Setting (Normal): set this to whatever speeds works for you. 0-stop, 127-full speed.
byte drivespeed2 =
    110;  // For Speed Setting (Over Throttle): set this for when needing extra power. 0-stop, 127-full speed.

byte turnspeed = 75;  // the higher this number the faster it will spin in place, lower - the easier to control.
                      // Recommend beginner: 40 to 50, experienced: 50+, I like 75

byte ramping = 1;  // Ramping- the lower this number the longer R2 will take to speedup or slow down,
                   // change this by increments of 1

int invertTurnDirection = -1;  // This may need to be set to 1 for some configurations

// ---------------------------------------------------------------------------------------
//                          Dome Configuration
// ---------------------------------------------------------------------------------------

byte domespeed = 100;  // If using a speed controller for the dome, sets the top speed
                       // Use a number up to 127

byte domeAutoSpeed = 60;  // Speed used when dome automation is active - Valid Values: 50 - 100
int time360DomeTurn =
    2500;  // milliseconds for dome to complete 360 turn at domeAutoSpeed - Valid Values: 2000 - 8000 (2000 = 2 seconds)

// ---------------------------------------------------------------------------------------
//                          Controller Deadzone Settings
// ---------------------------------------------------------------------------------------

byte joystickFootDeadZoneRange = 15;  // For controllers that centering problems, use the lowest number with no drift
byte joystickDomeDeadZoneRange = 10;  // For controllers that centering problems, use the lowest number with no drift
byte driveDeadBandRange = 10;         // Used to set the Sabertooth DeadZone for foot motors

// ---------------------------------------------------------------------------------------
//                          Communication Settings
// ---------------------------------------------------------------------------------------

int motorControllerBaudRate = 9600;  // Set the baud rate for the Syren motor controller
                                     // for packetized options are: 2400, 9600, 19200 and 38400

int marcDuinoBaudRate = 9600;  // Set the baud rate for the WCB on Serial1

unsigned int SerialLatency = 25;  // This is a delay factor in ms to prevent queueing of the Serial data.
// 25ms seems appropriate for HardwareSerial, values of 50ms or larger are needed for Software Emulation

// ---------------------------------------------------------------------------------------
//                          Button Mapping
// ---------------------------------------------------------------------------------------
// The built-in button mappings, and the list of hardcoded combos, are in
// src/default_mappings.cpp.
