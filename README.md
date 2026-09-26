# DroidNet-SHADOW-C2B5 - Main Controller Board

> **DroidNet's port of SHADOW.** This is the SHADOW firmware that DroidNet Signal Booster's SHADOW tab talks to. The runtime button map (the `@SM` protocol, edited live from DroidNet) exists only in this port: other SHADOW variants, including KnightShade's original SHADOW and SHADOW+MD, can't be remapped from DroidNet.

The primary control board for C2-B5, handling PS3 controller input, motor control, and inter-board communication.

## Hardware

- **Platform**: Arduino Mega ADK
- **Controller**: PS3 Move Navigation controllers (foot and dome)
- **Motor Controllers**:
  - Sabertooth 2x32/2x25 (foot drive)
  - SyRen 10 (dome rotation)

## Features

- **Runtime-editable button mapping**: each PS3 button combination sends a WCB stored command; the built-in defaults are in `src/default_mappings.cpp`, and DroidNet can change any combination while the droid runs (kept in EEPROM, controllers stay connected)
- **Dual PS3 controller support**: Separate controllers for foot and dome control
- **Motor ramping**: Smooth acceleration/deceleration for safe driving
- **Fault detection**: Automatic motor shutdown on controller disconnect or communication loss
- **Controller battery levels**: each controller's battery (`OK`/`LOW`/`CRIT`) is reported to DroidNet over the WCB link when it changes and every 30 s (`@SM BAT`, see `WIPDocs/COMMAND_MAPPING_DESIGN.md`)
- **Serial communication**: Commands sent to other boards via serial interfaces

## Serial Interfaces

- **Serial1 (9600 baud)**: Commands to the WCB (Wireless Communication Board), which routes them to the HCR Vocalizer, holoprojectors, and the other boards; also carries DroidNet's button-mapping requests (`@SM` protocol)
- **Serial2 (9600 baud)**: Motor controller communication (Sabertooth/SyRen)
- **Serial3**: Unused (HCR Vocalizer commands now go through the WCB on Serial1)

## Configuration

System settings are configured in `include/config.h` and `src/config.cpp`. The built-in button mappings are in `src/default_mappings.cpp`; DroidNet can change them at runtime (see `WIPDocs/COMMAND_MAPPING_DESIGN.md`).

### Key Configuration Areas

- **PS3 Controller MAC addresses**: Set in `include/controller_macs.h` (see Build Instructions)
- **Drive speeds**: Normal and overspeed settings
- **Button mappings**: built-in defaults in `src/default_mappings.cpp`; runtime changes from DroidNet are stored in EEPROM
- **Volume and brightness**: now WCB stored commands like any other mapping (SHADOW keeps no state for them)

## Safety Features

- **Critical fault detection**: Monitors PS3 communication and stops motors on timeout
- **Drive stick disable**: Emergency disable via PS3+Cross button combination
- **Motor ramping**: Prevents sudden acceleration that could damage drivetrain
- **Deadzone handling**: Eliminates controller drift issues

## Build Instructions

This is a PlatformIO project; run these from the repository root.

First-time setup: your PS3 controller MAC addresses live in `include/controller_macs.h`, which is gitignored so they never land in the repository. Create it from the template and fill in your addresses; the build stops with an error until it exists.

```bash
cp include/controller_macs.example.h include/controller_macs.h
```

```bash
pio run                                  # Build firmware
pio run -t upload                        # Upload to Mega ADK (keeps the EEPROM mapping)
pio device monitor --baud 115200         # Serial monitor
pio check                                # clang-tidy lint
pio test -e native                       # Unit tests on the host
pio test -e simavr --without-uploading   # Unit tests on an emulated ATmega2560
python3 -m unittest tools/shadowmap/test_shadowmap.py   # Pi-side client contract tests
```

For bench testing, the mapping protocol also works on the USB port, e.g. type `@SM DUMP` in the serial monitor, or use `tools/shadowmap/shadowmap.py --usb /dev/ttyACM0 dump`. Opening the USB port resets the Mega; DroidNet uses the WCB link instead.

Each build also copies the firmware to `build/SHADOWC2B5_<timestamp>.hex` and updates `build/SHADOWC2B5_latest.hex` (see `scripts/post_build.py`).

## Dependencies

- USB Host Shield 2.0 library, including PS3BT (`lib/USB_Host_Shield`, included in project)
- Sabertooth library (`lib/Sabertooth`, included in project); also drives the SyRen dome controller in packetized serial mode

## Button Mapping

Each button combination sends a WCB stored command. The built-in defaults are in `src/default_mappings.cpp`; DroidNet changes them at runtime over the WCB link, and the changes are kept in EEPROM (see `WIPDocs/COMMAND_MAPPING_DESIGN.md`).

DroidNet shows the live mapping. The old printable spreadsheet (`Reference/ControlMapping.xlsx`) is retired and no longer kept up to date.

## Repository Layout

- `src/`, `include/`: firmware sources and configuration
- `lib/`: bundled libraries (USB Host Shield, Sabertooth)
- `Reference/`: original SHADOW+MD sketches, pinout, controller diagrams, control mapping (reference only)
- `WIPDocs/`: working notes and helper scripts
- `scripts/post_build.py`: PlatformIO post-build step that copies firmware into `build/`

## Provenance

This firmware moved here from the `SHADOWC2B5/` folder of the private [C2B5](https://github.com/travisccook/C2B5) repo at commit `af0605ce` (2026-09-21), as a snapshot of the git-tracked files. Its history before that point (back to 2021) stays in C2B5, where the folder is left as-is. New work happens here.

## License

This code builds upon the SHADOW+MD system with extensive modifications for C2-B5 specific requirements.
