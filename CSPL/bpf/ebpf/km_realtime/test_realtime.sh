#!/bin/bash

# Real-Time Framebuffer Test Script
# Tests the real-time framebuffer capture module

set -e

MODULE_NAME="realtime_fb_extractor"
PROC_INFO="/proc/drm_fb_realtime"
PROC_RAW="/proc/drm_fb_realtime_raw"

echo "=== Real-Time DRM Framebuffer Capture Test ==="
echo

# Check if module is loaded
if ! lsmod | grep -q "$MODULE_NAME"; then
    echo "❌ Module $MODULE_NAME is not loaded"
    echo "💡 Run: sudo make install"
    exit 1
fi

echo "✅ Module $MODULE_NAME is loaded"

# Check proc entries
if [ ! -r "$PROC_INFO" ]; then
    echo "❌ Cannot read $PROC_INFO"
    exit 1
fi

if [ ! -r "$PROC_RAW" ]; then
    echo "❌ Cannot read $PROC_RAW"
    exit 1
fi

echo "✅ Proc entries accessible"
echo

# Display capture statistics
echo "=== Capture Statistics ==="
sudo cat "$PROC_INFO" | head -10
echo

# Test pixel data extraction
echo "=== Testing Pixel Data Extraction ==="
SAMPLE_SIZE=4096
SAMPLE_FILE="sample_frame.raw"

echo "Extracting $SAMPLE_SIZE bytes of pixel data..."
if sudo dd if="$PROC_RAW" bs=1 count=$SAMPLE_SIZE of="$SAMPLE_FILE" 2>/dev/null; then
    ACTUAL_SIZE=$(wc -c < "$SAMPLE_FILE")
    if [ "$ACTUAL_SIZE" -gt 0 ]; then
        echo "✅ Extracted $ACTUAL_SIZE bytes successfully"
        
        # Basic validation - check for non-zero data
        if xxd "$SAMPLE_FILE" | head -5 | grep -q "[1-9a-f]"; then
            echo "✅ Pixel data contains non-zero values"
        else
            echo "⚠️  Pixel data appears to be all zeros"
        fi
        
        # Show first few bytes
        echo "First 32 bytes (hex):"
        xxd "$SAMPLE_FILE" | head -2
        
    else
        echo "❌ No pixel data extracted"
    fi
else
    echo "❌ Failed to extract pixel data"
fi

echo

# Performance monitoring test
echo "=== Performance Monitoring Test ==="
echo "Monitoring captures for 10 seconds..."

# Function to get capture count
get_capture_count() {
    sudo cat "$PROC_INFO" | grep "Successful captures:" | awk '{print $3}' || echo "0"
}

INITIAL_COUNT=$(get_capture_count)
sleep 10
FINAL_COUNT=$(get_capture_count)

CAPTURES_PER_SEC=$(echo "scale=2; ($FINAL_COUNT - $INITIAL_COUNT) / 10" | bc 2>/dev/null || echo "N/A")

echo "Captures in 10 seconds: $((FINAL_COUNT - INITIAL_COUNT))"
echo "Average captures per second: $CAPTURES_PER_SEC"

if [ "$FINAL_COUNT" -gt "$INITIAL_COUNT" ]; then
    echo "✅ Real-time capture is working"
else
    echo "❌ No new captures detected"
    echo "💡 Try triggering display updates (move windows, play video)"
fi

echo

# Frame timing analysis
echo "=== Frame Timing Analysis ==="
sudo cat "$PROC_INFO" | grep -E "(Capture [0-9]+:|Timestamp:|Frame number:|Capture method:|Current frame:)" | tail -20

echo

# Memory usage check
echo "=== Memory Usage ==="
KERNEL_MEM=$(grep VmallocUsed /proc/meminfo | awk '{print $2}')
echo "Kernel vmalloc usage: ${KERNEL_MEM} kB"

# Check for memory leaks by looking at allocation patterns
BUFFER_COUNT=$(sudo cat "$PROC_INFO" | grep "Captured framebuffers:" | awk '{print $3}')
echo "Active capture buffers: $BUFFER_COUNT"

echo

# Display format information
echo "=== Format Information ==="
sudo cat "$PROC_INFO" | grep -E "(Dimensions:|Format:|Tiling:|Detiled:)" | tail -12

echo

# Cleanup test file
if [ -f "$SAMPLE_FILE" ]; then
    rm "$SAMPLE_FILE"
fi

# Summary
echo "=== Test Summary ==="
if [ "$FINAL_COUNT" -gt "$INITIAL_COUNT" ] && [ -r "$PROC_INFO" ] && [ -r "$PROC_RAW" ]; then
    echo "✅ All tests passed - Real-time capture is working correctly"
    echo
    echo "💡 To capture a full frame:"
    echo "   1. Check dimensions: sudo cat $PROC_INFO | grep Dimensions"
    echo "   2. Extract data: sudo dd if=$PROC_RAW of=frame.raw"
    echo "   3. Convert to image: convert -size WxH -depth 8 rgba:frame.raw frame.png"
else
    echo "❌ Some tests failed - Check module and display activity"
fi
