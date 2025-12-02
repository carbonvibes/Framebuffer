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
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_device.h>
#include <drm/drm_fourcc.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DRM FB Extractor");
MODULE_DESCRIPTION("Extract DRM framebuffer contents");
MODULE_VERSION("1.0");

#define PROC_NAME "drm_fb_contents"
#define MAX_FB_CAPTURE 10

struct fb_capture_data {
    struct drm_framebuffer *fb;
    struct drm_device *dev;
    void *buffer_data;
    size_t buffer_size;
    uint32_t width, height;
    uint32_t format;
    uint32_t pitch;
    bool valid;
};

static struct fb_capture_data captured_fbs[MAX_FB_CAPTURE];
static int capture_count = 0;
static DEFINE_MUTEX(capture_mutex);
static struct proc_dir_entry *proc_entry;

// Function to safely copy framebuffer data
static int copy_fb_data(struct drm_framebuffer *fb, struct fb_capture_data *capture)
{
    struct drm_gem_object *obj;
    size_t size;

    if (!fb || !fb->obj[0]) {
        pr_warn("Invalid framebuffer or GEM object\n");
        return -EINVAL;
    }

    // Get the primary GEM object
    obj = fb->obj[0];
    if (!obj) {
        pr_warn("No GEM object in framebuffer\n");
        return -EINVAL;
    }

    // Calculate buffer size
    size = fb->height * fb->pitches[0];
    
    // Allocate kernel buffer for the copy
    capture->buffer_data = vmalloc(size);
    if (!capture->buffer_data) {
        pr_err("Failed to allocate buffer for framebuffer data\n");
        return -ENOMEM;
    }

    // Try to get virtual address of the GEM object
    if (obj->filp && obj->filp->f_mapping) {
        // For SHMEM-backed objects, we need to map pages
        struct address_space *mapping = obj->filp->f_mapping;
        struct page *page;
        void *kaddr;
        size_t copied = 0;
        pgoff_t index = 0;
        
        while (copied < size && index < (obj->size >> PAGE_SHIFT)) {
            page = find_get_page(mapping, index);
            if (page) {
                kaddr = kmap(page);
                if (kaddr) {
                    size_t to_copy = min_t(size_t, PAGE_SIZE, size - copied);
                    memcpy((char*)capture->buffer_data + copied, kaddr, to_copy);
                    copied += to_copy;
                    kunmap(page);
                }
                put_page(page);
            }
            index++;
        }
        
        if (copied == 0) {
            vfree(capture->buffer_data);
            capture->buffer_data = NULL;
            return -ENODATA;
        }
        
        capture->buffer_size = copied;
    } else {
        // For other types of backing storage, we can't easily access the data
        // This would require driver-specific knowledge
        vfree(capture->buffer_data);
        capture->buffer_data = NULL;
        pr_info("Cannot access framebuffer data - unsupported backing type\n");
        return -ENOTSUPP;
    }

    // Store framebuffer metadata
    capture->fb = fb;
    capture->width = fb->width;
    capture->height = fb->height;
    capture->format = fb->format->format;
    capture->pitch = fb->pitches[0];
    capture->valid = true;

    pr_info("Captured framebuffer: %dx%d, format=0x%x, pitch=%d, size=%zu\n",
            capture->width, capture->height, capture->format, 
            capture->pitch, capture->buffer_size);

    return 0;
}

// Kprobe handler for drm_framebuffer_init
static int handler_drm_framebuffer_init(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_device *dev;
    struct drm_framebuffer *fb;
    const struct drm_framebuffer_funcs *funcs;

    // Extract parameters based on architecture
#ifdef CONFIG_X86_64
    dev = (struct drm_device *)regs->di;
    fb = (struct drm_framebuffer *)regs->si;
    funcs = (const struct drm_framebuffer_funcs *)regs->dx;
#elif defined(CONFIG_ARM64)
    dev = (struct drm_device *)regs->regs[0];
    fb = (struct drm_framebuffer *)regs->regs[1];
    funcs = (const struct drm_framebuffer_funcs *)regs->regs[2];
#else
    // For other architectures, you may need to adjust register access
    return 0;
#endif

    if (!dev || !fb) {
        return 0;
    }

    pr_info("DRM framebuffer init: dev=%p, fb=%p, %dx%d\n", 
            dev, fb, fb->width, fb->height);

    // Capture framebuffer data if we have space
    mutex_lock(&capture_mutex);
    if (capture_count < MAX_FB_CAPTURE) {
        struct fb_capture_data *capture = &captured_fbs[capture_count];
        memset(capture, 0, sizeof(*capture));
        capture->dev = dev;
        
        if (copy_fb_data(fb, capture) == 0) {
            capture_count++;
        }
    }
    mutex_unlock(&capture_mutex);

    return 0;
}

// Kprobe structure
static struct kprobe kp_drm_fb_init = {
    .symbol_name = "drm_framebuffer_init",
    .pre_handler = handler_drm_framebuffer_init,
};

// Proc file operations
static int drm_fb_proc_show(struct seq_file *m, void *v)
{
    int i;
    
    mutex_lock(&capture_mutex);
    
    seq_printf(m, "Captured framebuffers: %d\n\n", capture_count);
    
    for (i = 0; i < capture_count; i++) {
        struct fb_capture_data *capture = &captured_fbs[i];
        
        if (!capture->valid)
            continue;
            
        seq_printf(m, "Framebuffer %d:\n", i);
        seq_printf(m, "  Device: %p\n", capture->dev);
        seq_printf(m, "  Framebuffer: %p\n", capture->fb);
        seq_printf(m, "  Dimensions: %dx%d\n", capture->width, capture->height);
        seq_printf(m, "  Format: 0x%08x (%c%c%c%c)\n", 
                   capture->format,
                   (capture->format >> 0) & 0xff,
                   (capture->format >> 8) & 0xff,
                   (capture->format >> 16) & 0xff,
                   (capture->format >> 24) & 0xff);
        seq_printf(m, "  Pitch: %d bytes\n", capture->pitch);
        seq_printf(m, "  Buffer size: %zu bytes\n", capture->buffer_size);
        
        if (capture->buffer_data && capture->buffer_size > 0) {
            int j;
            seq_printf(m, "  Data available: YES\n");
            seq_printf(m, "  First 32 bytes: ");
            for (j = 0; j < min_t(size_t, 32, capture->buffer_size); j++) {
                seq_printf(m, "%02x ", ((unsigned char*)capture->buffer_data)[j]);
            }
            seq_printf(m, "\n");
        } else {
            seq_printf(m, "  Data available: NO\n");
        }
        seq_printf(m, "\n");
    }
    
    mutex_unlock(&capture_mutex);
    return 0;
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

// Module initialization
static int __init drm_fb_extractor_init(void)
{
    int ret;

    pr_info("DRM Framebuffer Extractor module loading\n");

    // Initialize capture array
    memset(captured_fbs, 0, sizeof(captured_fbs));
    capture_count = 0;

    // Register kprobe
    ret = register_kprobe(&kp_drm_fb_init);
    if (ret < 0) {
        pr_err("Failed to register kprobe: %d\n", ret);
        return ret;
    }

    // Create proc entry
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &drm_fb_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry\n");
        unregister_kprobe(&kp_drm_fb_init);
        return -ENOMEM;
    }

    pr_info("DRM Framebuffer Extractor loaded successfully\n");
    pr_info("Use 'cat /proc/%s' to view captured framebuffer data\n", PROC_NAME);
    
    return 0;
}

// Module cleanup
static void __exit drm_fb_extractor_exit(void)
{
    int i;

    pr_info("DRM Framebuffer Extractor module unloading\n");

    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }

    // Unregister kprobe
    unregister_kprobe(&kp_drm_fb_init);

    // Free allocated buffers
    mutex_lock(&capture_mutex);
    for (i = 0; i < capture_count; i++) {
        if (captured_fbs[i].buffer_data) {
            vfree(captured_fbs[i].buffer_data);
            captured_fbs[i].buffer_data = NULL;
        }
    }
    capture_count = 0;
    mutex_unlock(&capture_mutex);

    pr_info("DRM Framebuffer Extractor unloaded\n");
}

module_init(drm_fb_extractor_init);
module_exit(drm_fb_extractor_exit);