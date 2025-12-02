#!/usr/bin/env python3

import cv2
import numpy as np
import os
import math

# Video parameters
fps = 60
duration_sec = 40
total_frames = fps * duration_sec
frame_size = (640, 480)

FLASHES_PER_SECOND = 20  # Set to desired flashes per second (e.g., 2, 3, 4, 5, 6, 8)

# Output directory
output_dir = '/home/carbon/Documents/WashU/flash_violation_videos'
os.makedirs(output_dir, exist_ok=True)

def gamma_expand_srgb(c):
    """Convert sRGB to linear space using gamma expansion"""
    if c <= 0.04045:
        return c / 12.92
    else:
        return ((c + 0.055) / 1.055) ** 2.4

def srgb_to_linear_luminance(srgb_val):
    """Convert 8-bit sRGB value to linear luminance"""
    c = srgb_val / 255.0
    linear = gamma_expand_srgb(c)
    return linear

def create_michelson_violation_intensities():
    """Create intensities for Michelson violation: both > 0.8, contrast >= 1/17"""
    base_srgb = 238   # ~0.854 linear
    flash_srgb = 255  # 1.0 linear
    
    base_linear = srgb_to_linear_luminance(base_srgb)
    flash_linear = srgb_to_linear_luminance(flash_srgb)
    michelson = abs(flash_linear - base_linear) / (base_linear + flash_linear)
    
    print(f"Michelson setup: base={base_linear:.3f}, flash={flash_linear:.3f}, contrast={michelson:.4f}")
    return base_srgb, flash_srgb

def create_absolute_violation_intensities():
    """Create intensities for absolute violation: |I2-I1| >= 0.1"""
    base_srgb = 50    # ~0.015 linear  
    flash_srgb = 255  # 1.0 linear
    
    base_linear = srgb_to_linear_luminance(base_srgb)
    flash_linear = srgb_to_linear_luminance(flash_srgb)
    abs_diff = abs(flash_linear - base_linear)
    
    print(f"Absolute setup: base={base_linear:.3f}, flash={flash_linear:.3f}, diff={abs_diff:.3f}")
    return base_srgb, flash_srgb

def draw_large_circle(img, center, intensity):
    """Draw a large filled circle"""
    cv2.circle(img, center, 100, intensity, -1)

def draw_large_square(img, center, intensity):
    """Draw a large filled square"""
    x, y = center
    size = 150
    cv2.rectangle(img, (x-size//2, y-size//2), (x+size//2, y+size//2), intensity, -1)

def draw_large_triangle(img, center, intensity):
    """Draw a large filled triangle"""
    x, y = center
    size = 120
    pts = np.array([[x, y-size], [x-size, y+size//2], [x+size, y+size//2]], np.int32)
    cv2.fillPoly(img, [pts], intensity)

def draw_large_star(img, center, intensity):
    """Draw a large filled star"""
    x, y = center
    outer_radius = 100
    inner_radius = 50
    angles = np.linspace(0, 2*np.pi, 11)
    points = []
    for i, angle in enumerate(angles[:-1]):
        radius = outer_radius if i % 2 == 0 else inner_radius
        px = int(x + radius * np.cos(angle - np.pi/2))
        py = int(y + radius * np.sin(angle - np.pi/2))
        points.append([px, py])
    cv2.fillPoly(img, [np.array(points, np.int32)], intensity)

def draw_large_cross(img, center, intensity):
    """Draw a large cross"""
    x, y = center
    size = 100
    thickness = 40
    cv2.line(img, (x-size, y), (x+size, y), intensity, thickness)
    cv2.line(img, (x, y-size), (x, y+size), intensity, thickness)

def draw_large_diamond(img, center, intensity):
    """Draw a large filled diamond"""
    x, y = center
    size = 100
    pts = np.array([[x, y-size], [x+size, y], [x, y+size], [x-size, y]], np.int32)
    cv2.fillPoly(img, [pts], intensity)

def draw_large_hexagon(img, center, intensity):
    """Draw a large filled hexagon"""
    x, y = center
    radius = 80
    angles = np.linspace(0, 2*np.pi, 7)
    points = [[int(x + radius * np.cos(a)), int(y + radius * np.sin(a))] for a in angles[:-1]]
    cv2.fillPoly(img, [np.array(points, np.int32)], intensity)

def draw_large_ellipse(img, center, intensity):
    """Draw a large filled ellipse"""
    cv2.ellipse(img, center, (120, 80), 0, 0, 360, intensity, -1)

def draw_large_octagon(img, center, intensity):
    """Draw a large filled octagon"""
    x, y = center
    radius = 90
    angles = np.linspace(0, 2*np.pi, 9)
    points = [[int(x + radius * np.cos(a)), int(y + radius * np.sin(a))] for a in angles[:-1]]
    cv2.fillPoly(img, [np.array(points, np.int32)], intensity)

def draw_large_lightning(img, center, intensity):
    """Draw a large lightning bolt"""
    x, y = center
    size = 100
    pts = np.array([
        [x-20, y-size], [x+30, y-40], [x, y-40],
        [x+60, y+20], [x-10, y+20], [x+20, y+size]
    ], np.int32)
    cv2.fillPoly(img, [pts], intensity)

# Define 10 shapes with guaranteed large size
shapes = [
    ("mega_circle", draw_large_circle),
    ("mega_square", draw_large_square), 
    ("mega_triangle", draw_large_triangle),
    ("mega_star", draw_large_star),
    ("mega_cross", draw_large_cross),
    ("mega_diamond", draw_large_diamond),
    ("mega_hexagon", draw_large_hexagon),
    ("mega_ellipse", draw_large_ellipse),
    ("mega_octagon", draw_large_octagon),
    ("mega_lightning", draw_large_lightning)
]

def calculate_flash_timing(fps, flashes_per_second):
    """Calculate frame timing for desired flash frequency"""
    if flashes_per_second <= 0:
        return fps, 1  # Default to 1 flash per second
    
    frames_per_second = fps
    frames_per_flash_cycle = frames_per_second / flashes_per_second
    
    # Each flash needs at least 2 frames (base -> flash -> base for opposing changes)
    min_frames = 3  # 3 frames minimum for base->flash->base pattern
    
    if frames_per_flash_cycle < min_frames:
        # Too many flashes requested, limit it
        actual_flashes = frames_per_second / min_frames
        return min_frames, actual_flashes
    
    return int(frames_per_flash_cycle), flashes_per_second

def generate_guaranteed_violation(shape_name, draw_func, violation_type, video_index, flashes_per_second=8):
    """Generate a video guaranteed to violate flash guidelines"""
    
    print(f"\nGenerating {shape_name}_{violation_type}_violation.mp4...")
    print(f"Target: {flashes_per_second} flashes per second")
    
    # Get violation-specific intensities
    if violation_type == 'michelson':
        base_intensity, flash_intensity = create_michelson_violation_intensities()
    else:
        base_intensity, flash_intensity = create_absolute_violation_intensities()
    
    # Calculate flash timing
    cycle_frames, actual_flashes = calculate_flash_timing(fps, flashes_per_second)
    print(f"Cycle: {cycle_frames} frames, Actual: {actual_flashes:.1f} flashes/sec")
    
    filename = f"{shape_name}_{violation_type}_{int(actual_flashes)}fps_violation.mp4"
    filepath = os.path.join(output_dir, filename)
    
    # Create video writer
    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    writer = cv2.VideoWriter(filepath, fourcc, fps, frame_size, isColor=False)
    
    center = (frame_size[0] // 2, frame_size[1] // 2)
    
    for frame_num in range(total_frames):
        # Very dark background to maximize contrast
        frame = np.full((frame_size[1], frame_size[0]), 10, dtype=np.uint8)
        
        # Create flash pattern with controlled frequency
        cycle_pos = frame_num % cycle_frames
        
        # Pattern: base -> flash -> base for opposing changes
        if cycle_pos == 0:
            intensity = base_intensity
        elif cycle_pos == 1:
            intensity = flash_intensity  # Peak flash
        else:
            intensity = base_intensity   # Return to base
        
        # Draw shape with current intensity
        draw_func(frame, center, intensity)
        
        writer.write(frame)
    
    writer.release()
    print(f"✓ Created {filename}")

def main():
    """Generate all guaranteed violation videos"""
    print("=== Guaranteed Flash Violation Generator ===")
    print(f"Creating videos with {FLASHES_PER_SECOND} flashes per second")
    print(f"Duration: {duration_sec}s, Resolution: {frame_size[0]}x{frame_size[1]}")
    
    # Generate 5 Michelson violations and 5 absolute violations
    for i, (shape_name, draw_func) in enumerate(shapes):
        violation_type = 'michelson' if i < 5 else 'absolute'
        generate_guaranteed_violation(shape_name, draw_func, violation_type, i, FLASHES_PER_SECOND)
    
    print(f"\n=== Generation Complete ===")
    print(f"✓ Generated {len(shapes)} flash violation videos with {FLASHES_PER_SECOND} flashes per second")
    print("✓ Videos with ≤3 flashes/sec should be safe")
    print("✓ Videos with ≥4 flashes/sec should violate frequency guidelines")
    print("✓ All videos use base→flash→base pattern for opposing changes")

if __name__ == "__main__":
    main()
