#!/bin/bash

# Force Framebuffer Creation Test
# Attempts to create new framebuffers and measure temporal timing

echo "=========================================="
echo "FORCE NEW FRAMEBUFFER CREATION TEST"
echo "=========================================="

# Function to trigger new framebuffer creation
force_new_framebuffer() {
    echo "Attempting to force new framebuffer creation..."
    
    local initial_count=$(grep "Captured framebuffers:" /proc/drm_fb_pixels | awk '{print $3}')
    echo "Initial capture count: $initial_count"
    
    # Method 1: Try to create new windows/applications
    if command -v gnome-terminal >/dev/null 2>&1; then
        echo "Creating new terminal window..."
        gnome-terminal --window --title="LatencyTest" -- bash -c "echo 'Framebuffer test window'; sleep 2" &
        sleep 1
    fi
    
    # Method 2: Try xterm if available
    if command -v xterm >/dev/null 2>&1 && [ -n "$DISPLAY" ]; then
        echo "Creating xterm window..."
        xterm -title "LatencyTest" -e "echo 'Test window'; sleep 2" &
        sleep 1
    fi
    
    # Method 3: Try changing resolution temporarily
    if command -v xrandr >/dev/null 2>&1 && [ -n "$DISPLAY" ]; then
        echo "Attempting resolution change..."
        current_resolution=$(xrandr | grep "*" | head -1 | awk '{print $1}')
        echo "Current resolution: $current_resolution"
        
        # Get available resolutions
        output_name=$(xrandr | grep " connected" | head -1 | cut -d' ' -f1)
        echo "Primary output: $output_name"
        
        # Try to set a different resolution and back
        available_res=$(xrandr | grep -A 10 "$output_name connected" | grep -v "*" | grep "x" | head -1 | awk '{print $1}')
        if [ -n "$available_res" ]; then
            echo "Trying resolution: $available_res"
            xrandr --output "$output_name" --mode "$available_res" 2>/dev/null
            sleep 0.5
            xrandr --output "$output_name" --mode "$current_resolution" 2>/dev/null
            sleep 0.5
        fi
    fi
    
    # Method 4: Create a simple OpenGL context (if available)
    if command -v glxgears >/dev/null 2>&1 && [ -n "$DISPLAY" ]; then
        echo "Starting OpenGL application..."
        timeout 2s glxgears >/dev/null 2>&1 &
        sleep 1
    fi
    
    # Method 5: Try creating screenshot (might trigger framebuffer)
    if command -v gnome-screenshot >/dev/null 2>&1; then
        echo "Taking screenshot..."
        gnome-screenshot -f /tmp/test_screenshot.png >/dev/null 2>&1 &
        sleep 0.5
    fi
    
    sleep 2
    
    local final_count=$(grep "Captured framebuffers:" /proc/drm_fb_pixels | awk '{print $3}')
    echo "Final capture count: $final_count"
    
    if [ "$final_count" -gt "$initial_count" ]; then
        echo "✓ Success! New framebuffer(s) created: $((final_count - initial_count))"
        return 0
    else
        echo "⚠ No new framebuffers detected"
        return 1
    fi
}

# Function to analyze fresh captures
analyze_fresh_capture() {
    echo ""
    echo "ANALYZING FRESH CAPTURE:"
    echo "========================"
    
    # Get the very latest capture (Capture 0)
    latest_timestamp=$(grep -A 1 "Capture 0:" /proc/drm_fb_pixels | grep "Timestamp:" | awk '{print $2}')
    current_uptime_ns=$(awk '{print $1 * 1000000000}' /proc/uptime)
    
    echo "Latest capture timestamp: $latest_timestamp ns"
    echo "Current system uptime: $current_uptime_ns ns"
    
    # Calculate the temporal offset
    temporal_offset_ns=$((latest_timestamp - current_uptime_ns))
    temporal_offset_ms=$(echo "scale=3; $temporal_offset_ns / 1000000" | bc -l)
    
    echo "Temporal offset: $temporal_offset_ms ms"
    
    # Determine the temporal relationship
    if (( $(echo "$temporal_offset_ms < -100" | bc -l) )); then
        echo ""
        echo "🔴 RESULT: CAPTURING PAST FRAMES"
        echo "   Temporal lag: $(echo "scale=1; -1 * $temporal_offset_ms" | bc -l) ms"
        echo "   The framebuffer shows content from $(echo "scale=1; -1 * $temporal_offset_ms" | bc -l) ms ago"
        echo "   This means: t_capture = t_display - $(echo "scale=1; -1 * $temporal_offset_ms" | bc -l)ms"
    elif (( $(echo "$temporal_offset_ms > 100" | bc -l) )); then
        echo ""
        echo "🔵 RESULT: CAPTURING FUTURE FRAMES" 
        echo "   Temporal prediction: $(echo "scale=1; $temporal_offset_ms" | bc -l) ms"
        echo "   The framebuffer shows content from $(echo "scale=1; $temporal_offset_ms" | bc -l) ms in the future"
        echo "   This means: t_capture = t_display + $(echo "scale=1; $temporal_offset_ms" | bc -l)ms"
    else
        echo ""
        echo "🟢 RESULT: CAPTURING CURRENT FRAMES"
        echo "   Temporal offset: $(echo "scale=1; $temporal_offset_ms" | bc -l) ms"
        echo "   The framebuffer shows current content (within 100ms)"
        echo "   This means: t_capture ≈ t_display (offset: $(echo "scale=1; $temporal_offset_ms" | bc -l)ms)"
    fi
    
    # Additional analysis
    echo ""
    echo "DETAILED TIMING:"
    echo "================"
    
    # Convert timestamps to human readable
    latest_seconds=$((latest_timestamp / 1000000000))
    current_seconds=$((current_uptime_ns / 1000000000))
    
    echo "Capture occurred at: ${latest_seconds}.$(printf "%03d" $((latest_timestamp % 1000000000 / 1000000))) seconds uptime"
    echo "Current uptime: ${current_seconds}.$(printf "%03d" $((current_uptime_ns % 1000000000 / 1000000))) seconds"
    echo "Time difference: $(echo "scale=3; ($current_uptime_ns - $latest_timestamp) / 1000000" | bc -l) ms"
}

# Function to show capture details
show_capture_details() {
    echo ""
    echo "LATEST CAPTURE DETAILS:"
    echo "======================="
    
    grep -A 15 "Capture 0:" /proc/drm_fb_pixels | while read line; do
        if [[ "$line" =~ "Capture 1:" ]]; then
            break
        fi
        echo "  $line"
    done
}

# Main function
main() {
    # Check prerequisites
    if [ ! -f "/proc/drm_fb_pixels" ]; then
        echo "Error: Kernel module not found!"
        exit 1
    fi
    
    if ! command -v bc >/dev/null 2>&1; then
        echo "Error: 'bc' calculator required. Install with: sudo apt install bc"
        exit 1
    fi
    
    # Show current status
    echo "Current capture status:"
    capture_count=$(grep "Captured framebuffers:" /proc/drm_fb_pixels | awk '{print $3}')
    echo "Total captures: $capture_count"
    
    # Try to force new framebuffer creation
    if force_new_framebuffer; then
        # Analyze the fresh capture
        analyze_fresh_capture
        show_capture_details
    else
        echo ""
        echo "Could not generate new framebuffers."
        echo "This might mean:"
        echo "1. The display is not changing in ways that create new framebuffers"
        echo "2. The kernel module hooks framebuffer creation, not updates"
        echo "3. The system is using persistent framebuffers"
        echo ""
        echo "Analyzing existing captures for temporal relationships..."
        
        # Analyze the most recent existing capture
        analyze_fresh_capture
    fi
    
    echo ""
    echo "================================================"
    echo "INTERPRETATION:"
    echo "• If lag > 0: You're seeing old frames (t-k)"
    echo "• If lag < 0: You're seeing future frames (t+k)" 
    echo "• If lag ≈ 0: You're seeing current frames (t)"
    echo "================================================"
}

# Run the test
main "$@"
