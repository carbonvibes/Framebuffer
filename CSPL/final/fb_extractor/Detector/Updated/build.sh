#!/bin/bash

echo "Building Real-time Framebuffer Epilepsy Monitor"
echo "==============================================="

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake ..

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

# Build the project
echo "Building..."
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo ""
    echo "Build successful!"
    echo "Executable created: build/realtime_monitor"
    echo ""
    echo "To run:"
    echo "  1. Load the kernel module first: sudo insmod kernel.ko"
    echo "  2. Run the monitor: sudo ./build/realtime_monitor"
    echo "  3. Add -HDR flag for HDR content: sudo ./build/realtime_monitor -HDR"
else
    echo "Build failed!"
    exit 1
fi
