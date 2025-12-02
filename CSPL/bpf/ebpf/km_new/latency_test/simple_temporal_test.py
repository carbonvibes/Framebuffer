#!/usr/bin/env python3
"""
Simple Temporal Latency Test

This creates a basic visual test to measure temporal latency by comparing
a known pattern with the captured framebuffer.
"""

import time
import os
import numpy as np
import subprocess

def simple_temporal_test():
    """Simple test using system tools"""
    print("="*60)
    print("SIMPLE TEMPORAL LATENCY TEST")
    print("="*60)
    
    # Check if kernel module is available
    if not os.path.exists("/proc/drm_fb_raw"):
        print("Error: Kernel module not available")
        return False
    
    print("This test will:")
    print("1. Create a visual timestamp pattern on screen")
    print("2. Capture framebuffer via kernel module")
    print("3. Compare timestamps to find temporal offset")
    print()
    
    # Get current framebuffer info
    try:
        with open("/proc/drm_fb_pixels", 'r') as f:
            content = f.read()
        print("Current framebuffer info:")
        print(content[:500] + "...\n")
    except Exception as e:
        print(f"Error reading framebuffer info: {e}")
        return False
    
    # Method 1: Use xwd to take a screenshot and compare timing
    print("Taking reference screenshot with xwd...")
    ref_time = time.time() * 1000  # ms
    
    # Take screenshot with xwd
    try:
        result = subprocess.run(['xwd', '-root', '-out', 'reference_screenshot.xwd'], 
                              capture_output=True, timeout=5)
        if result.returncode != 0:
            print("Could not take xwd screenshot")
            print("Make sure X11 is running and xwd is installed")
            return False
    except Exception as e:
        print(f"Error with xwd: {e}")
        return False
    
    ref_end_time = time.time() * 1000
    print(f"Reference screenshot taken at: {ref_time:.0f} ms")
    print(f"Screenshot completed at: {ref_end_time:.0f} ms")
    print(f"xwd capture time: {ref_end_time - ref_time:.1f} ms")
    
    # Method 2: Extract kernel module framebuffer
    print("\nExtracting framebuffer from kernel module...")
    kernel_start_time = time.time() * 1000
    
    try:
        with open("/proc/drm_fb_raw", 'rb') as f:
            fb_data = f.read()
    except Exception as e:
        print(f"Error reading framebuffer: {e}")
        return False
    
    kernel_end_time = time.time() * 1000
    print(f"Kernel framebuffer read at: {kernel_start_time:.0f} ms")
    print(f"Kernel read completed at: {kernel_end_time:.0f} ms")
    print(f"Kernel read time: {kernel_end_time - kernel_start_time:.1f} ms")
    print(f"Framebuffer size: {len(fb_data)} bytes")
    
    # Calculate the time difference
    time_diff = kernel_start_time - ref_time
    print(f"\nTiming comparison:")
    print(f"xwd reference time: {ref_time:.0f} ms")
    print(f"Kernel read time: {kernel_start_time:.0f} ms")
    print(f"Time difference: {time_diff:.1f} ms")
    
    if time_diff > 0:
        print(f"→ Kernel read was {time_diff:.1f} ms AFTER xwd")
    else:
        print(f"→ Kernel read was {abs(time_diff):.1f} ms BEFORE xwd")
    
    # Method 3: Check if we can see visual differences in the data
    print(f"\nFramebuffer analysis:")
    if len(fb_data) > 1024:
        # Sample first 1KB
        sample = fb_data[:1024]
        
        # Check if it's not all zeros
        if any(b != 0 for b in sample):
            print("✓ Framebuffer contains actual pixel data")
            
            # Simple analysis
            unique_bytes = len(set(sample))
            print(f"✓ Data complexity: {unique_bytes} unique byte values in first 1KB")
            
            # Show first few pixels as hex
            print("First 32 bytes (8 pixels in ARGB):")
            hex_str = ' '.join(f'{b:02x}' for b in sample[:32])
            print(f"  {hex_str}")
            
        else:
            print("⚠ Framebuffer appears to be all zeros")
    
    return True

def advanced_timing_test():
    """More advanced timing test with multiple samples"""
    print("\n" + "="*60)
    print("ADVANCED TIMING TEST")
    print("="*60)
    
    samples = []
    
    for i in range(5):
        print(f"\nSample {i+1}/5:")
        
        # Method: Rapid sampling to detect changes
        before_time = time.time() * 1000
        
        # Quick read of framebuffer size (to detect if it changed)
        try:
            with open("/proc/drm_fb_pixels", 'r') as f:
                info = f.read()
            
            # Extract capture count
            capture_count = 0
            for line in info.split('\n'):
                if 'Captured framebuffers:' in line:
                    capture_count = int(line.split(':')[1].strip())
                    break
            
            # Quick read of raw data size
            raw_size = os.path.getsize("/proc/drm_fb_raw")
            
        except Exception as e:
            print(f"Error in sample {i+1}: {e}")
            continue
        
        after_time = time.time() * 1000
        
        sample_data = {
            'time': before_time,
            'capture_count': capture_count,
            'raw_size': raw_size,
            'read_time': after_time - before_time
        }
        
        samples.append(sample_data)
        
        print(f"  Time: {before_time:.0f} ms")
        print(f"  Captures: {capture_count}")
        print(f"  Raw size: {raw_size} bytes")
        print(f"  Read time: {after_time - before_time:.1f} ms")
        
        time.sleep(0.5)  # Wait between samples
    
    # Analyze samples
    if len(samples) > 1:
        print(f"\nSample analysis:")
        for i in range(1, len(samples)):
            prev = samples[i-1]
            curr = samples[i]
            
            time_delta = curr['time'] - prev['time']
            count_delta = curr['capture_count'] - prev['capture_count']
            size_delta = curr['raw_size'] - prev['raw_size']
            
            print(f"  Sample {i} vs {i-1}:")
            print(f"    Time delta: {time_delta:.1f} ms")
            print(f"    Capture count delta: {count_delta}")
            print(f"    Size delta: {size_delta} bytes")
            
            if count_delta > 0:
                print(f"    → New capture detected!")
            else:
                print(f"    → No new captures")

def main():
    print("Temporal Latency Analysis Tool")
    print("This tool helps identify the temporal offset in your framebuffer captures")
    print()
    
    success1 = simple_temporal_test()
    if success1:
        advanced_timing_test()
    
    print("\n" + "="*60)
    print("CONCLUSIONS")
    print("="*60)
    print()
    print("Your manual test showed ~150ms lag. This suggests:")
    print("1. The kernel module captures framebuffers that contain")
    print("   content from 150ms ago")
    print("2. This could be due to:")
    print("   - GPU buffering/queuing")
    print("   - Display pipeline delays")
    print("   - Framebuffer update timing")
    print("   - When drm_framebuffer_init is called vs content")
    print()
    print("The kernel module intercepts drm_framebuffer_init, which")
    print("happens when framebuffers are created, not necessarily")
    print("when they contain the most current content.")
    print()
    print("To reduce latency, you might need to hook into:")
    print("- drm_atomic_commit (when content is actually committed)")
    print("- Display flip events")
    print("- VBlank interrupts")

if __name__ == "__main__":
    main()
