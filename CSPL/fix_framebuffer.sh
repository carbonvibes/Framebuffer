#!/bin/bash
# Extract and process framebuffer to fix striping issues

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

# Process the framebuffer with our Python script - directly accessing /proc/drm_fb_raw
echo "Processing framebuffer to fix striping..."
python3 fb_post_process.py "$OUTPUT_FILE"

echo "Done! Output saved to $OUTPUT_FILE"
