#!/usr/bin/env python3

import math
from typing import Tuple

def gamma_expand(c: float) -> float:
    if c <= 0.04045:
        return c / 12.92
    else:
        return ((c + 0.055) / 1.055) ** 2.4

def srgb_to_linear(color: Tuple[int, int, int]) -> Tuple[float, float, float]:
    r, g, b = [x / 255.0 for x in color]
    return (gamma_expand(r), gamma_expand(g), gamma_expand(b))

def linear_to_xyz(r_lin: float, g_lin: float, b_lin: float) -> Tuple[float, float, float]:
    x = 0.4124 * r_lin + 0.3576 * g_lin + 0.1805 * b_lin
    y = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin
    z = 0.0193 * r_lin + 0.1192 * g_lin + 0.9505 * b_lin
    return (x, y, z)

def xyz_to_uv_prime(x: float, y: float, z: float) -> Tuple[float, float]:
    denom = x + 15 * y + 3 * z
    if denom <= 0:
        return (0, 0)
    u_prime = (4 * x) / denom
    v_prime = (9 * y) / denom
    return (u_prime, v_prime)

def color_diff_1976(color1: Tuple[int, int, int], color2: Tuple[int, int, int]) -> float:
    r1_lin, g1_lin, b1_lin = srgb_to_linear(color1)
    r2_lin, g2_lin, b2_lin = srgb_to_linear(color2)
    
    x1, y1, z1 = linear_to_xyz(r1_lin, g1_lin, b1_lin)
    x2, y2, z2 = linear_to_xyz(r2_lin, g2_lin, b2_lin)
    
    u1, v1 = xyz_to_uv_prime(x1, y1, z1)
    u2, v2 = xyz_to_uv_prime(x2, y2, z2)
    
    return math.sqrt((u1 - u2)**2 + (v1 - v2)**2)

def red_ratio(color: Tuple[int, int, int]) -> float:
    r_lin, g_lin, b_lin = srgb_to_linear(color)
    total = r_lin + g_lin + b_lin
    if total <= 0:
        return 0
    return r_lin / total

def is_harmful_red_transition(color1: Tuple[int, int, int], color2: Tuple[int, int, int]) -> bool:
    red1 = red_ratio(color1)
    red2 = red_ratio(color2)
    color_diff = color_diff_1976(color1, color2)
    
    high_red = (red1 >= 0.8) or (red2 >= 0.8)
    significant_diff = color_diff > 0.2
    
    return high_red and significant_diff

def has_opposing_red_changes(color1: Tuple[int, int, int], 
                           color2: Tuple[int, int, int], 
                           color3: Tuple[int, int, int]) -> bool:
    r1 = red_ratio(color1)
    r2 = red_ratio(color2)
    r3 = red_ratio(color3)
    
    return (r2 > r1 and r3 < r2) or (r2 < r1 and r3 > r2)

def is_red_flash(color1: Tuple[int, int, int], 
                 color2: Tuple[int, int, int], 
                 color3: Tuple[int, int, int]) -> bool:
    harmful_12 = is_harmful_red_transition(color1, color2)
    harmful_23 = is_harmful_red_transition(color2, color3)
    opposing = has_opposing_red_changes(color1, color2, color3)
    
    return harmful_12 and harmful_23 and opposing

# Test the sequences
sequences = [
    # Sequence 1: White -> High red -> White (large color difference)
    [(255, 255, 255), (0, 0, 255), (255, 255, 255)],           
    
    # Sequence 2: Green -> High red -> Green  
    [(0, 255, 0), (0, 0, 255), (0, 255, 0)],          
    
    # Sequence 3: Black -> High red -> Black
    [(0, 0, 0), (0, 0, 255), (0, 0, 0)],              
    
    # Sequence 4: Yellow -> High red -> Yellow (opposing red changes)
    [(0, 255, 255), (0, 0, 255), (0, 255, 255)],         
    
    # Sequence 5: Blue -> High red -> Blue
    [(255, 0, 0), (0, 0, 255), (255, 0, 0)],          
]

print("Debugging Red Flash Detection")
print("=" * 50)

for i, seq in enumerate(sequences):
    print(f"\nSequence {i+1}: {seq}")
    
    c1, c2, c3 = seq
    
    # Convert BGR to RGB for calculations
    c1_rgb = (c1[2], c1[1], c1[0])
    c2_rgb = (c2[2], c2[1], c2[0])
    c3_rgb = (c3[2], c3[1], c3[0])
    
    r1 = red_ratio(c1_rgb)
    r2 = red_ratio(c2_rgb)
    r3 = red_ratio(c3_rgb)
    
    diff_12 = color_diff_1976(c1_rgb, c2_rgb)
    diff_23 = color_diff_1976(c2_rgb, c3_rgb)
    
    harmful_12 = is_harmful_red_transition(c1_rgb, c2_rgb)
    harmful_23 = is_harmful_red_transition(c2_rgb, c3_rgb)
    opposing = has_opposing_red_changes(c1_rgb, c2_rgb, c3_rgb)
    
    is_flash = is_red_flash(c1_rgb, c2_rgb, c3_rgb)
    
    print(f"  Red ratios: {r1:.3f} -> {r2:.3f} -> {r3:.3f}")
    print(f"  Color diffs: {diff_12:.3f}, {diff_23:.3f}")
    print(f"  High red (≥0.8): {r1 >= 0.8}, {r2 >= 0.8}, {r3 >= 0.8}")
    print(f"  Significant diff (>0.2): {diff_12 > 0.2}, {diff_23 > 0.2}")
    print(f"  Harmful transitions: {harmful_12}, {harmful_23}")
    print(f"  Opposing changes: {opposing}")
    print(f"  Is red flash: {is_flash}")
