#!/bin/bash

# DRM Framebuffer Linear Extractor Helper Script

MODULE_NAME="drm_fb_pixel_extractor_v3"
PROC_INFO="/proc/drm_fb_pixels"
PROC_RAW="/proc/drm_fb_raw"

function show_usage() {
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  load       - Load the kernel module"
    echo "  unload     - Unload the kernel module"
    echo "  reload     - Reload the kernel module"
    echo "  info       - Show captured framebuffer information"
    echo "  extract    - Extract the latest framebuffer to a file"
    echo "  view       - Extract and convert to viewable image (requires ffmpeg)"
    echo "  monitor    - Monitor for new framebuffer captures"
    echo ""
}

function load_module() {
    echo "Loading DRM framebuffer extractor module..."
    if sudo insmod ${MODULE_NAME}.ko; then
        echo "Module loaded successfully"
        echo "You can now view capture info with: cat $PROC_INFO"
    else
        echo "Failed to load module"
        exit 1
    fi
}

function unload_module() {
    echo "Unloading DRM framebuffer extractor module..."
    if sudo rmmod $MODULE_NAME; then
        echo "Module unloaded successfully"
    else
        echo "Failed to unload module (may not be loaded)"
    fi
}

function reload_module() {
    unload_module
    sleep 1
    load_module
}

function show_info() {
    if [ ! -f "$PROC_INFO" ]; then
        echo "Module not loaded or proc entry not available"
        exit 1
    fi
    
    echo "=== DRM Framebuffer Capture Information ==="
    cat $PROC_INFO
}

function extract_framebuffer() {
    if [ ! -f "$PROC_RAW" ]; then
        echo "Module not loaded or no framebuffer data available"
        exit 1
    fi
    
    local output_file="${1:-framebuffer_linear.raw}"
    
    echo "Extracting framebuffer data to $output_file..."
    
    # Get framebuffer info
    local info=$(cat $PROC_INFO 2>/dev/null)
    if [ -z "$info" ]; then
        echo "No framebuffer data available"
        exit 1
    fi
    
    # Extract the most recent capture
    cp $PROC_RAW "$output_file"
    local size=$(stat -c%s "$output_file" 2>/dev/null || echo "unknown")
    
    echo "Extracted $size bytes to $output_file"
    
    # Try to extract dimensions from proc info
    local width=$(echo "$info" | grep -oP 'Dimensions: \K\d+(?=x)' | tail -1)
    local height=$(echo "$info" | grep -oP 'Dimensions: \d+x\K\d+' | tail -1)
    local format=$(echo "$info" | grep -oP 'Format: 0x[0-9a-fA-F]+ \(\K[^)]+' | tail -1)
    
    if [ ! -z "$width" ] && [ ! -z "$height" ]; then
        echo "Detected dimensions: ${width}x${height}, format: ${format:-unknown}"
        echo ""
        echo "To convert to viewable image, you can use:"
        echo "  ffmpeg -f rawvideo -pixel_format bgra -video_size ${width}x${height} -i $output_file output.png"
        echo ""
        echo "For other formats, try:"
        echo "  - XRGB8888/ARGB8888: -pixel_format bgra"
        echo "  - XBGR8888/ABGR8888: -pixel_format rgba"  
        echo "  - RGB565: -pixel_format rgb565le"
    fi
}

function view_framebuffer() {
    local temp_raw="temp_fb.raw"
    local output_png="${1:-framebuffer_linear.png}"
    
    # Extract framebuffer first
    extract_framebuffer "$temp_raw"
    
    # Get dimensions from proc info
    local info=$(cat $PROC_INFO 2>/dev/null)
    local width=$(echo "$info" | grep -oP 'Dimensions: \K\d+(?=x)' | tail -1)
    local height=$(echo "$info" | grep -oP 'Dimensions: \d+x\K\d+' | tail -1)
    local format=$(echo "$info" | grep -oP 'Format: 0x[0-9a-fA-F]+ \(\K[^)]+' | tail -1)
    
    if [ -z "$width" ] || [ -z "$height" ]; then
        echo "Could not determine framebuffer dimensions"
        rm -f "$temp_raw"
        exit 1
    fi
    
    echo "Converting ${width}x${height} ${format} framebuffer to PNG..."
    
    # Determine pixel format for ffmpeg
    local pixel_format="bgra"
    case "$format" in
        "XBGR8888"|"ABGR8888") pixel_format="rgba" ;;
        "RGB565") pixel_format="rgb565le" ;;
        *) pixel_format="bgra" ;;
    esac
    
    if command -v ffmpeg >/dev/null 2>&1; then
        if ffmpeg -y -f rawvideo -pixel_format "$pixel_format" \
                   -video_size "${width}x${height}" \
                   -i "$temp_raw" "$output_png" 2>/dev/null; then
            echo "Created $output_png"
            echo "File size: $(stat -c%s "$output_png" 2>/dev/null || echo "unknown") bytes"
        else
            echo "Failed to convert with ffmpeg"
        fi
    else
        echo "ffmpeg not found - install it to convert raw data to PNG"
        echo "Raw framebuffer saved as $temp_raw"
        return
    fi
    
    rm -f "$temp_raw"
}

function monitor_captures() {
    echo "Monitoring framebuffer captures (Ctrl+C to stop)..."
    echo "Watching $PROC_INFO for changes..."
    
    local last_count=0
    
    while true; do
        if [ -f "$PROC_INFO" ]; then
            local current_info=$(cat $PROC_INFO 2>/dev/null)
            local current_count=$(echo "$current_info" | grep -oP 'Captured framebuffers: \K\d+' || echo "0")
            
            if [ "$current_count" -gt "$last_count" ]; then
                echo ""
                echo "=== New capture detected at $(date) ==="
                echo "$current_info" | tail -20
                last_count=$current_count
            fi
        fi
        sleep 1
    done
}

# Main script logic
case "${1:-info}" in
    "load")
        load_module
        ;;
    "unload")
        unload_module
        ;;
    "reload")
        reload_module
        ;;
    "info")
        show_info
        ;;
    "extract")
        extract_framebuffer "$2"
        ;;
    "view")
        view_framebuffer "$2"
        ;;
    "monitor")
        monitor_captures
        ;;
    *)
        show_usage
        exit 1
        ;;
esac
