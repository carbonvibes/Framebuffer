#!/bin/bash

# Test script for DRM Framebuffer Blocking Functionality
# This script helps test the frame blocking feature

echo "=== DRM Framebuffer Blocking Test Script ==="
echo ""

# Check if module is loaded
if ! lsmod | grep -q drm_fb_pixel_extractor; then
    echo "ERROR: drm_fb_pixel_extractor module is not loaded!"
    echo "Load it first with: sudo make install"
    exit 1
fi

echo "Module is loaded. Testing frame blocking functionality..."
echo ""

# Function to show current status
show_status() {
    echo "--- Current Status ---"
    cat /proc/drm_fb_block
    echo ""
}

# Function to show frame info
show_frames() {
    echo "--- Frame Information ---"
    cat /proc/drm_fb_pixels | head -20
    echo ""
}

# Test sequence
echo "1. Initial status:"
show_status

echo "2. Enabling frame blocking..."
echo 1 | sudo tee /proc/drm_fb_block > /dev/null
show_status

echo "3. Waiting 5 seconds for some frame activity..."
sleep 5
show_status

echo "4. Disabling frame blocking..."
echo 0 | sudo tee /proc/drm_fb_block > /dev/null
show_status

echo "5. Waiting 3 seconds for normal frame activity..."
sleep 3
show_frames

echo "6. Testing reset command..."
echo reset | sudo tee /proc/drm_fb_block > /dev/null
show_status

echo "=== Test completed ==="
echo ""
echo "Usage examples:"
echo "  Enable blocking:  echo 1 | sudo tee /proc/drm_fb_block"
echo "  Disable blocking: echo 0 | sudo tee /proc/drm_fb_block"
echo "  Reset counter:    echo reset | sudo tee /proc/drm_fb_block"
echo "  View status:      cat /proc/drm_fb_block"
echo "  View frames:      cat /proc/drm_fb_pixels"
echo ""
echo "Monitor dmesg for blocking messages:"
echo "  sudo dmesg -w | grep 'BLOCKING framebuffer'"
