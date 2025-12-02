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
#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_vblank.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("VBlank Realtime FB Capture");
MODULE_DESCRIPTION("Real-time framebuffer capture via VBlank with robust pixel extraction");
MODULE_VERSION("1.0");

#define PROC_NAME "vblank_realtime_capture"
#define PROC_RAW_NAME "vblank_realtime_raw"
#define MAX_FB_CAPTURE 5
#define MAX_CAPTURE_SIZE (1920 * 1080 * 4) // Max 1080p for performance

// Intel tiling modes (ported from kernel_backup.c)
#define INTEL_TILING_NONE   0
#define INTEL_TILING_X      1
#define INTEL_TILING_Y      2
#define INTEL_TILING_YF     3

struct fb_pixel_data {
    struct drm_framebuffer *fb;
    struct drm_device *dev;
    void *pixel_buffer;
    size_t buffer_size;
    uint32_t width, height;
    uint32_t format;
    uint32_t pitch;
    uint64_t timestamp;
    uint32_t vblank_sequence;
    bool valid;
    bool has_pixels;
    bool is_detiled;
    uint32_t detected_tiling;
};

static struct fb_pixel_data captured_fbs[MAX_FB_CAPTURE];
static int capture_count = 0;
static int current_index = 0;
static DEFINE_MUTEX(capture_mutex);
static struct proc_dir_entry *proc_entry, *proc_raw_entry;
static atomic64_t vblank_events = ATOMIC64_INIT(0);
static atomic64_t successful_captures = ATOMIC64_INIT(0);

// Forward declarations
static int capture_vblank_fb(struct drm_crtc *crtc, uint32_t sequence);
static uint32_t detect_intel_tiling(struct drm_framebuffer *fb);
static int extract_gem_pixels(struct drm_gem_object *gem_obj, struct fb_pixel_data *capture);
static int convert_tiled_to_linear(uint8_t *src_buffer, uint8_t *dst_buffer,
                                 uint32_t width, uint32_t height, uint32_t pitch,
                                 uint32_t tiling);

// Tiling detection (ported from kernel_backup.c)
static uint32_t detect_intel_tiling(struct drm_framebuffer *fb)
{
    uint32_t modifier = fb->modifier;
    
    switch (modifier) {
        case DRM_FORMAT_MOD_LINEAR:
            return INTEL_TILING_NONE;
        case 0x0100000000000001ULL: // I915_FORMAT_MOD_X_TILED 
            return INTEL_TILING_X;
        case 0x0200000000000001ULL: // I915_FORMAT_MOD_Y_TILED
            return INTEL_TILING_Y;
        case 0x0300000000000001ULL: // I915_FORMAT_MOD_Yf_TILED
            return INTEL_TILING_YF;
        default:
            // Try to guess based on pitch alignment for older kernels
            if (fb->pitches[0] % 512 == 0) {
                return INTEL_TILING_X;
            } else if (fb->pitches[0] % 128 == 0) {
                return INTEL_TILING_Y;
            }
            return INTEL_TILING_NONE;
    }
}

// Tile offset calculations (ported from kernel_backup.c)
static inline uint32_t tile_offset_x(uint32_t x, uint32_t tile_width)
{
    return x % tile_width;
}

static inline uint32_t tile_offset_y(uint32_t y, uint32_t tile_height)
{
    return y % tile_height;
}

// Detiling function (ported from kernel_backup.c)
static int convert_tiled_to_linear(uint8_t *src_buffer, uint8_t *dst_buffer,
                                 uint32_t width, uint32_t height, uint32_t pitch,
                                 uint32_t tiling)
{
    uint32_t tile_w, tile_h;
    uint32_t tile_size, tiles_per_row;
    uint32_t x, y, tile_x, tile_y, tile_index;
    uint32_t in_tile_x, in_tile_y;
    uint32_t src_offset, dst_offset;
    uint32_t bytes_per_pixel = 4; // Assume 32-bit ARGB
    uint32_t byte_offset;
    
    if (!src_buffer || !dst_buffer) {
        return -EINVAL;
    }
    
    // Determine tile dimensions
    switch (tiling) {
        case INTEL_TILING_X:
            tile_w = 512;
            tile_h = 8;
            break;
        case INTEL_TILING_Y:
        case INTEL_TILING_YF:
            tile_w = 128;
            tile_h = 32;
            break;
        default:
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

// GEM pixel extraction (ported from kernel_backup.c)
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
    
    // Method 2: Try DMA-buf approach if it's an imported buffer
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

// VBlank framebuffer capture function
static int capture_vblank_fb(struct drm_crtc *crtc, uint32_t sequence)
{
    struct fb_pixel_data *capture;
    struct drm_plane *primary;
    struct drm_framebuffer *fb = NULL;
    int ret;
    size_t expected_size;
    
    if (!crtc || !crtc->dev) {
        return -EINVAL;
    }
    
    // Get the primary plane and its framebuffer
    primary = crtc->primary;
    if (primary && primary->state && primary->state->fb) {
        fb = primary->state->fb;
    }
    
    if (!fb || !fb->obj[0]) {
        // No active framebuffer on this CRTC
        return -ENODATA;
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
    capture->dev = crtc->dev;
    capture->width = fb->width;
    capture->height = fb->height;
    capture->format = fb->format->format;
    capture->pitch = fb->pitches[0];
    capture->timestamp = ktime_get_ns();
    capture->vblank_sequence = sequence;
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
    
    pr_info("VBlank FB info: %dx%d, format=0x%08x, pitch=%d, tiling=%s, seq=%u\n",
            capture->width, capture->height, capture->format, capture->pitch,
            (capture->detected_tiling == INTEL_TILING_X) ? "X-tiled" :
            (capture->detected_tiling == INTEL_TILING_Y) ? "Y-tiled" :
            (capture->detected_tiling == INTEL_TILING_YF) ? "Yf-tiled" : "linear",
            sequence);
    
    // Extract pixel data from the primary GEM object
    ret = extract_gem_pixels(fb->obj[0], capture);
    if (ret == 0) {
        capture->has_pixels = true;
        capture->valid = true;
        atomic64_inc(&successful_captures);
        
        if (capture->is_detiled) {
            pr_info("VBlank: Successfully captured and detiled framebuffer pixels: %dx%d, format=0x%08x, %zu bytes\n",
                    capture->width, capture->height, capture->format, capture->buffer_size);
        } else {
            pr_info("VBlank: Successfully captured framebuffer pixels: %dx%d, format=0x%08x, %zu bytes\n",
                    capture->width, capture->height, capture->format, capture->buffer_size);
        }
    } else {
        capture->has_pixels = false;
        capture->valid = true; // Still valid for metadata
        
        pr_info("VBlank: Captured framebuffer metadata only: %dx%d, format=0x%08x\n",
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

// VBlank event handler
static int handler_drm_vblank_event(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_crtc *crtc;
    struct drm_pending_vblank_event *e;
    
    atomic64_inc(&vblank_events);
    
    // Extract parameters based on architecture
#ifdef CONFIG_X86_64
    crtc = (struct drm_crtc *)regs->di;
    e = (struct drm_pending_vblank_event *)regs->si;
#elif defined(CONFIG_ARM64)
    crtc = (struct drm_crtc *)regs->regs[0];
    e = (struct drm_pending_vblank_event *)regs->regs[1];
#else
    return 0;
#endif

    if (!crtc) {
        return 0;
    }

    // Sample every 60th vblank event (roughly 1 second at 60fps)
    if (atomic64_read(&vblank_events) % 60 == 1) {
        uint32_t sequence = e ? e->event.sequence : 0;
        pr_info("VBlank event on CRTC, attempting capture...\n");
        capture_vblank_fb(crtc, sequence);
    }
    
    return 0;
}

// Kprobe structure
static struct kprobe kp_vblank = {
    .symbol_name = "drm_crtc_send_vblank_event",
    .pre_handler = handler_drm_vblank_event,
};

// Proc file implementations
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

static int vblank_fb_proc_show(struct seq_file *m, void *v)
{
    int i;
    
    mutex_lock(&capture_mutex);
    
    seq_printf(m, "VBlank Realtime Framebuffer Capture\n");
    seq_printf(m, "===================================\n");
    seq_printf(m, "VBlank events: %lld\n", atomic64_read(&vblank_events));
    seq_printf(m, "Successful captures: %lld\n", atomic64_read(&successful_captures));
    seq_printf(m, "Captured framebuffers: %d\n\n", capture_count);
    
    for (i = 0; i < capture_count; i++) {
        struct fb_pixel_data *capture = &captured_fbs[i];
        
        if (!capture->valid) continue;
            
        seq_printf(m, "Capture %d:\n", i);
        seq_printf(m, "  Timestamp: %llu ns\n", capture->timestamp);
        seq_printf(m, "  VBlank Sequence: %u\n", capture->vblank_sequence);
        seq_printf(m, "  Dimensions: %dx%d\n", capture->width, capture->height);
        seq_printf(m, "  Format: %s (0x%08x)\n", format_to_string(capture->format), capture->format);
        seq_printf(m, "  Pitch: %u\n", capture->pitch);
        seq_printf(m, "  Tiling: %s\n", 
                  (capture->detected_tiling == INTEL_TILING_X) ? "X-tiled" :
                  (capture->detected_tiling == INTEL_TILING_Y) ? "Y-tiled" :
                  (capture->detected_tiling == INTEL_TILING_YF) ? "Yf-tiled" : "linear");
        seq_printf(m, "  Detiled: %s\n", capture->is_detiled ? "YES" : "NO");
        seq_printf(m, "  Buffer size: %zu bytes\n", capture->buffer_size);
        seq_printf(m, "  Pixel data: %s\n", capture->has_pixels ? "AVAILABLE" : "NOT AVAILABLE");
        seq_printf(m, "\n");
    }
    
    seq_printf(m, "Usage: dd if=/proc/%s of=frame.raw\n", PROC_RAW_NAME);
    
    mutex_unlock(&capture_mutex);
    return 0;
}

static ssize_t vblank_fb_raw_read(struct file *file, char __user *buffer, size_t count, loff_t *pos)
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
    
    if (offset >= capture->buffer_size) {
        mutex_unlock(&capture_mutex);
        return 0;
    }
    
    to_copy = min_t(size_t, count, capture->buffer_size - offset);
    
    ret = copy_to_user(buffer, (char *)capture->pixel_buffer + offset, to_copy);
    if (ret) {
        mutex_unlock(&capture_mutex);
        return -EFAULT;
    }
    
    *pos += to_copy;
    mutex_unlock(&capture_mutex);
    return to_copy;
}

static int vblank_fb_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, vblank_fb_proc_show, NULL);
}

static const struct proc_ops vblank_fb_proc_ops = {
    .proc_open = vblank_fb_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static const struct proc_ops vblank_fb_raw_ops = {
    .proc_read = vblank_fb_raw_read,
};

static int __init vblank_realtime_init(void)
{
    int ret;
    
    pr_info("Initializing VBlank realtime framebuffer capture module\n");
    
    // Create proc files
    proc_entry = proc_create(PROC_NAME, 0444, NULL, &vblank_fb_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }
    
    proc_raw_entry = proc_create(PROC_RAW_NAME, 0444, NULL, &vblank_fb_raw_ops);
    if (!proc_raw_entry) {
        pr_err("Failed to create /proc/%s\n", PROC_RAW_NAME);
        proc_remove(proc_entry);
        return -ENOMEM;
    }
    
    // Register VBlank kprobe
    ret = register_kprobe(&kp_vblank);
    if (ret < 0) {
        pr_err("Failed to register VBlank kprobe: %d\n", ret);
        proc_remove(proc_raw_entry);
        proc_remove(proc_entry);
        return ret;
    }
    
    pr_info("VBlank realtime capture module loaded successfully\n");
    pr_info("View captured data: cat /proc/%s\n", PROC_NAME);
    pr_info("Extract raw pixels: dd if=/proc/%s of=frame.raw\n", PROC_RAW_NAME);
    
    return 0;
}

static void __exit vblank_realtime_exit(void)
{
    int i;
    
    pr_info("Unloading VBlank realtime framebuffer capture module\n");
    
    // Unregister kprobe
    unregister_kprobe(&kp_vblank);
    
    // Remove proc files
    proc_remove(proc_raw_entry);
    proc_remove(proc_entry);
    
    // Clean up captured data
    mutex_lock(&capture_mutex);
    for (i = 0; i < MAX_FB_CAPTURE; i++) {
        if (captured_fbs[i].pixel_buffer) {
            vfree(captured_fbs[i].pixel_buffer);
            captured_fbs[i].pixel_buffer = NULL;
        }
    }
    mutex_unlock(&capture_mutex);
    
    pr_info("VBlank realtime capture module unloaded\n");
    pr_info("Total VBlank events: %lld\n", atomic64_read(&vblank_events));
    pr_info("Successful captures: %lld\n", atomic64_read(&successful_captures));
}

module_init(vblank_realtime_init);
module_exit(vblank_realtime_exit);
