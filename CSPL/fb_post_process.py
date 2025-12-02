#!/usr/bin/env python3
"""
Framebuffer Post-Processor
This script fixes striping issues in raw framebuffer data by correctly handling
the pitch/stride of the framebuffer.
"""

import sys
import os
import re
import struct
import numpy as np
from PIL import Image

def process_framebuffer(output_path):
    """Process raw framebuffer data directly and save as PNG"""
    
    # First, get framebuffer info from proc file
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
    
    print(f"Framebuffer dimensions: {width}x{height}")
    print(f"Pitch: {pitch} bytes per row")
    print(f"Format: {pixel_format}")
    
    # Determine bytes per pixel
    if pixel_format in ['XRGB8888', 'ARGB8888', 'XBGR8888', 'ABGR8888']:
        bytes_per_pixel = 4
    elif pixel_format == 'RGB565':
        bytes_per_pixel = 2
    else:
        print(f"Warning: Unknown format {pixel_format}, assuming 4 bytes per pixel")
        bytes_per_pixel = 4
    
    # Create an empty numpy array for the image
    img_data = np.zeros((height, width, 4), dtype=np.uint8)
    
    # Read raw data directly from proc file and process it
    try:
        with open('/proc/drm_fb_raw', 'rb') as f:
            for y in range(height):
                # Read one row with pitch bytes
                row_data = f.read(pitch)
                if not row_data or len(row_data) < pitch:
                    print(f"Warning: End of file reached at row {y}")
                    break
                    
                # Process each pixel in the row
                for x in range(width):
                    if x * bytes_per_pixel + bytes_per_pixel <= pitch:
                        pixel_start = x * bytes_per_pixel
                        
                        # Extract pixel based on format
                        if pixel_format == 'XRGB8888':
                            # Format: 0xXXRRGGBB (little-endian)
                            b, g, r, _ = row_data[pixel_start:pixel_start+4]
                            img_data[y, x] = [r, g, b, 255]
                        elif pixel_format == 'XBGR8888':
                            # Format: 0xXXBBGGRR (little-endian)
                            r, g, b, _ = row_data[pixel_start:pixel_start+4]
                            img_data[y, x] = [r, g, b, 255]
                        elif pixel_format in ['ARGB8888', 'ABGR8888']:
                            # Handle alpha channel
                            if pixel_format == 'ARGB8888':
                                # Format: 0xAARRGGBB (little-endian)
                                b, g, r, a = row_data[pixel_start:pixel_start+4]
                            else:  # ABGR8888
                                # Format: 0xAABBGGRR (little-endian)
                                r, g, b, a = row_data[pixel_start:pixel_start+4]
                            img_data[y, x] = [r, g, b, a]
                        elif pixel_format == 'RGB565':
                            # Format: 16-bit RGB565
                            pixel = struct.unpack('<H', row_data[pixel_start:pixel_start+2])[0]
                            r = ((pixel >> 11) & 0x1F) << 3
                            g = ((pixel >> 5) & 0x3F) << 2
                            b = (pixel & 0x1F) << 3
                            img_data[y, x] = [r, g, b, 255]
                        else:
                            # Default handling: just use BGRA order
                            b, g, r, a = row_data[pixel_start:pixel_start+4]
                            img_data[y, x] = [r, g, b, a]
        
        # Create PIL Image and save
        img = Image.fromarray(img_data)
        img.save(output_path)
        print(f"Image saved successfully to {output_path}")
        
    except Exception as e:
        print(f"Error processing image: {e}")
        sys.exit(1)

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <output_image>")
        sys.exit(1)
    
    output_path = sys.argv[1]
    process_framebuffer(output_path)

if __name__ == "__main__":
    main()
