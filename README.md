# SHADOW-C2B5 - Main Controller Board

The primary control board for C2-B5, handling PS3 controller input, motor control, and inter-board communication.

## Hardware

- **Platform**: Arduino Mega ADK
- **Controller**: PS3 Move Navigation controllers (foot and dome)
- **Motor Controllers**:
  - Sabertooth 2x32/2x25 (foot drive)
  - SyRen 10 (dome rotation)

## Features

- **Table-driven button mapping**: Simple configuration of PS3 button combinations to droid functions
- **Dual PS3 controller support**: Separate controllers for foot and dome control
- **Motor ramping**: Smooth acceleration/deceleration for safe driving
- **Fault detection**: Automatic motor shutdown on controller disconnect or communication loss
- **Serial communication**: Commands sent to other boards via serial interfaces

## Serial Interfaces

- **Serial1 (9600 baud)**: Commands to the WCB (Wireless Communication Board), which routes them to the HCR Vocalizer, holoprojectors, and the other boards
- **Serial2 (9600 baud)**: Motor controller communication (Sabertooth/SyRen)
- **Serial3**: Unused (HCR Vocalizer commands now go through the WCB on Serial1)

## Configuration

Button mappings and system settings are configured in `include/config.h` and `src/config.cpp`.

### Key Configuration Areas

- **PS3 Controller MAC addresses**: Set your specific controller addresses
- **Drive speeds**: Normal and overspeed settings
- **Button mappings**: Table-driven approach for easy customization
- **Audio/visual settings**: Volume and brightness controls

## Safety Features

- **Critical fault detection**: Monitors PS3 communication and stops motors on timeout
- **Drive stick disable**: Emergency disable via PS3+Cross button combination
- **Motor ramping**: Prevents sudden acceleration that could damage drivetrain
- **Deadzone handling**: Eliminates controller drift issues

## Build Instructions

This is a PlatformIO project; run these from the repository root.

```bash
pio run                                  # Build firmware
pio run -t upload                        # Upload to Mega ADK
pio device monitor --baud 115200         # Serial monitor
pio check                                # clang-tidy lint
```

Each build also copies the firmware to `build/SHADOWC2B5_<timestamp>.hex` and updates `build/SHADOWC2B5_latest.hex` (see `scripts/post_build.py`).

## Dependencies

- USB Host Shield 2.0 library, including PS3BT (`lib/USB_Host_Shield`, included in project)
- Sabertooth library (`lib/Sabertooth`, included in project)
- SyRenSimplified library (`lib/SyRenSimplified`, included in project)

## Button Mapping

See `src/config.cpp` for complete button mapping tables. The system uses a table-driven approach where each button combination maps to a specific function code that triggers actions across the droid's systems.

The printable mapping lives in `Reference/ControlMapping.xlsx` (and `.pdf`). After changing the tables in `src/config.cpp`, update the spreadsheet with:

```bash
cd WIPDocs/scripts
python3 update_control_mapping.py ../../Reference/ControlMapping.xlsx ../../src/config.cpp
```

Requires openpyxl (`pip3 install openpyxl`). The script backs up the spreadsheet before changing it.

## Repository Layout

- `src/`, `include/`: firmware sources and configuration
- `lib/`: bundled libraries (USB Host Shield, Sabertooth, SyRenSimplified)
- `Reference/`: original SHADOW+MD sketches, pinout, controller diagrams, control mapping (reference only)
- `WIPDocs/`: working notes and helper scripts
- `scripts/post_build.py`: PlatformIO post-build step that copies firmware into `build/`

## Provenance

This firmware moved here from the `SHADOWC2B5/` folder of the private [C2B5](https://github.com/travisccook/C2B5) repo at commit `af0605ce` (2026-09-21), as a snapshot of the git-tracked files. Its history before that point (back to 2021) stays in C2B5, where the folder is left as-is. New work happens here.

## License

This code builds upon the SHADOW+MD system with extensive modifications for C2-B5 specific requirements.
