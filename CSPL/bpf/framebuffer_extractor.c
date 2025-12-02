#define _POSIX_C_SOURCE 200809L  // For usleep

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <time.h>

/*
 * Framebuffer Data Extractor
 * This program works alongside eBPF scripts to extract framebuffer data
 * It provides multiple approaches to access framebuffer data:
 * 1. Direct framebuffer device access (/dev/fb0)
 * 2. Memory mapping of graphics memory
 * 3. DRM/KMS interface access
 */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    size_t size;
    void *memory;
    int fd;
} framebuffer_t;

// Global variables
static framebuffer_t fb = {0};
static volatile int running = 1;

void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    running = 0;
}

int init_framebuffer() {
    struct fb_var_screeninfo var_info;
    struct fb_fix_screeninfo fix_info;
    
    printf("Opening framebuffer device /dev/fb0...\n");
    fb.fd = open("/dev/fb0", O_RDWR);
    if (fb.fd == -1) {
        perror("Error opening framebuffer device");
        return -1;
    }
    
    // Get variable screen info
    if (ioctl(fb.fd, FBIOGET_VSCREENINFO, &var_info) == -1) {
        perror("Error reading variable information");
        close(fb.fd);
        return -1;
    }
    
    // Get fixed screen info
    if (ioctl(fb.fd, FBIOGET_FSCREENINFO, &fix_info) == -1) {
        perror("Error reading fixed information");
        close(fb.fd);
        return -1;
    }
    
    // Store framebuffer information
    fb.width = var_info.xres;
    fb.height = var_info.yres;
    fb.bpp = var_info.bits_per_pixel;
    fb.pitch = fix_info.line_length;
    fb.size = fix_info.smem_len;
    
    printf("Framebuffer info:\n");
    printf("  Resolution: %dx%d\n", fb.width, fb.height);
    printf("  Bits per pixel: %d\n", fb.bpp);
    printf("  Line length: %d bytes\n", fb.pitch);
    printf("  Total size: %zu bytes (%.2f MB)\n", fb.size, fb.size / (1024.0 * 1024.0));
    printf("  Pixel format: %d-bit ", fb.bpp);
    
    switch (fb.bpp) {
        case 16:
            printf("RGB565\n");
            break;
        case 24:
            printf("RGB888\n");
            break;
        case 32:
            printf("RGBA8888/BGRA8888\n");
            break;
        default:
            printf("Unknown\n");
            break;
    }
    
    // Map framebuffer memory
    printf("Mapping framebuffer memory...\n");
    fb.memory = mmap(0, fb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);
    if (fb.memory == MAP_FAILED) {
        perror("Error mapping framebuffer memory");
        close(fb.fd);
        return -1;
    }
    
    printf("Framebuffer successfully mapped at %p\n", fb.memory);
    return 0;
}

void cleanup_framebuffer() {
    if (fb.memory && fb.memory != MAP_FAILED) {
        munmap(fb.memory, fb.size);
    }
    if (fb.fd > 0) {
        close(fb.fd);
    }
}

void save_framebuffer_raw(const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Error opening output file");
        return;
    }
    
    printf("Saving raw framebuffer to %s...\n", filename);
    size_t written = fwrite(fb.memory, 1, fb.size, file);
    if (written != fb.size) {
        printf("Warning: Only wrote %zu of %zu bytes\n", written, fb.size);
    }
    
    fclose(file);
    printf("Saved %zu bytes to %s\n", written, filename);
}

void save_framebuffer_ppm(const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Error opening PPM file");
        return;
    }
    
    printf("Converting framebuffer to PPM format...\n");
    
    // Write PPM header
    fprintf(file, "P6\n%d %d\n255\n", fb.width, fb.height);
    
    // Convert pixels to RGB format
    uint8_t *src = (uint8_t *)fb.memory;
    uint8_t rgb[3];
    
    for (uint32_t y = 0; y < fb.height; y++) {
        for (uint32_t x = 0; x < fb.width; x++) {
            uint32_t offset = y * fb.pitch + x * (fb.bpp / 8);
            
            switch (fb.bpp) {
                case 16: {
                    // RGB565 to RGB888
                    uint16_t pixel = *(uint16_t *)(src + offset);
                    rgb[0] = ((pixel >> 11) & 0x1F) << 3;  // Red
                    rgb[1] = ((pixel >> 5) & 0x3F) << 2;   // Green
                    rgb[2] = (pixel & 0x1F) << 3;          // Blue
                    break;
                }
                case 24: {
                    // RGB888 (already in correct format, but might be BGR)
                    rgb[2] = src[offset];     // Blue -> Red
                    rgb[1] = src[offset + 1]; // Green
                    rgb[0] = src[offset + 2]; // Red -> Blue
                    break;
                }
                case 32: {
                    // RGBA8888/BGRA8888 to RGB888
                    rgb[2] = src[offset];     // Blue -> Red
                    rgb[1] = src[offset + 1]; // Green
                    rgb[0] = src[offset + 2]; // Red -> Blue
                    // Alpha channel ignored
                    break;
                }
                default:
                    rgb[0] = rgb[1] = rgb[2] = 0;
                    break;
            }
            
            fwrite(rgb, 1, 3, file);
        }
    }
    
    fclose(file);
    printf("Saved PPM image to %s\n", filename);
}

void analyze_framebuffer_content() {
    if (!fb.memory) {
        printf("Framebuffer not mapped\n");
        return;
    }
    
    printf("\n=== Framebuffer Content Analysis ===\n");
    
    uint8_t *data = (uint8_t *)fb.memory;
    uint64_t pixel_sum = 0;
    uint32_t non_zero_pixels = 0;
    uint32_t total_pixels = fb.width * fb.height;
    
    // Sample analysis - check every 100th pixel to avoid overwhelming output
    for (uint32_t i = 0; i < total_pixels; i += 100) {
        uint32_t offset = i * (fb.bpp / 8);
        if (offset < fb.size) {
            uint32_t pixel_value = 0;
            
            switch (fb.bpp) {
                case 16:
                    pixel_value = *(uint16_t *)(data + offset);
                    break;
                case 24:
                case 32:
                    pixel_value = *(uint32_t *)(data + offset) & 0xFFFFFF;
                    break;
            }
            
            if (pixel_value != 0) {
                non_zero_pixels++;
                pixel_sum += pixel_value;
            }
        }
    }
    
    printf("Sample analysis (every 100th pixel):\n");
    printf("  Total sampled pixels: %u\n", total_pixels / 100);
    printf("  Non-zero pixels: %u (%.2f%%)\n", non_zero_pixels, 
           (float)non_zero_pixels / (total_pixels / 100) * 100);
    
    if (non_zero_pixels > 0) {
        printf("  Average non-zero pixel value: 0x%lX\n", pixel_sum / non_zero_pixels);
        printf("  Framebuffer appears to contain image data\n");
    } else {
        printf("  Framebuffer appears to be blank/black\n");
    }
    
    // Check for common patterns
    uint32_t *words = (uint32_t *)data;
    uint32_t pattern_count = 0;
    uint32_t last_word = words[0];
    
    for (size_t i = 1; i < fb.size / 4 && i < 1000; i++) {
        if (words[i] == last_word) {
            pattern_count++;
        }
        last_word = words[i];
    }
    
    if (pattern_count > 800) {
        printf("  High pattern repetition detected - likely solid color or blank\n");
    } else {
        printf("  Low pattern repetition - likely contains varied image data\n");
    }
    
    printf("==========================================\n");
}

void monitor_framebuffer_changes() {
    if (!fb.memory) {
        printf("Framebuffer not mapped\n");
        return;
    }
    
    printf("Monitoring framebuffer for changes...\n");
    printf("Press Ctrl+C to stop monitoring\n\n");
    
    // Create a copy of the current framebuffer for comparison
    void *previous_frame = malloc(fb.size);
    if (!previous_frame) {
        perror("Error allocating memory for frame comparison");
        return;
    }
    
    memcpy(previous_frame, fb.memory, fb.size);
    
    int frame_count = 0;
    time_t start_time = time(NULL);
    
    while (running) {
        usleep(16667); // ~60 FPS check rate
        
        // Compare current framebuffer with previous frame
        size_t differences = 0;
        uint8_t *current = (uint8_t *)fb.memory;
        uint8_t *previous = (uint8_t *)previous_frame;
        
        // Sample check - compare every 1000th byte
        for (size_t i = 0; i < fb.size; i += 1000) {
            if (current[i] != previous[i]) {
                differences++;
            }
        }
        
        if (differences > 0) {
            frame_count++;
            time_t current_time = time(NULL);
            
            printf("Frame %d: %zu sample differences detected (%.2f%% changed)\n",
                   frame_count, differences, 
                   (float)differences / (fb.size / 1000) * 100);
            
            // Save interesting frames
            if (differences > 10) { // Significant change
                char filename[256];
                snprintf(filename, sizeof(filename), 
                        "framebuffer_capture_%d.raw", frame_count);
                save_framebuffer_raw(filename);
                
                snprintf(filename, sizeof(filename), 
                        "framebuffer_capture_%d.ppm", frame_count);
                save_framebuffer_ppm(filename);
            }
            
            // Update previous frame
            memcpy(previous_frame, fb.memory, fb.size);
            
            // Calculate FPS
            time_t elapsed = current_time - start_time;
            if (elapsed > 0) {
                printf("  Average capture rate: %.2f FPS\n", 
                       (float)frame_count / elapsed);
            }
        }
    }
    
    free(previous_frame);
    printf("\nCaptured %d frames with changes\n", frame_count);
}

void print_usage(const char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -a, --analyze  Analyze current framebuffer content\n");
    printf("  -m, --monitor  Monitor framebuffer for changes\n");
    printf("  -s, --save     Save current framebuffer as raw and PPM\n");
    printf("  -r, --raw      Save only raw framebuffer data\n");
    printf("  -p, --ppm      Save only PPM image\n");
    printf("\n");
    printf("This program extracts framebuffer data from /dev/fb0\n");
    printf("Run with appropriate permissions (usually as root)\n");
}

int main(int argc, char *argv[]) {
    printf("Framebuffer Data Extractor\n");
    printf("==========================\n");
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Parse command line arguments
    int analyze_mode = 0;
    int monitor_mode = 0;
    int save_raw = 0;
    int save_ppm = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--analyze") == 0) {
            analyze_mode = 1;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--monitor") == 0) {
            monitor_mode = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--save") == 0) {
            save_raw = save_ppm = 1;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--raw") == 0) {
            save_raw = 1;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--ppm") == 0) {
            save_ppm = 1;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Initialize framebuffer
    if (init_framebuffer() != 0) {
        return 1;
    }
    
    // Execute requested operations
    if (analyze_mode) {
        analyze_framebuffer_content();
    }
    
    if (save_raw) {
        save_framebuffer_raw("framebuffer_dump.raw");
    }
    
    if (save_ppm) {
        save_framebuffer_ppm("framebuffer_dump.ppm");
    }
    
    if (monitor_mode) {
        monitor_framebuffer_changes();
    }
    
    // If no specific mode was requested, default to basic info and single capture
    if (!analyze_mode && !monitor_mode && !save_raw && !save_ppm) {
        printf("\nNo specific operation requested. Performing basic capture...\n");
        analyze_framebuffer_content();
        save_framebuffer_raw("framebuffer_dump.raw");
        save_framebuffer_ppm("framebuffer_dump.ppm");
    }
    
    // Cleanup
    cleanup_framebuffer();
    
    printf("\nFramebuffer extraction completed.\n");
    return 0;
}
