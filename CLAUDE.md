# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DroidNet-SHADOW-C2B5 (formerly SHADOW-C2B5) is DroidNet's port of SHADOW: the main controller firmware for the C2-B5 astromech droid: a heavily modified SHADOW+MD running on an Arduino Mega ADK. It reads PS3 Move Navigation controllers over Bluetooth (USB Host Shield), drives the feet (Sabertooth) and dome (SyRen), and sends serial commands to the rest of the droid. It moved here from the `SHADOWC2B5/` folder of the C2B5 repo, which holds the other boards and the earlier history.

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
pio test -e native                       # Unit tests on the host (lib/ShadowMap + defaults)
pio test -e simavr --without-uploading   # Same tests on an emulated ATmega2560
python3 -m unittest tools/shadowmap/test_shadowmap.py   # Pi-side client vs. the firmware logic
```

`SHADOWC2B5` is the firmware environment (the default); `native` and `simavr` are test-only. Each firmware build also copies the firmware to `build/` via `scripts/post_build.py`. If a simavr test run hangs, that test program has probably run out of the Mega's 8 KB of SRAM (on AVR every Unity string literal lives in RAM): split the suite.

## Architecture

- `src/main.cpp`: controller setup, fault detection, drive/dome motor control; reads the controllers into `sm::PadState` and hands button mapping to `sm::Runtime`
- `src/config.cpp` / `include/config.h`: speeds, baud rates, `SHADOW_WDP_ANNOUNCE`. `SHADOW_FW_VERSION` is set by the build to the git commit (`scripts/git_rev.py`)
- `src/default_mappings.cpp`: the built-in button mappings (combo → WCB stored-command key) and the list of hardcoded combos
- `lib/ShadowMap/`: the runtime-editable button mapping (no Arduino dependency, so it builds and tests on the host): slot layout and lookup rule, EEPROM store, the `@SM` protocol DroidNet uses to edit mappings over the WCB link, the Serial1 TX queue, and `sm::Runtime` (what `loop()` does for the mapping). Design and protocol reference: `WIPDocs/COMMAND_MAPPING_DESIGN.md`
- `tools/shadowmap/`: reference Pi-side client (`shadowmap.py`, also a CLI), a host build of SHADOW's mapping side (`shadow_sim.cpp`), and their contract tests
- `test/`: Unity tests for `native` and `simavr`, including a replay of every button combination through the old table code (`test_legacy_equivalence`)
- `include/controller_macs.h`: the PS3 controller MAC addresses, which are the only check on who can connect and drive. It is gitignored; `include/controller_macs.example.h` is the committed template. Never commit real MAC addresses (or the Bluetooth dongle's address) anywhere in this repo.
- `lib/`: bundled USB Host Shield (PS3BT) and Sabertooth libraries. The SyRen dome controller is driven through the `Sabertooth` class in packetized serial mode. `main.cpp` includes Sabertooth by relative path, so `platformio.ini` sets `src_dir = .` and compiles `src/` plus `lib/Sabertooth` through `build_src_filter`.

### Serial Interfaces

- **Serial**: 115200 debug output; also accepts `@SM` mapping requests for bench testing (opening it resets the Mega)
- **Serial1 (9600)**: commands to the WCB (Wireless Communication Board), which routes them to the HCR Vocalizer, holoprojectors, and other boards. SHADOW also reads `@SM` mapping requests from DroidNet here and replies with `;S0@SM ...` lines, and announces itself every 25-30 s (`@WDP1 {...}`, `fw` = the git commit from `scripts/git_rev.py`; none while a controller fault is active). Everything SHADOW sends on Serial1 after `setup()` goes through `sm::TxQueue`; never `Serial1.print` directly in `loop()`, or a command can land inside a reply line. SHADOW is wired to **WCB 1 port S2** (both directions; its announce identified the port, and `@SM` round trips over it were verified on 2026-09-24)
- **Serial2 (9600)**: Sabertooth (feet) and SyRen (dome) packetized serial
- **Serial3**: unused

### Communication Protocols

- **WCB**: `;W1;C<name>` tells WCB 1 to run its stored command `<name>`. Every mapped button sends `<prefix><key><suffix>\r` (default prefix `;W1;C`)
- **@SM**: SHADOW's button-mapping protocol (see `WIPDocs/COMMAND_MAPPING_DESIGN.md`). Nothing SHADOW sends to the WCB may be a bare line (it gets broadcast to every board) or contain `^` or `;T`. The one bare line is the `@WDP1` device announce, which WCB 6.2.1+ captures; comment out `SHADOW_WDP_ANNOUNCE` for older WCB firmware
- **HCR Vocalizer**: `<...>` commands, routed through the WCB. Docs: https://docs.humancyborgrelations.com/astromech/platforms/embedded/commands
- **MarcDuino / JawaLite**: legacy astromech command formats used by the dome boards

## Critical Development Notes

1. **Safety Critical Code**: The main control loop must never be blocked. Motor control and fault detection require real-time responses.
2. **Serial Communication**: All inter-board communication uses 9600 baud. Higher rates have proven unreliable in the electrically noisy droid environment.
3. **Button Mapping**: DroidNet changes mappings at runtime (stored in EEPROM, which survives reboots and uploads). To change the built-in defaults, edit `src/default_mappings.cpp` and run the tests. DroidNet shows the live mapping; the old control-mapping spreadsheet is retired. EEPROM holds the mapping (1360 bytes from address 0); anything else that uses EEPROM must start after that.

## Development Best Practices

1. **Code Analysis**: Before implementing solutions, analyze 2-3 possible approaches with technical pros/cons. Choose the best approach and explain why.
2. **Error Handling**: After writing or modifying code, always check for potential errors. If debugging issues, request backtrace/console output for accurate diagnosis.
3. **C++ Standards**: Use modern C++ (C++17/20) best practices, STL, and follow ISO C++ guidelines for embedded development.
4. **Safety First**: Never compromise real-time performance or introduce blocking operations in control loops.
