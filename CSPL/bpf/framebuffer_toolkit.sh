#!/bin/bash

# Framebuffer Extraction Setup and Usage Script
# This script helps set up and run the framebuffer extraction tools

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        print_warning "Running as root. This is required for framebuffer access."
        return 0
    else
        print_error "This script requires root privileges for framebuffer access."
        print_status "Please run with sudo: sudo $0 $@"
        exit 1
    fi
}

# Check system requirements
check_requirements() {
    print_status "Checking system requirements..."
    
    # Check for framebuffer device
    if [[ ! -e /dev/fb0 ]]; then
        print_error "/dev/fb0 not found. Framebuffer device may not be available."
        print_status "Try loading framebuffer modules: modprobe fb"
        exit 1
    fi
    print_success "Framebuffer device /dev/fb0 found"
    
    # Check for bpftrace
    if ! command -v bpftrace &> /dev/null; then
        print_error "bpftrace not found. Please install bpftrace for eBPF functionality."
        print_status "Ubuntu/Debian: apt install bpftrace"
        print_status "CentOS/RHEL: yum install bpftrace"
        exit 1
    fi
    print_success "bpftrace found: $(which bpftrace)"
    
    # Check for gcc
    if ! command -v gcc &> /dev/null; then
        print_error "gcc not found. Please install build tools."
        print_status "Ubuntu/Debian: apt install build-essential"
        exit 1
    fi
    print_success "gcc found: $(which gcc)"
    
    # Check eBPF support
    if [[ ! -d /sys/kernel/debug/tracing ]]; then
        print_error "eBPF tracing not available. debugfs may not be mounted."
        print_status "Try: mount -t debugfs debugfs /sys/kernel/debug"
        exit 1
    fi
    print_success "eBPF tracing support available"
    
    # Check for specific tracepoints
    if [[ -d /sys/kernel/debug/tracing/events/drm ]]; then
        print_success "DRM tracepoints available"
    else
        print_warning "DRM tracepoints not found - limited eBPF functionality"
    fi
    
    if [[ -d /sys/kernel/debug/tracing/events/i915 ]]; then
        print_success "i915 graphics tracepoints available"
    else
        print_warning "i915 tracepoints not found - you may have different graphics hardware"
    fi
}

# Build the C extractor
build_extractor() {
    print_status "Building framebuffer extractor..."
    
    if make clean && make all; then
        print_success "Framebuffer extractor built successfully"
    else
        print_error "Failed to build framebuffer extractor"
        exit 1
    fi
}

# Setup eBPF scripts
setup_ebpf() {
    print_status "Setting up eBPF scripts..."
    
    chmod +x *.bt
    print_success "eBPF scripts are now executable"
}

# Display framebuffer information
show_framebuffer_info() {
    print_status "Gathering framebuffer information..."
    
    if [[ -e /dev/fb0 ]]; then
        print_status "Framebuffer device information:"
        fbset -i 2>/dev/null || print_warning "fbset not available, using alternative method"
        
        # Try to get info from sysfs
        if [[ -d /sys/class/graphics/fb0 ]]; then
            print_status "Framebuffer details from sysfs:"
            echo "  Name: $(cat /sys/class/graphics/fb0/name 2>/dev/null || echo 'Unknown')"
            echo "  Mode: $(cat /sys/class/graphics/fb0/mode 2>/dev/null || echo 'Unknown')"
            echo "  State: $(cat /sys/class/graphics/fb0/state 2>/dev/null || echo 'Unknown')"
        fi
    fi
    
    # Show graphics hardware
    print_status "Graphics hardware information:"
    lspci | grep -i vga || print_warning "No VGA devices found"
    lspci | grep -i display || print_warning "No display devices found"
}

# Run basic framebuffer extraction
run_basic_extraction() {
    print_status "Running basic framebuffer extraction..."
    
    if [[ ! -f ./framebuffer_extractor ]]; then
        print_error "Framebuffer extractor not built. Building now..."
        build_extractor
    fi
    
    print_status "Analyzing current framebuffer content..."
    ./framebuffer_extractor --analyze --save
    
    if [[ -f framebuffer_dump.ppm ]]; then
        print_success "Framebuffer captured as framebuffer_dump.ppm"
        file framebuffer_dump.ppm
    fi
    
    if [[ -f framebuffer_dump.raw ]]; then
        print_success "Raw framebuffer data saved as framebuffer_dump.raw"
        ls -lh framebuffer_dump.raw
    fi
}

# Run eBPF tracing
run_ebpf_tracing() {
    local script="$1"
    local duration="${2:-30}"
    
    print_status "Running eBPF tracing with $script for ${duration} seconds..."
    print_warning "This will trace kernel graphics operations. Generate some graphics activity!"
    print_status "Try moving windows, playing videos, or running graphical applications..."
    
    timeout ${duration}s bpftrace "$script" || print_status "eBPF tracing completed (or timed out)"
}

# Interactive menu
show_menu() {
    echo ""
    echo "======================================"
    echo "  Framebuffer Extraction Toolkit"
    echo "======================================"
    echo ""
    echo "1) Check system requirements"
    echo "2) Show framebuffer information"
    echo "3) Build framebuffer extractor"
    echo "4) Run basic framebuffer extraction"
    echo "5) Run eBPF framebuffer tracing (30s)"
    echo "6) Run eBPF advanced data extraction (30s)"
    echo "7) Run eBPF pixel capture (30s)"
    echo "8) Monitor framebuffer changes (Ctrl+C to stop)"
    echo "9) Combined extraction (C + eBPF)"
    echo "10) View captured images"
    echo "11) Clean up output files"
    echo "0) Exit"
    echo ""
    echo -n "Select an option [0-11]: "
}

# Main interactive loop
interactive_mode() {
    while true; do
        show_menu
        read -r choice
        
        case $choice in
            1)
                check_requirements
                ;;
            2)
                show_framebuffer_info
                ;;
            3)
                build_extractor
                ;;
            4)
                run_basic_extraction
                ;;
            5)
                setup_ebpf
                run_ebpf_tracing "framebuffer_tracer.bt" 30
                ;;
            6)
                setup_ebpf
                run_ebpf_tracing "framebuffer_data_extractor.bt" 30
                ;;
            7)
                setup_ebpf
                run_ebpf_tracing "pixel_data_capture.bt" 30
                ;;
            8)
                if [[ ! -f ./framebuffer_extractor ]]; then
                    build_extractor
                fi
                print_status "Starting framebuffer monitoring. Press Ctrl+C to stop."
                ./framebuffer_extractor --monitor
                ;;
            9)
                build_extractor
                setup_ebpf
                print_status "Starting combined extraction..."
                print_status "The eBPF tracer will run for 60 seconds while monitoring framebuffer"
                ./framebuffer_extractor --monitor &
                EXTRACTOR_PID=$!
                sleep 2
                timeout 60s bpftrace framebuffer_tracer.bt || true
                kill $EXTRACTOR_PID 2>/dev/null || true
                wait $EXTRACTOR_PID 2>/dev/null || true
                print_success "Combined extraction completed"
                ;;
            10)
                if command -v display &> /dev/null; then
                    for img in *.ppm; do
                        if [[ -f "$img" ]]; then
                            print_status "Displaying $img..."
                            display "$img" &
                        fi
                    done
                else
                    print_warning "ImageMagick not installed. Listing available images:"
                    ls -la *.ppm 2>/dev/null || print_status "No .ppm files found"
                fi
                ;;
            11)
                print_status "Cleaning up output files..."
                rm -f *.raw *.ppm framebuffer_*.raw framebuffer_*.ppm
                make clean 2>/dev/null || true
                print_success "Cleanup completed"
                ;;
            0)
                print_status "Exiting..."
                exit 0
                ;;
            *)
                print_error "Invalid option. Please try again."
                ;;
        esac
        
        echo ""
        echo "Press Enter to continue..."
        read -r
    done
}

# Main script logic
main() {
    echo "Framebuffer Extraction Toolkit"
    echo "=============================="
    echo ""
    
    # Parse command line arguments
    case "${1:-}" in
        "--check"|"-c")
            check_requirements
            ;;
        "--build"|"-b")
            check_root
            build_extractor
            ;;
        "--extract"|"-e")
            check_root
            check_requirements
            build_extractor
            run_basic_extraction
            ;;
        "--trace"|"-t")
            check_root
            check_requirements
            setup_ebpf
            run_ebpf_tracing "framebuffer_tracer.bt" "${2:-30}"
            ;;
        "--monitor"|"-m")
            check_root
            check_requirements
            build_extractor
            ./framebuffer_extractor --monitor
            ;;
        "--help"|"-h")
            echo "Usage: $0 [option]"
            echo ""
            echo "Options:"
            echo "  --check, -c     Check system requirements"
            echo "  --build, -b     Build the framebuffer extractor"
            echo "  --extract, -e   Run basic framebuffer extraction"
            echo "  --trace, -t     Run eBPF tracing [duration]"
            echo "  --monitor, -m   Monitor framebuffer changes"
            echo "  --help, -h      Show this help"
            echo ""
            echo "If no option is provided, interactive mode will start."
            ;;
        "")
            check_root
            check_requirements
            interactive_mode
            ;;
        *)
            print_error "Unknown option: $1"
            print_status "Use --help for usage information"
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"
