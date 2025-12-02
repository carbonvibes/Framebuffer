#!/bin/bash

echo "=== Simple Frame Freezer Test ==="

# Build
echo "Building module..."
make clean && make

if [ ! -f simple_freeze.ko ]; then
    echo "Build failed!"
    exit 1
fi

# Load module
echo "Loading module..."
sudo insmod simple_freeze.ko

# Show usage
echo -e "\n=== Usage ==="
echo "Set freeze duration: echo 5 > /sys/module/simple_freeze/parameters/freeze_duration"
echo "Set frame delay:     echo 1000 > /sys/module/simple_freeze/parameters/frame_delay_ms"
echo "Start freeze:        echo 1 > /sys/kernel/simple_freeze/freeze"
echo "Check status:        cat /sys/kernel/simple_freeze/status"

echo -e "\n=== Current Parameters ==="
echo "Duration: $(cat /sys/module/simple_freeze/parameters/freeze_duration) seconds"
echo "Delay:    $(cat /sys/module/simple_freeze/parameters/frame_delay_ms) ms"
echo "Status:   $(cat /sys/kernel/simple_freeze/status)"

echo -e "\n=== Quick Test (3 seconds) ==="
echo "Starting freeze..."
sudo bash -c "echo 1 > /sys/kernel/simple_freeze/freeze"
echo "Check dmesg for results!"

echo -e "\n=== To unload later ==="
echo "sudo rmmod simple_freeze"
