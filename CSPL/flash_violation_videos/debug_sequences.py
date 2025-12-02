#!/usr/bin/env python3

import sys
sys.path.append('/home/carbon/Documents/WashU/flash_violation_videos')

from generate_color_flash_violations import *

def debug_color_sequences():
    """Debug why some color sequences aren't detected as red flashes."""
    
    # Colors are in BGR format for OpenCV
    sequences = [
        # Sequence 1: Low red -> High red -> Low red
        [(0, 0, 128), (0, 0, 255), (0, 0, 64)],           # Low red -> High red -> Low red
        
        # Sequence 2: Green -> High red -> Green  
        [(0, 255, 0), (0, 0, 255), (0, 255, 0)],          # Green -> Pure red -> Green
        
        # Sequence 3: Black -> High red -> Black
        [(0, 0, 0), (0, 0, 255), (0, 0, 0)],              # Black -> Pure red -> Black
        
        # Sequence 4: Medium red -> Very high red -> Low red
        [(0, 50, 200), (0, 0, 255), (0, 0, 100)],         # Medium red -> Pure red -> Low red
        
        # Sequence 5: Blue -> High red -> Blue
        [(255, 0, 0), (0, 0, 255), (255, 0, 0)],          # Blue -> Pure red -> Blue
    ]
    
    for i, seq in enumerate(sequences):
        print(f"\n=== Sequence {i+1}: {seq} ===")
        
        # Convert BGR to RGB for calculations
        color1_rgb = (seq[0][2], seq[0][1], seq[0][0])
        color2_rgb = (seq[1][2], seq[1][1], seq[1][0])
        color3_rgb = (seq[2][2], seq[2][1], seq[2][0])
        
        print(f"RGB colors: {color1_rgb}, {color2_rgb}, {color3_rgb}")
        
        # Calculate red ratios
        r1 = red_ratio(color1_rgb)
        r2 = red_ratio(color2_rgb)
        r3 = red_ratio(color3_rgb)
        print(f"Red ratios: {r1:.3f}, {r2:.3f}, {r3:.3f}")
        
        # Check harmful transitions
        harmful_12 = is_harmful_red_transition(color1_rgb, color2_rgb)
        harmful_23 = is_harmful_red_transition(color2_rgb, color3_rgb)
        print(f"Harmful transitions: 1->2: {harmful_12}, 2->3: {harmful_23}")
        
        # Check color differences
        diff_12 = color_diff_1976(color1_rgb, color2_rgb)
        diff_23 = color_diff_1976(color2_rgb, color3_rgb)
        print(f"Color differences: 1->2: {diff_12:.3f}, 2->3: {diff_23:.3f}")
        
        # Check red ratio ≥ 0.8 requirement
        high_red_12 = (r1 >= 0.8) or (r2 >= 0.8)
        high_red_23 = (r2 >= 0.8) or (r3 >= 0.8)
        print(f"High red (≥0.8): 1->2: {high_red_12}, 2->3: {high_red_23}")
        
        # Check opposing changes
        opposing = has_opposing_red_changes(color1_rgb, color2_rgb, color3_rgb)
        print(f"Opposing red changes: {opposing}")
        
        # Final red flash check
        is_flash = is_red_flash(color1_rgb, color2_rgb, color3_rgb)
        print(f"✓ Is red flash: {is_flash}")

if __name__ == "__main__":
    debug_color_sequences()
