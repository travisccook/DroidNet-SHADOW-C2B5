#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <PS3BT.h>  // Required for ButtonEnum type

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
//                          Audio/Visual Settings
// ---------------------------------------------------------------------------------------

extern int brightnessInterval;         // Brightness change per 'click'
extern int brightness;                 // Starting brightness level
extern int volumeInterval;            // Volume change per 'click'
extern int volume;                     // Starting volume level

// ---------------------------------------------------------------------------------------
//                          Communication Settings
// ---------------------------------------------------------------------------------------

extern int motorControllerBaudRate;    // Sabertooth/SyRen baud rate
extern int marcDuinoBaudRate;          // MarcDuino board baud rate
extern unsigned int SerialLatency;     // Serial communication delay (ms)

// ---------------------------------------------------------------------------------------
//                          Hardware Addresses
// ---------------------------------------------------------------------------------------

#define SYREN_ADDR 129                 // Serial Address for Dome SyRen
#define SABERTOOTH_ADDR 128            // Serial Address for Foot Sabertooth
#define ENABLE_UHS_DEBUGGING 1         // USB Host Shield debugging


// ---------------------------------------------------------------------------------------
//                          MarcDuino Function Code Documentation
// ---------------------------------------------------------------------------------------
//
// Standard MarcDuino Function Codes:
//     2 = Scream - all panels open
//     7 = Faint / Short Circuit
//     9 = Leia message
//    10 = Disco
//    26 = Volume Up
//    27 = Volume Down
//    28 = Volume Max
//    29 = Volume Mid
//    76 = Magic Panel ON
//    77 = Magic Panel OFF
//    78 = Magic Panel Flicker (10 seconds)
//
// Custom C2-B5 Function Codes:
//    79 = Reset - Silent
//    80 = Reset - Chatty
//    81 = Yes
//    82 = No
//    83 = Imperial March
//    84 = Random Happy Sound
//    85 = Random Holo Twitch
//    86 = TEMP Open Body Servos (REMOVED)
//    87 = TEMP Close Body Servos (REMOVED)
//    88 = Front Holo Up
//    89 = Front Holo Left
//    90 = Front Holo Right
//    91 = Front Holo Down
//    92 = Front Holo Center
//    93 = Random Scared Sound
//    94 = Random Sad Sound
//    95 = Raspberry
//    96 = In Love
//    97 = Jedi Theme
//    98 = Brightness Down
//    99 = Brightness Mid
//    100 = Brightness Up
//    101 = Brightness Max
//    102 = Toggle Body Door (REMOVED)
//    103 = Toggle Utility Arms (REMOVED)
//    104 = Scanning
//    105 = Alarm
//    106 = Random Angry Sound
//    107 = Cantina
//    108 = Extreme Sad Sound
//
// ---------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------
//                          Button Mapping System
// ---------------------------------------------------------------------------------------

// Button mapping structure using PS3 library's ButtonEnum directly
struct SimpleButtonMapping {
    ButtonEnum primary;
    ButtonEnum modifier; // Use NO_MODIFIER for buttons without modifiers
    int functionCode;    // MarcDuino function code
    const char* description;  // Human-readable description of the function
};

// Special constant for "no modifier" - using an unused ButtonEnum value
constexpr ButtonEnum NO_MODIFIER = static_cast<ButtonEnum>(255);

// External declarations for button mapping arrays (defined in config.cpp)
extern const SimpleButtonMapping footButtonMappings[];
extern const SimpleButtonMapping domeButtonMappings[];
extern const size_t footButtonMappingsCount;
extern const size_t domeButtonMappingsCount;

// ---------------------------------------------------------------------------------------

#endif // CONFIG_H 