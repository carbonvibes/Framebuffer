#!/bin/bash

echo "=== Real-Time Framebuffer Capture Test ==="
echo "Triggering display updates to test Intel atomic commit probe..."

# Check initial state
echo "Initial capture count:"
sudo cat /proc/drm_fb_realtime | grep "Captured framebuffers:"

# Start monitoring kernel messages in background
echo "Starting kernel message monitoring..."
dmesg -w > /tmp/dmesg_output.log &
DMESG_PID=$!

# Wait a moment
sleep 1

echo "Triggering display updates..."

# Method 1: Mouse movements
for i in {1..10}; do
    xdotool mousemove $((100 + i*20)) $((100 + i*10))
    sleep 0.1
done

# Method 2: Create and move a window
if command -v xterm >/dev/null 2>&1; then
    echo "Creating test window..."
    xterm -geometry 200x100+50+50 -e "sleep 2" &
    XTERM_PID=$!
    sleep 1
    
    # Move the window
    for i in {1..5}; do
        xdotool search --name "xterm" windowmove $((50 + i*20)) $((50 + i*10)) 2>/dev/null || true
        sleep 0.2
    done
    
    wait $XTERM_PID 2>/dev/null || true
fi

# Method 3: Force a screen refresh
if command -v xrefresh >/dev/null 2>&1; then
    echo "Forcing screen refresh..."
    xrefresh
fi

# Wait for any delayed captures
sleep 2

# Stop monitoring
kill $DMESG_PID 2>/dev/null || true

echo "Checking for kernel messages..."
if [ -f /tmp/dmesg_output.log ]; then
    echo "Recent kernel messages:"
    tail -20 /tmp/dmesg_output.log | grep -i "atomic\|framebuffer\|primary\|plane" || echo "No relevant messages found"
    rm -f /tmp/dmesg_output.log
fi

echo "Final capture count:"
sudo cat /proc/drm_fb_realtime | grep -E "(Captured framebuffers:|Total capture attempts:|Successful captures:)"

echo "=== Test Complete ==="
