#!/bin/bash

# Simple Overlay Test Script - SAFE VERSION
# Tests basic overlay plane concepts without risky operations

set -e

MODULE_NAME="simple_overlay_test"
MODULE_DIR="/home/carbon/Documents/WashU/overlay_safety_module"
PROC_FILE="/proc/simple_overlay_test"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

is_module_loaded() {
    lsmod | grep -q "^${MODULE_NAME}"
}

build_module() {
    log_info "Building simple overlay test module..."
    cd "$MODULE_DIR"
    
    if make clean && make; then
        log_success "Module built successfully"
        return 0
    else
        log_error "Failed to build module"
        return 1
    fi
}

load_module() {
    log_info "Loading simple overlay test module..."
    cd "$MODULE_DIR"
    
    if is_module_loaded; then
        log_warning "Module already loaded, unloading first..."
        unload_module
    fi
    
    if insmod simple_overlay_test.ko; then
        log_success "Module loaded successfully"
        sleep 1
        return 0
    else
        log_error "Failed to load module"
        return 1
    fi
}

unload_module() {
    log_info "Unloading simple overlay test module..."
    
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

show_status() {
    log_info "Module status:"
    
    if is_module_loaded; then
        log_success "Module is loaded"
        
        if [[ -f "$PROC_FILE" ]]; then
            log_success "Proc interface available at $PROC_FILE"
            echo
            cat "$PROC_FILE"
        else
            log_warning "Proc interface not available"
        fi
    else
        log_warning "Module is not loaded"
    fi
}

test_overlay_simulation() {
    log_info "Testing overlay simulation..."
    
    if [[ ! -f "$PROC_FILE" ]]; then
        log_error "Module not loaded or proc interface not available"
        return 1
    fi
    
    log_info "Triggering overlay test simulation..."
    echo "test" > "$PROC_FILE"
    
    log_info "Checking kernel messages..."
    dmesg | grep -E "(Simple.*overlay|SIMULATING)" | tail -10 || echo "No simulation messages found"
    
    log_success "Overlay simulation test completed"
}

test_plane_discovery() {
    log_info "Testing plane discovery..."
    
    if [[ ! -f "$PROC_FILE" ]]; then
        log_error "Module not loaded or proc interface not available"
        return 1
    fi
    
    log_info "Triggering plane discovery..."
    echo "discover" > "$PROC_FILE"
    
    log_info "Checking results..."
    cat "$PROC_FILE" | grep -A 10 "Test State" || echo "No state information found"
    
    log_success "Plane discovery test completed"
}

check_drm_system() {
    log_info "Checking DRM system..."
    
    # Check for DRM devices
    if [[ -d /sys/class/drm ]]; then
        log_success "DRM subsystem found"
        log_info "DRM devices:"
        ls -1 /sys/class/drm/ | grep card | head -5
    else
        log_warning "DRM subsystem not found"
    fi
    
    # Check for graphics modules
    log_info "Graphics drivers loaded:"
    lsmod | grep -E "(drm|i915|amdgpu|nouveau|nvidia)" | head -5 || log_warning "No graphics drivers found"
    
    # Check for overlay/cursor plane capabilities
    if which drm_info >/dev/null 2>&1; then
        log_info "DRM capabilities (if drm_info available):"
        drm_info 2>/dev/null | grep -i plane | head -3 || echo "drm_info not available or no plane info"
    fi
}

run_safe_test() {
    log_info "Running safe overlay test sequence..."
    
    check_drm_system
    echo
    
    build_module || exit 1
    echo
    
    load_module || exit 1
    echo
    
    show_status
    echo
    
    test_plane_discovery
    echo
    
    test_overlay_simulation
    echo
    
    show_status
    echo
    
    log_success "Safe test sequence completed successfully!"
    log_info "The module is loaded and ready for testing"
    log_info "To manually test: echo 'test' > $PROC_FILE"
    log_info "To unload: sudo $0 unload"
}

show_help() {
    cat << EOF
Simple Overlay Test Script - SAFE VERSION

This script tests basic DRM overlay plane concepts without risky operations.
No kprobes, no frame interception, no detection algorithms.

Usage: $0 [COMMAND]

Commands:
    build       - Build the kernel module
    load        - Load the kernel module  
    unload      - Unload the kernel module
    status      - Show module status
    test        - Test overlay simulation
    discover    - Test plane discovery
    check       - Check DRM system
    safe-test   - Run complete safe test sequence
    help        - Show this help message

Examples:
    $0 safe-test            # Run complete safe test
    $0 test                 # Test overlay simulation
    $0 status               # Show current status

This version is SAFE and will not crash your system.
EOF
}

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
        test)
            test_overlay_simulation
            ;;
        discover)
            test_plane_discovery
            ;;
        check)
            check_drm_system
            ;;
        safe-test)
            check_root
            run_safe_test
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

main "$@"
