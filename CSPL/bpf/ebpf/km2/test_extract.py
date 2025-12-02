#!/usr/bin/env python3

import os
import sys
from PIL import Image

def test_simple_extraction():
    """Simple test to extract just a small portion"""
    
    print("Testing simple framebuffer extraction...")
    
    # Read just a small amount first
    try:
        with open('/proc/drm_fb_raw', 'rb') as f:
            # Read first 1MB
            data = f.read(1024 * 1024)
            print(f"Read {len(data)} bytes")
            
        if len(data) < 1024:
            print("Not enough data read")
            return
            
        # Try to create a small test image (100x100)
        width, height = 100, 100
        pitch = width * 4
        
        pixels = []
        for y in range(height):
            for x in range(width):
                offset = (y * pitch) + (x * 4)
                if offset + 4 <= len(data):
                    b = data[offset]
                    g = data[offset + 1] 
                    r = data[offset + 2]
                    pixels.extend([r, g, b])
                else:
                    pixels.extend([0, 0, 0])
        
        # Create image
        img = Image.new('RGB', (width, height))
        img.putdata([(pixels[j], pixels[j+1], pixels[j+2]) 
                    for j in range(0, len(pixels), 3)])
        
        img.save("test_small.png", "PNG")
        print("Saved: test_small.png")
        
        # Also try creating a strip from the actual framebuffer dimensions
        # 3840x1 strip (one row)
        width, height = 3840, 1
        pitch = 15360  # From the proc file info
        
        pixels = []
        for x in range(width):
            offset = x * 4
            if offset + 4 <= len(data):
                b = data[offset]
                g = data[offset + 1]
                r = data[offset + 2] 
                pixels.extend([r, g, b])
            else:
                pixels.extend([0, 0, 0])
                
        img = Image.new('RGB', (width, height))
        img.putdata([(pixels[j], pixels[j+1], pixels[j+2]) 
                    for j in range(0, len(pixels), 3)])
        
        img.save("test_strip.png", "PNG")
        print("Saved: test_strip.png")
        
        # Try with proper stride
        pixels = []
        y = 0  # First row
        row_start = y * pitch
        for x in range(width):
            offset = row_start + (x * 4)
            if offset + 4 <= len(data):
                b = data[offset]
                g = data[offset + 1]
                r = data[offset + 2]
                pixels.extend([r, g, b])
            else:
                pixels.extend([255, 0, 0])  # Red for missing data
                
        img = Image.new('RGB', (width, height))
        img.putdata([(pixels[j], pixels[j+1], pixels[j+2]) 
                    for j in range(0, len(pixels), 3)])
        
        img.save("test_strip_stride.png", "PNG")
        print("Saved: test_strip_stride.png")
        
        # Try first 10 rows
        height = 10
        pixels = []
        for y in range(height):
            row_start = y * pitch
            for x in range(width):
                offset = row_start + (x * 4)
                if offset + 4 <= len(data):
                    b = data[offset]
                    g = data[offset + 1]
                    r = data[offset + 2]
                    pixels.extend([r, g, b])
                else:
                    pixels.extend([255, 0, 0])  # Red for missing data
                    
        img = Image.new('RGB', (width, height))
        img.putdata([(pixels[j], pixels[j+1], pixels[j+2]) 
                    for j in range(0, len(pixels), 3)])
        
        img.save("test_10_rows.png", "PNG")
        print("Saved: test_10_rows.png")
        
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    test_simple_extraction()
