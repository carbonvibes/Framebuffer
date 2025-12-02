#!/usr/bin/env python3
"""
Real Temporal Latency Test

This script measures the actual temporal latency by:
1. Displaying a continuously updating millisecond timer on screen
2. Taking a framebuffer capture via the kernel module
3. Reading the timer value from the captured pixels
4. Comparing with current time to find the temporal offset (k)

If captured time = current_time - k, then k is the lag
If captured time = current_time + k, then k is the prediction
"""

import time
import subprocess
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import pygame
import threading
import queue
import sys

class RealTemporalLatencyTest:
    def __init__(self):
        self.proc_raw_path = "/proc/drm_fb_raw"
        self.proc_info_path = "/proc/drm_fb_pixels"
        self.running = False
        self.display_thread = None
        self.current_display_time = 0
        
    def get_framebuffer_info(self):
        """Get the current framebuffer dimensions and format"""
        try:
            with open(self.proc_info_path, 'r') as f:
                content = f.read()
            
            # Parse the most recent capture info
            lines = content.split('\n')
            for i, line in enumerate(lines):
                if 'Capture 0:' in line:  # Most recent capture
                    # Look for dimensions and format in the next few lines
                    for j in range(i+1, min(i+15, len(lines))):
                        if 'Dimensions:' in lines[j]:
                            dims = lines[j].split(':')[1].strip()
                            width, height = map(int, dims.split('x'))
                        elif 'Format:' in lines[j]:
                            format_info = lines[j].split(':')[1].strip()
                            
                    return {
                        'width': width,
                        'height': height,
                        'format': format_info
                    }
            
            return None
        except Exception as e:
            print(f"Error reading framebuffer info: {e}")
            return None
    
    def display_millisecond_timer(self, duration=10):
        """Display a continuously updating millisecond timer"""
        pygame.init()
        
        # Get screen resolution
        info = pygame.display.Info()
        screen_width = info.current_w
        screen_height = info.current_h
        
        # Create fullscreen display
        screen = pygame.display.set_mode((screen_width, screen_height), pygame.FULLSCREEN)
        pygame.display.set_caption("Temporal Latency Test - Press ESC to exit")
        
        # Font for the timer
        font_size = min(screen_width, screen_height) // 8
        font = pygame.font.Font(None, font_size)
        
        clock = pygame.time.Clock()
        start_time = time.time()
        
        print(f"Starting millisecond timer display for {duration} seconds...")
        print("The timer shows current time in milliseconds")
        print("Press ESC to stop early")
        
        try:
            while self.running and (time.time() - start_time) < duration:
                # Handle events
                for event in pygame.event.get():
                    if event.type == pygame.KEYDOWN:
                        if event.key == pygame.K_ESCAPE:
                            self.running = False
                            return
                
                # Get current time in milliseconds
                current_time_ms = int(time.time() * 1000)
                self.current_display_time = current_time_ms
                
                # Clear screen
                screen.fill((0, 0, 0))  # Black background
                
                # Create timer display
                time_str = f"{current_time_ms}"
                
                # Main timer (large)
                text_surface = font.render(time_str, True, (255, 255, 255))
                text_rect = text_surface.get_rect(center=(screen_width//2, screen_height//2))
                screen.blit(text_surface, text_rect)
                
                # Additional info (smaller)
                info_font = pygame.font.Font(None, 48)
                info_text = f"Current time: {current_time_ms} ms"
                info_surface = info_font.render(info_text, True, (255, 255, 0))
                screen.blit(info_surface, (50, 50))
                
                # Show seconds since start
                elapsed = time.time() - start_time
                elapsed_text = f"Elapsed: {elapsed:.1f}s / {duration}s"
                elapsed_surface = info_font.render(elapsed_text, True, (0, 255, 255))
                screen.blit(elapsed_surface, (50, 100))
                
                # Update display
                pygame.display.flip()
                
                # Target ~100 FPS for smooth millisecond updates
                clock.tick(100)
                
        except Exception as e:
            print(f"Error in display: {e}")
        finally:
            pygame.quit()
    
    def capture_and_analyze_framebuffer(self):
        """Capture framebuffer and extract the displayed time"""
        try:
            # Get framebuffer info
            fb_info = self.get_framebuffer_info()
            if not fb_info:
                print("Could not get framebuffer info")
                return None
            
            print(f"Framebuffer: {fb_info['width']}x{fb_info['height']}, format: {fb_info['format']}")
            
            # Read raw framebuffer data
            with open(self.proc_raw_path, 'rb') as f:
                raw_data = f.read()
            
            if not raw_data:
                print("No framebuffer data available")
                return None
            
            print(f"Captured {len(raw_data)} bytes of framebuffer data")
            
            # Convert to image for OCR/analysis
            # Assuming XRGB8888 format (4 bytes per pixel)
            width = fb_info['width']
            height = fb_info['height']
            
            if len(raw_data) >= width * height * 4:
                # Reshape the data
                pixel_data = np.frombuffer(raw_data[:width * height * 4], dtype=np.uint8)
                pixel_data = pixel_data.reshape((height, width, 4))
                
                # Convert BGRA to RGB (Intel format is usually BGRA)
                rgb_data = pixel_data[:, :, [2, 1, 0]]  # Drop alpha, convert BGR to RGB
                
                # Create PIL image
                image = Image.fromarray(rgb_data, 'RGB')
                
                # Save for debugging
                image.save('captured_frame.png')
                print("Saved captured frame as 'captured_frame.png'")
                
                # Try to extract timestamp using simple OCR approach
                return self.extract_timestamp_from_image(image)
            else:
                print(f"Insufficient data: got {len(raw_data)}, need {width * height * 4}")
                return None
                
        except Exception as e:
            print(f"Error capturing framebuffer: {e}")
            return None
    
    def extract_timestamp_from_image(self, image):
        """Extract timestamp from the captured image using simple methods"""
        try:
            # Convert to grayscale for better text detection
            gray_image = image.convert('L')
            
            # Crop to center area where timer should be
            width, height = image.size
            crop_box = (
                width // 4,
                height // 4,
                3 * width // 4,
                3 * height // 4
            )
            cropped = gray_image.crop(crop_box)
            cropped.save('timer_region.png')
            print("Saved timer region as 'timer_region.png'")
            
            # Try to use tesseract if available
            try:
                import pytesseract
                
                # Configure tesseract for digits only
                config = '--psm 8 -c tessedit_char_whitelist=0123456789'
                text = pytesseract.image_to_string(cropped, config=config)
                
                # Try to parse as timestamp
                text = text.strip().replace('\n', '').replace(' ', '')
                if text.isdigit():
                    return int(text)
                
            except ImportError:
                print("pytesseract not available, trying simple pixel analysis")
            
            # Fallback: analyze pixel patterns (very basic)
            # Look for white text on black background
            pixels = np.array(cropped)
            
            # Find bright regions (potential text)
            bright_pixels = np.where(pixels > 200)
            if len(bright_pixels[0]) > 0:
                print("Found bright text regions in the image")
                # This is where you'd implement more sophisticated text recognition
                # For now, return a placeholder that indicates we found something
                return "TEXT_DETECTED_BUT_NOT_PARSED"
            
            return None
            
        except Exception as e:
            print(f"Error extracting timestamp: {e}")
            return None
    
    def run_temporal_test(self, duration=10):
        """Run the complete temporal latency test"""
        print("="*60)
        print("REAL TEMPORAL LATENCY TEST")
        print("="*60)
        print()
        print("This test will:")
        print("1. Display a continuously updating millisecond timer")
        print("2. Capture the framebuffer via kernel module")
        print("3. Extract the timer value from captured pixels")
        print("4. Calculate temporal offset (lag or prediction)")
        print()
        
        # Check if kernel module is available
        if not os.path.exists(self.proc_raw_path):
            print("Error: Kernel module not available")
            return False
        
        self.running = True
        
        # Start the display in a thread
        display_thread = threading.Thread(target=self.display_millisecond_timer, args=(duration,))
        display_thread.start()
        
        # Wait a bit for display to start
        time.sleep(2)
        
        try:
            # Take multiple captures during the test
            captures = []
            
            for i in range(5):  # Take 5 samples
                print(f"\nCapture {i+1}/5:")
                
                # Record the current time
                capture_time = time.time() * 1000  # Convert to ms
                display_time = self.current_display_time
                
                print(f"Current time: {int(capture_time)} ms")
                print(f"Display showing: {display_time} ms")
                
                # Capture framebuffer
                extracted_time = self.capture_and_analyze_framebuffer()
                
                if extracted_time and isinstance(extracted_time, int):
                    # Calculate temporal latency
                    latency = capture_time - extracted_time
                    
                    print(f"Extracted time from capture: {extracted_time} ms")
                    print(f"Temporal latency: {latency:.1f} ms")
                    
                    if latency > 0:
                        print(f"→ Frame is {latency:.1f} ms OLD (lag)")
                    elif latency < 0:
                        print(f"→ Frame is {abs(latency):.1f} ms in FUTURE (prediction)")
                    else:
                        print("→ Frame is CURRENT (no latency)")
                    
                    captures.append({
                        'capture_time': capture_time,
                        'extracted_time': extracted_time,
                        'latency': latency
                    })
                else:
                    print("Could not extract timestamp from capture")
                
                time.sleep(1)  # Wait between captures
            
            # Analyze results
            if captures:
                latencies = [c['latency'] for c in captures]
                avg_latency = sum(latencies) / len(latencies)
                
                print("\n" + "="*60)
                print("TEMPORAL LATENCY RESULTS")
                print("="*60)
                print(f"Successful captures: {len(captures)}")
                print(f"Average temporal latency: {avg_latency:.1f} ms")
                print(f"Min latency: {min(latencies):.1f} ms")
                print(f"Max latency: {max(latencies):.1f} ms")
                
                if avg_latency > 0:
                    print(f"\n→ Framebuffer shows content from {avg_latency:.1f} ms AGO")
                    print("  This indicates capture LAG")
                elif avg_latency < 0:
                    print(f"\n→ Framebuffer shows content {abs(avg_latency):.1f} ms in the FUTURE")
                    print("  This indicates capture PREDICTION")
                else:
                    print("\n→ Framebuffer shows CURRENT content")
                
                # Compare with your measurement
                print(f"\nYour measurement: ~150 ms lag")
                if abs(avg_latency - 150) < 50:
                    print("✓ Results are consistent with your measurement!")
                else:
                    print("⚠ Results differ from your measurement")
            
        except KeyboardInterrupt:
            print("\nTest interrupted")
        finally:
            self.running = False
            display_thread.join(timeout=2)
        
        return True

def main():
    # Check dependencies
    try:
        import pygame
        import numpy as np
        from PIL import Image
    except ImportError as e:
        print(f"Missing dependency: {e}")
        print("Install with: pip3 install pygame numpy pillow")
        return False
    
    # Optional: check for tesseract
    try:
        import pytesseract
        print("✓ Tesseract available for OCR")
    except ImportError:
        print("⚠ Tesseract not available - will use basic pixel analysis")
        print("  For better results: pip3 install pytesseract")
    
    test = RealTemporalLatencyTest()
    return test.run_temporal_test(duration=15)

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
