#!/bin/bash

# Simple Temporal Latency Test
# Tests whether captured framebuffers show past, current, or future content

echo "========================================"
echo "TEMPORAL FRAMEBUFFER LATENCY TEST"
echo "========================================"
echo "Testing if captures show: current time (t), past time (t-k), or future time (t+k)"
echo ""

# Function to get current capture timestamp from kernel module
get_latest_capture_timestamp() {
    grep "Timestamp:" /proc/drm_fb_pixels | head -1 | awk '{print $2}'
}

# Function to get system uptime in nanoseconds
get_system_uptime_ns() {
    awk '{print $1 * 1000000000}' /proc/uptime
}

# Function to trigger display changes and measure timing
test_temporal_offset() {
    echo "Triggering display changes and measuring capture timing..."
    
    # Get baseline
    initial_capture_count=$(grep "Captured framebuffers:" /proc/drm_fb_pixels | awk '{print $3}')
    echo "Initial capture count: $initial_capture_count"
    
    # Record time before triggering change
    before_system_time=$(get_system_uptime_ns)
    before_timestamp=$(date +%s%N)
    
    echo "System uptime before: $before_system_time ns"
    echo "Wall clock before: $before_timestamp ns"
    
    # Trigger multiple display changes to increase chance of new framebuffer
    echo "Triggering display changes..."
    for i in {1..5}; do
        # Method 1: Change brightness
        if [ -d "/sys/class/backlight" ]; then
            for backlight in /sys/class/backlight/*/brightness; do
                if [ -w "$backlight" ]; then
                    original=$(cat "$backlight" 2>/dev/null || echo "10")
                    echo $((original + 1)) > "$backlight" 2>/dev/null
                    sleep 0.1
                    echo "$original" > "$backlight" 2>/dev/null
                    break
                fi
            done
        fi
        
        # Method 2: Try xrandr if available
        if command -v xrandr >/dev/null 2>&1 && [ -n "$DISPLAY" ]; then
            xrandr --output $(xrandr | grep " connected" | head -1 | cut -d' ' -f1) --brightness $(echo "scale=2; 0.9 + $i * 0.02" | bc -l) 2>/dev/null
        fi
        
        sleep 0.2
    done
    
    # Reset brightness
    if command -v xrandr >/dev/null 2>&1 && [ -n "$DISPLAY" ]; then
        xrandr --output $(xrandr | grep " connected" | head -1 | cut -d' ' -f1) --brightness 1.0 2>/dev/null
    fi
    
    # Record time after changes
    after_system_time=$(get_system_uptime_ns)
    after_timestamp=$(date +%s%N)
    
    echo "System uptime after: $after_system_time ns"
    echo "Wall clock after: $after_timestamp ns"
    
    # Check for new captures
    sleep 1
    final_capture_count=$(grep "Captured framebuffers:" /proc/drm_fb_pixels | awk '{print $3}')
    echo "Final capture count: $final_capture_count"
    
    if [ "$final_capture_count" -gt "$initial_capture_count" ]; then
        echo "✓ New capture(s) detected!"
        analyze_capture_timing "$before_system_time" "$after_system_time"
    else
        echo "⚠ No new captures detected"
        echo "Analyzing existing capture timing..."
        analyze_existing_captures
    fi
}

# Function to analyze capture timing
analyze_capture_timing() {
    local trigger_start=$1
    local trigger_end=$2
    
    echo ""
    echo "TEMPORAL ANALYSIS:"
    echo "=================="
    
    # Get the latest capture timestamp
    latest_capture_ts=$(get_latest_capture_timestamp)
    current_uptime=$(get_system_uptime_ns)
    
    echo "Latest capture timestamp: $latest_capture_ts ns"
    echo "Current system uptime: $current_uptime ns"
    
    # Calculate temporal offset
    temporal_offset=$(echo "scale=3; ($latest_capture_ts - $current_uptime) / 1000000" | bc -l)
    
    echo "Temporal offset: $temporal_offset ms"
    
    # Determine if past, current, or future
    if (( $(echo "$temporal_offset < -50" | bc -l) )); then
        echo ""
        echo "🔴 RESULT: CAPTURING PAST FRAMES"
        echo "   Lag: $(echo "scale=1; -1 * $temporal_offset" | bc -l) ms"
        echo "   Your framebuffer shows content from $(echo "scale=1; -1 * $temporal_offset" | bc -l) ms ago"
    elif (( $(echo "$temporal_offset > 50" | bc -l) )); then
        echo ""
        echo "🔵 RESULT: CAPTURING FUTURE FRAMES"
        echo "   Prediction: $(echo "scale=1; $temporal_offset" | bc -l) ms"
        echo "   Your framebuffer shows content from $(echo "scale=1; $temporal_offset" | bc -l) ms in the future"
    else
        echo ""
        echo "🟢 RESULT: CAPTURING CURRENT FRAMES"
        echo "   Offset: $(echo "scale=1; $temporal_offset" | bc -l) ms"
        echo "   Your framebuffer shows current content"
    fi
}

# Function to analyze existing captures
analyze_existing_captures() {
    echo ""
    echo "ANALYZING EXISTING CAPTURES:"
    echo "============================"
    
    # Get all capture timestamps
    timestamps=($(grep "Timestamp:" /proc/drm_fb_pixels | awk '{print $2}'))
    current_uptime=$(get_system_uptime_ns)
    
    echo "Current uptime: $current_uptime ns"
    echo ""
    
    for i in "${!timestamps[@]}"; do
        ts="${timestamps[$i]}"
        offset=$(echo "scale=3; ($ts - $current_uptime) / 1000000" | bc -l)
        age=$(echo "scale=3; ($current_uptime - $ts) / 1000000" | bc -l)
        
        echo "Capture $i:"
        echo "  Timestamp: $ts ns"
        echo "  Age: $age ms ago"
        echo "  Offset: $offset ms"
        
        if (( $(echo "$offset < -1000" | bc -l) )); then
            echo "  Status: OLD CAPTURE ($(echo "scale=1; -1 * $offset / 1000" | bc -l) seconds old)"
        elif (( $(echo "$offset < -50" | bc -l) )); then
            echo "  Status: PAST FRAME (lag: $(echo "scale=1; -1 * $offset" | bc -l) ms)"
        elif (( $(echo "$offset > 50" | bc -l) )); then
            echo "  Status: FUTURE FRAME (prediction: $(echo "scale=1; $offset" | bc -l) ms)"
        else
            echo "  Status: CURRENT FRAME (offset: $(echo "scale=1; $offset" | bc -l) ms)"
        fi
        echo ""
    done
}

# Function to check timing methodology
check_timing_methodology() {
    echo "TIMING METHODOLOGY CHECK:"
    echo "========================="
    
    # Compare different time sources
    uptime_ns=$(get_system_uptime_ns)
    wall_clock_ns=$(date +%s%N)
    
    echo "System uptime: $uptime_ns ns"
    echo "Wall clock: $wall_clock_ns ns"
    echo "Difference: $(echo "scale=0; ($wall_clock_ns - $uptime_ns) / 1000000000" | bc -l) seconds"
    echo ""
    echo "Note: Kernel module uses ktime_get_ns() which is uptime-based"
    echo "This test compares capture timestamps to current uptime"
    echo ""
}

# Main execution
main() {
    # Check if kernel module is loaded
    if [ ! -f "/proc/drm_fb_pixels" ]; then
        echo "Error: Kernel module not found!"
        echo "Make sure the DRM framebuffer module is loaded."
        exit 1
    fi
    
    # Check if bc is available for calculations
    if ! command -v bc >/dev/null 2>&1; then
        echo "Error: 'bc' calculator not found. Please install it:"
        echo "sudo apt install bc"
        exit 1
    fi
    
    check_timing_methodology
    test_temporal_offset
    
    echo ""
    echo "================================================"
    echo "SUMMARY:"
    echo "This test measures temporal offset by comparing"
    echo "kernel capture timestamps to current system time."
    echo ""
    echo "• Negative offset = Past frames (lag)"
    echo "• Positive offset = Future frames (prediction)" 
    echo "• Near zero = Current frames"
    echo "================================================"
}

# Run the test
main "$@"
