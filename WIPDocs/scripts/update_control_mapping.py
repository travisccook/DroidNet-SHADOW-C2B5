#!/usr/bin/env python3
"""
Update Control Mapping Excel File from C2B5 config.cpp
This script updates the ControlMapping.xlsx file with current button mappings from config.cpp
It also adds new rows for mappings not present in the Excel template

Usage:
    python3 update_control_mapping.py <excel_file> <config_cpp_file>
    
Example:
    python3 update_control_mapping.py ../../Reference/ControlMapping.xlsx ../../SHADOWC2B5/src/config.cpp
"""

import sys
import re
import shutil
from datetime import datetime
from pathlib import Path

try:
    from openpyxl import load_workbook
    from openpyxl.styles import Alignment, Font
except ImportError:
    print("Error: openpyxl is not installed.")
    print("Please install it with: pip3 install openpyxl")
    sys.exit(1)


def parse_config_cpp(filepath):
    """Parse the config.cpp file and extract button mappings"""
    foot_mappings = {}
    dome_mappings = {}
    
    with open(filepath, 'r') as f:
        content = f.read()
    
    # Parse foot button mappings
    foot_match = re.search(r'const SimpleButtonMapping footButtonMappings\[\] = \{(.*?)\};', 
                          content, re.DOTALL)
    if foot_match:
        foot_section = foot_match.group(1)
        mapping_pattern = r'\{\s*(\w+),\s*(\w+),\s*(\d+),\s*"([^"]+)"\s*\}'
        for match in re.finditer(mapping_pattern, foot_section):
            button, modifier, code, desc = match.groups()
            # Create the combo key
            if modifier == 'NO_MODIFIER':
                combo = button
            elif modifier == 'CIRCLE':
                combo = f"O + {button}"
            elif modifier == 'CROSS':
                combo = f"X + {button}"
            else:
                combo = f"{modifier} + {button}"
            foot_mappings[combo] = (int(code), desc)
    
    # Parse dome button mappings  
    dome_match = re.search(r'const SimpleButtonMapping domeButtonMappings\[\] = \{(.*?)\};', 
                          content, re.DOTALL)
    if dome_match:
        dome_section = dome_match.group(1)
        mapping_pattern = r'\{\s*(\w+),\s*(\w+),\s*(\d+),\s*"([^"]+)"\s*\}'
        for match in re.finditer(mapping_pattern, dome_section):
            button, modifier, code, desc = match.groups()
            # Create the combo key
            if modifier == 'NO_MODIFIER':
                combo = button
            elif modifier == 'CIRCLE':
                combo = f"O + {button}"
            elif modifier == 'CROSS':
                combo = f"X + {button}"
            else:
                combo = f"{modifier} + {button}"
            dome_mappings[combo] = (int(code), desc)
    
    return foot_mappings, dome_mappings


def backup_file(filepath):
    """Create a backup of the Excel file"""
    backup_path = filepath.parent / f"{filepath.stem}_backup_{datetime.now().strftime('%Y%m%d_%H%M%S')}{filepath.suffix}"
    shutil.copy2(filepath, backup_path)
    print(f"Created backup: {backup_path}")
    return backup_path


def update_excel(excel_path, foot_mappings, dome_mappings):
    """Update the Excel file with new mappings"""
    # Create backup first
    backup_path = backup_file(excel_path)
    
    try:
        # Load workbook
        wb = load_workbook(excel_path)
        ws = wb.active
        
        print("\nProcessing Excel file...")
        
        # First pass: Update existing mappings
        existing_dome_combos = set()
        existing_foot_combos = set()
        updates_made = 0
        
        for row in range(4, ws.max_row + 1):
            # Check dome controller
            dome_combo = ws[f'A{row}'].value
            if dome_combo and dome_combo.strip():
                combo_key = dome_combo.strip()
                existing_dome_combos.add(combo_key)
                
                if combo_key in dome_mappings:
                    code, desc = dome_mappings[combo_key]
                    ws[f'B{row}'] = code
                    ws[f'C{row}'] = desc
                    updates_made += 1
            
            # Check foot controller
            foot_combo = ws[f'E{row}'].value
            if foot_combo and foot_combo.strip():
                combo_key = foot_combo.strip()
                existing_foot_combos.add(combo_key)
                
                if combo_key in foot_mappings:
                    code, desc = foot_mappings[combo_key]
                    ws[f'F{row}'] = code
                    ws[f'G{row}'] = desc
                    updates_made += 1
        
        # Combine all existing combos
        existing_combos = existing_dome_combos | existing_foot_combos
        
        print(f"Updated {updates_made} existing mappings")
        
        # Find missing mappings
        all_combos = set(foot_mappings.keys()) | set(dome_mappings.keys())
        missing_combos = all_combos - existing_combos
        
        if missing_combos:
            print(f"\nFound {len(missing_combos)} missing button combinations:")
            for combo in sorted(missing_combos):
                print(f"  - {combo}")
            
            # Find the last PS row (where to insert new rows)
            last_ps_row = None
            for row in range(ws.max_row, 3, -1):
                dome_combo = ws[f'A{row}'].value
                if dome_combo and 'PS + ' in dome_combo:
                    last_ps_row = row
                    break
            
            if not last_ps_row:
                # If no PS section found, add before special combos
                for row in range(ws.max_row, 3, -1):
                    val_c = ws[f'C{row}'].value
                    val_g = ws[f'G{row}'].value
                    if (val_c and 'special' in str(val_c).lower()) or \
                       (val_g and 'special' in str(val_g).lower()):
                        last_ps_row = row - 2
                        break
                
                if not last_ps_row:
                    last_ps_row = ws.max_row
            
            # Group missing combos by modifier
            by_modifier = {}
            for combo in missing_combos:
                if ' + ' in combo:
                    modifier = combo.split(' + ')[0]
                else:
                    modifier = 'Base'
                
                if modifier not in by_modifier:
                    by_modifier[modifier] = []
                by_modifier[modifier].append(combo)
            
            # Add missing combos after the PS section
            insert_row = last_ps_row + 2  # Leave a blank row
            
            # Insert blank row first
            ws.insert_rows(insert_row)
            insert_row += 1
            
            for modifier in ['L1', 'L2']:  # These are most likely to be missing
                if modifier in by_modifier:
                    print(f"\nAdding {modifier} combinations:")
                    
                    for combo in sorted(by_modifier[modifier]):
                        # Insert new row
                        ws.insert_rows(insert_row)
                        
                        # Add combo text
                        ws[f'A{insert_row}'] = combo
                        ws[f'E{insert_row}'] = combo
                        
                        # Add dome mapping if exists
                        if combo in dome_mappings:
                            code, desc = dome_mappings[combo]
                            ws[f'B{insert_row}'] = code
                            ws[f'C{insert_row}'] = desc
                        else:
                            ws[f'B{insert_row}'] = ""
                            ws[f'C{insert_row}'] = ""
                        
                        # Add foot mapping if exists
                        if combo in foot_mappings:
                            code, desc = foot_mappings[combo]
                            ws[f'F{insert_row}'] = code
                            ws[f'G{insert_row}'] = desc
                        else:
                            ws[f'F{insert_row}'] = ""
                            ws[f'G{insert_row}'] = ""
                        
                        print(f"  Added row {insert_row}: {combo}")
                        insert_row += 1
                    
                    # Add spacing between modifier groups
                    ws.insert_rows(insert_row)
                    insert_row += 1
        
        # Save the workbook
        wb.save(excel_path)
        print(f"\nSuccessfully saved Excel file")
        
    except Exception as e:
        print(f"Error updating Excel file: {e}")
        print(f"Restoring from backup...")
        shutil.copy2(backup_path, excel_path)
        raise


def main():
    """Main function"""
    if len(sys.argv) < 3 or len(sys.argv) > 4:
        print(__doc__)
        sys.exit(1)
    
    excel_path = Path(sys.argv[1])
    config_path = Path(sys.argv[2])
    
    # Validate files exist
    if not excel_path.exists():
        print(f"Error: Excel file not found: {excel_path}")
        sys.exit(1)
    
    if not config_path.exists():
        print(f"Error: Config file not found: {config_path}")
        sys.exit(1)
    
    print(f"Reading config from: {config_path}")
    foot_mappings, dome_mappings = parse_config_cpp(config_path)
    
    print(f"\nFound {len(foot_mappings)} foot mappings")
    print(f"Found {len(dome_mappings)} dome mappings")
    
    # Check for auto-run flag
    auto_run = len(sys.argv) > 3 and sys.argv[3] == '--auto'
    
    if not auto_run:
        # Ask for confirmation
        response = input("\nProceed with updating the Excel file? (y/n): ")
        if response.lower() != 'y':
            print("Cancelled.")
            sys.exit(0)
    else:
        print("\nAuto-run mode: Proceeding with update...")
    
    update_excel(excel_path, foot_mappings, dome_mappings)
    
    print("\nDone! Please check the Excel file for:")
    print("- Updated existing mappings")
    print("- New rows added for L1+PS and L2 combinations")


if __name__ == "__main__":
    main()