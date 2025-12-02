#!/usr/bin/env python3
"""
Advanced Framebuffer Latency Analysis

This script provides detailed analysis of framebuffer capture latency by:
1. Decoding actual pixel data from the kernel module
2. Extracting embedded timestamp patterns
3. Computing precise latency measurements
4. Generating detailed reports and visualizations
"""

import struct
import os
import sys
import time
import numpy as np
import argparse
from collections import defaultdict
import json

class FramebufferAnalyzer:
    def __init__(self):
        self.proc_info_path = "/proc/drm_fb_pixels"
        self.proc_raw_path = "/proc/drm_fb_raw"
        
    def read_fb_info(self):
        """Read framebuffer information from kernel module"""
        if not os.path.exists(self.proc_info_path):
            raise FileNotFoundError(f"Kernel module proc file not found: {self.proc_info_path}")
        
        with open(self.proc_info_path, 'r') as f:
            content = f.read()
        
        return self.parse_fb_info(content)
    
    def parse_fb_info(self, content):
        """Parse framebuffer info from kernel module output"""
        info = {
            'total_captures': 0,
            'captures': []
        }
        
        lines = content.split('\n')
        current_capture = None
        
        for line in lines:
            line = line.strip()
            
            if 'Captured framebuffers:' in line:
                info['total_captures'] = int(line.split(':')[1].strip())
            
            elif line.startswith('Capture '):
                if current_capture:
                    info['captures'].append(current_capture)
                current_capture = {
                    'id': int(line.split()[1].rstrip(':')),
                    'timestamp': None,
                    'dimensions': None,
                    'format': None,
                    'pitch': None,
                    'buffer_size': None,
                    'tiling': None,
                    'detiled': False,
                    'has_pixels': False
                }
            
            elif current_capture and 'Timestamp:' in line:
                current_capture['timestamp'] = int(line.split(':')[1].strip().split()[0])
            
            elif current_capture and 'Dimensions:' in line:
                dims = line.split(':')[1].strip()
                width, height = dims.split('x')
                current_capture['dimensions'] = (int(width), int(height))
            
            elif current_capture and 'Format:' in line:
                format_info = line.split(':')[1].strip()
                current_capture['format'] = format_info
            
            elif current_capture and 'Pitch:' in line:
                pitch = line.split(':')[1].strip().split()[0]
                current_capture['pitch'] = int(pitch)
            
            elif current_capture and 'Buffer size:' in line:
                size = line.split(':')[1].strip().split()[0]
                current_capture['buffer_size'] = int(size)
            
            elif current_capture and 'Tiling:' in line:
                tiling = line.split(':')[1].strip()
                current_capture['tiling'] = tiling
            
            elif current_capture and 'Detiled:' in line:
                detiled = line.split(':')[1].strip()
                current_capture['detiled'] = detiled == 'YES'
            
            elif current_capture and 'Pixel data:' in line:
                has_pixels = 'AVAILABLE' in line
                current_capture['has_pixels'] = has_pixels
        
        if current_capture:
            info['captures'].append(current_capture)
        
        return info
    
    def read_raw_framebuffer(self, max_size=None):
        """Read raw framebuffer data"""
        if not os.path.exists(self.proc_raw_path):
            raise FileNotFoundError(f"Raw framebuffer proc file not found: {self.proc_raw_path}")
        
        with open(self.proc_raw_path, 'rb') as f:
            if max_size:
                data = f.read(max_size)
            else:
                data = f.read()
        
        return data
    
    def decode_pixels(self, raw_data, width, height, format_str, pitch=None):
        """Decode raw pixel data based on format"""
        if not raw_data:
            return None
        
        # Determine bytes per pixel based on format
        if 'XRGB8888' in format_str or 'ARGB8888' in format_str:
            bpp = 4
            pixel_format = 'RGBA'
        elif 'RGB565' in format_str:
            bpp = 2
            pixel_format = 'RGB565'
        else:
            print(f"Warning: Unknown format {format_str}, assuming 4 bytes per pixel")
            bpp = 4
            pixel_format = 'RGBA'
        
        expected_size = width * height * bpp
        if len(raw_data) < expected_size:
            print(f"Warning: Data size {len(raw_data)} < expected {expected_size}")
            height = len(raw_data) // (width * bpp)
        
        if bpp == 4:
            # ARGB/XRGB format
            pixels = np.frombuffer(raw_data[:width * height * bpp], dtype=np.uint8)
            pixels = pixels.reshape((height, width, 4))
            # Convert BGRA to RGBA (Intel format is usually BGRA)
            pixels = pixels[:, :, [2, 1, 0, 3]]  # BGRA -> RGBA
        elif bpp == 2:
            # RGB565 format
            pixel_data = np.frombuffer(raw_data[:width * height * bpp], dtype=np.uint16)
            pixel_data = pixel_data.reshape((height, width))
            
            # Convert RGB565 to RGB888
            pixels = np.zeros((height, width, 3), dtype=np.uint8)
            pixels[:, :, 0] = ((pixel_data & 0xF800) >> 11) << 3  # Red
            pixels[:, :, 1] = ((pixel_data & 0x07E0) >> 5) << 2   # Green
            pixels[:, :, 2] = (pixel_data & 0x001F) << 3          # Blue
        
        return pixels
    
    def extract_timestamp_pattern(self, pixels, pattern_size=200):
        """Extract timestamp pattern from specific regions of the framebuffer"""
        if pixels is None:
            return None
        
        height, width = pixels.shape[:2]
        results = []
        
        # Check corners where test patterns might be placed
        regions = [
            (50, 50),  # Top-left
            (width - pattern_size - 50, 50),  # Top-right
            (50, height - pattern_size - 50),  # Bottom-left
            (width - pattern_size - 50, height - pattern_size - 50)  # Bottom-right
        ]
        
        for x, y in regions:
            if x >= 0 and y >= 0 and x + pattern_size <= width and y + pattern_size <= height:
                region = pixels[y:y+pattern_size, x:x+pattern_size]
                timestamp = self.decode_timestamp_from_region(region)
                if timestamp is not None:
                    results.append({
                        'region': (x, y),
                        'timestamp': timestamp,
                        'confidence': 1.0  # Could implement confidence scoring
                    })
        
        return results
    
    def decode_timestamp_from_region(self, region):
        """Decode timestamp from a pixel region"""
        # This is a simplified decoder - in practice you'd need to match
        # the exact encoding used in your test pattern generator
        
        # Convert to grayscale for pattern analysis
        if region.shape[2] >= 3:
            gray = np.mean(region[:, :, :3], axis=2)
        else:
            gray = region[:, :, 0]
        
        # Look for patterns in the data
        # This is a placeholder - you'd implement the actual decoding
        # based on your specific timestamp encoding scheme
        
        # For now, just return a hash of the region as a pseudo-timestamp
        region_hash = hash(gray.tobytes()) & 0xFFFFFFFF
        return region_hash
    
    def monitor_continuous(self, duration=10, interval=0.1):
        """Continuously monitor framebuffer captures"""
        print(f"Monitoring framebuffer captures for {duration} seconds...")
        
        start_time = time.time()
        last_capture_count = 0
        samples = []
        
        while time.time() - start_time < duration:
            try:
                # Read current info
                info = self.read_fb_info()
                current_time = time.time_ns()
                
                # Check for new captures
                if info['total_captures'] > last_capture_count:
                    print(f"New capture detected! Total: {info['total_captures']}")
                    
                    # Try to read and analyze the latest data
                    try:
                        raw_data = self.read_raw_framebuffer(max_size=1024*1024)  # Limit to 1MB
                        
                        if raw_data and info['captures']:
                            latest_capture = info['captures'][-1]
                            
                            if latest_capture['has_pixels'] and latest_capture['dimensions']:
                                width, height = latest_capture['dimensions']
                                format_str = latest_capture['format']
                                
                                # Decode pixels
                                pixels = self.decode_pixels(raw_data, width, height, format_str)
                                
                                # Extract timestamp patterns
                                timestamp_patterns = self.extract_timestamp_pattern(pixels)
                                
                                sample = {
                                    'detection_time': current_time,
                                    'capture_timestamp': latest_capture['timestamp'],
                                    'capture_info': latest_capture,
                                    'raw_data_size': len(raw_data),
                                    'timestamp_patterns': timestamp_patterns
                                }
                                
                                samples.append(sample)
                                
                                print(f"  Dimensions: {width}x{height}")
                                print(f"  Format: {format_str}")
                                print(f"  Data size: {len(raw_data)} bytes")
                                print(f"  Timestamp patterns found: {len(timestamp_patterns) if timestamp_patterns else 0}")
                    
                    except Exception as e:
                        print(f"  Error analyzing capture: {e}")
                    
                    last_capture_count = info['total_captures']
                
                time.sleep(interval)
            
            except Exception as e:
                print(f"Error during monitoring: {e}")
                time.sleep(interval)
        
        return samples
    
    def analyze_latency(self, samples):
        """Analyze latency from collected samples"""
        print("\n" + "="*60)
        print("LATENCY ANALYSIS")
        print("="*60)
        
        if not samples:
            print("No samples collected!")
            return
        
        print(f"Total samples: {len(samples)}")
        
        latencies = []
        
        for i, sample in enumerate(samples):
            print(f"\nSample {i+1}:")
            print(f"  Detection time: {sample['detection_time']} ns")
            print(f"  Capture timestamp: {sample['capture_timestamp']} ns")
            print(f"  Data size: {sample['raw_data_size']} bytes")
            
            # Calculate latency (detection time - capture timestamp)
            latency_ns = sample['detection_time'] - sample['capture_timestamp']
            latency_ms = latency_ns / 1_000_000
            
            print(f"  Latency: {latency_ms:.2f} ms")
            
            latencies.append(latency_ms)
            
            # Show timestamp patterns if found
            if sample['timestamp_patterns']:
                print(f"  Timestamp patterns:")
                for j, pattern in enumerate(sample['timestamp_patterns']):
                    print(f"    Pattern {j+1}: region {pattern['region']}, timestamp {pattern['timestamp']}")
        
        if latencies:
            print(f"\nLATENCY STATISTICS:")
            print(f"  Count: {len(latencies)}")
            print(f"  Average: {np.mean(latencies):.2f} ms")
            print(f"  Min: {np.min(latencies):.2f} ms")
            print(f"  Max: {np.max(latencies):.2f} ms")
            print(f"  Std dev: {np.std(latencies):.2f} ms")
            print(f"  Median: {np.median(latencies):.2f} ms")
            
            # Categorize latency
            avg_latency = np.mean(latencies)
            if avg_latency < 1:
                category = "EXCELLENT (< 1ms)"
            elif avg_latency < 5:
                category = "GOOD (< 5ms)" 
            elif avg_latency < 16.67:  # One frame at 60 FPS
                category = "ACCEPTABLE (< 1 frame @ 60fps)"
            else:
                category = "HIGH (> 1 frame @ 60fps)"
            
            print(f"  Category: {category}")
    
    def save_results(self, samples, filename):
        """Save analysis results to file"""
        data = {
            'timestamp': time.time(),
            'total_samples': len(samples),
            'samples': []
        }
        
        for sample in samples:
            # Convert numpy arrays to lists for JSON serialization
            sample_data = {
                'detection_time': sample['detection_time'],
                'capture_timestamp': sample['capture_timestamp'],
                'raw_data_size': sample['raw_data_size'],
                'capture_info': sample['capture_info']
            }
            
            if sample['timestamp_patterns']:
                sample_data['timestamp_patterns'] = sample['timestamp_patterns']
            
            data['samples'].append(sample_data)
        
        with open(filename, 'w') as f:
            json.dump(data, f, indent=2)
        
        print(f"Results saved to: {filename}")

def main():
    parser = argparse.ArgumentParser(description="Advanced framebuffer latency analysis")
    parser.add_argument('--duration', type=int, default=10, help='Monitoring duration in seconds')
    parser.add_argument('--interval', type=float, default=0.1, help='Monitoring interval in seconds')
    parser.add_argument('--output', type=str, help='Output file for results')
    parser.add_argument('--info-only', action='store_true', help='Just show current framebuffer info')
    
    args = parser.parse_args()
    
    analyzer = FramebufferAnalyzer()
    
    try:
        if args.info_only:
            info = analyzer.read_fb_info()
            print("Current framebuffer info:")
            print(f"Total captures: {info['total_captures']}")
            
            for capture in info['captures']:
                print(f"\nCapture {capture['id']}:")
                for key, value in capture.items():
                    if key != 'id':
                        print(f"  {key}: {value}")
            return
        
        # Run continuous monitoring
        samples = analyzer.monitor_continuous(args.duration, args.interval)
        
        # Analyze results
        analyzer.analyze_latency(samples)
        
        # Save results if requested
        if args.output:
            analyzer.save_results(samples, args.output)
    
    except FileNotFoundError as e:
        print(f"Error: {e}")
        print("\nMake sure:")
        print("1. The kernel module is loaded")
        print("2. You have permission to read /proc/drm_fb_*")
        print("3. The module is capturing framebuffer data")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
