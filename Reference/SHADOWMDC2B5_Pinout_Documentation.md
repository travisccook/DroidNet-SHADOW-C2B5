# SHADOW+MD+C2B5 Pinout Documentation

## Hardware Overview
**Target Platform:** Arduino Mega ADK Rev3  
**Author:** Travis Cook (travisccook) - Modified from original SHADOW+MD by vint43  
**Repository:** https://github.com/travisccook/C2B5  

---

## Serial Port Assignments

### Serial (Hardware UART0)
- **Pins:** TX0 (Pin 1), RX0 (Pin 0)
- **Purpose:** USB Debug Console
- **Baud Rate:** 115200
- **Usage:** Debug output, console messages, system status

### Serial1 (Hardware UART1) 
- **Pins:** TX1 (Pin 18), RX1 (Pin 19)
- **Purpose:** MarcDuino Dome Control Board
- **Baud Rate:** 9600 (configurable via `marcDuinoBaudRate`)
- **Usage:** 
  - PSI Pro lighting control
  - Holo projector control
  - Logic display control
  - Magic Panel control
  - FlthyHPs effects board

### Serial2 (Hardware UART2)
- **Pins:** TX2 (Pin 16), RX2 (Pin 17) 
- **Purpose:** Motor Controller Communication
- **Baud Rate:** 9600 (configurable via `motorControllerBaudRate`)
- **Connected Devices:**
  - Sabertooth 2x32/2x25 (Foot Drive) - Address 128
  - SyRen 10 (Dome Drive) - Address 129

### Serial3 (Hardware UART3)
- **Pins:** TX3 (Pin 14), RX3 (Pin 15)
- **Purpose:** HCR Vocalizer Communication  
- **Baud Rate:** 9600 (configurable via `marcDuinoBaudRate`)
- **Usage:** Audio/voice commands and control

---

## Motor Controller Configuration

### Sabertooth 2x32/2x25 (Foot Drive)
- **Serial Address:** 128 (`SABERTOOTH_ADDR`)
- **Connection:** Serial2 (Pins 16/17)
- **DIP Switch Settings:** 1 and 2 Down, All Others Up
- **Purpose:** Differential drive for foot motors
- **Control:** Mixed mode (drive + turn commands)

### SyRen 10 (Dome Drive)  
- **Serial Address:** 129 (`SYREN_ADDR`)
- **Connection:** Serial2 (Pins 16/17) - Shared with Sabertooth
- **DIP Switch Settings:** 1, 2, and 4 Down, All Others Up (Packetized Serial)
- **Purpose:** Single motor control for dome rotation
- **Note:** Simple Serial mode removed due to reliability issues

---

## USB Host Shield (PS3 Controllers)

### Hardware Requirements
- USB Host Shield compatible with Arduino Mega
- **Purpose:** PS3 Bluetooth controller interface
- **Controllers Supported:**
  - PS3 Move Navigation Controller (Primary - Foot control)
  - PS3 Move Navigation Controller (Secondary - Dome control, optional)

### Pin Connections (Standard USB Host Shield)
- **D10:** SS (Slave Select)
- **D9:** INT (Interrupt)  
- **D7:** GPX (General Purpose)
- **D6:** RST (Reset)
- **SPI Pins:** D50 (MISO), D51 (MOSI), D52 (SCK)
- **Power:** 5V, 3.3V, GND

---

## Analog Inputs

### A0 (Analog Pin 0)
- **Purpose:** Random seed for dome automation
- **Usage:** `randomSeed(analogRead(0))`

---

## Configuration Constants

### Hardware Addresses
```cpp
#define SYREN_ADDR 129                 // Serial Address for Dome SyRen
#define SABERTOOTH_ADDR 128            // Serial Address for Foot Sabertooth  
#define ENABLE_UHS_DEBUGGING 1         // USB Host Shield debugging
```

### Communication Settings
- **Motor Controller Baud:** 9600 (default)
- **MarcDuino Baud:** 9600 (default)
- **Serial Latency:** Configurable delay between commands
- **USB Task Frequency:** High frequency polling required

---

## PS3 Controller Configuration

### Controller 1 (Foot Control)
- **Purpose:** Primary drive control
- **Functions:**
  - Left analog stick: Forward/backward drive + turning
  - Directional buttons: MarcDuino function triggers
  - L1/L2: Safety stops and special functions
  - L3 + L1: Overspeed toggle
  - PS + CROSS: Disable drive
  - PS + CIRCLE: Enable drive

### Controller 2 (Dome Control - Optional)
- **Purpose:** Dome rotation and secondary functions  
- **Functions:**
  - Left analog stick X-axis: Dome rotation
  - Directional buttons: Dome-specific MarcDuino functions
  - L2: Access foot controller dome control mode

---

## Power Requirements

### Arduino Mega ADK
- **Power Input:** 7-12V DC (recommended 7-9V)
- **USB Power:** 5V via USB connector
- **Current:** ~500mA for controller + shields

### Motor Controllers
- **Sabertooth:** 6V-24V battery input (separate from Arduino)
- **SyRen:** 6V-24V battery input (separate from Arduino)
- **Important:** Motor controllers should have separate battery power

### Accessories
- **USB Host Shield:** Powered from Arduino 5V/3.3V
- **MarcDuino Boards:** Typically 5V from Arduino or separate supply
- **HCR Vocalizer:** Check device specifications

---

## Safety Notes

1. **Motor Controller Isolation:** Keep motor battery power separate from Arduino logic power
2. **Fault Detection:** Built-in PS3 controller timeout safety stops
3. **Deadband Settings:** Configured for each motor controller to prevent drift
4. **Emergency Stops:** Multiple redundant stop conditions implemented
5. **Ramping:** Gradual acceleration/deceleration to prevent mechanical stress

---

## Wiring Recommendations

### Power Distribution
```
Main Battery --> Motor Controllers (Sabertooth + SyRen)
Separate 9V --> Arduino Mega ADK
Arduino 5V --> USB Host Shield, MarcDuino boards
```

### Serial Connections  
```
Arduino TX1 (18) --> MarcDuino RX
Arduino RX1 (19) --> MarcDuino TX
Arduino TX2 (16) --> Motor Controller Chain (Sabertooth + SyRen)
Arduino TX3 (14) --> HCR Vocalizer RX
```

### USB Host Shield
```
Standard SPI connections + power
USB connector for Bluetooth dongle (if required)
```

---

## Configuration Files

### config.h/config.cpp
- MAC addresses for authorized PS3 controllers
- Speed and timing parameters  
- Button mapping configurations
- Baud rate settings
- Hardware-specific tuning values

### Button Mappings
- Table-driven approach for easy customization
- Separate mappings for foot and dome controllers
- Support for button combinations with modifiers

---

## Troubleshooting

### Common Issues
1. **PS3 Controller Connection:** Verify MAC addresses in config
2. **Motor Response:** Check serial connections and addresses
3. **Audio Issues:** Verify Serial3 connections to HCR Vocalizer
4. **USB Host Problems:** Ensure adequate power supply

### Debug Features
- Serial console output with SHADOW_DEBUG
- Verbose logging with SHADOW_VERBOSE  
- Controller status monitoring
- Motor command echo capabilities

---

*This documentation is based on the SHADOW+MD+C2B5 codebase analysis. Verify all connections with your specific hardware before powering up.* 