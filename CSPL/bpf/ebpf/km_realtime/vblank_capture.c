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
#include <linux/interrupt.h>
#include <linux/workqueue.h>
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
MODULE_AUTHOR("VBlank FB Capture");
MODULE_DESCRIPTION("VBlank-based framebuffer capture for real content");
MODULE_VERSION("6.0");

#define PROC_NAME "vblank_fb_capture"
#define PROC_RAW_NAME "vblank_fb_raw"
#define MAX_FB_CAPTURE 5
#define MAX_CAPTURE_SIZE (1920 * 1080 * 4) // Max 1080p for performance

struct vblank_fb_data {
    struct drm_framebuffer *fb;
    struct drm_device *dev;
    void *pixel_buffer;
    size_t buffer_size;
    uint32_t width, height;
    uint32_t format;
    uint32_t pitch;
    uint64_t timestamp;
    uint32_t sequence;
    bool valid;
    bool has_pixels;
    int capture_method; // 0=vblank, 1=plane_update, 2=pipe_update
};

static struct vblank_fb_data captured_fbs[MAX_FB_CAPTURE];
static int capture_count = 0;
static int current_index = 0;
static DEFINE_MUTEX(capture_mutex);
static struct proc_dir_entry *proc_entry;
static struct proc_dir_entry *proc_raw_entry;

// Performance counters
static atomic64_t vblank_events = ATOMIC64_INIT(0);
static atomic64_t plane_updates = ATOMIC64_INIT(0);
static atomic64_t pipe_updates = ATOMIC64_INIT(0);
static atomic64_t successful_captures = ATOMIC64_INIT(0);

// Current framebuffer tracking per CRTC
static struct drm_framebuffer *current_fb[8] = {NULL}; // Support up to 8 CRTCs
static DEFINE_SPINLOCK(fb_tracking_lock);

// Workqueue for safe capture processing
static struct workqueue_struct *capture_wq;

struct capture_work {
    struct work_struct work;
    struct drm_framebuffer *fb;
    struct drm_device *dev;
    uint32_t sequence;
    int method;
};

// Safe pixel extraction optimized for real content
static int extract_real_pixels(struct drm_gem_object *gem_obj, struct vblank_fb_data *capture)
{
    if (!gem_obj || !capture || !capture->pixel_buffer) {
        return -EINVAL;
    }

    pr_info("Extracting pixels from GEM object, size=%zu\n", gem_obj->size);

    // Method 1: SHMEM mapping (most common for Intel)
    if (gem_obj->filp && gem_obj->filp->f_mapping) {
        struct address_space *mapping = gem_obj->filp->f_mapping;
        size_t copied = 0;
        pgoff_t index = 0;
        pgoff_t max_pages = min_t(pgoff_t, 
                                 (capture->buffer_size + PAGE_SIZE - 1) >> PAGE_SHIFT,
                                 (gem_obj->size + PAGE_SIZE - 1) >> PAGE_SHIFT);
        
        pr_info("Trying SHMEM method, max_pages=%lu\n", max_pages);
        
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
            } else {
                // Try to fault in the page
                struct vm_area_struct *vma;
                mmap_read_lock(current->mm);
                vma = find_vma(current->mm, (unsigned long)page_address(alloc_page(GFP_KERNEL)));
                if (vma) {
                    handle_mm_fault(vma, (unsigned long)page_address(vma->vm_start), FAULT_FLAG_WRITE, NULL);
                }
                mmap_read_unlock(current->mm);
            }
            index++;
        }
        
        pr_info("SHMEM extraction: copied %zu bytes\n", copied);
        
        if (copied > 0) {
            capture->has_pixels = true;
            atomic64_inc(&successful_captures);
            return 0;
        }
    }
    
    // Method 2: DMA-buf mapping (for imported buffers)
    if (gem_obj->dma_buf && gem_obj->import_attach) {
        struct dma_buf_map map;
        int ret;
        
        pr_info("Trying DMA-buf method\n");
        
        ret = dma_buf_vmap(gem_obj->dma_buf, &map);
        if (ret == 0 && !dma_buf_map_is_null(&map)) {
            size_t to_copy = min_t(size_t, gem_obj->dma_buf->size, capture->buffer_size);
            
            if (map.is_iomem) {
                memcpy_fromio(capture->pixel_buffer, map.vaddr_iomem, to_copy);
            } else {
                memcpy(capture->pixel_buffer, map.vaddr, to_copy);
            }
            
            dma_buf_vunmap(gem_obj->dma_buf, &map);
            
            pr_info("DMA-buf extraction: copied %zu bytes\n", to_copy);
            
            capture->has_pixels = true;
            atomic64_inc(&successful_captures);
            return 0;
        }
    }
    
    pr_warn("Could not extract pixel data from GEM object\n");
    return -ENODATA;
}

// Framebuffer capture function
static int capture_vblank_fb(struct drm_framebuffer *fb, struct drm_device *dev, 
                            uint32_t sequence, int method)
{
    struct vblank_fb_data *capture;
    size_t expected_size;
    int ret;
    
    if (!fb || !fb->obj[0]) {
        return -EINVAL;
    }
    
    // Size checks
    if (fb->width > 1920 || fb->height > 1080) {
        pr_info("Skipping large framebuffer: %dx%d\n", fb->width, fb->height);
        return -EINVAL;
    }
    
    expected_size = fb->height * fb->width * 4;
    if (expected_size > MAX_CAPTURE_SIZE) {
        return -EINVAL;
    }
    
    if (!mutex_trylock(&capture_mutex)) {
        return -EBUSY;
    }
    
    capture = &captured_fbs[current_index];
    
    // Clean up previous capture
    if (capture->pixel_buffer) {
        vfree(capture->pixel_buffer);
        capture->pixel_buffer = NULL;
    }
    
    // Initialize capture
    memset(capture, 0, sizeof(*capture));
    capture->fb = fb;
    capture->dev = dev;
    capture->width = fb->width;
    capture->height = fb->height;
    capture->format = fb->format->format;
    capture->pitch = fb->pitches[0];
    capture->timestamp = ktime_get_ns();
    capture->sequence = sequence;
    capture->capture_method = method;
    capture->buffer_size = expected_size;
    
    // Allocate buffer
    capture->pixel_buffer = vmalloc(capture->buffer_size);
    if (!capture->pixel_buffer) {
        mutex_unlock(&capture_mutex);
        return -ENOMEM;
    }
    
    // Extract pixels
    ret = extract_real_pixels(fb->obj[0], capture);
    capture->valid = true;
    
    pr_info("Captured FB %dx%d, format=0x%08x, method=%d, pixels=%s\n",
            capture->width, capture->height, capture->format, method,
            capture->has_pixels ? "YES" : "NO");
    
    // Update indices
    current_index = (current_index + 1) % MAX_FB_CAPTURE;
    if (capture_count < MAX_FB_CAPTURE) {
        capture_count++;
    }
    
    mutex_unlock(&capture_mutex);
    return 0;
}

// Work function for safe capture processing
static void capture_work_func(struct work_struct *work)
{
    struct capture_work *cwork = container_of(work, struct capture_work, work);
    
    if (cwork->fb && cwork->dev) {
        capture_vblank_fb(cwork->fb, cwork->dev, cwork->sequence, cwork->method);
        drm_framebuffer_put(cwork->fb); // Release reference
    }
    
    kfree(cwork);
}

// Schedule safe capture
static void schedule_capture(struct drm_framebuffer *fb, struct drm_device *dev, 
                           uint32_t sequence, int method)
{
    struct capture_work *work;
    
    if (!fb || !dev) return;
    
    work = kmalloc(sizeof(*work), GFP_ATOMIC);
    if (!work) return;
    
    INIT_WORK(&work->work, capture_work_func);
    work->fb = fb;
    work->dev = dev;
    work->sequence = sequence;
    work->method = method;
    
    drm_framebuffer_get(fb); // Take reference
    queue_work(capture_wq, &work->work);
}

// Hook 1: VBlank handler - captures what's currently being displayed
static int handler_drm_vblank_event(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_device *dev;
    unsigned int pipe;
    struct drm_crtc *crtc;
    unsigned long flags;
    
    atomic64_inc(&vblank_events);
    
#ifdef CONFIG_X86_64
    dev = (struct drm_device *)regs->di;
    pipe = (unsigned int)regs->si;
#elif defined(CONFIG_ARM64)
    dev = (struct drm_device *)regs->regs[0];
    pipe = (unsigned int)regs->regs[1];
#else
    return 0;
#endif

    if (!dev || pipe >= 8) {
        return 0;
    }
    
    // Find CRTC for this pipe
    drm_for_each_crtc(crtc, dev) {
        if (drm_crtc_index(crtc) == pipe) {
            break;
        }
    }
    
    if (!crtc) {
        return 0;
    }
    
    // Get current framebuffer
    spin_lock_irqsave(&fb_tracking_lock, flags);
    if (current_fb[pipe]) {
        uint32_t sequence = drm_crtc_vblank_count(crtc);
        schedule_capture(current_fb[pipe], dev, sequence, 0);
        
        // Debug every 60 vblanks (roughly 1 second at 60Hz)
        if (atomic64_read(&vblank_events) % 60 == 1) {
            pr_info("VBlank %lld: scheduling capture for pipe %d\n", 
                    atomic64_read(&vblank_events), pipe);
        }
    }
    spin_unlock_irqrestore(&fb_tracking_lock, flags);
    
    return 0;
}

// Hook 2: Intel plane update - tracks framebuffer changes
static int handler_intel_update_plane(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_plane *plane;
    struct drm_plane_state *plane_state;
    unsigned long flags;
    
    atomic64_inc(&plane_updates);
    
#ifdef CONFIG_X86_64
    plane = (struct drm_plane *)regs->di;
    plane_state = (struct drm_plane_state *)regs->si;
#elif defined(CONFIG_ARM64)
    plane = (struct drm_plane *)regs->regs[0];
    plane_state = (struct drm_plane_state *)regs->regs[1];
#else
    return 0;
#endif

    if (!plane || !plane_state || !plane_state->fb) {
        return 0;
    }
    
    // Only track primary plane updates
    if (plane->type == DRM_PLANE_TYPE_PRIMARY && plane->crtc) {
        int crtc_index = drm_crtc_index(plane->crtc);
        
        if (crtc_index < 8) {
            spin_lock_irqsave(&fb_tracking_lock, flags);
            
            // Update current framebuffer tracking
            if (current_fb[crtc_index]) {
                drm_framebuffer_put(current_fb[crtc_index]);
            }
            current_fb[crtc_index] = plane_state->fb;
            drm_framebuffer_get(plane_state->fb);
            
            spin_unlock_irqrestore(&fb_tracking_lock, flags);
            
            pr_info("Plane update: FB %dx%d on CRTC %d\n",
                    plane_state->fb->width, plane_state->fb->height, crtc_index);
            
            // Immediately capture the new framebuffer
            schedule_capture(plane_state->fb, plane->dev, 0, 1);
        }
    }
    
    return 0;
}

// Hook 3: Intel pipe update start - right before scanout
static int handler_intel_pipe_update_start(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_crtc *crtc;
    unsigned long flags;
    
    atomic64_inc(&pipe_updates);
    
#ifdef CONFIG_X86_64
    crtc = (struct drm_crtc *)regs->di;
#elif defined(CONFIG_ARM64)
    crtc = (struct drm_crtc *)regs->regs[0];
#else
    return 0;
#endif

    if (!crtc) {
        return 0;
    }
    
    int crtc_index = drm_crtc_index(crtc);
    if (crtc_index < 8) {
        spin_lock_irqsave(&fb_tracking_lock, flags);
        if (current_fb[crtc_index]) {
            schedule_capture(current_fb[crtc_index], crtc->dev, 0, 2);
            
            if (atomic64_read(&pipe_updates) % 60 == 1) {
                pr_info("Pipe update start: CRTC %d\n", crtc_index);
            }
        }
        spin_unlock_irqrestore(&fb_tracking_lock, flags);
    }
    
    return 0;
}

// Kprobe structures
static struct kprobe kp_vblank = {
    .symbol_name = "drm_crtc_send_vblank_event",
    .pre_handler = handler_drm_vblank_event,
};

static struct kprobe kp_plane_update = {
    .symbol_name = "intel_update_plane",
    .pre_handler = handler_intel_update_plane,
};

static struct kprobe kp_pipe_update = {
    .symbol_name = "intel_pipe_update_start",
    .pre_handler = handler_intel_pipe_update_start,
};

// Proc file implementations
static const char* method_to_string(int method)
{
    switch(method) {
        case 0: return "VBLANK";
        case 1: return "PLANE_UPDATE";
        case 2: return "PIPE_UPDATE";
        default: return "UNKNOWN";
    }
}

static int vblank_fb_proc_show(struct seq_file *m, void *v)
{
    int i;
    
    mutex_lock(&capture_mutex);
    
    seq_printf(m, "VBlank-based Framebuffer Capture\n");
    seq_printf(m, "================================\n");
    seq_printf(m, "VBlank events: %lld\n", atomic64_read(&vblank_events));
    seq_printf(m, "Plane updates: %lld\n", atomic64_read(&plane_updates));
    seq_printf(m, "Pipe updates: %lld\n", atomic64_read(&pipe_updates));
    seq_printf(m, "Successful captures: %lld\n", atomic64_read(&successful_captures));
    seq_printf(m, "Captured framebuffers: %d\n\n", capture_count);
    
    for (i = 0; i < capture_count; i++) {
        struct vblank_fb_data *capture = &captured_fbs[i];
        
        if (!capture->valid) continue;
            
        seq_printf(m, "Capture %d:\n", i);
        seq_printf(m, "  Timestamp: %llu ns\n", capture->timestamp);
        seq_printf(m, "  Sequence: %u\n", capture->sequence);
        seq_printf(m, "  Method: %s\n", method_to_string(capture->capture_method));
        seq_printf(m, "  Dimensions: %dx%d\n", capture->width, capture->height);
        seq_printf(m, "  Format: 0x%08x\n", capture->format);
        seq_printf(m, "  Pitch: %u\n", capture->pitch);
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
    struct vblank_fb_data *capture = NULL;
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
    .proc_lseek = default_llseek,
};

// Module initialization
static int __init vblank_fb_init(void)
{
    int ret;
    int i;

    pr_info("VBlank Framebuffer Capture loading\n");

    // Initialize arrays
    memset(captured_fbs, 0, sizeof(captured_fbs));
    for (i = 0; i < 8; i++) {
        current_fb[i] = NULL;
    }
    capture_count = 0;
    current_index = 0;

    // Create workqueue
    capture_wq = alloc_workqueue("vblank_capture", WQ_UNBOUND, 0);
    if (!capture_wq) {
        pr_err("Failed to create workqueue\n");
        return -ENOMEM;
    }

    // Register kprobes - continue even if some fail
    ret = register_kprobe(&kp_vblank);
    if (ret < 0) {
        pr_warn("Failed to register VBlank kprobe: %d\n", ret);
    }
    
    ret = register_kprobe(&kp_plane_update);
    if (ret < 0) {
        pr_warn("Failed to register plane update kprobe: %d\n", ret);
    }
    
    ret = register_kprobe(&kp_pipe_update);
    if (ret < 0) {
        pr_warn("Failed to register pipe update kprobe: %d\n", ret);
    }

    // Create proc entries
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &vblank_fb_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry\n");
        goto cleanup;
    }
    
    proc_raw_entry = proc_create(PROC_RAW_NAME, 0644, NULL, &vblank_fb_raw_ops);
    if (!proc_raw_entry) {
        pr_err("Failed to create raw proc entry\n");
        proc_remove(proc_entry);
        goto cleanup;
    }

    pr_info("VBlank FB Capture loaded successfully\n");
    pr_info("Registered hooks: VBlank, Plane Update, Pipe Update\n");
    pr_info("Use 'cat /proc/%s' to view captures\n", PROC_NAME);
    pr_info("Use 'dd if=/proc/%s of=frame.raw' to extract pixels\n", PROC_RAW_NAME);
    
    return 0;

cleanup:
    unregister_kprobe(&kp_vblank);
    unregister_kprobe(&kp_plane_update);
    unregister_kprobe(&kp_pipe_update);
    if (capture_wq) {
        destroy_workqueue(capture_wq);
    }
    return -ENOMEM;
}

// Module cleanup
static void __exit vblank_fb_exit(void)
{
    int i;
    unsigned long flags;

    pr_info("VBlank FB Capture unloading\n");

    // Remove proc entries
    if (proc_raw_entry) {
        proc_remove(proc_raw_entry);
    }
    if (proc_entry) {
        proc_remove(proc_entry);
    }

    // Unregister kprobes
    unregister_kprobe(&kp_vblank);
    unregister_kprobe(&kp_plane_update);
    unregister_kprobe(&kp_pipe_update);

    // Flush and destroy workqueue
    if (capture_wq) {
        flush_workqueue(capture_wq);
        destroy_workqueue(capture_wq);
    }

    // Clean up framebuffer tracking
    spin_lock_irqsave(&fb_tracking_lock, flags);
    for (i = 0; i < 8; i++) {
        if (current_fb[i]) {
            drm_framebuffer_put(current_fb[i]);
            current_fb[i] = NULL;
        }
    }
    spin_unlock_irqrestore(&fb_tracking_lock, flags);

    // Free captured buffers
    mutex_lock(&capture_mutex);
    for (i = 0; i < MAX_FB_CAPTURE; i++) {
        if (captured_fbs[i].pixel_buffer) {
            vfree(captured_fbs[i].pixel_buffer);
            captured_fbs[i].pixel_buffer = NULL;
        }
    }
    capture_count = 0;
    mutex_unlock(&capture_mutex);

    pr_info("VBlank FB Capture unloaded\n");
}

module_init(vblank_fb_init);
module_exit(vblank_fb_exit);
