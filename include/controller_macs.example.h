#ifndef CONTROLLER_MACS_H
#define CONTROLLER_MACS_H

// =======================================================================================
//                          PS3 Controller MAC Addresses (template)
// =======================================================================================
//
// Copy this file to include/controller_macs.h and replace the placeholders with your
// controllers' Bluetooth addresses (uppercase, two digits a byte, colon-separated, as in
// "00:1A:2B:3C:4D:05"). controller_macs.h is
// gitignored so your addresses stay out of the repository.
//
// A controller may connect only if its address matches one of these. Any other
// controller is dropped and the motors are stopped. Leave an unused slot as the
// placeholder: it never matches a real address.
//
// To find an address, uncomment SHADOW_DEBUG in include/config.h; the serial monitor
// then prints the address of each controller that connects.
//
// =======================================================================================

#define PS3_FOOT_MAC "XX:XX:XX:XX:XX:XX"         // FOOT controller
#define PS3_DOME_MAC "XX:XX:XX:XX:XX:XX"         // DOME controller (optional)
#define PS3_BACKUP_FOOT_MAC "XX:XX:XX:XX:XX:XX"  // Backup FOOT controller (optional)
#define PS3_BACKUP_DOME_MAC "XX:XX:XX:XX:XX:XX"  // Backup DOME controller (optional)

#endif
