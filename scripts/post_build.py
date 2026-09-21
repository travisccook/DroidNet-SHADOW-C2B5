#!/usr/bin/env python3
"""
PlatformIO post-build script to copy firmware files to build folder
Creates uniquely named firmware files with board name and timestamp
Supports both .hex files (AVR) and .bin files (ESP32)
For ESP32 boards, also copies bootloader, partition table, and boot_app0
"""

import os
import shutil
from datetime import datetime
Import("env")

def copy_firmware_to_build(source, target, env):
    """Copy the generated firmware file to the build folder with a unique name"""
    
    # Get the environment name (board name)
    env_name = env.get("PIOENV")
    
    # Get project directory
    project_dir = env.get("PROJECT_DIR")
    
    # Create build directory path
    build_dir = os.path.join(project_dir, "build")
    
    # Ensure build directory exists
    os.makedirs(build_dir, exist_ok=True)
    
    # Get the source firmware file path (target[0] is the firmware file)
    firmware_file = str(target[0])
    
    # Determine file extension based on the source file
    file_extension = os.path.splitext(firmware_file)[1]  # .hex or .bin
    
    # Create timestamp
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    
    # Create unique filename: boardname_YYYYMMDD_HHMMSS.ext
    new_filename = f"{env_name}_{timestamp}{file_extension}"
    dest_path = os.path.join(build_dir, new_filename)
    
    # Copy the firmware file
    if os.path.exists(firmware_file):
        shutil.copy2(firmware_file, dest_path)
        print(f"\nFirmware copied to: build/{new_filename}")
        
        # Also create a "latest" symlink for convenience
        latest_link = os.path.join(build_dir, f"{env_name}_latest{file_extension}")
        if os.path.exists(latest_link):
            os.remove(latest_link)
        # Use relative path for the symlink
        os.symlink(new_filename, latest_link)
        print(f"Latest symlink updated: build/{env_name}_latest{file_extension}")
        
        # For ESP32 boards, also copy bootloader, partitions, and boot_app0
        if file_extension == ".bin" and "esp32" in env.get("BOARD", "").lower():
            copy_esp32_components(env, build_dir, timestamp)
    else:
        print(f"\nWarning: Firmware file not found at {firmware_file}")

def create_merged_binary(env_name, esp32_dir, build_dir, timestamp):
    """Create a merged binary file containing all ESP32 components"""
    
    # Define the memory layout and files
    flash_layout = [
        {"offset": 0x1000, "file": f"{env_name}_bootloader_0x1000.bin"},
        {"offset": 0x8000, "file": f"{env_name}_partitions_0x8000.bin"},
        {"offset": 0xe000, "file": f"{env_name}_boot_app0_0xe000.bin"},
        {"offset": 0x10000, "file": f"{env_name}_app_0x10000.bin"}
    ]
    
    # Create merged binary with proper spacing
    merged_filename = f"{env_name}_{timestamp}_merged.bin"
    merged_path = os.path.join(build_dir, merged_filename)
    
    try:
        with open(merged_path, "wb") as merged:
            current_pos = 0
            
            for component in flash_layout:
                file_path = os.path.join(esp32_dir, component["file"])
                if not os.path.exists(file_path):
                    print(f"Warning: Missing component for merged binary: {component['file']}")
                    return
                
                # Write padding if needed
                if current_pos < component["offset"]:
                    padding_size = component["offset"] - current_pos
                    merged.write(b'\xff' * padding_size)
                    current_pos = component["offset"]
                
                # Write the component
                with open(file_path, "rb") as f:
                    data = f.read()
                    merged.write(data)
                    current_pos += len(data)
        
        # Create symlink to latest merged binary
        latest_merged = os.path.join(build_dir, f"{env_name}_latest_merged.bin")
        if os.path.exists(latest_merged):
            os.remove(latest_merged)
        os.symlink(merged_filename, latest_merged)
        
        print(f"\nMerged binary created: build/{merged_filename}")
        print(f"Latest merged binary: build/{env_name}_latest_merged.bin")
        print(f"Flash from address 0x0 for complete ESP32 firmware")
        
    except Exception as e:
        print(f"Error creating merged binary: {e}")

def copy_esp32_components(env, build_dir, timestamp):
    """Copy ESP32 bootloader, partition table, and boot_app0 files"""
    
    env_name = env.get("PIOENV")
    build_path = env.get("BUILD_DIR")
    
    # Define ESP32 component files and their user-friendly names
    # Look for bootloader and partitions in the PlatformIO build directory
    pio_build_dir = os.path.join(
        env.get("PROJECT_DIR"), ".pio", "build", env_name
    )
    
    esp32_components = [
        {
            "source": os.path.join(pio_build_dir, "bootloader.bin"),
            "name": "bootloader",
            "address": "0x1000",
            "required": True
        },
        {
            "source": os.path.join(pio_build_dir, "partitions.bin"),
            "name": "partitions",
            "address": "0x8000",
            "required": True
        },
        {
            # boot_app0.bin is in the ESP32 framework directory
            "source": os.path.join(
                os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32"),
                "tools/partitions/boot_app0.bin"
            ),
            "name": "boot_app0",
            "address": "0xe000",
            "required": True
        }
    ]
    
    # Create ESP32 subdirectory for this build
    esp32_dir = os.path.join(build_dir, f"{env_name}_{timestamp}_esp32_flash")
    os.makedirs(esp32_dir, exist_ok=True)
    
    # Copy each component with user-friendly naming
    copied_files = []
    for component in esp32_components:
        if os.path.exists(component["source"]):
            # Create filename: WCB_bootloader_0x1000.bin
            filename = f"{env_name}_{component['name']}_{component['address']}.bin"
            dest = os.path.join(esp32_dir, filename)
            shutil.copy2(component["source"], dest)
            copied_files.append(f"  - {filename} (flash at {component['address']})")
            
            # Also copy to latest directory
            latest_dir = os.path.join(build_dir, f"{env_name}_latest_esp32_flash")
            os.makedirs(latest_dir, exist_ok=True)
            latest_dest = os.path.join(latest_dir, filename)
            shutil.copy2(component["source"], latest_dest)
        elif component["required"]:
            print(f"Warning: Required ESP32 component not found: {component['source']}")
    
    # Copy the main firmware to the ESP32 directory as well
    main_firmware = os.path.join(build_dir, f"{env_name}_{timestamp}.bin")
    if os.path.exists(main_firmware):
        app_filename = f"{env_name}_app_0x10000.bin"
        app_dest = os.path.join(esp32_dir, app_filename)
        shutil.copy2(main_firmware, app_dest)
        copied_files.append(f"  - {app_filename} (flash at 0x10000)")
        
        # Also copy to latest directory
        latest_app_dest = os.path.join(latest_dir, app_filename)
        shutil.copy2(main_firmware, latest_app_dest)
    
    # Create merged binary file
    create_merged_binary(env_name, esp32_dir, build_dir, timestamp)
    
    # Create a flash instructions file
    instructions_path = os.path.join(esp32_dir, "FLASH_INSTRUCTIONS.txt")
    with open(instructions_path, "w") as f:
        f.write(f"ESP32 Flash Instructions for {env_name}\n")
        f.write(f"Built on: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write("=" * 50 + "\n\n")
        f.write("Option 1: Flash merged binary (EASIEST - single file):\n\n")
        f.write("esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 \\\n")
        f.write("  --before default_reset --after hard_reset write_flash -z \\\n")
        f.write("  --flash_mode dio --flash_freq 40m --flash_size detect \\\n")
        f.write(f"  0x0 {env_name}_{timestamp}_merged.bin\n\n")
        f.write("Option 2: Flash individual components:\n\n")
        f.write("esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 \\\n")
        f.write("  --before default_reset --after hard_reset write_flash -z \\\n")
        f.write("  --flash_mode dio --flash_freq 40m --flash_size detect \\\n")
        f.write(f"  0x1000 {env_name}_bootloader_0x1000.bin \\\n")
        f.write(f"  0x8000 {env_name}_partitions_0x8000.bin \\\n")
        f.write(f"  0xe000 {env_name}_boot_app0_0xe000.bin \\\n")
        f.write(f"  0x10000 {env_name}_app_0x10000.bin\n\n")
        f.write("Note: Adjust the port (/dev/ttyUSB0) to match your system.\n")
        f.write("On Windows use COM ports (e.g., COM3), on Mac use /dev/tty.usbserial-*\n")
    
    # Copy instructions to latest directory
    latest_instructions = os.path.join(latest_dir, "FLASH_INSTRUCTIONS.txt")
    shutil.copy2(instructions_path, latest_instructions)
    
    if copied_files:
        print(f"\nESP32 components copied to: build/{env_name}_{timestamp}_esp32_flash/")
        print("\n".join(copied_files))
        print(f"\nLatest ESP32 components in: build/{env_name}_latest_esp32_flash/")
        print(f"See FLASH_INSTRUCTIONS.txt for complete flashing guide")

# Register the post-build action for both hex and bin files
# AVR boards generate .hex files, ESP32 boards generate .bin files
env.AddPostAction("$BUILD_DIR/firmware.hex", copy_firmware_to_build)
env.AddPostAction("$BUILD_DIR/firmware.bin", copy_firmware_to_build)