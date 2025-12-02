#!/bin/bash

# DRM Overlay Safety Module Test Script
# This script helps test and validate the overlay safety module functionality

set -e

MODULE_NAME="overlay_safety"
MODULE_DIR="/home/carbon/Documents/WashU/overlay_safety_module"
PROC_FILE="/proc/overlay_safety"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root
check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

# Check if module is loaded
is_module_loaded() {
    lsmod | grep -q "^${MODULE_NAME}"
}

# Check if proc file exists
proc_file_exists() {
    [[ -f "$PROC_FILE" ]]
}

# Build the module
build_module() {
    log_info "Building overlay safety module..."
    cd "$MODULE_DIR"
    
    if make clean && make; then
        log_success "Module built successfully"
        return 0
    else
        log_error "Failed to build module"
        return 1
    fi
}

# Load the module
load_module() {
    log_info "Loading overlay safety module..."
    cd "$MODULE_DIR"
    
    if is_module_loaded; then
        log_warning "Module already loaded, unloading first..."
        unload_module
    fi
    
    if insmod overlay_safety.ko; then
        log_success "Module loaded successfully"
        sleep 1  # Give module time to initialize
        return 0
    else
        log_error "Failed to load module"
        return 1
    fi
}

# Unload the module
unload_module() {
    log_info "Unloading overlay safety module..."
    
    if is_module_loaded; then
        if rmmod "$MODULE_NAME"; then
            log_success "Module unloaded successfully"
            return 0
        else
            log_error "Failed to unload module"
            return 1
        fi
    else
        log_warning "Module not loaded"
        return 0
    fi
}

# Show module status
show_status() {
    log_info "Module status:"
    
    if is_module_loaded; then
        log_success "Module is loaded"
        
        if proc_file_exists; then
            log_success "Proc interface available at $PROC_FILE"
            echo
            log_info "Module statistics:"
            echo "----------------------------------------"
            cat "$PROC_FILE"
            echo "----------------------------------------"
        else
            log_warning "Proc interface not available"
        fi
    else
        log_warning "Module is not loaded"
    fi
}

# Monitor module activity
monitor_activity() {
    local duration=${1:-30}
    
    log_info "Monitoring module activity for $duration seconds..."
    log_info "Starting graphics activity to trigger frame analysis..."
    
    # Start some graphics activity in background
    (
        # Try different methods to generate graphics activity
        which glxgears >/dev/null 2>&1 && timeout 10s glxgears >/dev/null 2>&1 &
        which firefox >/dev/null 2>&1 && timeout 5s firefox --new-window about:blank >/dev/null 2>&1 &
        
        # Change brightness to trigger display updates
        for i in {1..5}; do
            if [[ -f /sys/class/backlight/*/brightness ]]; then
                current=$(cat /sys/class/backlight/*/brightness | head -1)
                echo $((current + 1)) > /sys/class/backlight/*/brightness 2>/dev/null || true
                sleep 1
                echo "$current" > /sys/class/backlight/*/brightness 2>/dev/null || true
                sleep 1
            fi
        done
    ) &
    
    # Monitor for specified duration
    local count=0
    while [[ $count -lt $duration ]]; do
        if proc_file_exists; then
            clear
            echo "=== Overlay Safety Module Monitor (${count}s/${duration}s) ==="
            cat "$PROC_FILE"
            echo
            echo "Recent kernel messages:"
            dmesg | grep -E "(overlay_safety|DRM.*overlay)" | tail -5 || echo "No recent messages"
            echo
            echo "Press Ctrl+C to stop monitoring early"
        else
            log_error "Proc interface not available"
            break
        fi
        
        sleep 2
        count=$((count + 2))
    done
    
    # Clean up background processes
    pkill -f glxgears 2>/dev/null || true
    pkill -f firefox 2>/dev/null || true
    
    log_success "Monitoring completed"
}

# Test malicious frame detection
test_detection() {
    log_info "Testing malicious frame detection..."
    
    if ! proc_file_exists; then
        log_error "Module not loaded or proc interface not available"
        return 1
    fi
    
    log_info "Current statistics:"
    grep -E "(frames analyzed|Malicious frames|Detection rate)" "$PROC_FILE" || true
    
    log_info "Generating graphics activity to trigger detection..."
    
    # Generate various graphics activities
    for i in {1..10}; do
        log_info "Trigger attempt $i/10..."
        
        # Try to trigger different framebuffer formats/sizes
        if which xrandr >/dev/null 2>&1; then
            # Change resolution briefly (this often creates new framebuffers)
            current_res=$(xrandr | grep '\*' | awk '{print $1}' | head -1)
            available_res=$(xrandr | grep -v '\*' | grep 'x' | awk '{print $1}' | head -1)
            
            if [[ -n "$available_res" && "$available_res" != "$current_res" ]]; then
                log_info "Testing resolution change: $current_res -> $available_res"
                xrandr -s "$available_res" 2>/dev/null || true
                sleep 1
                xrandr -s "$current_res" 2>/dev/null || true
                sleep 1
            fi
        fi
        
        # Create window activity
        if which xterm >/dev/null 2>&1; then
            timeout 2s xterm -e "echo 'Graphics test $i'; sleep 1" >/dev/null 2>&1 &
        fi
        
        sleep 2
    done
    
    sleep 2
    
    log_info "Final statistics:"
    grep -E "(frames analyzed|Malicious frames|Detection rate)" "$PROC_FILE" || true
    
    # Check for emergency mode activation
    if grep -q "Emergency Mode: ACTIVE" "$PROC_FILE"; then
        log_success "Emergency mode was activated during test!"
    else
        log_info "No emergency mode activation detected"
    fi
}

# Check system compatibility
check_compatibility() {
    log_info "Checking system compatibility..."
    
    # Check kernel version
    kernel_version=$(uname -r)
    log_info "Kernel version: $kernel_version"
    
    # Check for DRM support
    if [[ -d /sys/class/drm ]]; then
        log_success "DRM subsystem detected"
        
        # List DRM devices
        log_info "DRM devices:"
        ls -la /sys/class/drm/ | grep card || log_warning "No DRM cards found"
    else
        log_warning "DRM subsystem not found"
    fi
    
    # Check for graphics drivers
    log_info "Loaded graphics drivers:"
    lsmod | grep -E "(i915|amdgpu|nouveau|nvidia)" || log_warning "No common graphics drivers found"
    
    # Check kernel headers
    if [[ -d "/lib/modules/$kernel_version/build" ]]; then
        log_success "Kernel headers available"
    else
        log_warning "Kernel headers not found - may need to install linux-headers-$kernel_version"
    fi
    
    # Check for required build tools
    for tool in make gcc; do
        if which "$tool" >/dev/null 2>&1; then
            log_success "$tool available"
        else
            log_warning "$tool not found"
        fi
    done
}

# Show help
show_help() {
    cat << EOF
DRM Overlay Safety Module Test Script

Usage: $0 [COMMAND]

Commands:
    build       - Build the kernel module
    load        - Load the kernel module
    unload      - Unload the kernel module
    status      - Show module status and statistics
    monitor     - Monitor module activity (default: 30 seconds)
    test        - Test malicious frame detection
    check       - Check system compatibility
    full-test   - Run complete test sequence
    help        - Show this help message

Examples:
    $0 full-test                # Run complete test sequence
    $0 monitor 60               # Monitor for 60 seconds
    $0 status                   # Show current status

Note: This script must be run as root (use sudo)
EOF
}

# Run full test sequence
full_test() {
    log_info "Running full test sequence..."
    
    check_compatibility
    echo
    
    build_module || exit 1
    echo
    
    load_module || exit 1
    echo
    
    show_status
    echo
    
    test_detection
    echo
    
    monitor_activity 20
    echo
    
    show_status
    echo
    
    log_info "Full test sequence completed"
    log_info "To unload module: $0 unload"
}

# Main script logic
main() {
    case "${1:-help}" in
        build)
            check_root
            build_module
            ;;
        load)
            check_root
            build_module && load_module
            ;;
        unload)
            check_root
            unload_module
            ;;
        status)
            show_status
            ;;
        monitor)
            monitor_activity "${2:-30}"
            ;;
        test)
            check_root
            test_detection
            ;;
        check)
            check_compatibility
            ;;
        full-test)
            check_root
            full_test
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            log_error "Unknown command: $1"
            show_help
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"
