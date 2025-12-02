#!/usr/bin/env python3
"""
DRM Framebuffer Data Extractor
Extracts raw framebuffer data from /proc/drm_fb_raw and converts it to viewable images
"""

import struct
import os
import sys
from PIL import Image

def parse_fb_info():
    """Parse framebuffer information from /proc/drm_fb_pixels"""
    captures = []
    
    try:
        with open('/proc/drm_fb_pixels', 'r') as f:
            content = f.read()
    except PermissionError:
        print("Error: Need sudo permissions to read /proc/drm_fb_pixels")
        return captures
    except FileNotFoundError:
        print("Error: /proc/drm_fb_pixels not found. Is the kernel module loaded?")
        return captures
    
    lines = content.split('\n')
    current_capture = {}
    
    for line in lines:
        line = line.strip()
        if line.startswith('Capture '):
            if current_capture:
                captures.append(current_capture)
            current_capture = {'id': int(line.split()[1].rstrip(':'))}
        elif line.startswith('Dimensions:'):
            dims = line.split(':')[1].strip()
            width, height = map(int, dims.split('x'))
            current_capture['width'] = width
            current_capture['height'] = height
        elif line.startswith('Format:'):
            format_info = line.split(':')[1].strip()
            format_hex = format_info.split()[0]
            format_name = format_info.split('(')[1].rstrip(')')
            current_capture['format_hex'] = format_hex
            current_capture['format_name'] = format_name
        elif line.startswith('Pitch:'):
            pitch = int(line.split(':')[1].strip().split()[0])
            current_capture['pitch'] = pitch
        elif line.startswith('Buffer size:'):
            size = int(line.split(':')[1].strip().split()[0])
            current_capture['buffer_size'] = size
        elif line.startswith('Pixel data:'):
            available = line.split(':')[1].strip() == 'AVAILABLE'
            current_capture['pixel_available'] = available
        elif line.startswith('Timestamp:'):
            timestamp = int(line.split(':')[1].strip().split()[0])
            current_capture['timestamp'] = timestamp
    
    if current_capture:
        captures.append(current_capture)
    
    return captures

def extract_raw_data(buffer_size):
    """Extract raw pixel data from /proc/drm_fb_raw"""
    try:
        with open('/proc/drm_fb_raw', 'rb') as f:
            data = f.read(buffer_size)
        return data
    except PermissionError:
        print("Error: Need sudo permissions to read /proc/drm_fb_raw")
        return None
    except FileNotFoundError:
        print("Error: /proc/drm_fb_raw not found. Is the kernel module loaded?")
        return None

def convert_pixel_format(raw_data, width, height, format_name, pitch):
    """Convert raw pixel data to RGB format based on the pixel format"""
    
    if format_name == 'XRGB8888':
        # XRGB8888: 32-bit, X=unused, R=red, G=green, B=blue
        # Layout: [B, G, R, X] (little endian)
        pixels = []
        for y in range(height):
            row_offset = y * pitch
            for x in range(width):
                pixel_offset = row_offset + (x * 4)
                if pixel_offset + 3 < len(raw_data):
                    b, g, r, x_unused = struct.unpack_from('<BBBB', raw_data, pixel_offset)
                    pixels.append((r, g, b))
                else:
                    pixels.append((0, 0, 0))  # Black for missing data
        
        # Create PIL image
        img = Image.new('RGB', (width, height))
        img.putdata(pixels)
        return img
    
    elif format_name == 'ARGB8888':
        # ARGB8888: 32-bit, A=alpha, R=red, G=green, B=blue
        # Layout: [B, G, R, A] (little endian)
        pixels = []
        for y in range(height):
            row_offset = y * pitch
            for x in range(width):
                pixel_offset = row_offset + (x * 4)
                if pixel_offset + 3 < len(raw_data):
                    b, g, r, a = struct.unpack_from('<BBBB', raw_data, pixel_offset)
                    pixels.append((r, g, b, a))
                else:
                    pixels.append((0, 0, 0, 255))  # Black with full alpha
        
        # Create PIL image with alpha
        img = Image.new('RGBA', (width, height))
        img.putdata(pixels)
        return img
    
    elif format_name == 'RGB565':
        # RGB565: 16-bit, 5-bit red, 6-bit green, 5-bit blue
        pixels = []
        for y in range(height):
            row_offset = y * pitch
            for x in range(width):
                pixel_offset = row_offset + (x * 2)
                if pixel_offset + 1 < len(raw_data):
                    pixel_val = struct.unpack_from('<H', raw_data, pixel_offset)[0]
                    r = ((pixel_val >> 11) & 0x1F) << 3  # 5 bits -> 8 bits
                    g = ((pixel_val >> 5) & 0x3F) << 2   # 6 bits -> 8 bits
                    b = (pixel_val & 0x1F) << 3          # 5 bits -> 8 bits
                    pixels.append((r, g, b))
                else:
                    pixels.append((0, 0, 0))
        
        img = Image.new('RGB', (width, height))
        img.putdata(pixels)
        return img
    
    else:
        print(f"Unsupported pixel format: {format_name}")
        return None

def save_framebuffer_images():
    """Main function to extract and save framebuffer images"""
    print("DRM Framebuffer Data Extractor")
    print("=" * 40)
    
    # Parse framebuffer information
    captures = parse_fb_info()
    if not captures:
        print("No framebuffer captures found!")
        return
    
    print(f"Found {len(captures)} framebuffer captures:")
    for i, capture in enumerate(captures):
        print(f"  Capture {capture['id']}: {capture['width']}x{capture['height']} "
              f"({capture['format_name']}) - {capture['buffer_size']} bytes")
    
    print("\nExtracting images...")
    
    # Extract raw data (assumes the /proc/drm_fb_raw contains the most recent capture)
    for i, capture in enumerate(captures):
        if not capture.get('pixel_available', False):
            print(f"Skipping capture {capture['id']} - no pixel data available")
            continue
            
        print(f"\nProcessing capture {capture['id']}...")
        
        # For this implementation, we'll extract from the raw proc file
        # In reality, you might need to seek to specific offsets if multiple captures are stored
        raw_data = extract_raw_data(capture['buffer_size'])
        if raw_data is None:
            continue
        
        if len(raw_data) < capture['buffer_size']:
            print(f"Warning: Only got {len(raw_data)} bytes, expected {capture['buffer_size']}")
        
        # Convert to image
        img = convert_pixel_format(
            raw_data, 
            capture['width'], 
            capture['height'], 
            capture['format_name'],
            capture['pitch']
        )
        
        if img:
            # Save image
            filename = f"framebuffer_capture_{capture['id']}_{capture['width']}x{capture['height']}_{capture['format_name']}.png"
            img.save(filename)
            print(f"Saved: {filename}")
            
            # Also save a smaller preview if it's a large image
            if capture['width'] > 1920 or capture['height'] > 1080:
                preview = img.resize((capture['width']//4, capture['height']//4), Image.LANCZOS)
                preview_filename = f"framebuffer_preview_{capture['id']}.png"
                preview.save(preview_filename)
                print(f"Saved preview: {preview_filename}")
        else:
            print(f"Failed to convert capture {capture['id']}")
    
    print("\nDone! Check the current directory for generated images.")

if __name__ == "__main__":
    # Check if PIL is available
    try:
        from PIL import Image
    except ImportError:
        print("Error: PIL (Pillow) is required. Install with: pip install Pillow")
        sys.exit(1)
    
    if os.geteuid() != 0:
        print("Note: You may need to run this script with sudo to access /proc files")
    
    save_framebuffer_images()
