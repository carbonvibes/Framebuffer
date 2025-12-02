#!/usr/bin/python3
"""
Real-Time Framebuffer to Image Converter
Converts captured framebuffer data to viewable images
"""

import struct
import sys
import os
import argparse
from PIL import Image
import numpy as np

def read_capture_info():
    """Read capture information from proc file"""
    try:
        with open('/proc/drm_fb_realtime', 'r') as f:
            content = f.read()
        
        captures = []
        current_capture = {}
        
        for line in content.split('\n'):
            line = line.strip()
            if line.startswith('Capture '):
                if current_capture:
                    captures.append(current_capture)
                current_capture = {'id': line.split(':')[0]}
            elif ':' in line and current_capture:
                key, value = line.split(':', 1)
                key = key.strip()
                value = value.strip()
                
                if key == 'Dimensions':
                    width, height = value.split('x')
                    current_capture['width'] = int(width)
                    current_capture['height'] = int(height)
                elif key == 'Format':
                    current_capture['format'] = value.split('(')[0].strip()
                elif key == 'Buffer size':
                    current_capture['buffer_size'] = int(value.split()[0])
                elif key == 'Pixel data':
                    current_capture['has_pixels'] = 'AVAILABLE' in value
                elif key == 'Capture method':
                    current_capture['method'] = value
                elif key == 'Timestamp':
                    current_capture['timestamp'] = value
        
        if current_capture:
            captures.append(current_capture)
        
        return captures
    except Exception as e:
        print(f"Error reading capture info: {e}")
        return []

def extract_framebuffer():
    """Extract raw framebuffer data"""
    try:
        with open('/proc/drm_fb_realtime_raw', 'rb') as f:
            return f.read()
    except Exception as e:
        print(f"Error reading framebuffer data: {e}")
        return None

def convert_to_image(raw_data, width, height, output_file):
    """Convert raw RGBA data to image"""
    try:
        # Ensure we have enough data
        expected_size = width * height * 4
        if len(raw_data) < expected_size:
            print(f"Warning: Got {len(raw_data)} bytes, expected {expected_size}")
            # Pad with zeros if needed
            raw_data += b'\x00' * (expected_size - len(raw_data))
        
        # Convert to numpy array
        pixel_array = np.frombuffer(raw_data[:expected_size], dtype=np.uint8)
        pixel_array = pixel_array.reshape((height, width, 4))
        
        # RGBA to RGB (drop alpha channel)
        rgb_array = pixel_array[:, :, :3]
        
        # Create PIL image
        image = Image.fromarray(rgb_array, 'RGB')
        
        # Save image
        image.save(output_file)
        print(f"✅ Saved image: {output_file} ({width}x{height})")
        
        return True
    except Exception as e:
        print(f"Error converting to image: {e}")
        return False

def analyze_pixel_data(raw_data, width, height):
    """Analyze pixel data for debugging"""
    if len(raw_data) < 16:
        print("Not enough data for analysis")
        return
    
    print("\n=== Pixel Data Analysis ===")
    print(f"Data size: {len(raw_data)} bytes")
    print(f"Expected size: {width * height * 4} bytes")
    
    # Show first few pixels
    print("First 4 pixels (RGBA):")
    for i in range(min(4, len(raw_data) // 4)):
        offset = i * 4
        r, g, b, a = struct.unpack('BBBB', raw_data[offset:offset+4])
        print(f"  Pixel {i}: R={r:3d} G={g:3d} B={b:3d} A={a:3d} (0x{r:02x}{g:02x}{b:02x}{a:02x})")
    
    # Basic statistics
    pixel_array = np.frombuffer(raw_data[:width*height*4], dtype=np.uint8)
    print(f"\nStatistics:")
    print(f"  Min value: {pixel_array.min()}")
    print(f"  Max value: {pixel_array.max()}")
    print(f"  Mean value: {pixel_array.mean():.1f}")
    print(f"  Non-zero pixels: {np.count_nonzero(pixel_array)}")

def main():
    parser = argparse.ArgumentParser(description='Real-Time Framebuffer to Image Converter')
    parser.add_argument('output', nargs='?', default='framebuffer.png', 
                       help='Output image file (default: framebuffer.png)')
    parser.add_argument('--list', action='store_true', 
                       help='List available captures')
    parser.add_argument('--analyze', action='store_true', 
                       help='Analyze pixel data')
    parser.add_argument('--format', choices=['png', 'jpg', 'bmp'], default='png',
                       help='Output image format')
    
    args = parser.parse_args()
    
    # Check if running as root
    if os.geteuid() != 0:
        print("❌ This script requires root privileges to access /proc files")
        print("💡 Run with: sudo python3 fb_to_image.py")
        sys.exit(1)
    
    # Read capture information
    captures = read_capture_info()
    if not captures:
        print("❌ No captures found. Is the module loaded?")
        sys.exit(1)
    
    if args.list:
        print("=== Available Captures ===")
        for capture in captures:
            print(f"{capture.get('id', 'Unknown')}: "
                  f"{capture.get('width', 0)}x{capture.get('height', 0)} "
                  f"({capture.get('method', 'Unknown')}) "
                  f"- {'✅' if capture.get('has_pixels') else '❌'}")
        return
    
    # Find the most recent capture with pixel data
    recent_capture = None
    for capture in reversed(captures):
        if capture.get('has_pixels'):
            recent_capture = capture
            break
    
    if not recent_capture:
        print("❌ No captures with pixel data found")
        print("💡 Try triggering display updates (move windows, play video)")
        sys.exit(1)
    
    print(f"Using {recent_capture.get('id', 'capture')}: "
          f"{recent_capture.get('width')}x{recent_capture.get('height')} "
          f"({recent_capture.get('method')})")
    
    # Extract raw data
    raw_data = extract_framebuffer()
    if not raw_data:
        print("❌ Failed to extract framebuffer data")
        sys.exit(1)
    
    width = recent_capture.get('width', 0)
    height = recent_capture.get('height', 0)
    
    if args.analyze:
        analyze_pixel_data(raw_data, width, height)
    
    # Convert to image
    if width > 0 and height > 0:
        output_file = args.output
        if not output_file.endswith(f'.{args.format}'):
            output_file = f"{output_file}.{args.format}"
        
        success = convert_to_image(raw_data, width, height, output_file)
        if success:
            print(f"🎉 Successfully converted framebuffer to {output_file}")
        else:
            print("❌ Failed to convert framebuffer to image")
    else:
        print("❌ Invalid dimensions found in capture")

if __name__ == '__main__':
    main()
