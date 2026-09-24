#include "../include/default_mappings.h"

// =======================================================================================
//                          Built-in Button Mappings
// =======================================================================================
//
// Each entry maps a controller button combo to a WCB stored command. SHADOW sends
// <prefix><key><suffix>\r to the WCB, which is ";W1;C<key>\r" by default: "run stored
// command <key> on WCB 1". Keys are at most 15 characters, the WCB's limit.
//
// These are only the defaults. DroidNet can change any slot at runtime; the change is kept
// in EEPROM and survives reboots and firmware uploads. Anything not listed here is unmapped.
//
//   FOOT(<modifier>, <button>) / DOME(<modifier>, <button>)
//     modifier: NONE L1 L2 PS CIRCLE CROSS
//     button:   UP RIGHT DOWN LEFT L3 PS
//
// When more than one modifier is held, the first of L1, L2, PS, CIRCLE, CROSS wins, then no
// modifier. An unmapped combo in that layer does nothing; it doesn't fall back to another
// layer. CROSS, CIRCLE and PS also count when held on the other controller.
//
// Reserved (hardcoded in main.cpp, so they can't be mapped):
//   PS + CROSS / PS + CIRCLE  - disable / enable the drive stick (either controller)
//   PS alone, PS + PS         - PS is only a button here with L1 or L2 held
//   FOOT L3 + L1              - toggle over-speed
//   L2 + CROSS / L2 + CIRCLE  - dome automation off / on (CROSS/CIRCLE aren't buttons here)
//   L2 + stick                - dome rotation (only when no dome controller is connected)
//   L1 or L2 held             - stops the foot motors
// =======================================================================================

#define FOOT(mod, btn) sm::slotIndex(sm::Ctrl::F, sm::Mod::mod, sm::Btn::btn)
#define DOME(mod, btn) sm::slotIndex(sm::Ctrl::D, sm::Mod::mod, sm::Btn::btn)

const sm::DefaultMapping defaultButtonMappings[] PROGMEM = {
    // ---- FOOT controller ----

    // L1 + directional buttons - Emphasis Sounds
    // (extremehappysou / extremeangrysou are cut to the WCB's 15-character key limit)
    {FOOT(L1, UP), "extremehappysou"},     // Extreme Happy
    {FOOT(L1, LEFT), "scream"},            // Extreme Scared (Scream)
    {FOOT(L1, RIGHT), "extremesadsound"},  // Extreme Sad
    {FOOT(L1, DOWN), "extremeangrysou"},   // Extreme Angry

    // L2 + directional buttons - Music
    {FOOT(L2, UP), "imperialmarch"},  // Imperial March
    {FOOT(L2, LEFT), "cantina"},      // Cantina
    {FOOT(L2, RIGHT), "disco"},       // Disco
    {FOOT(L2, DOWN), "jeditheme"},    // Jedi Theme
    {FOOT(L2, PS), "mahna"},          // MahnaMahna

    // PS + directional buttons - was brightness; map WCB stored commands from DroidNet

    // CIRCLE + directional buttons
    {FOOT(CIRCLE, UP), "yes"},     // Yes
    {FOOT(CIRCLE, LEFT), "alarm"},  // Alarm
    {FOOT(CIRCLE, RIGHT), "love"},  // In Love
    {FOOT(CIRCLE, DOWN), "no"},     // No

    // CROSS + directional buttons
    {FOOT(CROSS, UP), "leia"},            // Leia Message
    {FOOT(CROSS, RIGHT), "shortcircuit"},  // Short Circuit

    // Directional buttons, no modifier
    {FOOT(NONE, UP), "happysound"},    // Random Happy
    {FOOT(NONE, LEFT), "scaredsound"},  // Random Scared
    {FOOT(NONE, RIGHT), "sadsound"},    // Random Sad
    {FOOT(NONE, DOWN), "angrysound"},   // Random Angry

    // ---- DOME controller ----

    // L1 + directional buttons - Holo Movement
    {DOME(L1, UP), "frontholoup"},        // Front Holo Up
    {DOME(L1, LEFT), "fronthololeft"},    // Front Holo Left
    {DOME(L1, RIGHT), "frontholoright"},  // Front Holo Right
    {DOME(L1, DOWN), "frontholodown"},    // Front Holo Down
    {DOME(L1, L3), "frontholocenter"},    // Front Holo Center

    // L2 + directional buttons
    {DOME(L2, UP), "GDdownload"},  // GDDownload
    {DOME(L2, LEFT), "GDcollar"},  // Collar
    {DOME(L2, RIGHT), "GDsuccess"},  // Success
    {DOME(L2, DOWN), "redlight"},  // Red Light

    // PS + directional buttons - was volume; map WCB stored commands from DroidNet

    // CROSS + directional buttons
    {DOME(CROSS, LEFT), "scanning"},        // Scanning
    {DOME(CROSS, RIGHT), "GDc2song"},       // C2 Song
    {DOME(CROSS, DOWN), "GDrecruitsong"},   // Empire Needs You

    // Directional buttons, no modifier
    {DOME(NONE, UP), "singlemuse"},    // Random Muse
    {DOME(NONE, LEFT), "resetchatty"},  // Reset Chatty
    {DOME(NONE, RIGHT), "holotwitch"},  // Random Holo Twitch
    {DOME(NONE, DOWN), "resetsilent"},  // Reset Silent
};

#undef FOOT
#undef DOME

template <typename T, size_t N>
constexpr size_t countOf(const T (&array)[N]) {
    return static_cast<void>(array), N;  // C++11 constexpr: a single return statement
}

const uint8_t defaultButtonMappingsCount = countOf(defaultButtonMappings);
