#!/usr/bin/env python3
"""
Update Control Mapping Excel File from the C2B5 built-in button mappings
This script updates the ControlMapping.xlsx file with the built-in mappings in
src/default_mappings.cpp: the WCB stored-command key goes where the function code used to,
and the entry's trailing comment is the description. Rows for mappable combos that no longer
have a built-in mapping are blanked. It also adds new rows for mappings not present in the
Excel template.

The built-in mappings are only the defaults: DroidNet can change any combo at runtime.

Usage:
    python3 update_control_mapping.py <excel_file> <default_mappings_cpp>

Example:
    python3 update_control_mapping.py ../../Reference/ControlMapping.xlsx ../../src/default_mappings.cpp
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


MODIFIER_LABELS = {'NONE': None, 'L1': 'L1', 'L2': 'L2', 'PS': 'PS', 'CIRCLE': 'O', 'CROSS': 'X'}
BUTTONS = ('UP', 'RIGHT', 'DOWN', 'LEFT', 'L3', 'PS')


def combo_name(modifier, button):
    """The spreadsheet's name for a combo: 'UP', 'L1 + UP', 'O + UP' (CIRCLE), 'X + UP' (CROSS)."""
    label = MODIFIER_LABELS[modifier]
    return button if label is None else f"{label} + {button}"


def mappable_combos(controller):
    """Every combo SHADOW can map (reserved ones excluded, see default_mappings.cpp)."""
    combos = set()
    for modifier in MODIFIER_LABELS:
        for button in BUTTONS:
            if button == 'PS' and modifier not in ('L1', 'L2'):
                continue  # PS is only a button with L1/L2 held
            if controller == 'FOOT' and modifier == 'L1' and button == 'L3':
                continue  # over-speed toggle
            combos.add(combo_name(modifier, button))
    return combos


def parse_config_cpp(filepath):
    """Parse src/default_mappings.cpp and extract the built-in button mappings.

    Entries look like:  {FOOT(L1, UP), "extremehappysou"},  // Extreme Happy
    Returns (foot, dome) dicts of combo name -> (key, description).
    """
    foot_mappings = {}
    dome_mappings = {}

    with open(filepath, 'r') as f:
        content = f.read()

    pattern = re.compile(r'\{\s*(FOOT|DOME)\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*,\s*"([^"]*)"\s*\}\s*,?[ \t]*(?://[ \t]*(.*))?')
    for match in pattern.finditer(content):
        controller, modifier, button, key, desc = match.groups()
        if modifier not in MODIFIER_LABELS or button not in BUTTONS:
            raise ValueError(f"Unknown combo {controller}({modifier}, {button}) in {filepath}")
        target = foot_mappings if controller == 'FOOT' else dome_mappings
        target[combo_name(modifier, button)] = (key, (desc or key).strip())

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
        cleared = 0
        
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
                elif combo_key in mappable_combos('DOME'):
                    ws[f'B{row}'] = ""
                    ws[f'C{row}'] = ""  # no built-in mapping any more
                    cleared += 1
            
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
                elif combo_key in mappable_combos('FOOT'):
                    ws[f'F{row}'] = ""
                    ws[f'G{row}'] = ""  # no built-in mapping any more
                    cleared += 1
        
        # Combine all existing combos
        existing_combos = existing_dome_combos | existing_foot_combos
        
        print(f"Updated {updates_made} existing mappings, blanked {cleared} with no built-in mapping")
        
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
        print(f"Error: mappings file not found: {config_path}")
        sys.exit(1)
    
    print(f"Reading built-in mappings from: {config_path}")
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