#!/bin/bash

# RUN CPU SCRIPT

set -e  # Exit on any error

# config vars
DURATIONS=("5s" "10s" "60s" "300s" "600s" "1800s")
RESOLUTIONS=(
    # name:height:width
    "720:1280:720"
    "1080:1920:1080" 
    "2000:2560:1440"
)
# screen size
DEFAULT_SIZE=14
# distance away from screen
DEFAULT_DEPTH=24
OUTPUT_FILE="results.csv"

# Print functions
print_status() { echo "[INFO] $1"; }
print_success() { echo "[SUCCESS] $1"; }
print_error() { echo "[ERROR] $1"; }

# Check if required tools are available
if ! command -v cmake &> /dev/null; then
    print_error "cmake not found. Install cmake."
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    print_error "g++ not found. Install build-essential."
    exit 1
fi

# Install OpenCV if needed
if ! pkg-config --exists opencv4; then
    print_status "Installing OpenCV..."
    sudo apt update
    sudo apt install -y libopencv-dev libopencv-contrib-dev
fi

# Build the project using CMake
print_status "Building project with CMake..."

# Create build directory if it doesn't exist
mkdir -p build
cd build

# Configure and build
cmake ..
make -j$(nproc)

print_success "Project built successfully"

# Go back to root directory
cd ..

# Process single video:
process_video() {
    local input_file=$1
    local height=$2
    local width=$3
    
    if [[ ! -f "$input_file" ]]; then
        print_status "Skipping $input_file (not found)"
        return
    fi
    
    print_status "Processing $input_file (${width}x${height})"
    ./build/epicap -in "$input_file" -h "$height" -w "$width" -size "$DEFAULT_SIZE" -d "$DEFAULT_DEPTH" -out "$OUTPUT_FILE"
    print_success "Completed $input_file"
}

# Loop One: Process all durations at standard resolution (1024x576)
print_status "Starting standard resolution loop..."
for duration in "${DURATIONS[@]}"; do
    process_video "${duration}.mp4" 576 1024
done

# Loop Two: Process all durations at multiple resolutions
print_status "Starting multi-resolution loop..."
for duration in "${DURATIONS[@]}"; do
    for resolution in "${RESOLUTIONS[@]}"; do
        IFS=':' read -r res_name width height <<< "$resolution"
        process_video "${duration}_${res_name}.mp4" "$height" "$width"
    done
done

print_success "All processing completed!"
