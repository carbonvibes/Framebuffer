#!/usr/bin/env python3
"""
Temporal Framebuffer Latency Test

This test measures the actual temporal offset of captured framebuffers:
- If capture shows time t-k: we're capturing frames from k time units ago (lag)
- If capture shows time t+k: we're capturing frames from k time units in the future (prediction)
- If capture shows time t: we're capturing current frames (ideal)
"""

import time
import struct
import os
import sys
import subprocess
import threading
import numpy as np
from datetime import datetime
import pygame
import argparse

class TemporalLatencyTester:
    def __init__(self):
        self.proc_info_path = "/proc/drm_fb_pixels"
        self.proc_raw_path = "/proc/drm_fb_raw"
        
        # For timestamp encoding in visual patterns
        self.timestamp_region_size = 100
        self.pixel_size = 4  # RGBA
        
        pygame.init()
        
    def create_timestamp_display(self, timestamp_ms):
        """Create a visual display with embedded timestamp"""
        # Create a large timestamp display
        font = pygame.font.Font(None, 72)
        
        # Create main surface
        screen = pygame.display.get_surface()
        if screen is None:
            screen = pygame.display.set_mode((1920, 1080))
        
        # Clear with timestamp-based background color
        bg_color = (
            (timestamp_ms >> 16) & 0xFF,
            (timestamp_ms >> 8) & 0xFF,
            timestamp_ms & 0xFF
        )
        screen.fill(bg_color)
        
        # Large timestamp text
        timestamp_text = f"TIME: {timestamp_ms}"
        text_surface = font.render(timestamp_text, True, (255, 255, 255))
        screen.blit(text_surface, (100, 100))
        
        # Add millisecond counter in corner
        ms_text = f"MS: {timestamp_ms % 10000}"
        ms_surface = font.render(ms_text, True, (255, 255, 0))
        screen.blit(ms_surface, (1500, 100))
        
        # Create a visual timestamp pattern in top-left corner
        self.create_timestamp_pattern(screen, timestamp_ms, (50, 50))
        
        # Add frame number
        frame_num = (timestamp_ms // 16) % 1000  # Assuming ~60fps
        frame_text = f"FRAME: {frame_num}"
        frame_surface = font.render(frame_text, True, (0, 255, 255))
        screen.blit(frame_surface, (100, 200))
        
        pygame.display.flip()
        return timestamp_ms
    
    def create_timestamp_pattern(self, screen, timestamp_ms, pos):
        """Create a visual pattern that encodes the timestamp"""
        x, y = pos
        
        # Use lower 16 bits of timestamp for pattern
        pattern_bits = timestamp_ms & 0xFFFF
        
        # Create 4x4 grid of colored squares
        square_size = 20
        for row in range(4):
            for col in range(4):
                bit_index = row * 4 + col
                bit_value = (pattern_bits >> bit_index) & 1
                
                color = (255, 255, 255) if bit_value else (0, 0, 0)
                rect = pygame.Rect(x + col * square_size, y + row * square_size, square_size, square_size)
                pygame.draw.rect(screen, color, rect)
                
        # Add border with timestamp color
        border_color = (
            (timestamp_ms >> 12) & 0xFF,
            (timestamp_ms >> 6) & 0xFF,
            timestamp_ms & 0x3F
        )
        border_rect = pygame.Rect(x-2, y-2, 4*square_size+4, 4*square_size+4)
        pygame.draw.rect(screen, border_color, border_rect, 2)
    
    def decode_timestamp_from_framebuffer(self, raw_data, width, height):
        """Try to decode timestamp from captured framebuffer data"""
        if not raw_data or len(raw_data) < width * height * 4:
            return None
        
        # Convert raw data to numpy array (assuming BGRA format)
        pixels = np.frombuffer(raw_data, dtype=np.uint8)
        pixels = pixels.reshape((height, width, 4))
        
        # Look for the timestamp pattern in top-left corner (around 50,50)
        pattern_x, pattern_y = 50, 50
        pattern_size = 80  # 4x4 grid of 20px squares
        
        if pattern_x + pattern_size < width and pattern_y + pattern_size < height:
            pattern_region = pixels[pattern_y:pattern_y+pattern_size, pattern_x:pattern_x+pattern_size]
            
            # Try to decode the 4x4 bit pattern
            decoded_bits = 0
            square_size = 20
            
            for row in range(4):
                for col in range(4):
                    # Sample the center of each square
                    sample_y = pattern_y + row * square_size + square_size // 2
                    sample_x = pattern_x + col * square_size + square_size // 2
                    
                    if sample_y < height and sample_x < width:
                        # Check if pixel is white (255) or black (0)
                        pixel_val = pixels[sample_y, sample_x, :3].mean()
                        bit_value = 1 if pixel_val > 128 else 0
                        
                        bit_index = row * 4 + col
                        decoded_bits |= (bit_value << bit_index)
            
            return decoded_bits
        
        return None
    
    def run_temporal_test(self, duration=30, display_fps=60):
        """Run the temporal latency test"""
        print(f"Starting temporal latency test for {duration} seconds at {display_fps} FPS...")
        print("This will display changing timestamps and measure what gets captured.")
        
        # Initialize display
        pygame.display.set_caption("Temporal Latency Test")
        clock = pygame.time.Clock()
        
        # Track display and capture events
        display_events = []
        capture_results = []
        
        start_time = time.time()
        last_capture_count = self.get_capture_count()
        
        frame_count = 0
        
        while time.time() - start_time < duration:
            # Handle pygame events
            for event in pygame.event.get():
                if event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                    return self.analyze_temporal_results(display_events, capture_results)
            
            # Get current timestamp
            current_ms = int(time.time() * 1000)
            
            # Display timestamp
            displayed_timestamp = self.create_timestamp_display(current_ms)
            
            # Record display event
            display_events.append({
                'frame': frame_count,
                'display_time': time.time(),
                'display_timestamp_ms': displayed_timestamp,
                'system_time_ns': time.time_ns()
            })
            
            # Check for new captures
            current_capture_count = self.get_capture_count()
            if current_capture_count > last_capture_count:
                print(f"New capture detected at frame {frame_count}!")
                
                # Try to read and analyze the captured data
                try:
                    # Get framebuffer info
                    fb_info = self.get_latest_capture_info()
                    
                    if fb_info and fb_info.get('has_pixels'):
                        # Read raw data
                        raw_data = self.read_raw_framebuffer(1024*1024)  # 1MB sample
                        
                        if raw_data:
                            # Try to decode timestamp
                            decoded_timestamp = self.decode_timestamp_from_framebuffer(
                                raw_data, fb_info['width'], fb_info['height']
                            )
                            
                            capture_result = {
                                'frame': frame_count,
                                'capture_time': time.time(),
                                'capture_timestamp_ns': fb_info['timestamp'],
                                'decoded_timestamp': decoded_timestamp,
                                'fb_info': fb_info,
                                'system_time_ns': time.time_ns()
                            }
                            
                            capture_results.append(capture_result)
                            
                            print(f"  Captured FB: {fb_info['width']}x{fb_info['height']}")
                            print(f"  Decoded timestamp: {decoded_timestamp}")
                
                except Exception as e:
                    print(f"  Error analyzing capture: {e}")
                
                last_capture_count = current_capture_count
            
            frame_count += 1
            clock.tick(display_fps)
        
        pygame.quit()
        return self.analyze_temporal_results(display_events, capture_results)
    
    def get_capture_count(self):
        """Get current capture count from kernel module"""
        try:
            with open(self.proc_info_path, 'r') as f:
                content = f.read()
            
            for line in content.split('\n'):
                if 'Captured framebuffers:' in line:
                    return int(line.split(':')[1].strip())
            return 0
        except:
            return 0
    
    def get_latest_capture_info(self):
        """Get info about the latest capture"""
        try:
            with open(self.proc_info_path, 'r') as f:
                content = f.read()
            
            # Parse the first capture (most recent)
            lines = content.split('\n')
            capture_info = {}
            in_capture = False
            
            for line in lines:
                line = line.strip()
                
                if line.startswith('Capture 0:'):
                    in_capture = True
                    continue
                elif line.startswith('Capture 1:'):
                    break
                
                if in_capture:
                    if 'Timestamp:' in line:
                        capture_info['timestamp'] = int(line.split(':')[1].strip().split()[0])
                    elif 'Dimensions:' in line:
                        dims = line.split(':')[1].strip()
                        width, height = dims.split('x')
                        capture_info['width'] = int(width)
                        capture_info['height'] = int(height)
                    elif 'Pixel data:' in line:
                        capture_info['has_pixels'] = 'AVAILABLE' in line
            
            return capture_info if capture_info else None
        except:
            return None
    
    def read_raw_framebuffer(self, max_size=None):
        """Read raw framebuffer data"""
        try:
            with open(self.proc_raw_path, 'rb') as f:
                if max_size:
                    return f.read(max_size)
                else:
                    return f.read()
        except:
            return None
    
    def analyze_temporal_results(self, display_events, capture_results):
        """Analyze temporal offset between display and capture"""
        print("\n" + "="*60)
        print("TEMPORAL LATENCY ANALYSIS")
        print("="*60)
        
        print(f"Display events: {len(display_events)}")
        print(f"Capture events: {len(capture_results)}")
        
        if not capture_results:
            print("\nNo captures detected during test!")
            print("Try:")
            print("1. Triggering more display changes")
            print("2. Running for a longer duration") 
            print("3. Checking if the kernel module is capturing new framebuffers")
            return
        
        temporal_offsets = []
        
        for capture in capture_results:
            print(f"\nCapture at frame {capture['frame']}:")
            print(f"  Capture time: {capture['capture_time']:.3f}")
            print(f"  Decoded timestamp: {capture['decoded_timestamp']}")
            
            # Find the closest display event
            closest_display = None
            min_time_diff = float('inf')
            
            for display in display_events:
                time_diff = abs(capture['capture_time'] - display['display_time'])
                if time_diff < min_time_diff:
                    min_time_diff = time_diff
                    closest_display = display
            
            if closest_display and capture['decoded_timestamp'] is not None:
                # Compare displayed timestamp vs captured timestamp
                displayed_ms = closest_display['display_timestamp_ms'] & 0xFFFF
                captured_ms = capture['decoded_timestamp']
                
                # Calculate temporal offset
                temporal_offset_ms = captured_ms - displayed_ms
                
                # Handle wrap-around for 16-bit values
                if temporal_offset_ms > 32768:
                    temporal_offset_ms -= 65536
                elif temporal_offset_ms < -32768:
                    temporal_offset_ms += 65536
                
                temporal_offsets.append(temporal_offset_ms)
                
                print(f"  Displayed timestamp: {displayed_ms}")
                print(f"  Captured timestamp: {captured_ms}")
                print(f"  Temporal offset: {temporal_offset_ms} ms")
                
                if temporal_offset_ms < -50:
                    print(f"  → PAST FRAME (lag of {abs(temporal_offset_ms)} ms)")
                elif temporal_offset_ms > 50:
                    print(f"  → FUTURE FRAME (prediction of {temporal_offset_ms} ms)")
                else:
                    print(f"  → CURRENT FRAME (within 50ms)")
        
        if temporal_offsets:
            print(f"\nTEMPORAL OFFSET STATISTICS:")
            avg_offset = np.mean(temporal_offsets)
            print(f"  Average offset: {avg_offset:.1f} ms")
            print(f"  Min offset: {np.min(temporal_offsets):.1f} ms")
            print(f"  Max offset: {np.max(temporal_offsets):.1f} ms")
            print(f"  Std deviation: {np.std(temporal_offsets):.1f} ms")
            
            print(f"\nCONCLUSION:")
            if avg_offset < -50:
                print(f"  🔴 CAPTURING PAST FRAMES (lag: {abs(avg_offset):.1f} ms)")
                print(f"  Your framebuffer shows content from {abs(avg_offset):.1f} ms ago")
            elif avg_offset > 50:
                print(f"  🔵 CAPTURING FUTURE FRAMES (prediction: {avg_offset:.1f} ms)")
                print(f"  Your framebuffer shows content from {avg_offset:.1f} ms in the future")
            else:
                print(f"  🟢 CAPTURING CURRENT FRAMES (offset: {avg_offset:.1f} ms)")
                print(f"  Your framebuffer shows current content")

def main():
    parser = argparse.ArgumentParser(description="Test temporal framebuffer latency")
    parser.add_argument('--duration', type=int, default=30, help='Test duration in seconds')
    parser.add_argument('--fps', type=int, default=60, help='Display refresh rate')
    
    args = parser.parse_args()
    
    tester = TemporalLatencyTester()
    
    # Check if kernel module is available
    if not os.path.exists(tester.proc_info_path):
        print("Error: Kernel module not found. Make sure it's loaded.")
        sys.exit(1)
    
    print("Temporal Framebuffer Latency Test")
    print("=================================")
    print("This test will:")
    print("1. Display rapidly changing timestamps")
    print("2. Capture what the kernel module sees")
    print("3. Compare displayed vs captured timestamps")
    print("4. Determine if you're seeing past, current, or future frames")
    print(f"\nRunning for {args.duration} seconds at {args.fps} FPS...")
    print("Press ESC to stop early\n")
    
    try:
        tester.run_temporal_test(args.duration, args.fps)
    except KeyboardInterrupt:
        print("\nTest interrupted by user")
    except Exception as e:
        print(f"Test failed: {e}")

if __name__ == "__main__":
    main()
