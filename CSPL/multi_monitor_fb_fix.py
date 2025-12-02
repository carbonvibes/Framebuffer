#!/usr/bin/env python3
"""
Framebuffer Post-Processor for Multi-Monitor Setups
This script fixes issues with framebuffer data in multi-monitor configurations
by implementing various memory layout transformations.
"""

import sys
import os
import re
import struct
import numpy as np
from PIL import Image

# Number of different stride/pitch values to try
NUM_VARIANTS = 8

def extract_fb_info():
    """Extract framebuffer information from proc file"""
    try:
        with open('/proc/drm_fb_pixels', 'r') as f:
            fb_info = f.read()
    except FileNotFoundError:
        print("Error: /proc/drm_fb_pixels not found. Is the module loaded?")
        sys.exit(1)
    
    # Extract dimensions and pitch
    width_match = re.search(r'Dimensions: (\d+)x(\d+)', fb_info)
    pitch_match = re.search(r'Pitch: (\d+) bytes/row', fb_info)
    format_match = re.search(r'Format: (0x[0-9a-fA-F]+) \(([A-Z0-9]+)\)', fb_info)
    
    if not width_match or not pitch_match or not format_match:
        print("Error: Could not parse framebuffer information")
        sys.exit(1)
    
    width = int(width_match.group(1))
    height = int(width_match.group(2))
    pitch = int(pitch_match.group(1))
    pixel_format = format_match.group(2)
    
    return width, height, pitch, pixel_format

def read_raw_data():
    """Read raw framebuffer data into memory"""
    try:
        with open('/proc/drm_fb_raw', 'rb') as f:
            raw_data = f.read()
        return raw_data
    except Exception as e:
        print(f"Error reading raw data: {e}")
        sys.exit(1)

def process_variant(raw_data, width, height, pitch, pixel_format, bytes_per_pixel, variant, output_path):
    """Process raw data with a specific variant of layout handling"""
    try:
        # Create output image
        img_data = np.zeros((height, width, 4), dtype=np.uint8)
        
        # Apply different stride/layout handling based on variant
        if variant == 0:
            # Standard approach: row by row
            for y in range(height):
                row_start = y * pitch
                for x in range(width):
                    if row_start + (x+1)*bytes_per_pixel <= len(raw_data):
                        pixel_start = row_start + x * bytes_per_pixel
                        process_pixel(raw_data, pixel_start, pixel_format, img_data, y, x)
        
        elif variant == 1:
            # Half pitch - some GPUs might use a different stride
            half_pitch = pitch // 2
            for y in range(height):
                row_start = y * half_pitch
                for x in range(width):
                    if row_start + (x+1)*bytes_per_pixel <= len(raw_data):
                        pixel_start = row_start + x * bytes_per_pixel
                        process_pixel(raw_data, pixel_start, pixel_format, img_data, y, x)
        
        elif variant == 2:
            # Double width - treating the buffer as if width is doubled
            row_length = width * bytes_per_pixel
            for y in range(height):
                for x in range(width):
                    if y*pitch + x*bytes_per_pixel < len(raw_data):
                        pixel_start = y*pitch + x*bytes_per_pixel
                        process_pixel(raw_data, pixel_start, pixel_format, img_data, y, x)
        
        elif variant == 3:
            # Double height - treating the buffer as if height is doubled
            for y in range(height):
                row_start = (y * 2) * pitch
                for x in range(width):
                    if row_start + (x+1)*bytes_per_pixel <= len(raw_data):
                        pixel_start = row_start + x * bytes_per_pixel
                        process_pixel(raw_data, pixel_start, pixel_format, img_data, y, x)
        
        elif variant == 4:
            # Column-major instead of row-major layout
            for x in range(width):
                for y in range(height):
                    idx = (x * height + y) * bytes_per_pixel
                    if idx + bytes_per_pixel <= len(raw_data):
                        process_pixel(raw_data, idx, pixel_format, img_data, y, x)
        
        elif variant == 5:
            # Try interleaved rows (for dual-monitor side-by-side)
            half_width = width // 2
            for y in range(height):
                for x in range(half_width):
                    # First monitor
                    idx1 = y*pitch + x*bytes_per_pixel
                    if idx1 + bytes_per_pixel <= len(raw_data):
                        process_pixel(raw_data, idx1, pixel_format, img_data, y, x)
                    
                    # Second monitor
                    idx2 = y*pitch + (x+half_width)*bytes_per_pixel
                    if idx2 + bytes_per_pixel <= len(raw_data):
                        process_pixel(raw_data, idx2, pixel_format, img_data, y, x+half_width)
        
        elif variant == 6:
            # Try interleaved columns (for dual-monitor stacked)
            half_height = height // 2
            for y in range(half_height):
                for x in range(width):
                    # Top monitor
                    idx1 = y*pitch + x*bytes_per_pixel
                    if idx1 + bytes_per_pixel <= len(raw_data):
                        process_pixel(raw_data, idx1, pixel_format, img_data, y, x)
                    
                    # Bottom monitor
                    idx2 = (y+half_height)*pitch + x*bytes_per_pixel
                    if idx2 + bytes_per_pixel <= len(raw_data):
                        process_pixel(raw_data, idx2, pixel_format, img_data, y+half_height, x)
        
        elif variant == 7:
            # Try with pitch as exact row size (no padding)
            exact_pitch = width * bytes_per_pixel
            for y in range(height):
                row_start = y * exact_pitch
                for x in range(width):
                    if row_start + (x+1)*bytes_per_pixel <= len(raw_data):
                        pixel_start = row_start + x * bytes_per_pixel
                        process_pixel(raw_data, pixel_start, pixel_format, img_data, y, x)
        
        # Create and save image
        variant_path = output_path.replace('.png', f'_variant{variant}.png')
        img = Image.fromarray(img_data)
        img.save(variant_path)
        print(f"Saved variant {variant} to {variant_path}")
        
    except Exception as e:
        print(f"Error processing variant {variant}: {e}")

def process_pixel(raw_data, pixel_start, pixel_format, img_data, y, x):
    """Extract and process a single pixel"""
    try:
        if pixel_format == 'XRGB8888':
            b, g, r, _ = raw_data[pixel_start:pixel_start+4]
            img_data[y, x] = [r, g, b, 255]
        elif pixel_format == 'XBGR8888':
            r, g, b, _ = raw_data[pixel_start:pixel_start+4]
            img_data[y, x] = [r, g, b, 255]
        elif pixel_format in ['ARGB8888', 'ABGR8888']:
            if pixel_format == 'ARGB8888':
                b, g, r, a = raw_data[pixel_start:pixel_start+4]
            else:  # ABGR8888
                r, g, b, a = raw_data[pixel_start:pixel_start+4]
            img_data[y, x] = [r, g, b, a]
        elif pixel_format == 'RGB565':
            pixel = struct.unpack('<H', raw_data[pixel_start:pixel_start+2])[0]
            r = ((pixel >> 11) & 0x1F) << 3
            g = ((pixel >> 5) & 0x3F) << 2
            b = (pixel & 0x1F) << 3
            img_data[y, x] = [r, g, b, 255]
        else:
            # Default handling
            b, g, r, a = raw_data[pixel_start:pixel_start+4]
            img_data[y, x] = [r, g, b, a]
    except:
        # Handle any out of bounds errors silently
        pass

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <output_image_base>")
        sys.exit(1)
    
    output_path = sys.argv[1]
    
    # Get framebuffer info
    width, height, pitch, pixel_format = extract_fb_info()
    print(f"Framebuffer: {width}x{height}, pitch={pitch}, format={pixel_format}")
    
    # Determine bytes per pixel
    if pixel_format in ['XRGB8888', 'ARGB8888', 'XBGR8888', 'ABGR8888']:
        bytes_per_pixel = 4
    elif pixel_format == 'RGB565':
        bytes_per_pixel = 2
    else:
        print(f"Warning: Unknown format {pixel_format}, assuming 4 bytes per pixel")
        bytes_per_pixel = 4
    
    # Read raw data
    raw_data = read_raw_data()
    print(f"Read {len(raw_data)} bytes of raw data")
    
    # Process variants
    print(f"Processing {NUM_VARIANTS} different layout variants...")
    for variant in range(NUM_VARIANTS):
        process_variant(raw_data, width, height, pitch, pixel_format, 
                        bytes_per_pixel, variant, output_path)
    
    print(f"Done! Check the output files to see which variant works best.")

if __name__ == "__main__":
    main()
