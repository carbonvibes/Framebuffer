#!/usr/bin/env python3

import os
import sys
from PIL import Image

def parse_fb_info():
    """Parse framebuffer information from proc file"""
    try:
        with open('/proc/drm_fb_pixels', 'r') as f:
            content = f.read()
    except FileNotFoundError:
        print("Error: /proc/drm_fb_pixels not found. Make sure the kernel module is loaded.")
        return []
    
    framebuffers = []
    lines = content.split('\n')
    current_fb = {}
    
    for line in lines:
        line = line.strip()
        if line.startswith('Capture '):
            if current_fb:
                framebuffers.append(current_fb)
            current_fb = {}
        elif 'Dimensions:' in line:
            dims = line.split(': ')[1]
            width, height = map(int, dims.split('x'))
            current_fb['width'] = width
            current_fb['height'] = height
        elif 'Format:' in line:
            format_info = line.split(': ')[1]
            if '(' in format_info:
                format_code = format_info.split('(')[1].split(')')[0]
                current_fb['format'] = format_code
            else:
                # Extract hex format code
                hex_part = format_info.split(' ')[0]
                current_fb['format_hex'] = hex_part
        elif 'Pitch:' in line:
            pitch = int(line.split(': ')[1].split(' ')[0])
            current_fb['pitch'] = pitch
        elif 'Buffer size:' in line:
            size = int(line.split(': ')[1].split(' ')[0])
            current_fb['buffer_size'] = size
        elif 'Pixel data: AVAILABLE' in line:
            current_fb['has_data'] = True
    
    if current_fb:
        framebuffers.append(current_fb)
    
    return framebuffers

def convert_pixel_format(data, width, height, pitch, format_name):
    """Convert different pixel formats to RGB"""
    
    print(f"Converting format: {format_name}, pitch: {pitch}, expected_pitch: {width * 4}")
    
    # Calculate bytes per pixel based on format
    if format_name in ['XRGB8888', 'ARGB8888']:
        bpp = 4
        expected_pitch = width * bpp
    elif format_name == 'RGB565':
        bpp = 2
        expected_pitch = width * bpp
    else:
        print(f"Unknown format {format_name}, assuming 4 bpp")
        bpp = 4
        expected_pitch = width * bpp
    
    # Check if pitch matches expected
    if pitch != expected_pitch:
        print(f"WARNING: Pitch mismatch! Expected {expected_pitch}, got {pitch}")
        print(f"This might cause stride issues. Trying with actual pitch...")
    
    pixels = []
    
    try:
        if format_name in ['XRGB8888', 'ARGB8888']:
            # 32-bit formats: each pixel is 4 bytes (B, G, R, A/X)
            for y in range(height):
                row_start = y * pitch
                for x in range(width):
                    pixel_start = row_start + (x * 4)
                    
                    if pixel_start + 3 >= len(data):
                        # Pad with black if we run out of data
                        pixels.extend([0, 0, 0])
                        continue
                    
                    # DRM formats are typically little-endian BGRA/BGRX
                    b = data[pixel_start]
                    g = data[pixel_start + 1] 
                    r = data[pixel_start + 2]
                    # a = data[pixel_start + 3]  # Alpha/X component
                    
                    pixels.extend([r, g, b])
                    
        elif format_name == 'RGB565':
            # 16-bit format: 5 bits red, 6 bits green, 5 bits blue
            for y in range(height):
                row_start = y * pitch
                for x in range(width):
                    pixel_start = row_start + (x * 2)
                    
                    if pixel_start + 1 >= len(data):
                        pixels.extend([0, 0, 0])
                        continue
                    
                    # Little-endian 16-bit value
                    pixel16 = data[pixel_start] | (data[pixel_start + 1] << 8)
                    
                    # Extract RGB components
                    r = ((pixel16 >> 11) & 0x1F) << 3  # 5 bits -> 8 bits
                    g = ((pixel16 >> 5) & 0x3F) << 2   # 6 bits -> 8 bits  
                    b = (pixel16 & 0x1F) << 3          # 5 bits -> 8 bits
                    
                    pixels.extend([r, g, b])
        else:
            # Fallback: assume XRGB8888
            for y in range(height):
                row_start = y * pitch
                for x in range(width):
                    pixel_start = row_start + (x * 4)
                    
                    if pixel_start + 3 >= len(data):
                        pixels.extend([0, 0, 0])
                        continue
                    
                    b = data[pixel_start]
                    g = data[pixel_start + 1]
                    r = data[pixel_start + 2]
                    
                    pixels.extend([r, g, b])
                    
    except Exception as e:
        print(f"Error converting pixels: {e}")
        return None
    
    return pixels

def extract_and_save_images():
    """Extract framebuffer data and save as images"""
    
    # Parse framebuffer info
    framebuffers = parse_fb_info()
    
    if not framebuffers:
        print("No framebuffers found with available data")
        return
    
    print(f"Found {len(framebuffers)} framebuffer(s)")
    
    for i, fb in enumerate(framebuffers):
        if not fb.get('has_data', False):
            print(f"Framebuffer {i}: No pixel data available")
            continue
            
        width = fb['width']
        height = fb['height'] 
        pitch = fb['pitch']
        buffer_size = fb['buffer_size']
        format_name = fb.get('format', 'UNKNOWN')
        
        print(f"\nProcessing Framebuffer {i}:")
        print(f"  Dimensions: {width}x{height}")
        print(f"  Format: {format_name}")
        print(f"  Pitch: {pitch} bytes/row")
        print(f"  Buffer size: {buffer_size} bytes")
        
        # Read raw data
        try:
            with open('/proc/drm_fb_raw', 'rb') as f:
                raw_data = f.read(buffer_size)
                
            if len(raw_data) != buffer_size:
                print(f"  WARNING: Read {len(raw_data)} bytes, expected {buffer_size}")
                
        except Exception as e:
            print(f"  Error reading raw data: {e}")
            continue
        
        # Convert pixel format
        rgb_pixels = convert_pixel_format(raw_data, width, height, pitch, format_name)
        
        if rgb_pixels is None:
            print(f"  Failed to convert pixel format")
            continue
            
        # Create and save image
        try:
            # Create PIL image from RGB data
            img = Image.new('RGB', (width, height))
            img.putdata([(rgb_pixels[j], rgb_pixels[j+1], rgb_pixels[j+2]) 
                        for j in range(0, len(rgb_pixels), 3)])
            
            # Save multiple formats for debugging
            base_name = f"framebuffer_{i}_{width}x{height}_{format_name}"
            
            # Save as PNG (lossless)
            png_file = f"{base_name}.png"
            img.save(png_file, "PNG")
            print(f"  Saved: {png_file}")
            
            # Save as JPG (for easier viewing)
            jpg_file = f"{base_name}.jpg"
            img.save(jpg_file, "JPEG", quality=95)
            print(f"  Saved: {jpg_file}")
            
            # Try different interpretations if the image looks wrong
            if width > 1000:  # Large framebuffer, try half-width interpretation
                print(f"  Trying alternative interpretation (half-width)...")
                try:
                    alt_width = width // 2
                    alt_height = height * 2
                    if alt_height * pitch <= buffer_size:
                        alt_pixels = convert_pixel_format(raw_data, alt_width, alt_height, pitch, format_name)
                        if alt_pixels:
                            alt_img = Image.new('RGB', (alt_width, alt_height))
                            alt_img.putdata([(alt_pixels[j], alt_pixels[j+1], alt_pixels[j+2]) 
                                           for j in range(0, len(alt_pixels), 3)])
                            alt_file = f"{base_name}_alt.png"
                            alt_img.save(alt_file, "PNG")
                            print(f"  Saved alternative: {alt_file}")
                except:
                    pass
            
        except Exception as e:
            print(f"  Error creating image: {e}")
            continue
    
    print("\nDone! Check the generated image files.")

if __name__ == "__main__":
    extract_and_save_images()
