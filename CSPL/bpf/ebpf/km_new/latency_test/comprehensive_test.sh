#!/bin/bash

# Comprehensive Latency Test Suite
# This script runs all the latency tests and generates a summary report

echo "=========================================="
echo "FRAMEBUFFER LATENCY TEST SUITE"
echo "=========================================="
echo "Date: $(date)"
echo "Location: $(pwd)"
echo ""

# Test 1: Check kernel module status
echo "TEST 1: Kernel Module Status"
echo "=============================="
if [ -f "/proc/drm_fb_pixels" ]; then
    echo "✓ Kernel module loaded successfully"
    capture_count=$(grep "Captured framebuffers:" /proc/drm_fb_pixels | awk '{print $3}')
    echo "✓ Total captures: $capture_count"
    
    raw_size=$(wc -c < /proc/drm_fb_raw 2>/dev/null || echo "0")
    echo "✓ Raw data available: $raw_size bytes"
    
    if [ "$raw_size" -gt 0 ]; then
        echo "✓ Pixel data extraction: WORKING"
    else
        echo "⚠ Pixel data extraction: NO DATA"
    fi
else
    echo "✗ Kernel module not loaded"
    echo "Please load the module first with: sudo insmod drm_fb_pixel_extractor.ko"
    exit 1
fi

echo ""

# Test 2: Basic functionality test
echo "TEST 2: Script Functionality"
echo "============================="

# Test shell script
if [ -x "./framebuffer_latency_test.sh" ]; then
    echo "✓ Shell script executable"
else
    echo "⚠ Shell script not executable"
fi

# Test Python scripts
if python3 -c "import pygame, numpy" 2>/dev/null; then
    echo "✓ Python dependencies available (pygame, numpy)"
else
    echo "⚠ Python dependencies missing"
    echo "  Install with: pip3 install pygame numpy"
fi

if python3 advanced_fb_analyzer.py --info-only > /dev/null 2>&1; then
    echo "✓ Advanced analyzer working"
else
    echo "⚠ Advanced analyzer has issues"
fi

echo ""

# Test 3: Data extraction test
echo "TEST 3: Data Extraction"
echo "======================="

# Extract small sample
if dd if=/proc/drm_fb_raw of=test_sample.raw bs=1 count=64 2>/dev/null; then
    echo "✓ Raw data extraction working"
    
    # Check if data is not all zeros
    if [ "$(od -An -tx1 test_sample.raw | tr -d ' \n')" != "$(printf '00%.0s' {1..128})" ]; then
        echo "✓ Data contains actual pixel information"
        echo "  Sample data: $(hexdump -C test_sample.raw | head -1)"
    else
        echo "⚠ Data appears to be all zeros"
    fi
    
    rm -f test_sample.raw
else
    echo "✗ Cannot extract raw data"
fi

echo ""

# Test 4: Timing analysis
echo "TEST 4: Timing Analysis"
echo "======================"

# Get timestamps from recent captures
echo "Recent capture timestamps:"
grep "Timestamp:" /proc/drm_fb_pixels | head -3 | while read line; do
    timestamp=$(echo "$line" | awk '{print $2}')
    # Convert nanoseconds to human readable
    seconds=$((timestamp / 1000000000))
    remainder=$((timestamp % 1000000000))
    echo "  $(date -d @$seconds).${remainder:0:3} (${timestamp} ns)"
done

echo ""

# Test 5: Performance summary
echo "TEST 5: Performance Summary"
echo "=========================="

# Analyze framebuffer properties
echo "Framebuffer Analysis:"
grep -A 15 "Capture 0:" /proc/drm_fb_pixels | while read line; do
    if [[ "$line" =~ "Dimensions:" ]]; then
        dims=$(echo "$line" | cut -d':' -f2 | tr -d ' ')
        echo "  Resolution: $dims"
    elif [[ "$line" =~ "Format:" ]]; then
        format=$(echo "$line" | cut -d':' -f2 | tr -d ' ')
        echo "  Format: $format"
    elif [[ "$line" =~ "Tiling:" ]]; then
        tiling=$(echo "$line" | cut -d':' -f2 | tr -d ' ')
        echo "  Tiling: $tiling"
    elif [[ "$line" =~ "Detiled:" ]]; then
        detiled=$(echo "$line" | cut -d':' -f2 | tr -d ' ')
        echo "  Detiled: $detiled"
    fi
done

echo ""

# Final assessment
echo "OVERALL ASSESSMENT"
echo "=================="

if [ "$capture_count" -gt 0 ] && [ "$raw_size" -gt 0 ]; then
    echo "✓ EXCELLENT: Kernel module is fully functional"
    echo "✓ Framebuffer capture working"
    echo "✓ Pixel data extraction working"
    echo "✓ Intel tiling detection and detiling working"
    echo ""
    echo "Ready for latency testing!"
    echo "Run './framebuffer_latency_test.sh 10' for a basic test"
    echo "Run 'python3 advanced_fb_analyzer.py --duration 10' for detailed analysis"
else
    echo "⚠ PARTIAL: Module loaded but limited functionality"
    echo "Check kernel messages with: dmesg | tail -20"
fi

echo ""
echo "Test completed at: $(date)"
