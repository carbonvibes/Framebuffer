#!/usr/bin/env python3
"""
framebuffer_grab.py — Direct framebuffer reader and display tool for Linux.

 • Framebuffer path : Direct /dev/fb0 reading
 • Wayland path     : xdg-desktop-portal RequestScreenshot -> PipeWire DMA-BUF
 • X11   path       : Xlib XShmGetImage
 • Shows the image in a Tk window and writes YYYY-MM-DD_HH-MM-SS.png
"""

import os, sys, time, io, struct, fcntl
from datetime import datetime
from PIL import Image, ImageTk
import tkinter as tk

# ---------- 0. Direct framebuffer reading ----------
def grab_framebuffer(fb_device='/dev/fb0'):
    """Read framebuffer directly from device."""
    try:
        # Get framebuffer info using ioctl
        FBIOGET_VSCREENINFO = 0x4600
        FBIOGET_FSCREENINFO = 0x4602
        
        with open(fb_device, 'rb') as fb:
            # Get variable screen info (resolution, color depth, etc.)
            vinfo = bytearray(160)  # struct fb_var_screeninfo size
            fcntl.ioctl(fb.fileno(), FBIOGET_VSCREENINFO, vinfo)
            
            # Parse the important fields from fb_var_screeninfo
            xres = struct.unpack('I', vinfo[0:4])[0]
            yres = struct.unpack('I', vinfo[4:8])[0]
            xres_virtual = struct.unpack('I', vinfo[8:12])[0]
            yres_virtual = struct.unpack('I', vinfo[12:16])[0]
            bits_per_pixel = struct.unpack('I', vinfo[24:28])[0]
            
            print(f"Framebuffer info: {xres}x{yres}, {bits_per_pixel} bpp")
            print(f"Virtual: {xres_virtual}x{yres_virtual}")
            
            # Calculate bytes per pixel and line
            bytes_per_pixel = bits_per_pixel // 8
            line_length = xres_virtual * bytes_per_pixel
            
            # Read the framebuffer data
            fb.seek(0)
            fb_data = fb.read(yres * line_length)
            
            if not fb_data:
                return None
            
            # Check if framebuffer contains actual data (not all zeros)
            non_zero_bytes = sum(1 for b in fb_data[:10000] if b != 0)  # Sample first 10KB
            total_sampled = min(len(fb_data), 10000)
            print(f"Framebuffer data check: {non_zero_bytes}/{total_sampled} non-zero bytes")
            
            if non_zero_bytes == 0:
                print("Warning: Framebuffer appears to be all black (no active content)")
                print("This is common on modern systems using DRM/KMS instead of legacy framebuffer")
                
            # Convert based on bits per pixel
            if bits_per_pixel == 32:
                # RGBA/BGRA format
                img = Image.frombytes('RGBA', (xres, yres), fb_data[:yres * xres * 4], 'raw', 'BGRA')
                img = img.convert('RGB')  # Remove alpha channel
            elif bits_per_pixel == 24:
                # RGB format
                img = Image.frombytes('RGB', (xres, yres), fb_data[:yres * xres * 3], 'raw', 'BGR')
            elif bits_per_pixel == 16:
                # RGB565 format
                img = Image.frombytes('RGB', (xres, yres), fb_data[:yres * xres * 2], 'raw', 'RGB;16')
            else:
                print(f"Unsupported bits per pixel: {bits_per_pixel}")
                return None
                
            return img
            
    except PermissionError:
        print(f"Permission denied accessing {fb_device}. Try running with sudo.")
        return None
    except FileNotFoundError:
        print(f"Framebuffer device {fb_device} not found.")
        return None
    except Exception as e:
        print(f"Error reading framebuffer: {e}")
        return None

def grab_raw_framebuffer(raw_file, width=1920, height=1080, bpp=32):
    """Read framebuffer from a raw dump file (like from BPF tools)."""
    try:
        with open(raw_file, 'rb') as f:
            file_size = os.path.getsize(raw_file)
            print(f"Raw framebuffer file: {raw_file}")
            print(f"File size: {file_size} bytes")
            
            # Calculate expected size
            bytes_per_pixel = bpp // 8
            expected_size = width * height * bytes_per_pixel
            print(f"Expected size for {width}x{height}x{bpp}: {expected_size} bytes")
            
            # Read the data
            fb_data = f.read()
            
            # Check if we have data
            non_zero_bytes = sum(1 for b in fb_data[:10000] if b != 0)
            print(f"Raw data check: {non_zero_bytes}/10000 non-zero bytes")
            
            if non_zero_bytes == 0:
                print("Warning: Raw framebuffer appears to be all zeros")
                return None
                
            # Try to determine actual dimensions from file size
            if file_size != expected_size:
                # Try common resolutions
                common_sizes = [
                    (1920, 1080, 32), (1920, 1080, 24), 
                    (1366, 768, 32), (1366, 768, 24),
                    (1280, 720, 32), (1280, 720, 24)
                ]
                for w, h, b in common_sizes:
                    if file_size == w * h * (b // 8):
                        width, height, bpp = w, h, b
                        print(f"Auto-detected resolution: {width}x{height}x{bpp}")
                        break
            
            bytes_per_pixel = bpp // 8
            expected_pixels = width * height
            actual_data_size = min(len(fb_data), expected_pixels * bytes_per_pixel)
            
            # Convert based on bits per pixel
            if bpp == 32:
                # RGBA/BGRA format
                img = Image.frombytes('RGBA', (width, height), 
                                    fb_data[:actual_data_size], 'raw', 'BGRA')
                img = img.convert('RGB')
            elif bpp == 24:
                # RGB format  
                img = Image.frombytes('RGB', (width, height), 
                                    fb_data[:actual_data_size], 'raw', 'BGR')
            elif bpp == 16:
                # RGB565 format
                img = Image.frombytes('RGB', (width, height), 
                                    fb_data[:actual_data_size], 'raw', 'RGB;16')
            else:
                print(f"Unsupported bits per pixel: {bpp}")
                return None
                
            return img
            
    except FileNotFoundError:
        print(f"Raw framebuffer file {raw_file} not found.")
        return None
    except Exception as e:
        print(f"Error reading raw framebuffer: {e}")
        return None

# ---------- 1. Try Wayland portal first ----------
def grab_wayland():
    try:
        import dbus
        bus = dbus.SessionBus()
        portal = bus.get_object("org.freedesktop.portal.Desktop",
                                "/org/freedesktop/portal/desktop")
        iface = dbus.Interface(portal, "org.freedesktop.portal.Screenshot")
        token = "pygrab" + str(int(time.time() * 1e6))
        opts = {"interactive": False, "modal": False}
        handle = iface.Screenshot("", opts, dbus_interface="org.freedesktop.portal.Screenshot")
        # Wait for the response signal on the handle
        loop = dbus.mainloop.glib.DBusGMainLoop()
        bus = dbus.SessionBus(mainloop=loop)
        from gi.repository import GLib
        img_uri = {"uri": None}

        def cb(response, result):
            if response == 0 and 'uri' in result:
                img_uri["uri"] = str(result['uri'])
            GLib.MainLoop().quit()

        bus.add_signal_receiver(cb, signal_name="Response",
                                dbus_interface="org.freedesktop.portal.Request",
                                path=handle)
        GLib.MainLoop().run()
        if not img_uri["uri"]:
            return None
        # xdg-desktop-portal puts the file in /run/user/$UID/…
        path = img_uri["uri"].replace("file://", "")
        return Image.open(path)
    except Exception:
        return None

# ---------- 2. Fallback to X11 ----------
def grab_x11():
    try:
        from Xlib import display, X, Xutil
        dsp = display.Display()
        root = dsp.screen().root
        geom = root.get_geometry()
        width, height = geom.width, geom.height

        # Use XShm if available
        img = root.get_image(0, 0, width, height, X.ZPixmap, 0xffffffff)
        pil = Image.frombytes("RGB", (width, height), img.data, "raw", "BGRX")
        return pil
    except Exception:
        return None

# ---------- 3. Main ----------
def main():
    # Try BPF-captured raw framebuffer first
    raw_fb_path = "/home/carbon/Documents/WashU/bpf/framebuffer_dump.raw"
    if os.path.exists(raw_fb_path):
        print("Found BPF framebuffer dump, trying to read...")
        img = grab_raw_framebuffer(raw_fb_path)
        if img is not None:
            print("Successfully loaded BPF framebuffer data!")
        else:
            print("BPF framebuffer dump failed, trying live framebuffer...")
            img = grab_framebuffer()
    else:
        # Try live framebuffer first
        img = grab_framebuffer()
    
    if img is None:
        print("Framebuffer capture failed, trying Wayland...")
        img = grab_wayland()
    if img is None:
        print("Wayland capture failed, trying X11...")
        img = grab_x11()
    if img is None:
        print("Could not capture screen via framebuffer, Wayland portal or X11.")
        sys.exit(1)

    # save PNG
    fname = datetime.now().strftime("screenshot_%Y-%m-%d_%H-%M-%S.png")
    img.save(fname)
    print("Saved", fname)

    # show in a tiny Tk window
    root = tk.Tk()
    root.title(fname)
    tk_img = ImageTk.PhotoImage(img)
    label = tk.Label(root, image=tk_img)
    label.pack()
    root.mainloop()

if __name__ == "__main__":
    main()
