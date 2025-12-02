#!/bin/bash

echo "=== Screen Dimmer Test ==="

# Check if backlight devices exist
echo "Available backlight devices:"
ls /sys/class/backlight/ 2>/dev/null || echo "No backlight devices found in /sys/class/backlight/"

# Build the module
echo -e "\n=== Building module ==="
make clean && make

if [ ! -f screen_dimmer.ko ]; then
    echo "ERROR: Build failed!"
    exit 1
fi

echo "Build successful!"

# Load the module
echo -e "\n=== Loading module ==="
sudo insmod screen_dimmer.ko

# Check if it loaded successfully
if lsmod | grep -q screen_dimmer; then
    echo "Module loaded successfully!"
else
    echo "ERROR: Module failed to load!"
    exit 1
fi

# Check kernel messages
echo -e "\n=== Kernel messages ==="
dmesg | tail -10 | grep SCREEN_DIMMER

# Test the interface
echo -e "\n=== Testing interface ==="

if [ -d /sys/kernel/screen_dimmer ]; then
    echo "✓ Sysfs interface created"
    
    echo -e "\n--- Device info ---"
    cat /sys/kernel/screen_dimmer/info
    
    echo -e "\n--- Current brightness ---"
    cat /sys/kernel/screen_dimmer/brightness
    
    echo -e "\n=== Running brightness test ==="
    echo "Current brightness: $(cat /sys/kernel/screen_dimmer/brightness)"
    
    echo "Setting to 50%..."
    echo 50 > /sys/kernel/screen_dimmer/brightness
    sleep 1
    echo "Current: $(cat /sys/kernel/screen_dimmer/brightness)"
    
    echo "Setting to 20% (dark)..."
    echo 20 > /sys/kernel/screen_dimmer/brightness
    sleep 2
    echo "Current: $(cat /sys/kernel/screen_dimmer/brightness)"
    
    echo "Restoring original brightness..."
    echo restore > /sys/kernel/screen_dimmer/dim
    sleep 1
    echo "Current: $(cat /sys/kernel/screen_dimmer/brightness)"
    
    echo -e "\n=== Test completed! ==="
    echo "Quick commands:"
    echo "  echo 30 > /sys/kernel/screen_dimmer/brightness  # Set to 30%"
    echo "  echo dark > /sys/kernel/screen_dimmer/dim       # Quick dim"
    echo "  echo normal > /sys/kernel/screen_dimmer/dim     # Full brightness"
    echo "  echo restore > /sys/kernel/screen_dimmer/dim    # Restore original"
    
else
    echo "ERROR: Sysfs interface not found!"
    echo "Check dmesg for error messages"
fi

echo -e "\n=== To unload module later ==="
echo "sudo rmmod screen_dimmer"
