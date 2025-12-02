#!/usr/bin/env python3
import sys
import os
from Xlib import display, X, Xutil, error
from PIL import Image

def grab_x11(filename="shot.png"):
    if not filename:
        print("Error: filename cannot be empty")
        return False
    
    output_dir = os.path.dirname(filename)
    if output_dir and not os.path.exists(output_dir):
        try:
            os.makedirs(output_dir, exist_ok=True)
        except OSError as e:
            print(f"Error: Could not create directory {output_dir}: {e}")
            return False
    
    dsp = None
    try:
        # Connect to X display
        dsp = display.Display()
        if not dsp:
            print("Error: Could not connect to X display")
            return False
            
        root = dsp.screen().root
        geom = root.get_geometry()
        w, h = geom.width, geom.height
        
        if w <= 0 or h <= 0:
            print("Error: Invalid screen dimensions")
            return False

        # Ask X server to sync with VBlank to avoid tearing
        try:
            dsp.sync()
        except error.BadValue:
            pass  # some servers ignore this

        # Use shared memory extension if available for better performance
        use_shm = dsp.has_extension("MIT-SHM")
        try:
            if use_shm:
                img = root.get_image(0, 0, w, h, X.ZPixmap, 0xffffffff, plane_mask=~0)
            else:
                img = root.get_image(0, 0, w, h, X.ZPixmap, 0xffffffff)
        except Exception as e:
            print(f"Error: Could not capture screen image: {e}")
            return False

        # Convert to PIL Image and save
        try:
            pil = Image.frombytes("RGB", (w, h), img.data, "raw", "BGRX")
            pil.save(filename, "PNG")
            print(f"Screenshot saved: {filename}")
            return True
        except Exception as e:
            print(f"Error: Could not save image to {filename}: {e}")
            return False
            
    except error.DisplayError as e:
        print(f"Error: X11 display error: {e}")
        return False
    except Exception as e:
        print(f"Error: Unexpected error: {e}")
        return False
    finally:
        # Always close the display connection
        if dsp:
            try:
                dsp.close()
            except Exception:
                pass  # ignore errors during cleanup

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Capture X11 screenshot')
    parser.add_argument('filename', nargs='?', default='shot.png',
                       help='Output filename (default: shot.png)')
    parser.add_argument('--version', action='version', version='X11 Screenshot Tool 1.0')
    
    args = parser.parse_args()
    
    success = grab_x11(args.filename)
    sys.exit(0 if success else 1)