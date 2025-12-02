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
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_atomic.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DRM FB Content Extractor");
MODULE_DESCRIPTION("Extract actual DRM framebuffer pixel content");
MODULE_VERSION("2.0");

#define PROC_NAME "drm_fb_pixels"
#define PROC_RAW_NAME "drm_fb_raw"
#define MAX_FB_CAPTURE 5
#define MAX_CAPTURE_SIZE (3840 * 1080 * 4) // Max 1080p RGBA

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
};

static struct fb_pixel_data captured_fbs[MAX_FB_CAPTURE];
static int capture_count = 0;
static int current_index = 0;
static DEFINE_MUTEX(capture_mutex);
static struct proc_dir_entry *proc_entry;
static struct proc_dir_entry *proc_raw_entry;

// Forward declarations
static int capture_fb_pixels(struct drm_framebuffer *fb, struct drm_device *dev);

// Function to extract linear pixel data using proper GEM object vmap
static int extract_gem_pixels_linear(struct drm_framebuffer *fb, struct fb_pixel_data *capture)
{
    struct drm_gem_object *gem_obj;
    struct dma_buf_map map = DMA_BUF_MAP_INIT_VADDR(NULL);
    int ret;
    
    if (!fb || !capture) {
        return -EINVAL;
    }

    pr_info("Extracting linear pixels using GEM object vmap\n");

    // Get the GEM object using the proper DRM framebuffer helper
    gem_obj = drm_gem_fb_get_obj(fb, 0);
    if (!gem_obj) {
        pr_err("Failed to get GEM object from framebuffer\n");
        return -EINVAL;
    }

    pr_info("Found GEM object, requesting linear mapping\n");
    
    // Method 1: Try to get linear mapping through GEM object's vmap function
    if (gem_obj->funcs && gem_obj->funcs->vmap) {
        ret = gem_obj->funcs->vmap(gem_obj, &map);
        if (ret == 0 && !dma_buf_map_is_null(&map)) {
            size_t copy_size = min_t(size_t, gem_obj->size, capture->buffer_size);
            
            pr_info("GEM vmap successful, copying %zu bytes\n", copy_size);
            
            // Copy linear pixel data directly from GEM object's linear view
            if (map.is_iomem) {
                memcpy_fromio(capture->pixel_buffer, map.vaddr_iomem, copy_size);
            } else {
                memcpy(capture->pixel_buffer, map.vaddr, copy_size);
            }
            
            // Clean up the mapping
            gem_obj->funcs->vunmap(gem_obj, &map);
            
            capture->has_pixels = true;
            pr_info("Successfully copied linear framebuffer data via GEM vmap\n");
            return 0;
        } else {
            pr_warn("GEM vmap failed: %d\n", ret);
        }
    }
    
    // Method 2: Try DMA-buf vmap if GEM vmap failed
    if (gem_obj->dma_buf) {
        struct dma_buf *db = gem_obj->dma_buf;
        
        pr_info("Trying DMA-buf vmap as fallback\n");
        
        // Flush GPU caches to ensure coherency
        ret = dma_buf_begin_cpu_access(db, DMA_FROM_DEVICE);
        if (ret) {
            pr_err("Failed to begin CPU access: %d\n", ret);
            return ret;
        }
        
        // Map the DMA buffer - should provide linear mapping
        ret = dma_buf_vmap(db, &map);
        if (ret == 0 && !dma_buf_map_is_null(&map)) {
            size_t copy_size = min_t(size_t, db->size, capture->buffer_size);
            
            pr_info("DMA-buf vmap successful, copying %zu bytes\n", copy_size);
            
            // Copy linear pixel data
            if (map.is_iomem) {
                memcpy_fromio(capture->pixel_buffer, map.vaddr_iomem, copy_size);
            } else {
                memcpy(capture->pixel_buffer, map.vaddr, copy_size);
            }
            
            // Clean up the mapping
            dma_buf_vunmap(db, &map);
            dma_buf_end_cpu_access(db, DMA_FROM_DEVICE);
            
            capture->has_pixels = true;
            pr_info("Successfully copied linear framebuffer data via DMA-buf vmap\n");
            return 0;
        } else {
            pr_err("DMA-buf vmap failed: %d\n", ret);
            dma_buf_end_cpu_access(db, DMA_FROM_DEVICE);
        }
    }
    
    pr_err("All linear mapping methods failed\n");
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
    
    // Filter out small framebuffers (likely cursors/overlays)
    if (fb->width < 800 || fb->height < 600) {
        pr_debug("Skipping small framebuffer %dx%d (likely cursor/overlay)\n", 
                fb->width, fb->height);
        return 0;
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
    
    // Calculate expected buffer size
    expected_size = capture->height * capture->pitch;
    if (expected_size > MAX_CAPTURE_SIZE) {
        expected_size = MAX_CAPTURE_SIZE;
        pr_warn("Framebuffer too large, limiting to %d bytes\n", MAX_CAPTURE_SIZE);
    }
    
    capture->buffer_size = expected_size;
    
    // Allocate buffer for pixel data
    capture->pixel_buffer = vmalloc(capture->buffer_size);
    if (!capture->pixel_buffer) {
        pr_err("Failed to allocate pixel buffer (%zu bytes)\n", capture->buffer_size);
        mutex_unlock(&capture_mutex);
        return -ENOMEM;
    }
    
    // Extract linear pixel data using proper GEM object vmap
    ret = extract_gem_pixels_linear(fb, capture);
    if (ret == 0) {
        capture->has_pixels = true;
        capture->valid = true;
        
        pr_info("Successfully captured framebuffer pixels: %dx%d, format=0x%08x, %zu bytes\n",
                capture->width, capture->height, capture->format, capture->buffer_size);
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

// Kprobe handler for drm_atomic_helper_commit_tail - when framebuffers are actually displayed
static int handler_drm_atomic_commit_tail(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_atomic_state *state;
    
    // Extract parameters based on architecture
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

    pr_info("Intercepted atomic commit tail on device %p\n", state->dev);

    // For now, let's just log that we caught the atomic commit
    // In a real implementation, we'd iterate through the planes
    // but this requires more complex atomic state handling
    
    return 0;
}

// Handler for CRTC vblank events - when framebuffer content is displayed
static int handler_drm_crtc_vblank(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_device *dev;
    unsigned int pipe;
    
#ifdef CONFIG_X86_64
    dev = (struct drm_device *)regs->di;
    pipe = (unsigned int)regs->si;
#elif defined(CONFIG_ARM64)
    dev = (struct drm_device *)regs->regs[0];
    pipe = (unsigned int)regs->regs[1];
#else
    return 0;
#endif

    if (!dev) {
        return 0;
    }

    // Try to find the CRTC for this pipe and get its framebuffer
    struct drm_crtc *crtc;
    drm_for_each_crtc(crtc, dev) {
        if (drm_crtc_index(crtc) == pipe && crtc->primary && crtc->primary->fb) {
            if (crtc->primary->fb->width >= 800 && crtc->primary->fb->height >= 600) {
                pr_info("VBlank event - capturing primary framebuffer %dx%d\n", 
                       crtc->primary->fb->width, crtc->primary->fb->height);
                capture_fb_pixels(crtc->primary->fb, dev);
                break; // Only capture once per vblank
            }
        }
    }
    
    return 0;
}

// Fallback handler for drm_framebuffer_init (in case atomic commits don't work)
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

    // Only process larger framebuffers (main screen content)
    if (fb->width >= 800 && fb->height >= 600) {
        pr_info("Capturing framebuffer at init: %dx%d\n", fb->width, fb->height);
        capture_fb_pixels(fb, dev);
    }
    
    return 0;
}

// Kprobe structures
static struct kprobe kp_atomic_commit_tail = {
    .symbol_name = "drm_atomic_helper_commit_tail",
    .pre_handler = handler_drm_atomic_commit_tail,
};

static struct kprobe kp_vblank = {
    .symbol_name = "drm_crtc_send_vblank_event",
    .pre_handler = handler_drm_crtc_vblank,
};

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
    
    seq_printf(m, "DRM Framebuffer Pixel Extractor\n");
    seq_printf(m, "Captured framebuffers: %d\n\n", capture_count);
    
    for (i = 0; i < capture_count; i++) {
        struct fb_pixel_data *capture = &captured_fbs[i];
        
        if (!capture->valid)
            continue;
            
        seq_printf(m, "Capture %d:\n", i);
        seq_printf(m, "  Timestamp: %llu ns\n", capture->timestamp);
        seq_printf(m, "  Device: %p\n", capture->dev);
        seq_printf(m, "  Framebuffer: %p\n", capture->fb);
        seq_printf(m, "  Dimensions: %dx%d\n", capture->width, capture->height);
        seq_printf(m, "  Format: 0x%08x (%s)\n", capture->format, format_to_string(capture->format));
        seq_printf(m, "  Pitch: %d bytes/row\n", capture->pitch);
        seq_printf(m, "  Modifier: 0x%llx\n", (unsigned long long)capture->fb->modifier);
        seq_printf(m, "  Buffer size: %zu bytes\n", capture->buffer_size);
        seq_printf(m, "  Pixel data: %s\n", capture->has_pixels ? "AVAILABLE" : "NOT AVAILABLE");
        
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
        
        // Sample more pixels to detect if framebuffer contains actual content
        if (capture->buffer_size >= 16) {
            uint32_t *pixels = (uint32_t*)capture->pixel_buffer;
            uint32_t mid_pixel = pixels[capture->buffer_size / 8];
            uint32_t last_pixel = pixels[(capture->buffer_size / 4) - 1];
            bool has_variation = false;
            uint32_t sample_count = min_t(uint32_t, 100, capture->buffer_size / 4);
            uint32_t i;
            seq_printf(m, "  Mid pixel (ARGB): 0x%08x\n", mid_pixel);
            seq_printf(m, "  Last pixel (ARGB): 0x%08x\n", last_pixel);
            
            // Check for non-uniform content (actual screen data vs solid color)
            for (i = 0; i < sample_count; i += 10) {
                if (pixels[i] != first_pixel) {
                    has_variation = true;
                    break;
                }
            }
            seq_printf(m, "  Content variation: %s\n", has_variation ? "YES (likely screen content)" : "NO (likely solid color/empty)");
        }
            }
        }
        seq_printf(m, "\n");
    }
    
    seq_printf(m, "Usage:\n");
    seq_printf(m, "  To extract raw pixel data: dd if=/proc/%s bs=1 skip=X count=Y of=framebuffer.raw\n", PROC_RAW_NAME);
    seq_printf(m, "  Where X is the byte offset and Y is the buffer size from above\n");
    
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

    // Register primary kprobe for atomic commits (preferred method)
    ret = register_kprobe(&kp_atomic_commit_tail);
    if (ret < 0) {
        pr_warn("Failed to register drm_atomic_helper_commit_tail kprobe: %d\n", ret);
    } else {
        pr_info("Successfully registered atomic commit tail kprobe\n");
    }
    
    // Register vblank kprobe for display timing
    ret = register_kprobe(&kp_vblank);
    if (ret < 0) {
        pr_warn("Failed to register drm_crtc_send_vblank_event kprobe: %d\n", ret);
    } else {
        pr_info("Successfully registered vblank kprobe\n");
    }
    
    // Register fallback framebuffer init kprobe
    ret = register_kprobe(&kp_drm_fb_init);
    if (ret < 0) {
        pr_warn("Failed to register drm_framebuffer_init kprobe: %d\n", ret);
        // Don't fail completely - other kprobes might work
    } else {
        pr_info("Successfully registered framebuffer init kprobe\n");
    }

    // Create proc entries
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &drm_fb_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry %s\n", PROC_NAME);
        unregister_kprobe(&kp_atomic_commit_tail);
        unregister_kprobe(&kp_vblank);
        unregister_kprobe(&kp_drm_fb_init);
        return -ENOMEM;
    }
    
    proc_raw_entry = proc_create(PROC_RAW_NAME, 0644, NULL, &drm_fb_raw_ops);
    if (!proc_raw_entry) {
        pr_err("Failed to create proc entry %s\n", PROC_RAW_NAME);
        proc_remove(proc_entry);
        unregister_kprobe(&kp_atomic_commit_tail);
        unregister_kprobe(&kp_vblank);
        unregister_kprobe(&kp_drm_fb_init);
        return -ENOMEM;
    }

    pr_info("DRM Framebuffer Pixel Extractor loaded successfully\n");
    pr_info("Use 'cat /proc/%s' to view capture info\n", PROC_NAME);
    pr_info("Use 'cat /proc/%s' to access raw pixel data\n", PROC_RAW_NAME);
    
    return 0;
}

// Module cleanup
static void __exit drm_fb_extractor_exit(void)
{
    int i;

    pr_info("DRM Framebuffer Pixel Extractor unloading\n");

    // Remove proc entries
    if (proc_raw_entry) {
        proc_remove(proc_raw_entry);
    }
    if (proc_entry) {
        proc_remove(proc_entry);
    }

    // Unregister kprobes
    unregister_kprobe(&kp_atomic_commit_tail);
    unregister_kprobe(&kp_vblank);
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

    pr_info("DRM Framebuffer Pixel Extractor unloaded\n");
}

module_init(drm_fb_extractor_init);
module_exit(drm_fb_extractor_exit);