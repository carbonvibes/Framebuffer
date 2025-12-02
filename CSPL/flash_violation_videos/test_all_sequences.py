#!/usr/bin/env python3

# Test all sequences
import sys
import os
sys.path.append('/home/carbon/Documents/WashU/flash_violation_videos')

from generate_color_flash_violations import *

sequences = [
    [(255, 255, 255), (0, 0, 255), (255, 255, 255)],   # White -> Red -> White
    [(0, 255, 0), (0, 0, 255), (0, 255, 0)],          # Green -> Red -> Green
    [(0, 0, 0), (0, 0, 255), (0, 0, 0)],              # Black -> Red -> Black
    [(0, 255, 255), (0, 0, 255), (0, 255, 255)],      # Yellow -> Red -> Yellow
    [(255, 0, 0), (0, 0, 255), (255, 0, 0)],          # Blue -> Red -> Blue
]

print("Testing all sequences...")
for i, seq in enumerate(sequences):
    print(f"\nSequence {i+1}: {seq}")
    
    c1, c2, c3 = seq
    
    # Convert BGR to RGB for calculation
    c1_rgb = (c1[2], c1[1], c1[0])
    c2_rgb = (c2[2], c2[1], c2[0])
    c3_rgb = (c3[2], c3[1], c3[0])
    
    if is_red_flash(c1_rgb, c2_rgb, c3_rgb):
        print(f"  ✓ Red flash detected")
        print(f"    Red ratios: {red_ratio(c1_rgb):.3f}, {red_ratio(c2_rgb):.3f}, {red_ratio(c3_rgb):.3f}")
        print(f"    Color diff 1->2: {color_diff_1976(c1_rgb, c2_rgb):.3f}")
        print(f"    Color diff 2->3: {color_diff_1976(c2_rgb, c3_rgb):.3f}")
    else:
        print(f"  ⚠ Red flash NOT detected")
