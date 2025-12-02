# DRM Overlay Safety Module

This kernel module implements a safety mechanism for protecting against malicious framebuffers by using DRM overlay planes to cover potentially harmful content with safe content.

## Overview

The module demonstrates the concept of using higher-priority DRM planes (overlay or cursor planes) to quickly cover malicious content detected in the primary framebuffer. This approach allows for sub-10ms response times even after v-blank, providing real-time protection against seizure-inducing or harmful visual content.

## Key Features

- **Real-time Frame Analysis**: Intercepts DRM framebuffer initialization to analyze frames
- **Malicious Content Detection**: Placeholder logic for detecting harmful patterns (easily extensible)
- **Emergency Overlay System**: Uses overlay or cursor planes to cover entire screen with safe content
- **Atomic Commits**: Leverages DRM atomic operations for fast, reliable plane updates
- **Statistics and Monitoring**: Provides detailed status via `/proc/overlay_safety`

## Architecture

### Core Components

1. **Frame Interception**: Uses kprobes on `drm_framebuffer_init()` to catch new frames
2. **Analysis Engine**: Analyzes frames for malicious patterns (extensible detection logic)
3. **Safety Overlay System**: Manages overlay/cursor planes for emergency coverage
4. **Work Queue**: Handles overlay application in separate context for performance
5. **Timer System**: Automatically removes safety overlay after timeout

### Safety Mechanism Flow

```
Frame Creation → Analysis → Malicious Detection → Schedule Work → Apply Overlay → Timer Cleanup
                     ↓
              Normal Frame → Continue Processing
```

## Building and Installation

### Prerequisites

- Linux kernel headers for your running kernel
- Build tools (make, gcc)
- Root access for module loading

### Build

```bash
cd overlay_safety_module
make
```

### Install

```bash
make install
```

### Monitor Status

```bash
make status
# or directly:
cat /proc/overlay_safety
```

### View Kernel Messages

```bash
make dmesg
```

### Uninstall

```bash
make uninstall
```

## Testing the Module

### 1. Basic Functionality Test

After loading the module, trigger DRM activity by:
- Opening applications with graphics
- Changing display settings
- Playing videos
- Running graphics benchmarks

Monitor with:
```bash
watch -n 1 cat /proc/overlay_safety
```

### 2. Malicious Frame Simulation

The current detection logic flags every 5th frame as malicious for demonstration. You can modify the `is_frame_malicious()` function to test specific patterns:

```c
// In overlay_safety.c, modify this function:
static bool is_frame_malicious(struct drm_framebuffer *fb)
{
    // Add your detection logic here
    // For testing, you can flag specific formats/sizes
    if (fb->format && fb->format->format == DRM_FORMAT_RGB565) {
        return true; // Flag RGB565 as malicious for testing
    }
    
    return false;
}
```

### 3. Overlay Plane Testing

The module will automatically discover available overlay and cursor planes. Check the proc output to see what planes were found:

```bash
cat /proc/overlay_safety | grep -A 10 "Safety State"
```

## Extending the Detection Logic

The module provides a framework for implementing sophisticated detection algorithms. Key areas for extension:

### 1. Pixel Data Analysis

Integrate with the pixel extraction code from your original module:

```c
// Add to frame_analysis struct:
struct frame_analysis {
    // ...existing fields...
    void *pixel_data;
    size_t pixel_size;
    // Analysis results
    float brightness_variance;
    float color_change_rate;
    bool has_rapid_flashing;
};
```

### 2. Pattern Detection

Implement specific pattern detection:

```c
static bool detect_seizure_patterns(void *pixel_data, uint32_t width, uint32_t height)
{
    // Implement:
    // - Rapid color changes
    // - High contrast patterns
    // - Strobing effects
    // - Geometric patterns at specific frequencies
    return false;
}
```

### 3. Machine Learning Integration

For advanced detection, you could integrate with kernel-based ML:

```c
// Placeholder for ML-based detection
static bool ml_detect_malicious(struct frame_analysis *analysis)
{
    // Feature extraction
    // Model inference
    // Confidence scoring
    return false;
}
```

## Configuration Options

### Compile-time Configuration

Modify these defines in `overlay_safety.c`:

```c
#define MAX_CAPTURES 10              // Number of frames to track
#define SAFETY_OVERLAY_COLOR 0xFF000000  // Black overlay color
#define MALICIOUS_THRESHOLD 50       // Detection threshold
```

### Runtime Tuning

The module could be extended with sysfs parameters:

```c
// Add module parameters
static int detection_sensitivity = 50;
module_param(detection_sensitivity, int, 0644);
MODULE_PARM_DESC(detection_sensitivity, "Detection sensitivity (0-100)");
```

## Performance Considerations

### Response Time

- **Detection**: < 1ms (simple patterns)
- **Overlay Application**: < 5ms (atomic commit)
- **Total Response**: < 10ms (target)

### Memory Usage

- **Per Frame**: ~100 bytes (metadata only)
- **Pixel Buffers**: Optional, for detailed analysis
- **Total**: < 1MB typical usage

### CPU Impact

- **Interception**: Minimal (kprobe overhead)
- **Analysis**: Depends on detection complexity
- **Overlay**: Very low (hardware accelerated)

## Integration with Original Module

To integrate with your framebuffer extraction module:

1. **Shared Data Structures**: Use similar frame capture structures
2. **Pixel Data Integration**: Add pixel extraction to analysis phase
3. **Unified Proc Interface**: Combine status reporting
4. **Coordinated Detection**: Use extracted pixels for detailed analysis

Example integration point:

```c
// In analyze_and_protect()
if (analysis->is_malicious) {
    // Extract pixels for detailed analysis (from your module)
    extract_gem_pixels(fb->obj[0], analysis);
    
    // Perform detailed pattern analysis
    if (detailed_pattern_analysis(analysis->pixel_data)) {
        trigger_protection = true;
    }
}
```

## Debugging and Troubleshooting

### Common Issues

1. **No Planes Found**: Check if display driver supports overlay/cursor planes
2. **Module Load Failures**: Verify kernel headers match running kernel
3. **No Frame Detection**: Ensure graphics activity is occurring

### Debug Logging

Enable verbose logging by modifying log levels:

```c
// Add debug prints
#define DEBUG_VERBOSE 1

#if DEBUG_VERBOSE
#define debug_print(fmt, ...) pr_info("OVERLAY_DEBUG: " fmt, ##__VA_ARGS__)
#else
#define debug_print(fmt, ...) do {} while(0)
#endif
```

### Kernel Log Analysis

```bash
# Watch for module messages
dmesg -w | grep -E "(overlay_safety|DRM.*overlay)"

# Check DRM subsystem messages
dmesg | grep -i drm | tail -20
```

## Safety Considerations

This is a proof-of-concept module for research purposes. For production use:

1. **Thorough Testing**: Test with all graphics drivers and configurations
2. **Error Handling**: Add comprehensive error recovery
3. **Security Review**: Ensure no privilege escalation vulnerabilities
4. **Performance Testing**: Validate minimal impact on graphics performance
5. **Hardware Compatibility**: Test across different GPU vendors and models

## License

GPL v2 - Same as Linux kernel

## Future Enhancements

- **Multi-monitor Support**: Handle multiple displays independently
- **Configurable Overlay Content**: Custom safety patterns, logos, or warnings
- **Persistence**: Remember malicious applications across sessions
- **User-space Communication**: Interface for security applications
- **Hardware Integration**: Use GPU security features when available
