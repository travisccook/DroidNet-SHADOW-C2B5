#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =======================================================================================
//                          SHADOW+MD+C2B5 Configuration File
// =======================================================================================
//
// This file contains all user-configurable settings for the droid controller.
// Modify these values to customize your droid's behavior.
//
// =======================================================================================

// ---------------------------------------------------------------------------------------
//                          Debug and Logging Settings
// ---------------------------------------------------------------------------------------

//#define SHADOW_DEBUG   // uncomment this for console DEBUG output
#define SHADOW_VERBOSE // uncomment this for console VERBOSE output

// Firmware version, reported to DroidNet (@SM HELLO) and in the WCB device announce.
#define SHADOW_FW_VERSION "2.0.0"

// Announce SHADOW to its WCB every 30 s ("@WDP1 {...}", WCB 6.2.1 and later), so the WCB mesh
// and DroidNet can see which port SHADOW is on. Comment out for WCB firmware older than 6.2.1,
// which would broadcast the announce line to every board instead of recognising it.
#define SHADOW_WDP_ANNOUNCE

// ---------------------------------------------------------------------------------------
//                          PS3 Controller MAC Addresses
// ---------------------------------------------------------------------------------------

extern String PS3ControllerFootMac;      // Primary FOOT PS3 controller MAC address
extern String PS3ControllerDomeMAC;      // Primary DOME PS3 controller MAC address (Optional)
extern String PS3ControllerBackupFootMac; // Backup FOOT controller MAC address (Optional)
extern String PS3ControllerBackupDomeMAC; // Backup DOME controller MAC address (Optional)

// ---------------------------------------------------------------------------------------
//                          Drive System Configuration
// ---------------------------------------------------------------------------------------

extern byte drivespeed1;               // Normal drive speed (0-127)
extern byte drivespeed2;               // Over throttle drive speed (0-127)
extern byte turnspeed;                 // Turn speed in place
extern byte ramping;                   // Speed ramping factor
extern int invertTurnDirection;        // Turn direction multiplier (-1 or 1)

// ---------------------------------------------------------------------------------------
//                          Dome Configuration
// ---------------------------------------------------------------------------------------

extern byte domespeed;                 // Maximum dome rotation speed (0-127)
extern byte domeAutoSpeed;            // Speed for dome automation (50-100)
extern int time360DomeTurn;           // Time in ms for full 360° dome turn (2000-8000)

// ---------------------------------------------------------------------------------------
//                          Controller Deadzone Settings
// ---------------------------------------------------------------------------------------

extern byte joystickFootDeadZoneRange; // Foot controller deadzone
extern byte joystickDomeDeadZoneRange; // Dome controller deadzone
extern byte driveDeadBandRange;        // Sabertooth motor deadband

// ---------------------------------------------------------------------------------------
//                          Communication Settings
// ---------------------------------------------------------------------------------------

extern int motorControllerBaudRate;    // Sabertooth/SyRen baud rate
extern int marcDuinoBaudRate;          // WCB (Serial1) baud rate
extern unsigned int SerialLatency;     // Serial communication delay (ms)

// ---------------------------------------------------------------------------------------
//                          Hardware Addresses
// ---------------------------------------------------------------------------------------

#define SYREN_ADDR 129                 // Serial Address for Dome SyRen
#define SABERTOOTH_ADDR 128            // Serial Address for Foot Sabertooth
#define ENABLE_UHS_DEBUGGING 1         // USB Host Shield debugging


// ---------------------------------------------------------------------------------------
//                          Button Mapping
// ---------------------------------------------------------------------------------------
//
// The built-in button mappings are in src/default_mappings.cpp. DroidNet can change them at
// runtime; see WIPDocs/COMMAND_MAPPING_DESIGN.md.
//
// ---------------------------------------------------------------------------------------

#endif // CONFIG_H 