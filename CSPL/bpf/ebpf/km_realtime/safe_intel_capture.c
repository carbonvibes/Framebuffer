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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safe Intel Probe");
MODULE_DESCRIPTION("Safe Intel probe for framebuffer capture");
MODULE_VERSION("5.0");

#define PROC_NAME "safe_intel_capture"
#define MAX_FB_CAPTURE 3
#define MAX_CAPTURE_SIZE (1920 * 1080 * 4) // Limit to 1080p for safety

struct safe_fb_data {
    struct drm_framebuffer *fb;
    void *pixel_buffer;
    size_t buffer_size;
    uint32_t width, height;
    uint32_t format;
    uint64_t timestamp;
    bool valid;
    bool has_pixels;
};

static struct safe_fb_data captured_fbs[MAX_FB_CAPTURE];
static int capture_count = 0;
static int current_index = 0;
static DEFINE_MUTEX(capture_mutex);
static struct proc_dir_entry *proc_entry;

// Performance counters
static atomic64_t intel_probe_calls = ATOMIC64_INIT(0);
static atomic64_t successful_captures = ATOMIC64_INIT(0);

// SAFE pixel extraction - minimal and non-blocking
static int extract_pixels_safe(struct drm_gem_object *gem_obj, struct safe_fb_data *capture)
{
    if (!gem_obj || !capture || !capture->pixel_buffer) {
        return -EINVAL;
    }

    // Only try the safest method - SHMEM with immediate bailout
    if (gem_obj->filp && gem_obj->filp->f_mapping) {
        struct address_space *mapping = gem_obj->filp->f_mapping;
        size_t copied = 0;
        pgoff_t index = 0;
        pgoff_t max_pages = min_t(pgoff_t, 4, // Only try first 4 pages for safety
                                 (capture->buffer_size + PAGE_SIZE - 1) >> PAGE_SHIFT);
        
        while (copied < capture->buffer_size && index < max_pages) {
            struct page *page = find_get_page(mapping, index);
            if (page) {
                void *kaddr = kmap_atomic(page);
                if (kaddr) {
                    size_t to_copy = min_t(size_t, PAGE_SIZE, capture->buffer_size - copied);
                    memcpy((char*)capture->pixel_buffer + copied, kaddr, to_copy);
                    copied += to_copy;
                    kunmap_atomic(kaddr);
                }
                put_page(page);
            }
            index++;
        }
        
        if (copied > 0) {
            capture->has_pixels = true;
            atomic64_inc(&successful_captures);
            return 0;
        }
    }
    
    return -ENODATA;
}

// SAFE framebuffer capture with size limits and error checking
static int capture_safe_fb(struct drm_framebuffer *fb)
{
    struct safe_fb_data *capture;
    size_t expected_size;
    int ret;
    
    if (!fb || !fb->obj[0]) {
        return -EINVAL;
    }
    
    // Safety checks
    if (fb->width > 1920 || fb->height > 1080) {
        return -EINVAL; // Skip large framebuffers
    }
    
    expected_size = fb->height * fb->width * 4;
    if (expected_size > MAX_CAPTURE_SIZE) {
        return -EINVAL;
    }
    
    // Try to acquire mutex with timeout
    if (!mutex_trylock(&capture_mutex)) {
        return -EBUSY; // Don't block
    }
    
    capture = &captured_fbs[current_index];
    
    // Clean up previous capture
    if (capture->pixel_buffer) {
        vfree(capture->pixel_buffer);
        capture->pixel_buffer = NULL;
    }
    
    // Initialize capture structure
    memset(capture, 0, sizeof(*capture));
    capture->fb = fb;
    capture->width = fb->width;
    capture->height = fb->height;
    capture->format = fb->format->format;
    capture->timestamp = ktime_get_ns();
    capture->buffer_size = expected_size;
    
    // Allocate buffer
    capture->pixel_buffer = vmalloc(capture->buffer_size);
    if (!capture->pixel_buffer) {
        mutex_unlock(&capture_mutex);
        return -ENOMEM;
    }
    
    // Extract pixels safely
    ret = extract_pixels_safe(fb->obj[0], capture);
    capture->valid = true;
    
    // Update indices
    current_index = (current_index + 1) % MAX_FB_CAPTURE;
    if (capture_count < MAX_FB_CAPTURE) {
        capture_count++;
    }
    
    mutex_unlock(&capture_mutex);
    return 0;
}

// MINIMAL and SAFE Intel atomic commit tail handler
static int handler_intel_atomic_commit_tail(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_atomic_state *state;
    struct drm_crtc *crtc;
    struct drm_crtc_state *crtc_state;
    int i;
    
    // Increment counter
    atomic64_inc(&intel_probe_calls);
    
    // Minimal debug output to avoid spam
    if (atomic64_read(&intel_probe_calls) % 100 == 1) {
        pr_info("Safe Intel probe: %lld calls\n", atomic64_read(&intel_probe_calls));
    }
    
#ifdef CONFIG_X86_64
    state = (struct drm_atomic_state *)regs->di;
#elif defined(CONFIG_ARM64)
    state = (struct drm_atomic_state *)regs->regs[0];
#else
    return 0;
#endif

    // Safety check
    if (!state) {
        return 0;
    }

    // SAFE iteration with immediate bailout on any error
    __drm_for_each_crtc(crtc, state->dev) {
        if (!crtc) continue;
        
        crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
        if (!crtc_state || !crtc_state->active) continue;
        
        // Look for primary plane updates
        struct drm_plane *plane;
        struct drm_plane_state *plane_state;
        
        __drm_for_each_plane(plane, state->dev) {
            if (!plane || plane->type != DRM_PLANE_TYPE_PRIMARY) continue;
            if (plane->crtc != crtc) continue;
            
            plane_state = drm_atomic_get_new_plane_state(state, plane);
            if (!plane_state || !plane_state->fb) continue;
            
            // Found a primary plane framebuffer update
            pr_info("Safe: Found FB update %dx%d\n", 
                    plane_state->fb->width, plane_state->fb->height);
            
            capture_safe_fb(plane_state->fb);
            
            // Only capture one FB per atomic commit
            return 0;
        }
    }
    
    return 0;
}

// Kprobe structure
static struct kprobe kp_intel_atomic_commit_tail = {
    .symbol_name = "intel_atomic_commit_tail",
    .pre_handler = handler_intel_atomic_commit_tail,
};

// Proc file for displaying capture information
static int safe_intel_proc_show(struct seq_file *m, void *v)
{
    int i;
    
    mutex_lock(&capture_mutex);
    
    seq_printf(m, "Safe Intel Framebuffer Capture\n");
    seq_printf(m, "==============================\n");
    seq_printf(m, "Intel probe calls: %lld\n", atomic64_read(&intel_probe_calls));
    seq_printf(m, "Successful captures: %lld\n", atomic64_read(&successful_captures));
    seq_printf(m, "Captured framebuffers: %d\n\n", capture_count);
    
    for (i = 0; i < capture_count; i++) {
        struct safe_fb_data *capture = &captured_fbs[i];
        
        if (!capture->valid) continue;
            
        seq_printf(m, "Capture %d:\n", i);
        seq_printf(m, "  Timestamp: %llu ns\n", capture->timestamp);
        seq_printf(m, "  Dimensions: %dx%d\n", capture->width, capture->height);
        seq_printf(m, "  Format: 0x%08x\n", capture->format);
        seq_printf(m, "  Buffer size: %zu bytes\n", capture->buffer_size);
        seq_printf(m, "  Pixel data: %s\n", capture->has_pixels ? "AVAILABLE" : "NOT AVAILABLE");
        seq_printf(m, "\n");
    }
    
    mutex_unlock(&capture_mutex);
    return 0;
}

static int safe_intel_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, safe_intel_proc_show, NULL);
}

static const struct proc_ops safe_intel_proc_ops = {
    .proc_open = safe_intel_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Module initialization
static int __init safe_intel_init(void)
{
    int ret;

    pr_info("Safe Intel FB Capture loading\n");

    // Initialize capture array
    memset(captured_fbs, 0, sizeof(captured_fbs));
    capture_count = 0;
    current_index = 0;

    // Register kprobe
    ret = register_kprobe(&kp_intel_atomic_commit_tail);
    if (ret < 0) {
        pr_err("Failed to register intel_atomic_commit_tail kprobe: %d\n", ret);
        return ret;
    }

    // Create proc entry
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &safe_intel_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry %s\n", PROC_NAME);
        unregister_kprobe(&kp_intel_atomic_commit_tail);
        return -ENOMEM;
    }

    pr_info("Safe Intel FB Capture loaded successfully\n");
    pr_info("Use 'cat /proc/%s' to view capture info\n", PROC_NAME);
    
    return 0;
}

// Module cleanup
static void __exit safe_intel_exit(void)
{
    int i;

    pr_info("Safe Intel FB Capture unloading\n");

    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }

    // Unregister kprobe
    unregister_kprobe(&kp_intel_atomic_commit_tail);

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

    pr_info("Safe Intel FB Capture unloaded\n");
}

module_init(safe_intel_init);
module_exit(safe_intel_exit);
