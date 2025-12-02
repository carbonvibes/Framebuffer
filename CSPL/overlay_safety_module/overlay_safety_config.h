#ifndef OVERLAY_SAFETY_CONFIG_H
#define OVERLAY_SAFETY_CONFIG_H

/*
 * Configuration header for DRM Overlay Safety Module
 * Adjust these parameters to tune detection sensitivity and behavior
 */

/* Detection Algorithm Configuration */

// Rapid flashing detection
#define FLASHING_BRIGHT_THRESHOLD    200    // Brightness level considered "bright" (0-255)
#define FLASHING_DARK_THRESHOLD      50     // Brightness level considered "dark" (0-255)
#define FLASHING_RATIO_THRESHOLD     0.7    // Ratio of bright/dark pixels to trigger detection

// Seizure pattern detection
#define SEIZURE_COLOR_DIFF_THRESHOLD 128    // Color difference threshold for adjacent pixels
#define SEIZURE_PATTERN_RATIO        0.3    // Ratio of high-contrast transitions to trigger

// High contrast detection
#define CONTRAST_BRIGHT_LIMIT        240    // RGB values above this are "very bright"
#define CONTRAST_DARK_LIMIT          15     // RGB values below this are "very dark"
#define CONTRAST_RATIO_THRESHOLD     0.4    // Ratio of extreme pixels to trigger

// Luminance variance
#define LUMINANCE_VARIANCE_THRESHOLD 10000.0 // Variance threshold for detection

// Risk scoring weights
#define RISK_WEIGHT_FLASHING         40.0   // Weight for rapid flashing detection
#define RISK_WEIGHT_SEIZURE          35.0   // Weight for seizure pattern detection
#define RISK_WEIGHT_CONTRAST         25.0   // Weight for high contrast detection
#define RISK_WEIGHT_VARIANCE         20.0   // Weight for high luminance variance

// Overall risk threshold
#define MALICIOUS_RISK_THRESHOLD     50.0   // Risk score above which frame is considered malicious

/* Performance Configuration */

// Pixel sampling for performance
#define MAX_PIXELS_SAMPLE            10000  // Maximum pixels to sample for analysis
#define VARIANCE_PIXELS_SAMPLE       5000   // Pixels to sample for variance calculation

// Analysis timing
#define ANALYSIS_TIMEOUT_MS          5      // Maximum time to spend on analysis (milliseconds)
#define OVERLAY_TRIGGER_DELAY_MS     1      // Delay before triggering overlay (for debouncing)

/* Safety Overlay Configuration */

// Overlay appearance
#define SAFETY_OVERLAY_COLOR         0xFF000000  // ARGB color for safety overlay (black)
#define SAFETY_OVERLAY_OPACITY       255         // Opacity (0-255, 255 = fully opaque)
#define SAFETY_OVERLAY_DURATION_MS   1000        // How long to keep overlay active

// Alternative safety colors
#define SAFETY_COLOR_BLACK           0xFF000000  // Solid black
#define SAFETY_COLOR_WHITE           0xFFFFFFFF  // Solid white  
#define SAFETY_COLOR_GRAY            0xFF808080  // Medium gray
#define SAFETY_COLOR_BLUE            0xFF0000FF  // Blue (calming)

/* Detection Tuning Profiles */

// Conservative profile (fewer false positives)
#ifdef CONFIG_CONSERVATIVE_DETECTION
#undef FLASHING_RATIO_THRESHOLD
#define FLASHING_RATIO_THRESHOLD     0.8
#undef SEIZURE_PATTERN_RATIO
#define SEIZURE_PATTERN_RATIO        0.4
#undef CONTRAST_RATIO_THRESHOLD  
#define CONTRAST_RATIO_THRESHOLD     0.5
#undef MALICIOUS_RISK_THRESHOLD
#define MALICIOUS_RISK_THRESHOLD     70.0
#endif

// Aggressive profile (catch more potential threats)
#ifdef CONFIG_AGGRESSIVE_DETECTION
#undef FLASHING_RATIO_THRESHOLD
#define FLASHING_RATIO_THRESHOLD     0.5
#undef SEIZURE_PATTERN_RATIO
#define SEIZURE_PATTERN_RATIO        0.2
#undef CONTRAST_RATIO_THRESHOLD
#define CONTRAST_RATIO_THRESHOLD     0.3
#undef MALICIOUS_RISK_THRESHOLD
#define MALICIOUS_RISK_THRESHOLD     30.0
#endif

/* Hardware-Specific Configuration */

// Intel GPU optimizations
#ifdef CONFIG_INTEL_GPU_OPTIMIZATIONS
#define INTEL_TILING_AWARE           1       // Take tiling into account for analysis
#define INTEL_OVERLAY_PREFERRED      1       // Prefer overlay plane over cursor
#endif

// AMD GPU optimizations  
#ifdef CONFIG_AMD_GPU_OPTIMIZATIONS
#define AMD_ASYNC_ANALYSIS           1       // Use async analysis for AMD GPUs
#endif

// NVIDIA GPU optimizations
#ifdef CONFIG_NVIDIA_GPU_OPTIMIZATIONS
#define NVIDIA_CUDA_ACCELERATION     1       // Use CUDA for analysis if available
#endif

/* Debug and Logging Configuration */

// Debug levels
#define DEBUG_LEVEL_NONE             0
#define DEBUG_LEVEL_BASIC            1
#define DEBUG_LEVEL_DETAILED         2
#define DEBUG_LEVEL_VERBOSE          3

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL                  DEBUG_LEVEL_BASIC
#endif

// Debug macros
#if DEBUG_LEVEL >= DEBUG_LEVEL_BASIC
#define debug_basic(fmt, ...)        pr_info("OVERLAY_DEBUG: " fmt, ##__VA_ARGS__)
#else
#define debug_basic(fmt, ...)        do {} while(0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_DETAILED
#define debug_detailed(fmt, ...)     pr_info("OVERLAY_DETAIL: " fmt, ##__VA_ARGS__)
#else
#define debug_detailed(fmt, ...)     do {} while(0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_VERBOSE
#define debug_verbose(fmt, ...)      pr_info("OVERLAY_VERBOSE: " fmt, ##__VA_ARGS__)
#else
#define debug_verbose(fmt, ...)      do {} while(0)
#endif

/* Feature Flags */

#define FEATURE_PIXEL_EXTRACTION     1       // Enable pixel data extraction
#define FEATURE_TILING_SUPPORT       1       // Support for tiled framebuffers
#define FEATURE_MULTI_MONITOR        1       // Multi-monitor support
#define FEATURE_STATISTICS           1       // Collect and report statistics
#define FEATURE_PROC_INTERFACE       1       // Provide /proc interface
#define FEATURE_SYSFS_INTERFACE      0       // Provide /sys interface (future)

/* Memory Management */

#define MAX_FRAMEBUFFER_CAPTURES     10      // Maximum number of framebuffers to track
#define MAX_PIXEL_BUFFER_SIZE        (3840 * 2160 * 4)  // Max 4K RGBA buffer
#define PIXEL_BUFFER_CACHE_SIZE      5       // Number of pixel buffers to cache

/* Timing and Performance */

#define KPROBE_HANDLER_TIMEOUT_NS    1000000 // 1ms timeout for kprobe handlers
#define WORK_QUEUE_TIMEOUT_MS        100     // Timeout for work queue operations
#define ATOMIC_COMMIT_TIMEOUT_MS     50      // Timeout for DRM atomic commits

/* Security and Safety */

#define ENABLE_RATE_LIMITING         1       // Prevent excessive overlay triggers
#define MAX_OVERLAYS_PER_SECOND      10      // Maximum overlay activations per second
#define COOLDOWN_PERIOD_MS           100     // Cooldown between overlay activations

#define ENABLE_EMERGENCY_FALLBACK    1       // Use emergency fallback if overlay fails
#define EMERGENCY_FALLBACK_METHOD    1       // 1=cursor plane, 2=disable display, 3=system alert

/* Compatibility Settings */

#define MIN_KERNEL_VERSION           KERNEL_VERSION(5, 4, 0)   // Minimum supported kernel
#define REQUIRE_DRM_ATOMIC           1       // Require atomic DRM support
#define FALLBACK_LEGACY_DRM          0       // Allow fallback to legacy DRM (not recommended)

/* Module Parameters (can be overridden at load time) */

// These can be module parameters in the actual implementation:
// module_param(detection_sensitivity, int, 0644);
// module_param(overlay_duration_ms, int, 0644);
// module_param(safety_overlay_color, uint, 0644);

#define DEFAULT_DETECTION_SENSITIVITY 50     // 0-100 scale
#define DEFAULT_OVERLAY_DURATION     1000    // milliseconds
#define DEFAULT_SAFETY_COLOR         SAFETY_COLOR_BLACK

/* Validation Macros */

#define VALIDATE_RISK_SCORE(score) \
    ((score) >= 0.0 && (score) <= 100.0)

#define VALIDATE_PIXEL_RATIO(ratio) \
    ((ratio) >= 0.0 && (ratio) <= 1.0)

#define VALIDATE_COLOR_VALUE(color) \
    ((color) <= 0xFFFFFFFF)

#define VALIDATE_DIMENSIONS(w, h) \
    ((w) > 0 && (h) > 0 && (w) <= 7680 && (h) <= 4320)  // Up to 8K resolution

/* Helper Macros */

#define EXTRACT_ALPHA(color)   (((color) >> 24) & 0xFF)
#define EXTRACT_RED(color)     (((color) >> 16) & 0xFF)
#define EXTRACT_GREEN(color)   (((color) >> 8) & 0xFF)
#define EXTRACT_BLUE(color)    ((color) & 0xFF)

#define MAKE_ARGB(a, r, g, b)  (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

#define CLAMP(value, min, max) \
    ((value) < (min) ? (min) : ((value) > (max) ? (max) : (value)))

#define ARRAY_SIZE(arr)        (sizeof(arr) / sizeof((arr)[0]))

/* Conditional Compilation Helpers */

#ifdef CONFIG_X86_64
#define ARCH_SPECIFIC_OPTIMIZATIONS  1
#define USE_FAST_PIXEL_ACCESS        1
#endif

#ifdef CONFIG_ARM64
#define ARCH_SPECIFIC_OPTIMIZATIONS  1
#define USE_NEON_ACCELERATION        1
#endif

/* Version and Build Information */

#define OVERLAY_SAFETY_VERSION_MAJOR 1
#define OVERLAY_SAFETY_VERSION_MINOR 0
#define OVERLAY_SAFETY_VERSION_PATCH 0

#define OVERLAY_SAFETY_BUILD_DATE    __DATE__
#define OVERLAY_SAFETY_BUILD_TIME    __TIME__

#endif /* OVERLAY_SAFETY_CONFIG_H */
