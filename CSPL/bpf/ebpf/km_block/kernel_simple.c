#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/kprobes.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_device.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DRM Frame Blocker");
MODULE_DESCRIPTION("Block DRM framebuffer initialization");
MODULE_VERSION("1.0");

#define PROC_BLOCK_NAME "drm_fb_block"

// Frame blocking functionality
static bool block_frames = false;
static DEFINE_MUTEX(block_mutex);
static struct proc_dir_entry *proc_block_entry;
static int blocked_frame_count = 0;

// Kprobe handler for drm_framebuffer_init
static int handler_drm_framebuffer_init(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_device *dev;
    struct drm_framebuffer *fb;
    bool should_block;
    
    // Check if frame blocking is enabled
    mutex_lock(&block_mutex);
    should_block = block_frames;
    if (should_block) {
        blocked_frame_count++;
    }
    mutex_unlock(&block_mutex);
    
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

    if (should_block) {
        pr_info("BLOCKING framebuffer init: %dx%d, format=0x%08x (blocked count: %d)\n", 
                fb->width, fb->height, fb->format ? fb->format->format : 0, blocked_frame_count);
        
        // Return non-zero to prevent the original function from executing
        // This effectively blocks the framebuffer initialization
        return 1;
    }

    pr_info("Allowing framebuffer init: %dx%d, format=0x%08x\n", 
            fb->width, fb->height, fb->format ? fb->format->format : 0);
    
    return 0;
}

// Kprobe structure
static struct kprobe kp_drm_fb_init = {
    .symbol_name = "drm_framebuffer_init",
    .pre_handler = handler_drm_framebuffer_init,
};

// Proc file for controlling frame blocking
static ssize_t drm_fb_block_read(struct file *file, char __user *buffer, size_t count, loff_t *pos)
{
    char output[256];
    int len;
    
    if (*pos > 0) {
        return 0; // EOF
    }
    
    mutex_lock(&block_mutex);
    len = snprintf(output, sizeof(output), 
                   "Frame blocking: %s\n"
                   "Blocked frame count: %d\n"
                   "\nUsage:\n"
                   "  echo 1 > /proc/%s  # Enable frame blocking\n"
                   "  echo 0 > /proc/%s  # Disable frame blocking\n"
                   "  echo reset > /proc/%s  # Reset blocked count\n",
                   block_frames ? "ENABLED" : "DISABLED",
                   blocked_frame_count,
                   PROC_BLOCK_NAME, PROC_BLOCK_NAME, PROC_BLOCK_NAME);
    mutex_unlock(&block_mutex);
    
    if (count < len) {
        return -EINVAL;
    }
    
    if (copy_to_user(buffer, output, len)) {
        return -EFAULT;
    }
    
    *pos += len;
    return len;
}

static ssize_t drm_fb_block_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
    char input[32];
    size_t len = min(count, sizeof(input) - 1);
    
    if (copy_from_user(input, buffer, len)) {
        return -EFAULT;
    }
    
    input[len] = '\0';
    
    // Remove trailing newline
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
    
    mutex_lock(&block_mutex);
    
    if (strcmp(input, "1") == 0 || strcmp(input, "enable") == 0) {
        block_frames = true;
        pr_info("Frame blocking ENABLED\n");
    } else if (strcmp(input, "0") == 0 || strcmp(input, "disable") == 0) {
        block_frames = false;
        pr_info("Frame blocking DISABLED\n");
    } else if (strcmp(input, "reset") == 0) {
        blocked_frame_count = 0;
        pr_info("Blocked frame count reset to 0\n");
    } else {
        mutex_unlock(&block_mutex);
        pr_warn("Invalid input: %s. Use 1/0/enable/disable/reset\n", input);
        return -EINVAL;
    }
    
    mutex_unlock(&block_mutex);
    
    return count;
}

static const struct proc_ops drm_fb_block_ops = {
    .proc_read = drm_fb_block_read,
    .proc_write = drm_fb_block_write,
    .proc_lseek = default_llseek,
};

// Module initialization
static int __init drm_fb_blocker_init(void)
{
    int ret;

    pr_info("DRM Frame Blocker loading\n");

    // Register kprobe
    ret = register_kprobe(&kp_drm_fb_init);
    if (ret < 0) {
        pr_err("Failed to register kprobe: %d\n", ret);
        return ret;
    }

    // Create proc entry
    proc_block_entry = proc_create(PROC_BLOCK_NAME, 0644, NULL, &drm_fb_block_ops);
    if (!proc_block_entry) {
        pr_err("Failed to create proc entry %s\n", PROC_BLOCK_NAME);
        unregister_kprobe(&kp_drm_fb_init);
        return -ENOMEM;
    }

    pr_info("DRM Frame Blocker loaded successfully\n");
    pr_info("Use 'cat /proc/%s' to view blocking status\n", PROC_BLOCK_NAME);
    pr_info("Use 'echo 1 > /proc/%s' to enable frame blocking\n", PROC_BLOCK_NAME);
    pr_info("Use 'echo 0 > /proc/%s' to disable frame blocking\n", PROC_BLOCK_NAME);
    
    return 0;
}

// Module cleanup
static void __exit drm_fb_blocker_exit(void)
{
    pr_info("DRM Frame Blocker unloading\n");

    // Remove proc entry
    if (proc_block_entry) {
        proc_remove(proc_block_entry);
    }

    // Unregister kprobe
    unregister_kprobe(&kp_drm_fb_init);

    pr_info("DRM Frame Blocker unloaded\n");
}

module_init(drm_fb_blocker_init);
module_exit(drm_fb_blocker_exit);
