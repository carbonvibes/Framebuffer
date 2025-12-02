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

def convert_pixel_format_with_stride(data, width, height, pitch, format_name):
    """Convert pixel formats respecting stride/pitch correctly"""
    
    print(f"Converting format: {format_name}")
    print(f"  Width: {width}, Height: {height}")
    print(f"  Pitch: {pitch} bytes/row")
    print(f"  Expected pitch: {width * 4} bytes/row")
    print(f"  Data size: {len(data)} bytes")
    
    # Calculate bytes per pixel
    if format_name in ['XRGB8888', 'ARGB8888']:
        bpp = 4
    elif format_name == 'RGB565':
        bpp = 2
    else:
        print(f"Unknown format {format_name}, assuming 4 bpp")
        bpp = 4
    
    pixels = []
    
    # Check if we have enough data
    expected_size = height * pitch
    if len(data) < expected_size:
        print(f"WARNING: Not enough data! Have {len(data)}, need {expected_size}")
        # Pad with zeros
        data = data + b'\x00' * (expected_size - len(data))
    
    try:
        if format_name in ['XRGB8888', 'ARGB8888']:
            # Process row by row, respecting the pitch
            for y in range(height):
                row_start = y * pitch
                
                # Process each pixel in this row
                for x in range(width):
                    pixel_offset = row_start + (x * bpp)
                    
                    if pixel_offset + bpp > len(data):
                        # Pad with black
                        pixels.extend([0, 0, 0])
                        continue
                    
                    # Extract BGRA/BGRX (little-endian)
                    b = data[pixel_offset]
                    g = data[pixel_offset + 1]
                    r = data[pixel_offset + 2]
                    # a = data[pixel_offset + 3]  # Alpha/X
                    
                    pixels.extend([r, g, b])
                    
        elif format_name == 'RGB565':
            for y in range(height):
                row_start = y * pitch
                for x in range(width):
                    pixel_offset = row_start + (x * bpp)
                    
                    if pixel_offset + bpp > len(data):
                        pixels.extend([0, 0, 0])
                        continue
                    
                    # Little-endian 16-bit
                    pixel16 = data[pixel_offset] | (data[pixel_offset + 1] << 8)
                    
                    r = ((pixel16 >> 11) & 0x1F) << 3
                    g = ((pixel16 >> 5) & 0x3F) << 2
                    b = (pixel16 & 0x1F) << 3
                    
                    pixels.extend([r, g, b])
        else:
            # Fallback to XRGB8888
            for y in range(height):
                row_start = y * pitch
                for x in range(width):
                    pixel_offset = row_start + (x * 4)
                    
                    if pixel_offset + 4 > len(data):
                        pixels.extend([0, 0, 0])
                        continue
                    
                    b = data[pixel_offset]
                    g = data[pixel_offset + 1]
                    r = data[pixel_offset + 2]
                    
                    pixels.extend([r, g, b])
                    
    except Exception as e:
        print(f"Error converting pixels: {e}")
        return None
    
    return pixels

def create_debug_images(data, width, height, pitch, format_name, base_name):
    """Create multiple interpretations for debugging"""
    
    images_created = []
    
    # 1. Standard interpretation
    print("  Creating standard interpretation...")
    pixels = convert_pixel_format_with_stride(data, width, height, pitch, format_name)
    if pixels:
        try:
            img = Image.new('RGB', (width, height))
            img.putdata([(pixels[j], pixels[j+1], pixels[j+2]) 
                        for j in range(0, len(pixels), 3)])
            
            filename = f"{base_name}_standard.png"
            img.save(filename, "PNG")
            print(f"    Saved: {filename}")
            images_created.append(filename)
        except Exception as e:
            print(f"    Error: {e}")
    
    # 2. Half-width interpretation (for dual monitor setups)
    if width >= 3840:  # Likely dual monitor
        print("  Creating half-width interpretation (dual monitor)...")
        half_width = width // 2
        double_height = height * 2
        
        # Check if this makes sense
        if double_height * pitch <= len(data):
            pixels = convert_pixel_format_with_stride(data, half_width, double_height, pitch, format_name)
            if pixels:
                try:
                    img = Image.new('RGB', (half_width, double_height))
                    img.putdata([(pixels[j], pixels[j+1], pixels[j+2]) 
                                for j in range(0, len(pixels), 3)])
                    
                    filename = f"{base_name}_half_width.png"
                    img.save(filename, "PNG")
                    print(f"    Saved: {filename}")
                    images_created.append(filename)
                except Exception as e:
                    print(f"    Error: {e}")
    
    # 3. Try different stride interpretations
    if pitch != width * 4:
        print("  Creating width-adjusted interpretation...")
        # Calculate effective width based on pitch
        effective_width = pitch // 4
        effective_height = (len(data) // pitch)
        
        if effective_height > 0:
            pixels = convert_pixel_format_with_stride(data, effective_width, effective_height, pitch, format_name)
            if pixels:
                try:
                    img = Image.new('RGB', (effective_width, effective_height))
                    img.putdata([(pixels[j], pixels[j+1], pixels[j+2]) 
                                for j in range(0, len(pixels), 3)])
                    
                    filename = f"{base_name}_stride_adjusted.png"
                    img.save(filename, "PNG")
                    print(f"    Saved: {filename}")
                    images_created.append(filename)
                except Exception as e:
                    print(f"    Error: {e}")
    
    # 4. Try to split into two monitors (side by side)
    if width == 3840 and height == 1080:
        print("  Creating split-monitor interpretation...")
        monitor_width = 1920
        monitor_height = 1080
        
        try:
            # Left monitor
            left_pixels = []
            for y in range(monitor_height):
                row_start = y * pitch
                for x in range(monitor_width):
                    pixel_offset = row_start + (x * 4)
                    if pixel_offset + 4 <= len(data):
                        b = data[pixel_offset]
                        g = data[pixel_offset + 1]
                        r = data[pixel_offset + 2]
                        left_pixels.extend([r, g, b])
                    else:
                        left_pixels.extend([0, 0, 0])
            
            left_img = Image.new('RGB', (monitor_width, monitor_height))
            left_img.putdata([(left_pixels[j], left_pixels[j+1], left_pixels[j+2]) 
                            for j in range(0, len(left_pixels), 3)])
            
            left_filename = f"{base_name}_left_monitor.png"
            left_img.save(left_filename, "PNG")
            print(f"    Saved: {left_filename}")
            images_created.append(left_filename)
            
            # Right monitor
            right_pixels = []
            for y in range(monitor_height):
                row_start = y * pitch
                for x in range(monitor_width, width):
                    pixel_offset = row_start + (x * 4)
                    if pixel_offset + 4 <= len(data):
                        b = data[pixel_offset]
                        g = data[pixel_offset + 1]
                        r = data[pixel_offset + 2]
                        right_pixels.extend([r, g, b])
                    else:
                        right_pixels.extend([0, 0, 0])
            
            right_img = Image.new('RGB', (monitor_width, monitor_height))
            right_img.putdata([(right_pixels[j], right_pixels[j+1], right_pixels[j+2]) 
                             for j in range(0, len(right_pixels), 3)])
            
            right_filename = f"{base_name}_right_monitor.png"
            right_img.save(right_filename, "PNG")
            print(f"    Saved: {right_filename}")
            images_created.append(right_filename)
            
        except Exception as e:
            print(f"    Error creating split monitors: {e}")
    
    return images_created

def extract_and_save_images():
    """Extract framebuffer data and save as images with multiple interpretations"""
    
    # Parse framebuffer info
    framebuffers = parse_fb_info()
    
    if not framebuffers:
        print("No framebuffers found with available data")
        return
    
    print(f"Found {len(framebuffers)} framebuffer(s)")
    
    # Only process the most recent framebuffer to avoid clutter
    for i, fb in enumerate(framebuffers[-1:], len(framebuffers)-1):
        if not fb.get('has_data', False):
            print(f"Framebuffer {i}: No pixel data available")
            continue
            
        width = fb['width']
        height = fb['height'] 
        pitch = fb['pitch']
        buffer_size = fb['buffer_size']
        format_name = fb.get('format', 'UNKNOWN')
        
        print(f"\nProcessing Framebuffer {i} (most recent):")
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
        
        # Create multiple interpretations
        base_name = f"fb_{i}_{width}x{height}_{format_name}"
        created_images = create_debug_images(raw_data, width, height, pitch, format_name, base_name)
        
        if created_images:
            print(f"\nCreated {len(created_images)} image interpretations:")
            for img in created_images:
                print(f"  - {img}")
        else:
            print("  Failed to create any images")
    
    print("\nDone! Check the generated image files to see which interpretation looks correct.")

if __name__ == "__main__":
    extract_and_save_images()
