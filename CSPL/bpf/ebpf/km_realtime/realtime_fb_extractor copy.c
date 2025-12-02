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
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
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
MODULE_AUTHOR("Real-Time DRM FB Extractor");
MODULE_DESCRIPTION("Extract DRM framebuffer content at display time with minimal latency");
MODULE_VERSION("3.0");

#define PROC_NAME "drm_fb_realtime"
#define PROC_RAW_NAME "drm_fb_realtime_raw"
#define MAX_FB_CAPTURE 10
#define MAX_CAPTURE_SIZE (3840 * 2160 * 4) // Max 4K RGBA

// Intel tiling definitions
#define INTEL_TILE_X_WIDTH  512
#define INTEL_TILE_X_HEIGHT 8
#define INTEL_TILE_Y_WIDTH  128
#define INTEL_TILE_Y_HEIGHT 32

// Intel format modifiers
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

struct realtime_fb_data {
    struct drm_framebuffer *fb;
    struct drm_device *dev;
    struct drm_crtc *crtc;
    struct drm_plane *plane;
    void *pixel_buffer;
    size_t buffer_size;
    uint32_t width, height;
    uint32_t format;
    uint32_t pitch;
    uint64_t capture_timestamp;
    uint32_t frame_number;
    bool valid;
    bool has_pixels;
    bool is_detiled;
    bool is_current_frame;
    enum intel_tiling detected_tiling;
    int capture_method; // 0=atomic_commit, 1=vblank, 2=plane_update
};

static struct realtime_fb_data captured_fbs[MAX_FB_CAPTURE];
static int capture_count = 0;
static int current_index = 0;
static DEFINE_MUTEX(capture_mutex);
static struct proc_dir_entry *proc_entry;
static struct proc_dir_entry *proc_raw_entry;

// Active framebuffer tracking for each CRTC
static struct drm_framebuffer *active_fb_per_crtc[8] = {NULL}; // Support up to 8 CRTCs
static DEFINE_SPINLOCK(active_fb_lock);

// Performance counters
static atomic64_t total_captures = ATOMIC64_INIT(0);
static atomic64_t successful_captures = ATOMIC64_INIT(0);
static atomic64_t atomic_captures = ATOMIC64_INIT(0);

// Tiling detection and conversion functions (same as before)
static enum intel_tiling detect_intel_tiling(struct drm_framebuffer *fb)
{
    uint64_t modifier;
    
    if (!fb || !fb->modifier)
        return INTEL_TILING_NONE;
    
    modifier = fb->modifier;
    
    switch (modifier) {
        case I915_FORMAT_MOD_X_TILED:
            return INTEL_TILING_X;
        case I915_FORMAT_MOD_Y_TILED:
            return INTEL_TILING_Y;
        case I915_FORMAT_MOD_Yf_TILED:
            return INTEL_TILING_YF;
        default:
            if (fb->pitches[0] % INTEL_TILE_X_WIDTH == 0) {
                return INTEL_TILING_X;
            }
            return INTEL_TILING_NONE;
    }
}

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
    uint32_t bytes_per_pixel = 4;
    
    if (!src_buffer || !dst_buffer) {
        return -EINVAL;
    }
    
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
            return -EINVAL;
    }
    
    tile_size = tile_w * tile_h;
    tiles_per_row = pitch / tile_w;
    
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint32_t byte_x = x * bytes_per_pixel;
            uint32_t byte_offset;
            
            tile_x = byte_x / tile_w;
            tile_y = y / tile_h;
            tile_index = tile_y * tiles_per_row + tile_x;
            
            in_tile_x = byte_x & (tile_w - 1);
            in_tile_y = y & (tile_h - 1);
            
            src_offset = tile_index * tile_size + in_tile_y * tile_w + in_tile_x;
            dst_offset = y * width * bytes_per_pixel + x * bytes_per_pixel;
            
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

// Fast pixel extraction function optimized for real-time capture
static int extract_gem_pixels_fast(struct drm_gem_object *gem_obj, 
                                   struct realtime_fb_data *capture)
{
    int ret = 0;
    void *raw_buffer = NULL;
    size_t raw_buffer_size;
    bool needs_detiling = false;
    ktime_t start_time, end_time;
    
    start_time = ktime_get();
    
    if (!gem_obj || !capture) {
        return -EINVAL;
    }

    atomic64_inc(&total_captures);

    if (capture->detected_tiling != INTEL_TILING_NONE) {
        needs_detiling = true;
        raw_buffer_size = capture->height * capture->pitch;
        raw_buffer = vmalloc(raw_buffer_size);
        if (!raw_buffer) {
            return -ENOMEM;
        }
    }

    // Optimized SHMEM extraction
    if (gem_obj->filp && gem_obj->filp->f_mapping) {
        struct address_space *mapping = gem_obj->filp->f_mapping;
        size_t copied = 0;
        pgoff_t index = 0;
        pgoff_t num_pages;
        void *target_buffer = needs_detiling ? raw_buffer : capture->pixel_buffer;
        size_t target_size = needs_detiling ? raw_buffer_size : capture->buffer_size;
        
        num_pages = min_t(pgoff_t, 
                         (target_size + PAGE_SIZE - 1) >> PAGE_SHIFT,
                         (gem_obj->size + PAGE_SIZE - 1) >> PAGE_SHIFT);
        
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
            } else {
                // Page not in memory, skip for real-time performance
                break;
            }
            index++;
        }
        
        if (copied > 0) {
            if (needs_detiling) {
                ret = convert_tiled_to_linear((uint8_t*)raw_buffer, 
                                            (uint8_t*)capture->pixel_buffer,
                                            capture->width, capture->height, 
                                            capture->pitch, capture->detected_tiling);
                if (ret == 0) {
                    capture->is_detiled = true;
                }
            }
            
            capture->has_pixels = true;
            atomic64_inc(&successful_captures);
        }
    }
    
    // Try DMA-buf method if SHMEM failed
    if (!capture->has_pixels && gem_obj->dma_buf && gem_obj->import_attach) {
        struct dma_buf_map map;
        void *target_buffer = needs_detiling ? raw_buffer : capture->pixel_buffer;
        size_t target_size = needs_detiling ? raw_buffer_size : capture->buffer_size;
        
        ret = dma_buf_vmap(gem_obj->dma_buf, &map);
        if (ret == 0 && !dma_buf_map_is_null(&map)) {
            size_t to_copy = min_t(size_t, gem_obj->dma_buf->size, target_size);
            
            if (map.is_iomem) {
                memcpy_fromio(target_buffer, map.vaddr_iomem, to_copy);
            } else {
                memcpy(target_buffer, map.vaddr, to_copy);
            }
            
            dma_buf_vunmap(gem_obj->dma_buf, &map);
            
            if (needs_detiling) {
                ret = convert_tiled_to_linear((uint8_t*)raw_buffer, 
                                            (uint8_t*)capture->pixel_buffer,
                                            capture->width, capture->height, 
                                            capture->pitch, capture->detected_tiling);
                if (ret == 0) {
                    capture->is_detiled = true;
                }
            }
            
            capture->has_pixels = true;
            atomic64_inc(&successful_captures);
        }
    }
    
    if (raw_buffer) {
        vfree(raw_buffer);
    }
    
    end_time = ktime_get();
    
    // Log if extraction took too long (> 1ms for real-time)
    if (ktime_to_us(ktime_sub(end_time, start_time)) > 1000) {
        pr_warn("Slow pixel extraction: %lld us\n", 
                ktime_to_us(ktime_sub(end_time, start_time)));
    }
    
    return capture->has_pixels ? 0 : -ENODATA;
}

// Capture framebuffer at display time
static int capture_realtime_fb(struct drm_framebuffer *fb, struct drm_device *dev,
                              struct drm_crtc *crtc)
{
    struct realtime_fb_data *capture;
    int ret;
    size_t expected_size;
    
    if (!fb || !fb->obj[0]) {
        return -EINVAL;
    }
    
    // Quick size check
    expected_size = fb->height * fb->width * 4;
    if (expected_size > MAX_CAPTURE_SIZE) {
        return -EINVAL;
    }
    
    mutex_lock(&capture_mutex);
    
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
    capture->crtc = crtc;
    capture->width = fb->width;
    capture->height = fb->height;
    capture->format = fb->format->format;
    capture->pitch = fb->pitches[0];
    capture->capture_timestamp = ktime_get_ns();
    capture->frame_number = 0;
    capture->capture_method = 0; // atomic commit
    capture->is_current_frame = false; // next frame
    capture->detected_tiling = detect_intel_tiling(fb);
    capture->buffer_size = expected_size;
    
    // Allocate buffer
    capture->pixel_buffer = vmalloc(capture->buffer_size);
    if (!capture->pixel_buffer) {
        mutex_unlock(&capture_mutex);
        return -ENOMEM;
    }
    
    // Extract pixels
    ret = extract_gem_pixels_fast(fb->obj[0], capture);
    if (ret == 0) {
        capture->valid = true;
        atomic64_inc(&atomic_captures);
    } else {
        capture->valid = true; // Still valid for metadata
    }
    
    // Update indices
    current_index = (current_index + 1) % MAX_FB_CAPTURE;
    if (capture_count < MAX_FB_CAPTURE) {
        capture_count++;
    }
    
    mutex_unlock(&capture_mutex);
    
    return 0;
}

// Kprobe handler for drm_atomic_commit
static int handler_drm_atomic_commit(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_atomic_state *state;
    struct drm_crtc *crtc;
    struct drm_crtc_state *crtc_state;
    int i;
    
#ifdef CONFIG_X86_64
    state = (struct drm_atomic_state *)regs->di;
#elif defined(CONFIG_ARM64)
    state = (struct drm_atomic_state *)regs->regs[0];
#else
    return 0;
#endif

    if (!state || !state->dev) {
        return 0;
    }

    // Process all CRTCs in the atomic commit
    for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
        if (crtc_state->active && crtc_state->plane_mask) {
            struct drm_plane *plane;
            struct drm_plane_state *plane_state;
            int j;
            
            // Find primary plane with framebuffer
            for_each_new_plane_in_state(state, plane, plane_state, j) {
                if (plane->crtc == crtc && plane_state->fb && 
                    plane->type == DRM_PLANE_TYPE_PRIMARY) {
                    
                    unsigned long flags;
                    int crtc_index = drm_crtc_index(crtc);
                    
                    if (crtc_index < 8) {
                        // Update active framebuffer tracking
                        spin_lock_irqsave(&active_fb_lock, flags);
                        if (active_fb_per_crtc[crtc_index]) {
                            drm_framebuffer_put(active_fb_per_crtc[crtc_index]);
                        }
                        active_fb_per_crtc[crtc_index] = plane_state->fb;
                        drm_framebuffer_get(plane_state->fb);
                        spin_unlock_irqrestore(&active_fb_lock, flags);
                        
                        // Capture at atomic commit time (next frame)
                        capture_realtime_fb(plane_state->fb, state->dev, crtc);
                    }
                    break;
                }
            }
        }
    }
    
    return 0;
}

// Kprobe structures
static struct kprobe kp_drm_atomic_commit = {
    .symbol_name = "drm_atomic_commit",
    .pre_handler = handler_drm_atomic_commit,
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

static const char* capture_method_to_string(int method)
{
    switch(method) {
        case 0: return "ATOMIC_COMMIT";
        default: return "UNKNOWN";
    }
}

static int drm_fb_realtime_proc_show(struct seq_file *m, void *v)
{
    int i;
    
    mutex_lock(&capture_mutex);
    
    seq_printf(m, "Real-Time DRM Framebuffer Extractor\n");
    seq_printf(m, "===================================\n");
    seq_printf(m, "Captured framebuffers: %d\n", capture_count);
    seq_printf(m, "Total capture attempts: %lld\n", atomic64_read(&total_captures));
    seq_printf(m, "Successful captures: %lld\n", atomic64_read(&successful_captures));
    seq_printf(m, "Atomic commit captures: %lld\n", atomic64_read(&atomic_captures));
    seq_printf(m, "\n");
    
    for (i = 0; i < capture_count; i++) {
        struct realtime_fb_data *capture = &captured_fbs[i];
        const char *tiling_str;
        
        if (!capture->valid)
            continue;
        
        switch (capture->detected_tiling) {
            case INTEL_TILING_X: tiling_str = "X-tiled"; break;
            case INTEL_TILING_Y: tiling_str = "Y-tiled"; break;
            case INTEL_TILING_YF: tiling_str = "Yf-tiled"; break;
            default: tiling_str = "Linear"; break;
        }
            
        seq_printf(m, "Capture %d:\n", i);
        seq_printf(m, "  Timestamp: %llu ns\n", capture->capture_timestamp);
        seq_printf(m, "  Frame number: %u\n", capture->frame_number);
        seq_printf(m, "  Capture method: %s\n", capture_method_to_string(capture->capture_method));
        seq_printf(m, "  Current frame: %s\n", capture->is_current_frame ? "YES" : "NO");
        seq_printf(m, "  Device: %p\n", capture->dev);
        seq_printf(m, "  CRTC: %p\n", capture->crtc);
        seq_printf(m, "  Framebuffer: %p\n", capture->fb);
        seq_printf(m, "  Dimensions: %dx%d\n", capture->width, capture->height);
        seq_printf(m, "  Format: 0x%08x (%s)\n", capture->format, format_to_string(capture->format));
        seq_printf(m, "  Pitch: %d bytes/row\n", capture->pitch);
        seq_printf(m, "  Buffer size: %zu bytes\n", capture->buffer_size);
        seq_printf(m, "  Tiling: %s\n", tiling_str);
        seq_printf(m, "  Detiled: %s\n", capture->is_detiled ? "YES" : "NO");
        seq_printf(m, "  Pixel data: %s\n", capture->has_pixels ? "AVAILABLE" : "NOT AVAILABLE");
        seq_printf(m, "\n");
    }
    
    seq_printf(m, "Usage:\n");
    seq_printf(m, "  To extract raw pixel data: dd if=/proc/%s bs=1 count=Y of=frame.raw\n", PROC_RAW_NAME);
    seq_printf(m, "  Data is in linear RGBA format, ready for display\n");
    
    mutex_unlock(&capture_mutex);
    return 0;
}

static ssize_t drm_fb_realtime_raw_read(struct file *file, char __user *buffer, size_t count, loff_t *pos)
{
    struct realtime_fb_data *capture = NULL;
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
    
    ret = copy_to_user(buffer, (char*)capture->pixel_buffer + offset, to_copy);
    if (ret) {
        mutex_unlock(&capture_mutex);
        return -EFAULT;
    }
    
    *pos += to_copy;
    mutex_unlock(&capture_mutex);
    
    return to_copy;
}

static int drm_fb_realtime_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, drm_fb_realtime_proc_show, NULL);
}

static const struct proc_ops drm_fb_realtime_proc_ops = {
    .proc_open = drm_fb_realtime_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static const struct proc_ops drm_fb_realtime_raw_ops = {
    .proc_read = drm_fb_realtime_raw_read,
    .proc_lseek = default_llseek,
};

// Module initialization
static int __init drm_fb_realtime_init(void)
{
    int ret;
    int i;

    pr_info("Real-Time DRM Framebuffer Extractor loading\n");

    // Initialize capture array
    memset(captured_fbs, 0, sizeof(captured_fbs));
    capture_count = 0;
    current_index = 0;
    
    // Initialize active framebuffer tracking
    for (i = 0; i < 8; i++) {
        active_fb_per_crtc[i] = NULL;
    }

    // Register kprobe for atomic commits
    ret = register_kprobe(&kp_drm_atomic_commit);
    if (ret < 0) {
        pr_err("Failed to register atomic commit kprobe: %d\n", ret);
        return ret;
    }

    // Create proc entries
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &drm_fb_realtime_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry %s\n", PROC_NAME);
        unregister_kprobe(&kp_drm_atomic_commit);
        return -ENOMEM;
    }
    
    proc_raw_entry = proc_create(PROC_RAW_NAME, 0644, NULL, &drm_fb_realtime_raw_ops);
    if (!proc_raw_entry) {
        pr_err("Failed to create proc entry %s\n", PROC_RAW_NAME);
        proc_remove(proc_entry);
        unregister_kprobe(&kp_drm_atomic_commit);
        return -ENOMEM;
    }

    pr_info("Real-Time DRM Framebuffer Extractor loaded successfully\n");
    pr_info("Use 'cat /proc/%s' to view capture info\n", PROC_NAME);
    pr_info("Use 'cat /proc/%s' to access raw pixel data\n", PROC_RAW_NAME);
    
    return 0;
}

// Module cleanup
static void __exit drm_fb_realtime_exit(void)
{
    int i;
    unsigned long flags;

    pr_info("Real-Time DRM Framebuffer Extractor unloading\n");

    // Remove proc entries
    if (proc_raw_entry) {
        proc_remove(proc_raw_entry);
    }
    if (proc_entry) {
        proc_remove(proc_entry);
    }

    // Unregister kprobe
    unregister_kprobe(&kp_drm_atomic_commit);

    // Clean up active framebuffer references
    spin_lock_irqsave(&active_fb_lock, flags);
    for (i = 0; i < 8; i++) {
        if (active_fb_per_crtc[i]) {
            drm_framebuffer_put(active_fb_per_crtc[i]);
            active_fb_per_crtc[i] = NULL;
        }
    }
    spin_unlock_irqrestore(&active_fb_lock, flags);

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

    pr_info("Real-Time DRM Framebuffer Extractor unloaded\n");
}

module_init(drm_fb_realtime_init);
module_exit(drm_fb_realtime_exit);
