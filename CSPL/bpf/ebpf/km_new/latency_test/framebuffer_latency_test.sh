#!/bin/bash

# Framebuffer Capture Latency Test (Shell Version)
# This script monitors framebuffer captures and provides timing analysis

PROC_INFO="/proc/drm_fb_pixels"
PROC_RAW="/proc/drm_fb_raw"
LOG_FILE="/tmp/fb_latency_test.log"
TEST_DURATION=${1:-10}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Framebuffer Capture Latency Test${NC}"
echo "=================================="

# Function to check if kernel module is loaded
check_kernel_module() {
    echo -e "${YELLOW}Checking kernel module...${NC}"
    
    if [ ! -f "$PROC_INFO" ]; then
        echo -e "${RED}Error: $PROC_INFO not found${NC}"
        echo "Is the kernel module loaded? Try:"
        echo "  sudo insmod your_module.ko"
        echo "  or"
        echo "  sudo modprobe your_module_name"
        return 1
    fi
    
    if [ ! -f "$PROC_RAW" ]; then
        echo -e "${RED}Error: $PROC_RAW not found${NC}"
        return 1
    fi
    
    echo -e "${GREEN}Kernel module appears to be loaded${NC}"
    
    # Show current module info
    echo -e "${YELLOW}Current module status:${NC}"
    head -20 "$PROC_INFO"
    echo ""
    
    return 0
}

# Function to get current capture count
get_capture_count() {
    grep "Captured framebuffers:" "$PROC_INFO" 2>/dev/null | awk '{print $3}' | head -1
}

# Function to get timestamp in nanoseconds
get_timestamp_ns() {
    date +%s%N
}

# Function to monitor captures
monitor_captures() {
    echo -e "${YELLOW}Starting capture monitoring for $TEST_DURATION seconds...${NC}"
    echo "Time(s),CaptureCount,RawDataSize,Timestamp" > "$LOG_FILE"
    
    local start_time=$(get_timestamp_ns)
    local last_count=0
    local test_start=$(date +%s)
    
    while true; do
        local current_time=$(date +%s)
        local elapsed=$((current_time - test_start))
        
        if [ $elapsed -ge $TEST_DURATION ]; then
            break
        fi
        
        local count=$(get_capture_count)
        local timestamp=$(get_timestamp_ns)
        
        if [ -n "$count" ] && [ "$count" -gt "$last_count" ]; then
            echo -e "${GREEN}New capture detected at ${elapsed}s! Count: $count${NC}"
            
            # Try to get raw data size
            local raw_size=0
            if [ -r "$PROC_RAW" ]; then
                raw_size=$(wc -c < "$PROC_RAW" 2>/dev/null || echo "0")
            fi
            
            echo "$elapsed,$count,$raw_size,$timestamp" >> "$LOG_FILE"
            last_count=$count
        fi
        
        # Show progress
        if [ $((elapsed % 2)) -eq 0 ] && [ $elapsed -ne 0 ]; then
            echo -e "${BLUE}Progress: ${elapsed}s / ${TEST_DURATION}s${NC}"
        fi
        
        sleep 0.1
    done
    
    echo -e "${GREEN}Monitoring complete${NC}"
}

# Function to create test pattern (requires xrandr and imagemagick)
create_test_pattern() {
    echo -e "${YELLOW}Creating test pattern...${NC}"
    
    if ! command -v convert &> /dev/null; then
        echo -e "${YELLOW}Warning: ImageMagick not found. Install with: sudo apt install imagemagick${NC}"
        return 1
    fi
    
    local pattern_file="/tmp/fb_test_pattern.png"
    local timestamp=$(get_timestamp_ns)
    
    # Create a simple pattern with timestamp
    convert -size 400x300 xc:black \
        -fill white -pointsize 24 \
        -annotate +50+50 "Test Pattern" \
        -annotate +50+100 "Time: $timestamp" \
        -annotate +50+150 "Frame: $(date +%s)" \
        -fill red -draw "rectangle 350,250 380,280" \
        "$pattern_file"
    
    # Display it (if X11 is available)
    if command -v display &> /dev/null && [ -n "$DISPLAY" ]; then
        display "$pattern_file" &
        local display_pid=$!
        sleep 2
        kill $display_pid 2>/dev/null
    fi
    
    return 0
}

# Function to trigger framebuffer updates
trigger_fb_updates() {
    echo -e "${YELLOW}Triggering framebuffer updates...${NC}"
    
    # Method 1: Try to change screen brightness (if available)
    if [ -d "/sys/class/backlight" ]; then
        for backlight in /sys/class/backlight/*/brightness; do
            if [ -w "$backlight" ]; then
                local original=$(cat "$backlight")
                echo "Changing backlight to trigger updates..."
                echo $((original + 1)) > "$backlight" 2>/dev/null
                sleep 0.1
                echo "$original" > "$backlight" 2>/dev/null
                break
            fi
        done
    fi
    
    # Method 2: Try to trigger DRM events
    if command -v xrandr &> /dev/null && [ -n "$DISPLAY" ]; then
        echo "Using xrandr to trigger display updates..."
        xrandr --output $(xrandr | grep " connected" | head -1 | cut -d' ' -f1) --brightness 1.0 2>/dev/null
    fi
    
    # Method 3: Use a simple X11 window (if available)
    if command -v xwininfo &> /dev/null && [ -n "$DISPLAY" ]; then
        echo "Creating test window..."
        ( 
            for i in {1..50}; do
                xsetroot -solid "#$(printf '%06x' $((RANDOM * RANDOM % 16777216)))" 2>/dev/null
                sleep 0.1
            done
        ) &
    fi
}

# Function to analyze results
analyze_results() {
    echo -e "${BLUE}Analyzing results...${NC}"
    echo "========================"
    
    if [ ! -f "$LOG_FILE" ]; then
        echo -e "${RED}No log file found${NC}"
        return 1
    fi
    
    local capture_count=$(tail -n +2 "$LOG_FILE" | wc -l)
    echo "Total captures detected: $capture_count"
    
    if [ $capture_count -eq 0 ]; then
        echo -e "${RED}No captures detected!${NC}"
        echo ""
        echo "Troubleshooting steps:"
        echo "1. Check if kernel module is properly loaded:"
        echo "   lsmod | grep -i drm"
        echo "2. Check kernel messages:"
        echo "   dmesg | tail -20"
        echo "3. Check module info:"
        echo "   cat $PROC_INFO"
        echo "4. Try triggering more framebuffer activity"
        return 1
    fi
    
    echo ""
    echo "Capture timeline:"
    echo "Time(s) | Count | Data Size | Notes"
    echo "--------|-------|-----------|------"
    
    tail -n +2 "$LOG_FILE" | while IFS=',' read -r time count size timestamp; do
        printf "%7s | %5s | %9s | " "$time" "$count" "$size"
        
        if [ "$size" -gt 0 ]; then
            echo "Data captured"
        else
            echo "Metadata only"
        fi
    done
    
    # Calculate timing statistics
    echo ""
    echo "Timing Analysis:"
    local first_capture=$(tail -n +2 "$LOG_FILE" | head -1 | cut -d',' -f1)
    local last_capture=$(tail -n +2 "$LOG_FILE" | tail -1 | cut -d',' -f1)
    
    if [ -n "$first_capture" ] && [ -n "$last_capture" ] && [ "$capture_count" -gt 1 ]; then
        local duration=$((last_capture - first_capture))
        local avg_interval=$(echo "scale=2; $duration / ($capture_count - 1)" | bc -l 2>/dev/null || echo "N/A")
        echo "  First capture at: ${first_capture}s"
        echo "  Last capture at: ${last_capture}s"
        echo "  Average interval: ${avg_interval}s"
        
        if command -v bc &> /dev/null; then
            local fps=$(echo "scale=1; 1 / $avg_interval" | bc -l 2>/dev/null || echo "N/A")
            echo "  Estimated capture rate: ${fps} Hz"
        fi
    fi
    
    # Show current kernel module status
    echo ""
    echo "Final kernel module status:"
    echo "$(get_capture_count) total framebuffers captured"
    
    # Check for any pixel data
    if [ -r "$PROC_RAW" ]; then
        local raw_size=$(wc -c < "$PROC_RAW" 2>/dev/null || echo "0")
        echo "Current raw data size: $raw_size bytes"
        
        if [ "$raw_size" -gt 0 ]; then
            echo -e "${GREEN}Raw pixel data is available for analysis${NC}"
            echo "To extract: dd if=$PROC_RAW of=framebuffer.raw bs=1"
        else
            echo -e "${YELLOW}No raw pixel data available${NC}"
        fi
    fi
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [test_duration_seconds]"
    echo ""
    echo "Options:"
    echo "  test_duration_seconds  Duration of test (default: 10)"
    echo ""
    echo "Examples:"
    echo "  $0           # Run 10 second test"
    echo "  $0 30        # Run 30 second test"
    echo ""
    echo "This script will:"
    echo "1. Check if the DRM framebuffer kernel module is loaded"
    echo "2. Monitor framebuffer captures for the specified duration"
    echo "3. Trigger display updates to generate framebuffer activity"
    echo "4. Analyze timing and latency of captures"
}

# Main execution
main() {
    # Check if help requested
    if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
        show_usage
        exit 0
    fi
    
    # Check kernel module
    if ! check_kernel_module; then
        exit 1
    fi
    
    # Start background tasks to trigger framebuffer updates
    echo -e "${YELLOW}Starting background display activity...${NC}"
    trigger_fb_updates &
    local trigger_pid=$!
    
    # Monitor captures
    monitor_captures
    
    # Stop background activity
    kill $trigger_pid 2>/dev/null
    
    # Analyze results
    analyze_results
    
    echo ""
    echo -e "${GREEN}Test complete! Log saved to: $LOG_FILE${NC}"
}

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}Warning: Not running as root. You might need sudo for kernel module access.${NC}"
fi

# Run main function
main "$@"
