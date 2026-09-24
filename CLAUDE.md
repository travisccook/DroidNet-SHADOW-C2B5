# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SHADOW-C2B5 is the main controller firmware for the C2-B5 astromech droid: a heavily modified SHADOW+MD running on an Arduino Mega ADK. It reads PS3 Move Navigation controllers over Bluetooth (USB Host Shield), drives the feet (Sabertooth) and dome (SyRen), and sends serial commands to the rest of the droid. It moved here from the `SHADOWC2B5/` folder of the C2B5 repo, which holds the other boards and the earlier history.

**CRITICAL SAFETY**: This code controls a physical robot. The main loop must NEVER be blocked as this could cause the robot to go out of control and cause harm. Real-time motor control and fault detection are safety-critical functions.

**FOLDER RESTRICTIONS**:

- Never make changes in the `Reference/` folder - it contains documentation only
- All generated .md files should be placed in the `WIPDocs/` folder

## Common Development Commands

```bash
pio run                                  # Build (or: ~/.platformio/penv/bin/pio run)
pio run -t upload                        # Upload to the Mega ADK
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor --baud 115200         # Serial monitor
pio run -t clean                         # Clean
pio check                                # clang-tidy
pio test -e native                       # Host unit tests (lib/WdpAnnounce)
```

The firmware environment is `SHADOWC2B5`, the default. Each build also copies the firmware to `build/` via
`scripts/post_build.py`, and `scripts/git_rev.py` defines `GIT_REV` (the short commit SHA, `-dirty` if modified).
`native` runs host unit tests only; the firmware env ignores all test suites, so `pio test` never uploads to the Mega.

### Updating Control Mapping Documentation

When button mappings change in `src/config.cpp`, update the Excel documentation:

```bash
cd WIPDocs/scripts
python3 update_control_mapping.py ../../Reference/ControlMapping.xlsx ../../src/config.cpp
```

The script updates existing rows, adds rows for new combinations, backs up the spreadsheet first, and keeps its formatting. Requires openpyxl (`pip3 install openpyxl`).

## Architecture

- `src/main.cpp`: controller setup, fault detection, drive/dome motor control, and the function-code `switch` that sends serial commands
- `src/config.cpp` / `include/config.h`: speeds, baud rates, and the table-driven button mappings (`footButtonMappings`, `domeButtonMappings`)
- `include/controller_macs.h`: the PS3 controller MAC addresses, which are the only check on who can connect and drive. It is gitignored; `include/controller_macs.example.h` is the committed template. Never commit real MAC addresses (or the Bluetooth dongle's address) anywhere in this repo.
- `lib/`: bundled USB Host Shield (PS3BT) and Sabertooth libraries. The SyRen dome controller is driven through the `Sabertooth` class in packetized serial mode. `main.cpp` includes Sabertooth by relative path, so `platformio.ini` sets `src_dir = .` and compiles `src/` plus `lib/Sabertooth` through `build_src_filter`.

### Serial Interfaces

- **Serial**: 115200 debug output
- **Serial1 (9600)**: commands to the WCB (Wireless Communication Board), which routes them to the HCR Vocalizer, holoprojectors, and other boards. Every 25-30 s it also carries a WDP Device Announce, `@WDP1 {"type":"Shadow","fw":"<GIT_REV>"}\r` (`lib/WdpAnnounce`, `wdpAnnounceTick()`), sent only when the TX buffer can take the whole line. WCB 1 labels the port "Shadow" in the mesh; `?WDP,DA` on WCB 1 shows `fw`.
- **Serial2 (9600)**: Sabertooth (feet) and SyRen (dome) packetized serial
- **Serial3**: unused

### Communication Protocols

- **WCB**: `;W1;C<name>` tells WCB 1 to run its stored command `<name>`
- **HCR Vocalizer**: `<...>` commands, routed through the WCB. Docs: https://docs.humancyborgrelations.com/astromech/platforms/embedded/commands
- **MarcDuino / JawaLite**: legacy astromech command formats used by the dome boards

## Critical Development Notes

1. **Safety Critical Code**: The main control loop must never be blocked. Motor control and fault detection require real-time responses.
2. **Serial Communication**: All inter-board communication uses 9600 baud. Higher rates have proven unreliable in the electrically noisy droid environment.
3. **Button Mapping**: To change controller behavior, edit the button tables in `src/config.cpp`, then update the control mapping spreadsheet.

## Development Best Practices

1. **Code Analysis**: Before implementing solutions, analyze 2-3 possible approaches with technical pros/cons. Choose the best approach and explain why.
2. **Error Handling**: After writing or modifying code, always check for potential errors. If debugging issues, request backtrace/console output for accurate diagnosis.
3. **C++ Standards**: Use modern C++ (C++17/20) best practices, STL, and follow ISO C++ guidelines for embedded development.
4. **Safety First**: Never compromise real-time performance or introduce blocking operations in control loops.
