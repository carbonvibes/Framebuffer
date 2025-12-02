# DRM Framebuffer Pixel Extractor with Intel Detiling

This kernel module extracts DRM framebuffer pixel content and automatically converts Intel X-tiled and Y-tiled framebuffers to linear format.

## Features

- **Automatic Intel Tiling Detection**: Detects X-tiled, Y-tiled, and Yf-tiled framebuffers
- **Real-time Detiling**: Converts tiled framebuffers to linear format in the kernel
- **Multiple Access Methods**: Supports SHMEM and DMA-buf pixel extraction
- **Proc Interface**: Easy access through `/proc` filesystem
- **Circular Buffer**: Stores up to 5 recent framebuffer captures

## Intel Tiling Support

The module automatically detects and handles:
- **X-tiling**: 512x8 byte tiles (legacy)
- **Y-tiling**: 128x32 byte tiles (modern)
- **Yf-tiling**: 128x32 byte tiles (compressed)
- **Linear**: No tiling (passthrough)

## Build Requirements

- Linux kernel headers for your running kernel
- GCC compiler
- Make

```bash
# Install kernel headers (Ubuntu/Debian)
sudo apt-get install linux-headers-$(uname -r)

# Or for RHEL/CentOS/Fedora
sudo yum install kernel-devel
# or
sudo dnf install kernel-devel
```

## Building and Installation

```bash
# Build the module
make all

# Install the module
make install

# Or combine both
make clean all install
```

## Usage

### 1. Check Module Status
```bash
make test
# or directly:
cat /proc/drm_fb_pixels
```

This shows captured framebuffer information including:
- Dimensions and format
- Detected tiling type
- Whether detiling was performed
- Pixel data availability

### 2. Extract Linear Framebuffer Data
```bash
make extract
# or directly:
sudo dd if=/proc/drm_fb_raw of=framebuffer_linear.raw bs=1
```

The extracted data is always in linear format, regardless of the original tiling.

### 3. Convert to Image
```bash
# For ARGB8888 format (4 bytes per pixel)
# Convert to PPM format for viewing
width=1920  # Get from proc output
height=1080 # Get from proc output

# Create PPM header and convert
echo "P6 $width $height 255" > framebuffer.ppm
# Extract RGB bytes (skip alpha) and append
dd if=framebuffer_linear.raw bs=4 | \
  hexdump -v -e '1/4 "%c"' -e '1/4 "%c"' -e '1/4 "%c"' -e '1/4 ""' >> framebuffer.ppm
```

## Module Management

```bash
# Reload module (unload, rebuild, install)
make reload

# Unload module
make uninstall

# Clean build files
make clean

# Show build information
make info

# Check if kernel headers are available
make check
```

## Output Format

The module always outputs pixel data in linear format with the following characteristics:

- **Layout**: Row-major order (left-to-right, top-to-bottom)
- **Pixel Format**: Preserved from original (ARGB8888, XRGB8888, etc.)
- **Byte Order**: Native byte order of the system
- **Size**: `width × height × bytes_per_pixel`

## Debugging

### Check Module Loading
```bash
dmesg | tail -20
# Look for messages like:
# "DRM Framebuffer Pixel Extractor with Intel Detiling loaded successfully"
```

### Monitor Framebuffer Captures
```bash
# Watch for new captures
watch -n 1 'cat /proc/drm_fb_pixels'
```

### Check Tiling Detection
```bash
# Look for tiling information in dmesg
dmesg | grep -i "tiling\|detil"
```

## Troubleshooting

### No Pixel Data Available
- Ensure the application is actively rendering to the framebuffer
- Try triggering screen updates (move windows, play video)
- Check dmesg for GEM object access errors

### Build Errors
```bash
# Check kernel headers
make check

# Verify kernel version compatibility
uname -r
ls /lib/modules/$(uname -r)/build
```

### Permission Errors
- Ensure you're using `sudo` for module operations
- Check if the proc files exist: `ls -la /proc/drm_fb_*`

## Performance Notes

- Detiling is performed in kernel space for efficiency
- Large framebuffers (>1080p) are automatically truncated
- Circular buffer prevents memory exhaustion
- Memory allocation uses `vmalloc()` for large buffers

## Comparison with Manual Detiling

**Before (manual process):**
1. Extract tiled framebuffer from kernel module
2. Copy to userspace
3. Run `intel_y_tile_to_linear` tool
4. Get linear output

**After (automatic detiling):**
1. Extract already-linear framebuffer from kernel module
2. Use directly - no additional processing needed

This eliminates the need for the separate detiling step and provides immediate access to linear pixel data.
