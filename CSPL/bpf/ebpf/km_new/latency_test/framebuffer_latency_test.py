#!/usr/bin/env python3
"""
Framebuffer Capture Latency Test

This script tests the latency of framebuffer capture by:
1. Creating a test pattern with timestamps
2. Displaying it on screen
3. Reading the captured framebuffer data from the kernel module
4. Comparing timestamps to determine capture latency
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

class FramebufferLatencyTester:
    def __init__(self, test_duration=10, pattern_fps=60):
        self.test_duration = test_duration
        self.pattern_fps = pattern_fps
        self.frame_interval = 1.0 / pattern_fps
        
        # Paths to kernel module proc files
        self.proc_info_path = "/proc/drm_fb_pixels"
        self.proc_raw_path = "/proc/drm_fb_raw"
        
        # Test pattern settings
        self.screen_width = 1920
        self.screen_height = 1080
        self.pattern_size = 200  # Size of the timestamp pattern
        
        # Results storage
        self.display_timestamps = []
        self.capture_data = []
        self.running = True
        
        # Initialize pygame
        pygame.init()
        
    def check_kernel_module(self):
        """Check if the kernel module is loaded and accessible"""
        if not os.path.exists(self.proc_info_path):
            print(f"Error: {self.proc_info_path} not found. Is the kernel module loaded?")
            return False
            
        if not os.path.exists(self.proc_raw_path):
            print(f"Error: {self.proc_raw_path} not found. Is the kernel module loaded?")
            return False
            
        try:
            with open(self.proc_info_path, 'r') as f:
                content = f.read()
                print("Kernel module info:")
                print(content[:500] + "..." if len(content) > 500 else content)
                return True
        except Exception as e:
            print(f"Error reading kernel module info: {e}")
            return False
    
    def create_timestamp_pattern(self, timestamp_ns):
        """Create a visual pattern that encodes a timestamp"""
        # Create a surface for the pattern
        pattern = pygame.Surface((self.pattern_size, self.pattern_size))
        pattern.fill((0, 0, 0))  # Black background
        
        # Convert timestamp to a visual pattern
        # Use the lower 32 bits of the timestamp for the pattern
        timestamp_32 = timestamp_ns & 0xFFFFFFFF
        
        # Create a pattern using the timestamp bits
        pixel_size = 4
        pattern_grid = self.pattern_size // pixel_size
        
        for y in range(pattern_grid):
            for x in range(pattern_grid):
                bit_index = (y * pattern_grid + x) % 32
                bit_value = (timestamp_32 >> bit_index) & 1
                
                color = (255, 255, 255) if bit_value else (0, 0, 0)
                rect = pygame.Rect(x * pixel_size, y * pixel_size, pixel_size, pixel_size)
                pattern.fill(color, rect)
        
        # Add a colored border based on timestamp to make it easier to spot changes
        border_color = (
            (timestamp_32 >> 16) & 0xFF,
            (timestamp_32 >> 8) & 0xFF,
            timestamp_32 & 0xFF
        )
        pygame.draw.rect(pattern, border_color, pattern.get_rect(), 2)
        
        return pattern, timestamp_32
    
    def display_test_pattern(self):
        """Display changing test patterns on screen"""
        try:
            screen = pygame.display.set_mode((self.screen_width, self.screen_height), pygame.FULLSCREEN)
            pygame.display.set_caption("Framebuffer Latency Test")
            
            clock = pygame.time.Clock()
            frame_count = 0
            
            print(f"Starting test pattern display for {self.test_duration} seconds...")
            print("Press ESC to stop early")
            
            start_time = time.time()
            
            while self.running and (time.time() - start_time) < self.test_duration:
                # Check for events
                for event in pygame.event.get():
                    if event.type == pygame.KEYDOWN:
                        if event.key == pygame.K_ESCAPE:
                            self.running = False
                
                # Get current timestamp
                current_time = time.time_ns()
                
                # Create background
                screen.fill((50, 50, 50))  # Dark gray background
                
                # Create and display timestamp pattern
                pattern, timestamp_32 = self.create_timestamp_pattern(current_time)
                screen.blit(pattern, (50, 50))  # Top-left corner
                screen.blit(pattern, (self.screen_width - self.pattern_size - 50, 50))  # Top-right corner
                screen.blit(pattern, (50, self.screen_height - self.pattern_size - 50))  # Bottom-left corner
                screen.blit(pattern, (self.screen_width - self.pattern_size - 50, 
                                    self.screen_height - self.pattern_size - 50))  # Bottom-right corner
                
                # Add frame counter and timestamp text
                font = pygame.font.Font(None, 36)
                text = font.render(f"Frame: {frame_count} | Time: {current_time}", True, (255, 255, 255))
                screen.blit(text, (200, 200))
                
                # Update display
                pygame.display.flip()
                
                # Record the display timestamp
                self.display_timestamps.append({
                    'frame': frame_count,
                    'display_time_ns': current_time,
                    'timestamp_32': timestamp_32,
                    'real_time': time.time()
                })
                
                frame_count += 1
                clock.tick(self.pattern_fps)
                
        except Exception as e:
            print(f"Error in display thread: {e}")
        finally:
            pygame.quit()
    
    def monitor_captures(self):
        """Monitor framebuffer captures from the kernel module"""
        print("Starting capture monitoring...")
        
        last_capture_count = 0
        
        while self.running:
            try:
                # Read kernel module info
                with open(self.proc_info_path, 'r') as f:
                    content = f.read()
                
                # Parse capture count
                lines = content.split('\n')
                capture_count = 0
                for line in lines:
                    if 'Captured framebuffers:' in line:
                        capture_count = int(line.split(':')[1].strip())
                        break
                
                # If we have new captures, try to read the raw data
                if capture_count > last_capture_count:
                    print(f"New capture detected! Total captures: {capture_count}")
                    
                    try:
                        # Read raw framebuffer data
                        with open(self.proc_raw_path, 'rb') as f:
                            raw_data = f.read()
                        
                        if raw_data:
                            self.capture_data.append({
                                'capture_time': time.time_ns(),
                                'real_time': time.time(),
                                'data_size': len(raw_data),
                                'raw_data': raw_data[:min(1024, len(raw_data))]  # Keep first 1KB for analysis
                            })
                            print(f"Captured {len(raw_data)} bytes of framebuffer data")
                    
                    except Exception as e:
                        print(f"Error reading raw capture data: {e}")
                    
                    last_capture_count = capture_count
                
                time.sleep(0.01)  # Check every 10ms
                
            except Exception as e:
                print(f"Error monitoring captures: {e}")
                time.sleep(0.1)
    
    def analyze_latency(self):
        """Analyze the captured data to determine latency"""
        print("\n" + "="*60)
        print("LATENCY ANALYSIS RESULTS")
        print("="*60)
        
        print(f"Display frames generated: {len(self.display_timestamps)}")
        print(f"Framebuffer captures: {len(self.capture_data)}")
        
        if not self.capture_data:
            print("No framebuffer captures found!")
            print("Make sure:")
            print("1. The kernel module is loaded")
            print("2. The display is triggering framebuffer updates")
            print("3. You have permission to read /proc/drm_fb_*")
            return
        
        # Try to match captured data with display timestamps
        latencies = []
        
        for i, capture in enumerate(self.capture_data):
            print(f"\nCapture {i+1}:")
            print(f"  Capture time: {capture['capture_time']} ns")
            print(f"  Data size: {capture['data_size']} bytes")
            
            # Try to decode timestamp pattern from captured data
            # This is simplified - in reality you'd need to parse the actual pixel data
            # based on the framebuffer format
            
            # Find the closest display timestamp
            closest_display = None
            min_diff = float('inf')
            
            for display in self.display_timestamps:
                diff = abs(capture['capture_time'] - display['display_time_ns'])
                if diff < min_diff:
                    min_diff = diff
                    closest_display = display
            
            if closest_display:
                latency_ns = capture['capture_time'] - closest_display['display_time_ns']
                latency_ms = latency_ns / 1_000_000
                
                print(f"  Closest display frame: {closest_display['frame']}")
                print(f"  Display time: {closest_display['display_time_ns']} ns")
                print(f"  Latency: {latency_ms:.2f} ms")
                
                latencies.append(latency_ms)
        
        if latencies:
            print(f"\nLATENCY STATISTICS:")
            print(f"  Average latency: {np.mean(latencies):.2f} ms")
            print(f"  Min latency: {np.min(latencies):.2f} ms")
            print(f"  Max latency: {np.max(latencies):.2f} ms")
            print(f"  Std deviation: {np.std(latencies):.2f} ms")
            
            # Determine if we're capturing current, previous, or next frames
            avg_latency = np.mean(latencies)
            frame_time_ms = 1000.0 / self.pattern_fps
            
            if abs(avg_latency) < frame_time_ms / 4:
                print(f"  Result: Capturing CURRENT frame (within {frame_time_ms/4:.1f}ms)")
            elif avg_latency > frame_time_ms / 2:
                print(f"  Result: Capturing PREVIOUS frame (delay > {frame_time_ms/2:.1f}ms)")
            elif avg_latency < -frame_time_ms / 2:
                print(f"  Result: Capturing NEXT frame (ahead by > {frame_time_ms/2:.1f}ms)")
            else:
                print(f"  Result: Capturing frame with {avg_latency:.1f}ms offset")
        
        # Additional analysis
        print(f"\nADDITIONAL INFO:")
        print(f"  Test duration: {self.test_duration} seconds")
        print(f"  Target FPS: {self.pattern_fps}")
        print(f"  Frame interval: {self.frame_interval*1000:.1f} ms")
        
        # Suggest improvements
        if not self.capture_data:
            print(f"\nTROUBLESHOOTING:")
            print(f"  1. Check if kernel module is loaded: lsmod | grep drm")
            print(f"  2. Check dmesg for module messages: dmesg | tail -20")
            print(f"  3. Try triggering more framebuffer updates")
    
    def run_test(self):
        """Run the complete latency test"""
        print("Framebuffer Capture Latency Test")
        print("="*40)
        
        # Check if kernel module is available
        if not self.check_kernel_module():
            return False
        
        try:
            # Start monitoring thread
            monitor_thread = threading.Thread(target=self.monitor_captures)
            monitor_thread.daemon = True
            monitor_thread.start()
            
            # Wait a bit for monitoring to start
            time.sleep(0.5)
            
            # Run the display test
            self.display_test_pattern()
            
            # Stop monitoring
            self.running = False
            
            # Wait a bit for any final captures
            time.sleep(1.0)
            
            # Analyze results
            self.analyze_latency()
            
            return True
            
        except KeyboardInterrupt:
            print("\nTest interrupted by user")
            self.running = False
            return False
        except Exception as e:
            print(f"Test failed: {e}")
            return False

def main():
    parser = argparse.ArgumentParser(description="Test framebuffer capture latency")
    parser.add_argument('--duration', type=int, default=10, help='Test duration in seconds')
    parser.add_argument('--fps', type=int, default=60, help='Test pattern FPS')
    parser.add_argument('--check-only', action='store_true', help='Only check if kernel module is loaded')
    
    args = parser.parse_args()
    
    tester = FramebufferLatencyTester(test_duration=args.duration, pattern_fps=args.fps)
    
    if args.check_only:
        tester.check_kernel_module()
        return
    
    # Check if we're running as root (might be needed for fullscreen and kernel module access)
    if os.geteuid() != 0:
        print("Warning: Running as non-root user. You might need sudo for kernel module access.")
    
    success = tester.run_test()
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
