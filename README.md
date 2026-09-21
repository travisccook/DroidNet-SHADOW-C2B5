# SHADOWC2B5 - Main Controller Board

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

- **Serial1 (9600 baud)**: MarcDuino-compatible commands to dome/panel boards
- **Serial2 (9600 baud)**: Motor controller communication (Sabertooth/SyRen)  
- **Serial3 (9600 baud)**: HCR Vocalizer commands

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

```bash
cd SHADOWC2B5
pio run                    # Build firmware
pio run -t upload         # Upload to Mega ADK
```

## Dependencies

- PS3BT library (included in project)
- Sabertooth library (included in project)
- USB Host Shield library (included in project)

## Button Mapping

See `src/config.cpp` for complete button mapping tables. The system uses a table-driven approach where each button combination maps to a specific function code that triggers actions across the droid's systems.

## License

This code builds upon the SHADOW+MD system with extensive modifications for C2-B5 specific requirements. 