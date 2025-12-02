#!/usr/bin/env python3
"""
Intel Y-Tiled Framebuffer Detiler
This script converts Intel Y-tiled framebuffer data to linear format
for proper display of multi-monitor setups.
"""

import sys
import os
import re
import struct
import numpy as np
from PIL import Image

# Intel Y-tile dimensions (128×32 pixels)
TILE_WIDTH = 128   # pixels
TILE_HEIGHT = 32   # pixels

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

def get_y_tiled_offset(x, y, pitch):
    """
    Calculate the byte offset for a pixel in Intel Y-tiled format.
    Based on documentation of Intel GPU tiling formats.
    
    Args:
        x: X coordinate in pixels
        y: Y coordinate in pixels
        pitch: Row pitch in bytes
    
    Returns:
        Byte offset of the pixel in the tiled buffer
    """
    # For 32bpp formats
    bytes_per_pixel = 4
    
    # Convert x,y to byte coordinates
    x_bytes = x * bytes_per_pixel
    
    # Calculate the tile position
    tile_width_bytes = TILE_WIDTH * bytes_per_pixel
    tile_height = TILE_HEIGHT
    
    # Tile numbers
    tile_x = x_bytes // tile_width_bytes
    tile_y = y // tile_height
    
    # Offset within the tile
    x_offset_bytes = x_bytes % tile_width_bytes
    y_offset = y % tile_height
    
    # Calculate the tile's starting offset
    tiles_per_row = pitch // tile_width_bytes
    tile_start = (tile_y * tiles_per_row + tile_x) * (tile_width_bytes * tile_height)
    
    # Calculate the offset within the tile (using the Y-tiled swizzling pattern)
    # The exact formula depends on the specific tiling format, but this
    # is a simplified approach for Y tiling
    row_offset = y_offset * tile_width_bytes
    
    # Combine the offsets to get the final position
    return tile_start + row_offset + x_offset_bytes




def detile_framebuffer(raw_data, width, height, pitch, pixel_format, output_path):
    """Convert tiled framebuffer data to linear format and save as image"""
    try:
        # Create output image
        img_data = np.zeros((height, width, 4), dtype=np.uint8)
        bytes_per_pixel = 4  # Assuming XRGB8888 or similar
        
        print(f"Detiling {width}x{height} framebuffer from Y-tiled format...")
        
        # Process each pixel
        for y in range(height):
            for x in range(width):
                # Get tiled offset
                tiled_offset = get_y_tiled_offset(x, y, pitch)
                
                # Check bounds
                if tiled_offset + bytes_per_pixel <= len(raw_data):
                    # Extract pixel based on format
                    if pixel_format == 'XRGB8888':
                        b, g, r, _ = raw_data[tiled_offset:tiled_offset+4]
                        img_data[y, x] = [r, g, b, 255]
                    elif pixel_format == 'XBGR8888':
                        r, g, b, _ = raw_data[tiled_offset:tiled_offset+4]
                        img_data[y, x] = [r, g, b, 255]
                    elif pixel_format in ['ARGB8888', 'ABGR8888']:
                        if pixel_format == 'ARGB8888':
                            b, g, r, a = raw_data[tiled_offset:tiled_offset+4]
                        else:  # ABGR8888
                            r, g, b, a = raw_data[tiled_offset:tiled_offset+4]
                        img_data[y, x] = [r, g, b, a]
                    else:
                        # Default handling
                        b, g, r, a = raw_data[tiled_offset:tiled_offset+4]
                        img_data[y, x] = [r, g, b, a]
        
        # Create and save image
        img = Image.fromarray(img_data)
        img.save(output_path)
        print(f"Saved detiled image to {output_path}")
        
    except Exception as e:
        print(f"Error detiling framebuffer: {e}")
        import traceback
        traceback.print_exc()

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <output_image>")
        sys.exit(1)
    
    output_path = sys.argv[1]
    
    # Get framebuffer info
    width, height, pitch, pixel_format = extract_fb_info()
    print(f"Framebuffer: {width}x{height}, pitch={pitch}, format={pixel_format}")
    
    # Read raw data
    try:
        with open('/proc/drm_fb_raw', 'rb') as f:
            raw_data = f.read()
        print(f"Read {len(raw_data)} bytes of raw data")
    except Exception as e:
        print(f"Error reading raw data: {e}")
        sys.exit(1)
    
    # Detile and save
    detile_framebuffer(raw_data, width, height, pitch, pixel_format, output_path)

if __name__ == "__main__":
    main()
