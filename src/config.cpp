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

String PS3ControllerFootMac = "XX:XX:XX:XX:XX:XX";  // Set this to your FOOT PS3 controller MAC address
String PS3ControllerDomeMAC = "XX:XX:XX:XX:XX:XX";  // Set to a secondary DOME PS3 controller MAC address (Optional)
String PS3ControllerBackupFootMac = "XX:XX:XX:XX:XX:XX";           // Set to the MAC Address of your BACKUP FOOT controller (Optional)
String PS3ControllerBackupDomeMAC = "XX:XX:XX:XX:XX:XX";           // Set to the MAC Address of your BACKUP DOME controller (Optional)

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
//                          Audio/Visual Settings
// ---------------------------------------------------------------------------------------

int brightnessInterval = 20;  // How much does one 'click' of brightness change?
int brightness = 150;         // Starting brightness level
int volumeInterval = 10;      // How much does one 'click' of volume change?
int volume = 100;             // Starting Volume Level

// ---------------------------------------------------------------------------------------
//                          Communication Settings
// ---------------------------------------------------------------------------------------

int motorControllerBaudRate = 9600;  // Set the baud rate for the Syren motor controller
                                     // for packetized options are: 2400, 9600, 19200 and 38400

int marcDuinoBaudRate = 9600;  // Set the baud rate for the MarcDuino boards

unsigned int SerialLatency = 25;  // This is a delay factor in ms to prevent queueing of the Serial data.
// 25ms seems appropriate for HardwareSerial, values of 50ms or larger are needed for Software Emulation


// ---------------------------------------------------------------------------------------
//                          Button Mapping Configuration
// ---------------------------------------------------------------------------------------

// SPECIAL HARDCODED BUTTON COMBINATIONS (NOT IN MAPPING TABLES)
// These combinations are handled directly in main.cpp and should NOT be
// duplicated in the mapping tables below:
//
// FOOT Controller Special Combos:
//   PS + CROSS        - Disable drive stick
//   PS + CIRCLE       - Enable drive stick
//   L3 + L1           - Toggle overspeed (when stick enabled)
//   L2 + CROSS        - Disable dome automation
//   L2 + CIRCLE       - Enable dome automation
//   L2 + joystick     - Control dome (ONLY when dome controller NOT connected)
//   L2 or L1 (held)   - Stop foot motors (safety: prevents driving while using these buttons)
//
// DOME Controller Special Combos:
//   (Currently none - all dome buttons are in the mapping table)
//
// ---------------------------------------------------------------------------------------

// FOOT controller button mappings using PS3 library constants directly
// NOTE: Modifier combinations MUST be listed before single buttons
// to prevent single button matches from overriding combinations
const SimpleButtonMapping footButtonMappings[] = {
    // L1 + directional buttons - Emphasis Sounds
    {UP, L1, 109, "Extreme Happy"},
    {LEFT, L1, 2, "Extreme Scared (Scream)"},
    {RIGHT, L1, 108, "Exteme Sad"},
    {DOWN, L1, 111, "Extreme Angry"},
    // NOTE: L3 + L1 = Toggle Drive Power (hardcoded in main.cpp)
    {PS, L1, 0, "UNUSED"},


    // L2 + directional buttons - Music
    {UP, L2, 83, "Imperial March"},
    {LEFT, L2, 107, "Cantina"},
    {RIGHT, L2, 10, "Disco"},
    {DOWN, L2, 97, "Jedi Theme"},
    {PS, L2, 117, "MahnaMahna"},
    {L3, L2, 0, "UNUSED"},

    // PS + directional buttons - Brightness Control
    {UP, PS, 100, "Brightness Up"},
    {LEFT, PS, 99, "Brightness Mid"},
    {RIGHT, PS, 101, "Brightness Max"},
    {DOWN, PS, 98, "Brightness Down"},

    // CIRCLE + directional buttons
    {UP, CIRCLE, 81, "Yes"},
    {LEFT, CIRCLE, 105, "Alarm"},
    {RIGHT, CIRCLE, 96, "In Love"},
    {DOWN, CIRCLE, 82, "No"},

    // CROSS + directional buttons
    {UP, CROSS, 9, "Leia Message"},
    {LEFT, CROSS, 0, "Raspberry TBD"},
    {RIGHT, CROSS, 7, "Short Circuit"},
    {DOWN, CROSS, 0, "UNUSED"},

    // Base directional buttons (no modifier) - check LAST
    {UP, NO_MODIFIER, 84, "Random Happy"},
    {LEFT, NO_MODIFIER, 93, "Random Scared"},
    {RIGHT, NO_MODIFIER, 94, "Random Sad"},
    {DOWN, NO_MODIFIER, 106, "Random Angry"}};

// DOME controller button mappings
// NOTE: Modifier combinations MUST be listed before single buttons
// to prevent single button matches from overriding combinations
const SimpleButtonMapping domeButtonMappings[] = {
    // L1 + directional buttons - Holo Movement
    {UP, L1, 88, "Front Holo Up"},
    {LEFT, L1, 89, "Front Holo Left"},
    {RIGHT, L1, 90, "Front Holo Right"},
    {DOWN, L1, 91, "Front Holo Down"},
    {L3, L1, 92, "Front Holo Center"},
    {PS, L1, 0, "UNUSED"},

    // L2 + directional buttons
    {UP, L2, 113, "GDDownload"},
    {LEFT, L2, 112, "Collar"},
    {RIGHT, L2, 114, "Success"},
    {DOWN, L2, 118, "Red Light"},
    {PS, L2, 0, "UNUSED"},
    {L3, L2, 0, "UNUSED"},

    // PS + directional buttons - Volume Control
    {UP, PS, 26, "Volume Up"},
    {LEFT, PS, 29, "Volume Mid"},
    {RIGHT, PS, 28, "Volume Max"},
    {DOWN, PS, 27, "Volume Down"},

    // CIRCLE + directional buttons
    {UP, CIRCLE, 0, "UNUSED"},
    {LEFT, CIRCLE, 0, "UNUSED"},
    {RIGHT, CIRCLE, 0, "UNUSED"},
    {DOWN, CIRCLE, 0, "UNUSED"},

    // CROSS + directional buttons
    {UP, CROSS, 0, "UNUSED"},
    {LEFT, CROSS, 104, "Scanning"},
    {RIGHT, CROSS, 115, "C2 Song"},
    {DOWN, CROSS, 116, "Empire Needs you"},

    // Base directional buttons (no modifier) - check LAST
    {UP, NO_MODIFIER, 110, "Random Muse"},
    {LEFT, NO_MODIFIER, 80, "Reset Chatty"},
    {RIGHT, NO_MODIFIER, 85, "Random Holo Twitch"},
    {DOWN, NO_MODIFIER, 79, "Reset Silent"}};

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

// Array size constants for easy iteration
const size_t footButtonMappingsCount = sizeof(footButtonMappings) / sizeof(footButtonMappings[0]);
const size_t domeButtonMappingsCount = sizeof(domeButtonMappings) / sizeof(domeButtonMappings[0]);