#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/kprobes.h>
#include <linux/version.h>
#include <linux/io.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/dma-buf.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_device.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_shmem_helper.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DRM FB Content Extractor");
MODULE_DESCRIPTION("Extract actual DRM framebuffer pixel content with detiling");
MODULE_VERSION("2.1");

#define PROC_NAME "drm_fb_pixels"
#define PROC_RAW_NAME "drm_fb_raw"
#define MAX_FB_CAPTURE 5
#define MAX_CAPTURE_SIZE (3840 * 1080 * 4) // Max 1080p RGBA

// Intel tiling definitions
#define INTEL_TILE_X_WIDTH  512
#define INTEL_TILE_X_HEIGHT 8
#define INTEL_TILE_Y_WIDTH  128
#define INTEL_TILE_Y_HEIGHT 32

// Intel format modifiers (in case they're not available in headers)
#ifndef I915_FORMAT_MOD_X_TILED
#define I915_FORMAT_MOD_X_TILED fourcc_mod_code(INTEL, 1)
#endif
#ifndef I915_FORMAT_MOD_Y_TILED
#define I915_FORMAT_MOD_Y_TILED fourcc_mod_code(INTEL, 2)
#endif
#ifndef I915_FORMAT_MOD_Yf_TILED
#define I915_FORMAT_MOD_Yf_TILED fourcc_mod_code(INTEL, 3)
#endif

enum intel_tiling {
    INTEL_TILING_NONE = 0,
    INTEL_TILING_X,
    INTEL_TILING_Y,
    INTEL_TILING_YF
};

struct fb_pixel_data {
    struct drm_framebuffer *fb;
    struct drm_device *dev;
    void *pixel_buffer;
    size_t buffer_size;
    uint32_t width, height;
    uint32_t format;
    uint32_t pitch;
    uint64_t timestamp;
    bool valid;
    bool has_pixels;
    bool is_detiled;
    enum intel_tiling detected_tiling;
};

static struct fb_pixel_data captured_fbs[MAX_FB_CAPTURE];
static int capture_count = 0;
static int current_index = 0;
static DEFINE_MUTEX(capture_mutex);
static struct proc_dir_entry *proc_entry;
static struct proc_dir_entry *proc_raw_entry;

// Intel tiling utility functions
static inline unsigned int tile_offset_x(unsigned int x, unsigned int tile_width)
{
    return (x & (tile_width - 1));
}

static inline unsigned int tile_offset_y(unsigned int y, unsigned int tile_height)
{
    return (y & (tile_height - 1));
}

// Detect Intel tiling based on framebuffer properties
static enum intel_tiling detect_intel_tiling(struct drm_framebuffer *fb)
{
    uint64_t modifier;
    
    if (!fb || !fb->modifier)
        return INTEL_TILING_NONE;
    
    modifier = fb->modifier;
    
    // Check for Intel-specific modifiers
    switch (modifier) {
        case I915_FORMAT_MOD_X_TILED:
            return INTEL_TILING_X;
        case I915_FORMAT_MOD_Y_TILED:
            return INTEL_TILING_Y;
        case I915_FORMAT_MOD_Yf_TILED:
            return INTEL_TILING_YF;
        default:
            // Try to detect based on pitch alignment
            if (fb->pitches[0] % INTEL_TILE_X_WIDTH == 0) {
                pr_info("Detected potential X-tiling based on pitch alignment\n");
                return INTEL_TILING_X;
            }
            return INTEL_TILING_NONE;
    }
}

// Convert tiled framebuffer to linear format
static int convert_tiled_to_linear(const uint8_t *src_buffer, uint8_t *dst_buffer,
                                  uint32_t width, uint32_t height, uint32_t pitch,
                                  enum intel_tiling tiling)
{
    uint32_t tile_w, tile_h;
    uint32_t x, y;
    uint32_t tile_x, tile_y, tile_index;
    uint32_t in_tile_x, in_tile_y;
    uint32_t src_offset, dst_offset;
    uint32_t tile_size, tiles_per_row;
    uint32_t bytes_per_pixel = 4; // Assuming 32-bit pixels (ARGB/XRGB)
    
    if (!src_buffer || !dst_buffer) {
        return -EINVAL;
    }
    
    // Set tile dimensions based on tiling type
    switch (tiling) {
        case INTEL_TILING_X:
            tile_w = INTEL_TILE_X_WIDTH;
            tile_h = INTEL_TILE_X_HEIGHT;
            break;
        case INTEL_TILING_Y:
        case INTEL_TILING_YF:
            tile_w = INTEL_TILE_Y_WIDTH;
            tile_h = INTEL_TILE_Y_HEIGHT;
            break;
        default:
            pr_warn("Unknown tiling type: %d\n", tiling);
            return -EINVAL;
    }
    
    tile_size = tile_w * tile_h;
    tiles_per_row = pitch / tile_w;
    
    pr_info("Converting %s-tiled buffer: %dx%d, pitch=%d, tile=%dx%d\n",
            (tiling == INTEL_TILING_X) ? "X" : "Y", width, height, pitch, tile_w, tile_h);
    
    // Convert pixel by pixel
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint32_t byte_x = x * bytes_per_pixel;
            uint32_t byte_offset;
            
            // Calculate tile coordinates
            tile_x = byte_x / tile_w;
            tile_y = y / tile_h;
            tile_index = tile_y * tiles_per_row + tile_x;
            
            // Calculate position within tile
            in_tile_x = tile_offset_x(byte_x, tile_w);
            in_tile_y = tile_offset_y(y, tile_h);
            
            // Calculate source offset in tiled buffer
            src_offset = tile_index * tile_size + in_tile_y * tile_w + in_tile_x;
            
            // Calculate destination offset in linear buffer
            dst_offset = y * width * bytes_per_pixel + x * bytes_per_pixel;
            
            // Copy all bytes for this pixel
            for (byte_offset = 0; byte_offset < bytes_per_pixel; byte_offset++) {
                if (src_offset + byte_offset < height * pitch &&
                    dst_offset + byte_offset < height * width * bytes_per_pixel) {
                    dst_buffer[dst_offset + byte_offset] = src_buffer[src_offset + byte_offset];
                }
            }
        }
    }
    
    return 0;
}

// Function to map and copy pixel data from GEM object with detiling support
static int extract_gem_pixels(struct drm_gem_object *gem_obj, struct fb_pixel_data *capture)
{
    int ret = 0;
    void *raw_buffer = NULL;
    size_t raw_buffer_size;
    bool needs_detiling = false;
    
    if (!gem_obj || !capture) {
        return -EINVAL;
    }

    pr_info("Extracting pixels from GEM object: size=%zu\n", gem_obj->size);

    // Check if we need detiling
    if (capture->detected_tiling != INTEL_TILING_NONE) {
        needs_detiling = true;
        raw_buffer_size = capture->height * capture->pitch;
        raw_buffer = vmalloc(raw_buffer_size);
        if (!raw_buffer) {
            pr_err("Failed to allocate raw buffer for detiling (%zu bytes)\n", raw_buffer_size);
            return -ENOMEM;
        }
        pr_info("Allocated raw buffer for detiling: %zu bytes\n", raw_buffer_size);
    }

    // Try different methods to access the GEM object data
    
    // Method 1: Try SHMEM-based GEM objects
    if (gem_obj->filp && gem_obj->filp->f_mapping) {
        struct address_space *mapping = gem_obj->filp->f_mapping;
        size_t copied = 0;
        pgoff_t index = 0;
        pgoff_t num_pages;
        void *target_buffer = needs_detiling ? raw_buffer : capture->pixel_buffer;
        size_t target_size = needs_detiling ? raw_buffer_size : capture->buffer_size;
        
        pr_info("Trying SHMEM mapping method\n");
        
        num_pages = (gem_obj->size + PAGE_SIZE - 1) >> PAGE_SHIFT;
        
        while (copied < target_size && index < num_pages) {
            struct page *page = find_get_page(mapping, index);
            if (page) {
                void *kaddr = kmap_atomic(page);
                if (kaddr) {
                    size_t to_copy = min_t(size_t, PAGE_SIZE, target_size - copied);
                    memcpy((char*)target_buffer + copied, kaddr, to_copy);
                    copied += to_copy;
                    kunmap_atomic(kaddr);
                }
                put_page(page);
            }
            index++;
        }
        
        if (copied > 0) {
            pr_info("Copied %zu bytes via SHMEM method\n", copied);
            if (needs_detiling) {
                ret = convert_tiled_to_linear((uint8_t*)raw_buffer, (uint8_t*)capture->pixel_buffer,
                                            capture->width, capture->height, capture->pitch,
                                            capture->detected_tiling);
                if (ret == 0) {
                    capture->is_detiled = true;
                    pr_info("Successfully detiled framebuffer\n");
                } else {
                    pr_warn("Failed to detile framebuffer: %d\n", ret);
                }
            }
            if (raw_buffer) vfree(raw_buffer);
            return ret;
        }
    }
    
    // Method 2: Try to use drm_gem_shmem_helper functions if available
    #ifdef CONFIG_DRM_GEM_SHMEM_HELPER
    // This method is commented out due to API complexity
    // and driver-specific requirements
    #endif
    
    // Method 3: Try DMA-buf approach if it's an imported buffer
    if (gem_obj->dma_buf && gem_obj->import_attach) {
        struct dma_buf_map map;
        void *target_buffer = needs_detiling ? raw_buffer : capture->pixel_buffer;
        size_t target_size = needs_detiling ? raw_buffer_size : capture->buffer_size;
        
        pr_info("Trying DMA-buf method\n");
        
        ret = dma_buf_vmap(gem_obj->dma_buf, &map);
        if (ret == 0 && !dma_buf_map_is_null(&map)) {
            size_t to_copy = min_t(size_t, gem_obj->dma_buf->size, target_size);
            
            if (map.is_iomem) {
                memcpy_fromio(target_buffer, map.vaddr_iomem, to_copy);
            } else {
                memcpy(target_buffer, map.vaddr, to_copy);
            }
            
            dma_buf_vunmap(gem_obj->dma_buf, &map);
            pr_info("Copied %zu bytes via DMA-buf method\n", to_copy);
            
            if (needs_detiling) {
                ret = convert_tiled_to_linear((uint8_t*)raw_buffer, (uint8_t*)capture->pixel_buffer,
                                            capture->width, capture->height, capture->pitch,
                                            capture->detected_tiling);
                if (ret == 0) {
                    capture->is_detiled = true;
                    pr_info("Successfully detiled framebuffer\n");
                } else {
                    pr_warn("Failed to detile framebuffer: %d\n", ret);
                }
            }
            if (raw_buffer) vfree(raw_buffer);
            return ret;
        }
    }
    
    if (raw_buffer) vfree(raw_buffer);
    pr_warn("Could not access pixel data from GEM object\n");
    return -ENODATA;
}

// Function to capture framebuffer pixel content
static int capture_fb_pixels(struct drm_framebuffer *fb, struct drm_device *dev)
{
    struct fb_pixel_data *capture;
    int ret;
    size_t expected_size;
    
    if (!fb || !fb->obj[0]) {
        pr_warn("Invalid framebuffer or missing GEM object\n");
        return -EINVAL;
    }
    
    mutex_lock(&capture_mutex);
    
    // Use circular buffer for captures
    capture = &captured_fbs[current_index];
    
    // Clean up previous capture
    if (capture->pixel_buffer) {
        vfree(capture->pixel_buffer);
        capture->pixel_buffer = NULL;
    }
    
    // Initialize capture structure
    memset(capture, 0, sizeof(*capture));
    capture->fb = fb;
    capture->dev = dev;
    capture->width = fb->width;
    capture->height = fb->height;
    capture->format = fb->format->format;
    capture->pitch = fb->pitches[0];
    capture->timestamp = ktime_get_ns();
    capture->is_detiled = false;
    
    // Detect Intel tiling
    capture->detected_tiling = detect_intel_tiling(fb);
    
    // Calculate expected buffer size (always linear output size)
    expected_size = capture->height * capture->width * 4; // 4 bytes per pixel for ARGB
    if (expected_size > MAX_CAPTURE_SIZE) {
        expected_size = MAX_CAPTURE_SIZE;
        pr_warn("Framebuffer too large, limiting to %d bytes\n", MAX_CAPTURE_SIZE);
    }
    
    capture->buffer_size = expected_size;
    
    // Allocate buffer for pixel data (linear format)
    capture->pixel_buffer = vmalloc(capture->buffer_size);
    if (!capture->pixel_buffer) {
        pr_err("Failed to allocate pixel buffer (%zu bytes)\n", capture->buffer_size);
        mutex_unlock(&capture_mutex);
        return -ENOMEM;
    }
    
    pr_info("Framebuffer info: %dx%d, format=0x%08x, pitch=%d, tiling=%s\n",
            capture->width, capture->height, capture->format, capture->pitch,
            (capture->detected_tiling == INTEL_TILING_X) ? "X-tiled" :
            (capture->detected_tiling == INTEL_TILING_Y) ? "Y-tiled" :
            (capture->detected_tiling == INTEL_TILING_YF) ? "Yf-tiled" : "linear");
    
    // Extract pixel data from the primary GEM object
    ret = extract_gem_pixels(fb->obj[0], capture);
    if (ret == 0) {
        capture->has_pixels = true;
        capture->valid = true;
        
        if (capture->is_detiled) {
            pr_info("Successfully captured and detiled framebuffer pixels: %dx%d, format=0x%08x, %zu bytes\n",
                    capture->width, capture->height, capture->format, capture->buffer_size);
        } else {
            pr_info("Successfully captured framebuffer pixels: %dx%d, format=0x%08x, %zu bytes\n",
                    capture->width, capture->height, capture->format, capture->buffer_size);
        }
    } else {
        capture->has_pixels = false;
        capture->valid = true; // Still valid for metadata
        
        pr_info("Captured framebuffer metadata only: %dx%d, format=0x%08x\n",
                capture->width, capture->height, capture->format);
    }
    
    // Update counters
    current_index = (current_index + 1) % MAX_FB_CAPTURE;
    if (capture_count < MAX_FB_CAPTURE) {
        capture_count++;
    }
    
    mutex_unlock(&capture_mutex);
    return 0;
}

// Kprobe handler for drm_framebuffer_init
static int handler_drm_framebuffer_init(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_device *dev;
    struct drm_framebuffer *fb;
    
    // Extract parameters based on architecture
#ifdef CONFIG_X86_64
    dev = (struct drm_device *)regs->di;
    fb = (struct drm_framebuffer *)regs->si;
#elif defined(CONFIG_ARM64)
    dev = (struct drm_device *)regs->regs[0];
    fb = (struct drm_framebuffer *)regs->regs[1];
#else
    return 0;
#endif

    if (!dev || !fb) {
        return 0;
    }

    pr_info("Intercepted framebuffer init: %dx%d, format=0x%08x\n", 
            fb->width, fb->height, fb->format ? fb->format->format : 0);

    // Capture the framebuffer content
    capture_fb_pixels(fb, dev);
    
    return 0;
}

// Kprobe structure
static struct kprobe kp_drm_fb_init = {
    .symbol_name = "drm_framebuffer_init",
    .pre_handler = handler_drm_framebuffer_init,
};

// Convert pixel format to string
static const char* format_to_string(uint32_t format)
{
    switch(format) {
        case DRM_FORMAT_XRGB8888: return "XRGB8888";
        case DRM_FORMAT_ARGB8888: return "ARGB8888";
        case DRM_FORMAT_RGB565: return "RGB565";
        case DRM_FORMAT_XBGR8888: return "XBGR8888";
        case DRM_FORMAT_ABGR8888: return "ABGR8888";
        default: return "UNKNOWN";
    }
}

// Proc file for displaying capture information
static int drm_fb_proc_show(struct seq_file *m, void *v)
{
    int i;
    
    mutex_lock(&capture_mutex);
    
    seq_printf(m, "DRM Framebuffer Pixel Extractor with Intel Detiling\n");
    seq_printf(m, "Captured framebuffers: %d\n\n", capture_count);
    
    for (i = 0; i < capture_count; i++) {
        struct fb_pixel_data *capture = &captured_fbs[i];
        const char *tiling_str;
        
        if (!capture->valid)
            continue;
        
        switch (capture->detected_tiling) {
            case INTEL_TILING_X:
                tiling_str = "X-tiled";
                break;
            case INTEL_TILING_Y:
                tiling_str = "Y-tiled";
                break;
            case INTEL_TILING_YF:
                tiling_str = "Yf-tiled";
                break;
            default:
                tiling_str = "Linear";
                break;
        }
            
        seq_printf(m, "Capture %d:\n", i);
        seq_printf(m, "  Timestamp: %llu ns\n", capture->timestamp);
        seq_printf(m, "  Device: %p\n", capture->dev);
        seq_printf(m, "  Framebuffer: %p\n", capture->fb);
        seq_printf(m, "  Dimensions: %dx%d\n", capture->width, capture->height);
        seq_printf(m, "  Format: 0x%08x (%s)\n", capture->format, format_to_string(capture->format));
        seq_printf(m, "  Pitch: %d bytes/row\n", capture->pitch);
        seq_printf(m, "  Buffer size: %zu bytes\n", capture->buffer_size);
        seq_printf(m, "  Tiling: %s\n", tiling_str);
        seq_printf(m, "  Detiled: %s\n", capture->is_detiled ? "YES" : "NO");
        seq_printf(m, "  Pixel data: %s\n", capture->has_pixels ? "AVAILABLE (LINEAR)" : "NOT AVAILABLE");
        
        if (capture->has_pixels && capture->pixel_buffer) {
            int j;
            seq_printf(m, "  First 64 bytes (hex): ");
            for (j = 0; j < min_t(size_t, 64, capture->buffer_size); j++) {
                seq_printf(m, "%02x", ((unsigned char*)capture->pixel_buffer)[j]);
                if ((j + 1) % 16 == 0) seq_printf(m, "\n                        ");
                else if ((j + 1) % 4 == 0) seq_printf(m, " ");
            }
            seq_printf(m, "\n");
            
            // Show some basic statistics
            if (capture->buffer_size >= 4) {
                uint32_t *pixels = (uint32_t*)capture->pixel_buffer;
                uint32_t first_pixel = pixels[0];
                seq_printf(m, "  First pixel (ARGB): 0x%08x\n", first_pixel);
            }
        }
        seq_printf(m, "\n");
    }
    
    seq_printf(m, "Usage:\n");
    seq_printf(m, "  To extract raw linear pixel data: dd if=/proc/%s bs=1 count=Y of=framebuffer.raw\n", PROC_RAW_NAME);
    seq_printf(m, "  Where Y is the buffer size from above\n");
    seq_printf(m, "  The extracted data is already in linear format (detiled if needed)\n");
    
    mutex_unlock(&capture_mutex);
    return 0;
}

// Proc file for raw pixel data access
static ssize_t drm_fb_raw_read(struct file *file, char __user *buffer, size_t count, loff_t *pos)
{
    struct fb_pixel_data *capture = NULL;
    loff_t offset = *pos;
    size_t to_copy;
    int ret;
    int i;
    
    mutex_lock(&capture_mutex);
    
    // Find the most recent capture with pixel data
    for (i = capture_count - 1; i >= 0; i--) {
        if (captured_fbs[i].valid && captured_fbs[i].has_pixels) {
            capture = &captured_fbs[i];
            break;
        }
    }
    
    if (!capture || !capture->pixel_buffer) {
        mutex_unlock(&capture_mutex);
        return -ENODATA;
    }
    
    // Check bounds
    if (offset >= capture->buffer_size) {
        mutex_unlock(&capture_mutex);
        return 0; // EOF
    }
    
    to_copy = min_t(size_t, count, capture->buffer_size - offset);
    
    ret = copy_to_user(buffer, (char*)capture->pixel_buffer + offset, to_copy);
    if (ret) {
        mutex_unlock(&capture_mutex);
        return -EFAULT;
    }
    
    *pos += to_copy;
    mutex_unlock(&capture_mutex);
    
    return to_copy;
}

static int drm_fb_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, drm_fb_proc_show, NULL);
}

static const struct proc_ops drm_fb_proc_ops = {
    .proc_open = drm_fb_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static const struct proc_ops drm_fb_raw_ops = {
    .proc_read = drm_fb_raw_read,
    .proc_lseek = default_llseek,
};

// Module initialization
static int __init drm_fb_extractor_init(void)
{
    int ret;

    pr_info("DRM Framebuffer Pixel Extractor loading\n");

    // Initialize capture array
    memset(captured_fbs, 0, sizeof(captured_fbs));
    capture_count = 0;
    current_index = 0;

    // Register kprobe
    ret = register_kprobe(&kp_drm_fb_init);
    if (ret < 0) {
        pr_err("Failed to register kprobe: %d\n", ret);
        return ret;
    }

    // Create proc entries
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &drm_fb_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry %s\n", PROC_NAME);
        unregister_kprobe(&kp_drm_fb_init);
        return -ENOMEM;
    }
    
    proc_raw_entry = proc_create(PROC_RAW_NAME, 0644, NULL, &drm_fb_raw_ops);
    if (!proc_raw_entry) {
        pr_err("Failed to create proc entry %s\n", PROC_RAW_NAME);
        proc_remove(proc_entry);
        unregister_kprobe(&kp_drm_fb_init);
        return -ENOMEM;
    }

    pr_info("DRM Framebuffer Pixel Extractor with Intel Detiling loaded successfully\n");
    pr_info("Use 'cat /proc/%s' to view capture info\n", PROC_NAME);
    pr_info("Use 'cat /proc/%s' to access raw linear pixel data\n", PROC_RAW_NAME);
    
    return 0;
}

// Module cleanup
static void __exit drm_fb_extractor_exit(void)
{
    int i;

    pr_info("DRM Framebuffer Pixel Extractor with Intel Detiling unloading\n");

    // Remove proc entries
    if (proc_raw_entry) {
        proc_remove(proc_raw_entry);
    }
    if (proc_entry) {
        proc_remove(proc_entry);
    }

    // Unregister kprobe
    unregister_kprobe(&kp_drm_fb_init);

    // Free allocated buffers
    mutex_lock(&capture_mutex);
    for (i = 0; i < MAX_FB_CAPTURE; i++) {
        if (captured_fbs[i].pixel_buffer) {
            vfree(captured_fbs[i].pixel_buffer);
            captured_fbs[i].pixel_buffer = NULL;
        }
    }
    capture_count = 0;
    mutex_unlock(&capture_mutex);

    pr_info("DRM Framebuffer Pixel Extractor with Intel Detiling unloaded\n");
}

module_init(drm_fb_extractor_init);
module_exit(drm_fb_extractor_exit);