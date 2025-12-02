#!/bin/bash

# Master test script for all safe frame freezing methods
# Run this to test all methods safely

echo "=========================================="
echo "Safe Frame Freezing Methods Test Suite"
echo "=========================================="
echo ""

BASE_DIR="/home/carbon/Documents/WashU/bpf/ebpf/km_block/safe_methods"
METHODS=("method1_atomic_commit" "method2_vsync_block" "method3_input_block" "method4_page_flip")
METHOD_NAMES=("Atomic Commit Blocker" "VSync/PageFlip Blocker" "Input Blocker (Safest)" "Page Flip Blocker")

# Function to cleanup any existing modules
cleanup_modules() {
    echo "Cleaning up any existing modules..."
    sudo rmmod atomic_commit_blocker 2>/dev/null || true
    sudo rmmod vsync_blocker 2>/dev/null || true
    sudo rmmod input_blocker 2>/dev/null || true
    sudo rmmod pageflip_blocker 2>/dev/null || true
    sudo rmmod drm_frame_blocker_minimal 2>/dev/null || true
    echo "Cleanup complete."
    echo ""
}

# Function to test a specific method
test_method() {
    local method_dir=$1
    local method_name=$2
    local method_num=$3
    
    echo "=========================================="
    echo "Testing Method $method_num: $method_name"
    echo "=========================================="
    
    cd "$BASE_DIR/$method_dir" || {
        echo "Error: Cannot enter directory $method_dir"
        return 1
    }
    
    echo "Building $method_name..."
    make clean > /dev/null 2>&1
    if ! make > /dev/null 2>&1; then
        echo "❌ Build failed for $method_name"
        echo "Check build errors:"
        make
        return 1
    fi
    
    echo "✅ Build successful for $method_name"
    
    echo "Installing module..."
    if ! make install > /dev/null 2>&1; then
        echo "❌ Installation failed for $method_name"
        dmesg | tail -5
        return 1
    fi
    
    echo "✅ Module installed successfully"
    
    # Wait a moment for module to initialize
    sleep 1
    
    echo "Running test..."
    case $method_num in
        1)
            echo "Testing atomic commit blocking (2 second freeze)..."
            echo 2 | sudo tee /sys/module/atomic_commit_blocker/parameters/freeze_duration > /dev/null
            echo 1 | sudo tee /sys/kernel/atomic_freezer/trigger > /dev/null
            sleep 3
            cat /sys/kernel/atomic_freezer/status
            ;;
        2)
            echo "Testing VSync blocking (2 second freeze)..."
            echo 2 | sudo tee /sys/module/vsync_blocker/parameters/freeze_duration > /dev/null
            echo 1 | sudo tee /sys/kernel/vsync_freezer/freeze > /dev/null
            sleep 3
            cat /sys/kernel/vsync_freezer/status
            ;;
        3)
            echo "Testing input blocking (safest - 3 second freeze)..."
            echo 3 | sudo tee /sys/module/input_blocker/parameters/freeze_duration > /dev/null
            echo "Try moving your mouse or typing - it should be blocked!"
            echo 1 | sudo tee /sys/kernel/input_freezer/freeze > /dev/null
            sleep 4
            echo "Input should be restored now"
            cat /sys/kernel/input_freezer/status
            ;;
        4)
            echo "Testing page flip blocking (2 second freeze)..."
            echo 2 | sudo tee /sys/module/pageflip_blocker/parameters/freeze_duration > /dev/null
            echo 1 | sudo tee /sys/kernel/pageflip_freezer/freeze > /dev/null
            sleep 3
            cat /sys/kernel/pageflip_freezer/status
            ;;
    esac
    
    echo ""
    echo "Recent kernel messages for this test:"
    dmesg | tail -5 | grep -E "(ATOMIC_BLOCKER|VSYNC_BLOCKER|INPUT_BLOCKER|PAGEFLIP_BLOCKER)"
    
    echo ""
    echo "Uninstalling $method_name..."
    make uninstall > /dev/null 2>&1
    
    echo "✅ Test completed for $method_name"
    echo ""
    
    return 0
}

# Function to show menu
show_menu() {
    echo "Select testing mode:"
    echo "1) Test Method 1: Atomic Commit Blocker (safest for display)"
    echo "2) Test Method 2: VSync/PageFlip Blocker"
    echo "3) Test Method 3: Input Blocker (safest overall)"
    echo "4) Test Method 4: Page Flip Blocker"
    echo "5) Test all methods sequentially"
    echo "6) Build all methods (no testing)"
    echo "7) Cleanup and exit"
    echo ""
    read -p "Enter your choice (1-7): " choice
}

# Function to build all methods
build_all() {
    echo "Building all methods..."
    for i in "${!METHODS[@]}"; do
        method_dir="${METHODS[$i]}"
        method_name="${METHOD_NAMES[$i]}"
        
        echo "Building $method_name..."
        cd "$BASE_DIR/$method_dir" || continue
        
        make clean > /dev/null 2>&1
        if make > /dev/null 2>&1; then
            echo "✅ $method_name built successfully"
        else
            echo "❌ $method_name build failed"
        fi
    done
    echo "Build phase completed."
}

# Main execution
cleanup_modules

while true; do
    show_menu
    
    case $choice in
        1)
            test_method "${METHODS[0]}" "${METHOD_NAMES[0]}" 1
            ;;
        2)
            test_method "${METHODS[1]}" "${METHOD_NAMES[1]}" 2
            ;;
        3)
            test_method "${METHODS[2]}" "${METHOD_NAMES[2]}" 3
            ;;
        4)
            test_method "${METHODS[3]}" "${METHOD_NAMES[3]}" 4
            ;;
        5)
            echo "Testing all methods sequentially..."
            for i in "${!METHODS[@]}"; do
                test_method "${METHODS[$i]}" "${METHOD_NAMES[$i]}" $((i+1))
                if [ $i -lt $((${#METHODS[@]}-1)) ]; then
                    echo "Waiting 2 seconds before next test..."
                    sleep 2
                fi
            done
            echo "All tests completed!"
            ;;
        6)
            build_all
            ;;
        7)
            cleanup_modules
            echo "Goodbye!"
            exit 0
            ;;
        *)
            echo "Invalid choice. Please try again."
            ;;
    esac
    
    echo ""
    read -p "Press Enter to continue..."
    echo ""
done
