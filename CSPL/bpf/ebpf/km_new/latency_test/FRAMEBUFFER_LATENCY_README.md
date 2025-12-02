# Framebuffer Capture Latency Testing

This directory contains tools to test the latency of your DRM framebuffer capture kernel module.

## Files Created

1. **framebuffer_latency_test.py** - Full-featured Python test with visual patterns
2. **framebuffer_latency_test.sh** - Simple shell script for basic monitoring  
3. **advanced_fb_analyzer.py** - Advanced analysis tool for detailed pixel examination

## Quick Start

### 1. First, make sure your kernel module is loaded:
```bash
# Load your module (adjust path as needed)
sudo insmod /path/to/your/module.ko

# Or if it's installed:
sudo modprobe your_module_name

# Check if it's loaded:
lsmod | grep -i drm
```

### 2. Check if the module is working:
```bash
# Check module status
cat /proc/drm_fb_pixels

# Or use the analyzer
python3 advanced_fb_analyzer.py --info-only
```

### 3. Run a basic latency test:
```bash
# Simple shell-based test (10 seconds)
./framebuffer_latency_test.sh

# Or specify duration
./framebuffer_latency_test.sh 30
```

### 4. Run advanced Python test:
```bash
# Install dependencies first
pip3 install pygame numpy

# Run full test with visual patterns
python3 framebuffer_latency_test.py

# Run with custom settings
python3 framebuffer_latency_test.py --duration 15 --fps 30
```

### 5. Run detailed analysis:
```bash
# Monitor and analyze captures
python3 advanced_fb_analyzer.py --duration 10

# Save results to file
python3 advanced_fb_analyzer.py --duration 10 --output results.json
```

## Understanding the Results

### Latency Categories:
- **< 1ms**: Excellent - capturing current frame
- **1-5ms**: Good - minimal delay  
- **5-16ms**: Acceptable - within one frame at 60fps
- **> 16ms**: High - more than one frame delay

### What the tests measure:
1. **Detection latency**: Time between display update and capture detection
2. **Data availability**: Whether pixel data is successfully captured
3. **Capture frequency**: How often framebuffers are captured
4. **Data integrity**: Whether captured data matches expected patterns

## Troubleshooting

### No captures detected:
1. Check if module is loaded: `lsmod | grep your_module`
2. Check kernel messages: `dmesg | tail -20`
3. Verify proc files exist: `ls -la /proc/drm_fb_*`
4. Try triggering more display activity

### Permission denied:
```bash
# Run with sudo if needed
sudo python3 framebuffer_latency_test.py
sudo ./framebuffer_latency_test.sh
```

### Missing dependencies:
```bash
# For Python scripts
sudo apt update
sudo apt install python3-pygame python3-numpy python3-pip

# For shell script features
sudo apt install imagemagick bc
```

## Interpreting Your Kernel Module

Based on your kernel module code, it:

1. **Intercepts framebuffer initialization** via kprobe on `drm_framebuffer_init`
2. **Detects Intel tiling** and converts to linear format if needed
3. **Captures pixel data** from GEM objects using multiple methods
4. **Stores up to 5 framebuffers** in a circular buffer
5. **Provides access** via `/proc/drm_fb_pixels` (info) and `/proc/drm_fb_raw` (data)

### Expected behavior:
- Captures should occur when new framebuffers are created/updated
- Each capture includes metadata (dimensions, format, tiling) and pixel data
- Data should be in linear format (detiled if Intel GPU)
- Most recent capture is available via `/proc/drm_fb_raw`

## Advanced Usage

### Extract raw framebuffer data:
```bash
# Get the buffer size from info
cat /proc/drm_fb_pixels

# Extract raw data
dd if=/proc/drm_fb_raw of=framebuffer.raw bs=1 count=BUFFER_SIZE

# Convert to viewable format (if you know dimensions)
# For ARGB 1920x1080:
ffmpeg -f rawvideo -pixel_format bgra -video_size 1920x1080 -i framebuffer.raw framebuffer.png
```

### Monitor in real-time:
```bash
# Watch for changes
watch -n 0.1 'cat /proc/drm_fb_pixels | head -10'

# Monitor capture count
while true; do
  count=$(grep "Captured framebuffers:" /proc/drm_fb_pixels | awk '{print $3}')
  echo "$(date): Captures = $count"
  sleep 1
done
```

## Notes

- The kernel module captures during framebuffer initialization, not every frame
- Latency depends on when framebuffers are created vs. when content changes
- For real-time frame capture, you might need to hook into different DRM events
- Intel GPU tiling is automatically detected and converted to linear format
- The module supports multiple concurrent framebuffers and various pixel formats

Run the tests and check the results to understand your specific capture timing!
