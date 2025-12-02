#!/usr/bin/env python3

import cv2
import numpy as np
import os
import math
from typing import Tuple, List

# Video parameters
fps = 60
duration_sec = 40
total_frames = fps * duration_sec
frame_size = (640, 480)

FLASHES_PER_SECOND = 20 

# Screen parameters for Flash Area Threshold (B.3)
SCREEN_DIAGONAL_INCHES = 24.0  # Typical monitor size
VIEWING_DISTANCE_INCHES = 24.0  # Typical viewing distance (2 feet)
THETA_H_DEG = 10.0  # Horizontal flash area threshold angle
THETA_V_DEG = 7.5   # Vertical flash area threshold angle

# Output directory
output_dir = '/home/carbon/Documents/WashU/flash_violation_videos'
OUTPUT_DIR = "color_flash_violations"

# Color constants for red flash violations
RED_HIGH = (0, 0, 255)      # Pure red (BGR format)
RED_LOW = (0, 0, 128)       # Dark red
BLACK = (0, 0, 0)           # Black
WHITE = (255, 255, 255)     # White
GREEN = (0, 255, 0)         # Pure green

def gamma_expand(c: float) -> float:
    """
    Gamma expansion from sRGB to linear RGB.
    Implements the gamma expansion from guidelines.v B.2.
    """
    if c <= 0.04045:
        return c / 12.92
    else:
        return ((c + 0.055) / 1.055) ** 2.4

def srgb_to_linear(color: Tuple[int, int, int]) -> Tuple[float, float, float]:
    """Convert sRGB color (0-255) to linear RGB (0-1)."""
    r, g, b = [x / 255.0 for x in color]
    return (gamma_expand(r), gamma_expand(g), gamma_expand(b))

def linear_to_xyz(r_lin: float, g_lin: float, b_lin: float) -> Tuple[float, float, float]:
    """
    Convert linear RGB to CIE XYZ (D65 white point).
    Uses transformation matrix from guidelines.v B.2.
    """
    x = 0.4124 * r_lin + 0.3576 * g_lin + 0.1805 * b_lin
    y = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin
    z = 0.0193 * r_lin + 0.1192 * g_lin + 0.9505 * b_lin
    return (x, y, z)

def xyz_to_uv_prime(x: float, y: float, z: float) -> Tuple[float, float]:
    """
    Convert CIE XYZ to CIE 1976 UCS chromaticity coordinates (u', v').
    Implements the conversion from guidelines.v B.2.
    """
    denom = x + 15 * y + 3 * z
    if denom <= 0:
        return (0, 0)
    
    u_prime = (4 * x) / denom
    v_prime = (9 * y) / denom
    return (u_prime, v_prime)

def color_diff_1976(color1: Tuple[int, int, int], color2: Tuple[int, int, int]) -> float:
    """
    Calculate Euclidean color difference in CIE 1976 UCS space.
    Implements color_diff_1976 from guidelines.v B.2.
    """
    # Convert both colors to linear RGB
    r1_lin, g1_lin, b1_lin = srgb_to_linear(color1)
    r2_lin, g2_lin, b2_lin = srgb_to_linear(color2)
    
    # Convert to XYZ
    x1, y1, z1 = linear_to_xyz(r1_lin, g1_lin, b1_lin)
    x2, y2, z2 = linear_to_xyz(r2_lin, g2_lin, b2_lin)
    
    # Convert to u', v'
    u1, v1 = xyz_to_uv_prime(x1, y1, z1)
    u2, v2 = xyz_to_uv_prime(x2, y2, z2)
    
    # Calculate Euclidean distance
    return math.sqrt((u1 - u2)**2 + (v1 - v2)**2)

def red_ratio(color: Tuple[int, int, int]) -> float:
    """
    Calculate red ratio in linear RGB space.
    Implements red_ratio from guidelines.v B.2.
    """
    r_lin, g_lin, b_lin = srgb_to_linear(color)
    total = r_lin + g_lin + b_lin
    
    if total <= 0:
        return 0
    
    return r_lin / total

def is_harmful_red_transition(color1: Tuple[int, int, int], color2: Tuple[int, int, int]) -> bool:
    """
    Check if a transition between two colors is a harmful red transition.
    Implements harmful_red_transition from guidelines.v B.2.
    """
    red1 = red_ratio(color1)
    red2 = red_ratio(color2)
    color_diff = color_diff_1976(color1, color2)
    
    # At least one frame must have red_ratio >= 0.8
    high_red = (red1 >= 0.8) or (red2 >= 0.8)
    
    # Color difference must be > 0.2
    significant_diff = color_diff > 0.2
    
    return high_red and significant_diff

def has_opposing_red_changes(color1: Tuple[int, int, int], 
                           color2: Tuple[int, int, int], 
                           color3: Tuple[int, int, int]) -> bool:
    """
    Check if three consecutive colors have opposing changes in red ratio.
    Implements opposing_red_changes from guidelines.v B.2.
    """
    r1 = red_ratio(color1)
    r2 = red_ratio(color2)
    r3 = red_ratio(color3)
    
    # Check for opposing changes: increase then decrease, or decrease then increase
    return (r2 > r1 and r3 < r2) or (r2 < r1 and r3 > r2)

def is_red_flash(color1: Tuple[int, int, int], 
                 color2: Tuple[int, int, int], 
                 color3: Tuple[int, int, int]) -> bool:
    """
    Check if three consecutive colors constitute a red flash.
    Implements is_red_flash from guidelines.v B.2.
    """
    # Must have harmful red transitions between consecutive pairs
    harmful_12 = is_harmful_red_transition(color1, color2)
    harmful_23 = is_harmful_red_transition(color2, color3)
    
    # Must have opposing red changes
    opposing = has_opposing_red_changes(color1, color2, color3)
    
    return harmful_12 and harmful_23 and opposing

def calculate_flash_area_threshold(viewing_distance_inches: float = 24) -> float:
    """
    Calculate flash area threshold in pixels² based on B.3 specification.
    Default viewing distance is 24 inches (typical monitor distance).
    """

    screen_diagonal_inches = 27
    width_pixels = frame_size[0]
    height_pixels = frame_size[1]
    
    # Calculate PPI
    diagonal_pixels = math.sqrt(width_pixels**2 + height_pixels**2)
    ppi = diagonal_pixels / screen_diagonal_inches
    
    # Viewing angles from B.3
    theta_h_deg = 10.0
    theta_v_deg = 7.5
    
    # Convert to radians
    theta_h_rad = math.radians(theta_h_deg)
    theta_v_rad = math.radians(theta_v_deg)
    
    # Calculate area threshold
    area_inches = (viewing_distance_inches * theta_h_rad) * (viewing_distance_inches * theta_v_rad)
    area_pixels = area_inches * (ppi ** 2) * 0.25
    
    return area_pixels

def calculate_shape_area(shape_type: str, params: dict) -> float:
    """Calculate area for different geometric shapes."""
    if shape_type == "circle":
        radius = params["radius"]
        return math.pi * radius * radius
    elif shape_type == "square":
        side = params["side"]
        return side * side
    elif shape_type == "rectangle":
        width = params["width"]
        height = params["height"]
        return width * height
    elif shape_type == "triangle":
        base = params["base"]
        height = params["height"]
        return 0.5 * base * height
    elif shape_type == "ellipse":
        a = params["a"]  # semi-major axis
        b = params["b"]  # semi-minor axis
        return math.pi * a * b
    else:
        raise ValueError(f"Unknown shape type: {shape_type}")

def ensure_area_threshold_violation(shape_type: str, params: dict) -> dict:
    """Ensure the shape area violates the B.3 threshold."""
    threshold = calculate_flash_area_threshold()
    current_area = calculate_shape_area(shape_type, params)
    
    if current_area <= threshold:
        # Scale up the shape to exceed threshold by at least 20%
        scale_factor = math.sqrt((threshold * 1.2) / current_area)
        
        if shape_type == "circle":
            params["radius"] = int(params["radius"] * scale_factor)
        elif shape_type == "square":
            params["side"] = int(params["side"] * scale_factor)
        elif shape_type == "rectangle":
            params["width"] = int(params["width"] * scale_factor)
            params["height"] = int(params["height"] * scale_factor)
        elif shape_type == "triangle":
            params["base"] = int(params["base"] * scale_factor)
            params["height"] = int(params["height"] * scale_factor)
        elif shape_type == "ellipse":
            params["a"] = int(params["a"] * scale_factor)
            params["b"] = int(params["b"] * scale_factor)
    
    return params

def create_red_flash_sequence() -> List[Tuple[int, int, int]]:
    
    # Colors are in BGR format
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
        
        # Sequence 6: White -> High red -> Black (opposing changes)
        [(255, 255, 255), (0, 0, 255), (0, 0, 0)],

    ]
    
    return sequences

def draw_shape(frame: np.ndarray, shape_type: str, params: dict, color: Tuple[int, int, int]):
    """Draw a geometric shape on the frame."""
    center_x = frame_size[0] // 2
    center_y = frame_size[1] // 2
    
    if shape_type == "circle":
        cv2.circle(frame, (center_x, center_y), params["radius"], color, -1)
    elif shape_type == "square":
        side = params["side"]
        top_left = (center_x - side//2, center_y - side//2)
        bottom_right = (center_x + side//2, center_y + side//2)
        cv2.rectangle(frame, top_left, bottom_right, color, -1)
    elif shape_type == "rectangle":
        width = params["width"]
        height = params["height"]
        top_left = (center_x - width//2, center_y - height//2)
        bottom_right = (center_x + width//2, center_y + height//2)
        cv2.rectangle(frame, top_left, bottom_right, color, -1)
    elif shape_type == "triangle":
        base = params["base"]
        height = params["height"]
        points = np.array([
            [center_x, center_y - height//2],           # Top
            [center_x - base//2, center_y + height//2], # Bottom left
            [center_x + base//2, center_y + height//2]  # Bottom right
        ], np.int32)
        cv2.fillPoly(frame, [points], color)
    elif shape_type == "ellipse":
        axes = (params["a"], params["b"])
        cv2.ellipse(frame, (center_x, center_y), axes, 0, 0, 360, color, -1)

def calculate_flash_timing(fps, flashes_per_second):
    """Calculate frame timing for desired flash frequency"""
    if flashes_per_second <= 0:
        return fps, 1  # Default to 1 flash per second
    
    frames_per_second = fps
    frames_per_flash_cycle = frames_per_second / flashes_per_second
    
    # Each flash needs at least 3 frames for color sequence pattern
    min_frames = 3  # 3 frames minimum for color1->color2->color3 pattern
    
    if frames_per_flash_cycle < min_frames:
        # Too many flashes requested, limit it
        actual_flashes = frames_per_second / min_frames
        return min_frames, actual_flashes
    
    return int(frames_per_flash_cycle), flashes_per_second

def generate_red_flash_video(shape_type: str, params: dict, color_sequence: List[Tuple[int, int, int]], 
                           output_filename: str):
    """Generate a video with red flash violations."""
    print(f"\nGenerating {os.path.basename(output_filename)}...")
    print(f"Target: {FLASHES_PER_SECOND} flashes per second")
    
    # Ensure shape violates area threshold
    params = ensure_area_threshold_violation(shape_type, params)
    
    # Calculate flash timing
    cycle_frames, actual_flashes = calculate_flash_timing(fps, FLASHES_PER_SECOND)
    print(f"Cycle: {cycle_frames} frames, Actual: {actual_flashes:.1f} flashes/sec")
    
    # Calculate timing
    total_frames = fps * duration_sec
    
    # Validate the color sequence creates red flashes
    print(f"  Validating color sequence: {color_sequence}")
    if len(color_sequence) >= 3:
        # Check the entire sequence as a triplet (since our sequences are exactly 3 colors)
        c1, c2, c3 = color_sequence[0], color_sequence[1], color_sequence[2]
        
        # Convert BGR to RGB for calculation
        c1_rgb = (c1[2], c1[1], c1[0])
        c2_rgb = (c2[2], c2[1], c2[0])
        c3_rgb = (c3[2], c3[1], c3[0])
        
        if is_red_flash(c1_rgb, c2_rgb, c3_rgb):
            print(f"  ✓ Red flash detected in sequence: {color_sequence}")
            print(f"    Red ratios: {red_ratio(c1_rgb):.3f}, {red_ratio(c2_rgb):.3f}, {red_ratio(c3_rgb):.3f}")
            print(f"    Color diff 1->2: {color_diff_1976(c1_rgb, c2_rgb):.3f}")
            print(f"    Color diff 2->3: {color_diff_1976(c2_rgb, c3_rgb):.3f}")
        else:
            print(f"  ⚠ Red flash NOT detected in sequence: {color_sequence}")
            r1, r2, r3 = red_ratio(c1_rgb), red_ratio(c2_rgb), red_ratio(c3_rgb)
            d12, d23 = color_diff_1976(c1_rgb, c2_rgb), color_diff_1976(c2_rgb, c3_rgb)
            print(f"    Red ratios: {r1:.3f}, {r2:.3f}, {r3:.3f}")
            print(f"    Color diffs: {d12:.3f}, {d23:.3f}")
            print(f"    Harmful 1->2: {is_harmful_red_transition(c1_rgb, c2_rgb)}")
            print(f"    Harmful 2->3: {is_harmful_red_transition(c2_rgb, c3_rgb)}")
            print(f"    Opposing: {has_opposing_red_changes(c1_rgb, c2_rgb, c3_rgb)}")
    
    # Create video writer
    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    out = cv2.VideoWriter(output_filename, fourcc, fps, frame_size)
    
    print(f"Generating {output_filename}...")
    print(f"  Shape: {shape_type} with parameters: {params}")
    print(f"  Shape area: {calculate_shape_area(shape_type, params):.0f} pixels²")
    print(f"  Area threshold: {calculate_flash_area_threshold():.0f} pixels²")
    print(f"  Flash rate: {FLASHES_PER_SECOND} Hz")
    
    for frame_num in range(total_frames):
        # Create black background
        frame = np.zeros((frame_size[1], frame_size[0], 3), dtype=np.uint8)
        
        # Determine current color based on flash sequence with controlled timing
        cycle_pos = frame_num % cycle_frames
        color_index = cycle_pos % len(color_sequence)
        current_color = color_sequence[color_index]
        
        # Draw the shape
        draw_shape(frame, shape_type, params, current_color)
        
        # Write frame
        out.write(frame)
    
    out.release()
    print(f"✓ Created {os.path.basename(output_filename)}")

def main():
    """Generate red flash violation videos."""
    # Create output directory
    full_output_dir = os.path.join(output_dir, OUTPUT_DIR)
    os.makedirs(full_output_dir, exist_ok=True)
    
    # Get red flash sequences
    color_sequences = create_red_flash_sequence()
    
    # Shape configurations that will violate area threshold
    shapes_configs = [
        ("circle", {"radius": 100}),
        ("square", {"side": 150}),
        ("rectangle", {"width": 200, "height": 120}),
        ("triangle", {"base": 240, "height": 180}),
        ("ellipse", {"a": 140, "b": 90}),
    ]
    
    print("=== Red Flash Color Threshold Violations Generator ===")
    print(f"Creating videos with {FLASHES_PER_SECOND} flashes per second")
    print(f"Duration: {duration_sec}s, Resolution: {frame_size[0]}x{frame_size[1]}")
    print(f"Area threshold: {calculate_flash_area_threshold():.0f} pixels²")
    print(f"Flash frequency: {FLASHES_PER_SECOND} Hz (violates ≥4 Hz threshold)")
    print()
    
    # Generate videos for each shape and color sequence combination
    video_count = 0
    for i, (shape_type, params) in enumerate(shapes_configs):
        for j, color_sequence in enumerate(color_sequences):
            output_filename = os.path.join(full_output_dir, f"red_flash_{shape_type}_{j+1}.mp4")
            generate_red_flash_video(shape_type, params, color_sequence, output_filename)
            video_count += 1
    
    print(f"\n=== Generation Complete ===")
    print(f"✓ Generated {video_count} red flash violation videos with {FLASHES_PER_SECOND} flashes per second")
    print("✓ Videos with ≤3 flashes/sec should be safe")
    print("✓ Videos with ≥4 flashes/sec should violate frequency guidelines")
    print(f"✓ Flash area threshold: {calculate_flash_area_threshold():.0f} pixels² (all shapes designed to exceed this)")
    print("✓ All videos use opposing red ratio changes for color violations")
    
    print("\nThese videos violate:")
    print("- B.2 Flash Color Threshold (harmful red transitions)")
    print("- B.3 Flash Area Threshold (area exceeds threshold)")
    print("- B.4 Flash Frequency Threshold (≥4 flashes per second)")

if __name__ == "__main__":
    main()
