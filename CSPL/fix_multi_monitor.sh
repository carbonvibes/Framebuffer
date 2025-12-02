#!/bin/bash
# Fix multi-monitor framebuffer using Intel Y-tiled detiler

# Check if the framebuffer module is loaded
if [ ! -e /proc/drm_fb_pixels ]; then
    echo "Error: Framebuffer module not loaded or proc file not found"
    echo "Make sure the framebuffer module is loaded with: sudo insmod /path/to/module.ko"
    exit 1
fi

# Output filename
OUTPUT_FILE="${1:-framebuffer.png}"

# Check if we have the required Python packages
pip3 install --quiet numpy pillow

# Process the framebuffer using Intel Y-tiled detiler
echo "Processing framebuffer using Intel Y-tiled memory detiler..."
python3 detile_framebuffer.py "$OUTPUT_FILE"

echo "Detiled framebuffer saved as $OUTPUT_FILE"
