#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_device.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DRM Frame Blocker");
MODULE_DESCRIPTION("Block DRM framebuffer initialization with auto-recovery");
MODULE_VERSION("2.0");

// Frame blocking functionality
static bool block_frames = false;
static int blocked_frame_count = 0;
static unsigned long block_start_time = 0;

// Auto-recovery timer
static struct timer_list recovery_timer;
static bool auto_recovery_enabled = true;
static int recovery_timeout = 5; // 5 seconds default

// Module parameters
module_param(auto_recovery_enabled, bool, 0644);
MODULE_PARM_DESC(auto_recovery_enabled, "Enable automatic recovery (default: true)");

module_param(recovery_timeout, int, 0644);
MODULE_PARM_DESC(recovery_timeout, "Recovery timeout in seconds (default: 5)");

// Recovery timer callback
static void recovery_timer_callback(struct timer_list *t)
{
    if (block_frames) {
        pr_warn("AUTO-RECOVERY: Disabling frame blocking after %d seconds to prevent system freeze\n", 
                recovery_timeout);
        
        block_frames = false;
        
        pr_info("Frame blocking disabled. Blocked %d frames total. System should recover shortly.\n", 
                blocked_frame_count);
    }
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

    if (block_frames) {
        blocked_frame_count++;
        
        // Start recovery timer on first block
        if (blocked_frame_count == 1 && auto_recovery_enabled) {
            block_start_time = jiffies;
            mod_timer(&recovery_timer, jiffies + msecs_to_jiffies(recovery_timeout * 1000));
            pr_info("AUTO-RECOVERY: Timer started - will recover in %d seconds\n", recovery_timeout);
        }
        
        pr_info("BLOCKING framebuffer init #%d: %dx%d, format=0x%08x\n", 
                blocked_frame_count,
                fb->width, fb->height, 
                fb->format ? fb->format->format : 0);
        
        // Return non-zero to prevent the original function from executing
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

// Custom write handler for block_frames parameter
static int block_frames_set(const char *val, const struct kernel_param *kp)
{
    int ret;
    bool new_value;
    
    ret = kstrtobool(val, &new_value);
    if (ret)
        return ret;
    
    if (new_value && !block_frames) {
        // Enabling blocking
        pr_info("Enabling frame blocking with %d second auto-recovery\n", recovery_timeout);
        blocked_frame_count = 0;
        block_start_time = 0;
    } else if (!new_value && block_frames) {
        // Disabling blocking
        pr_info("Disabling frame blocking (blocked %d frames)\n", blocked_frame_count);
        del_timer_sync(&recovery_timer);
    }
    
    block_frames = new_value;
    return 0;
}

static const struct kernel_param_ops block_frames_ops = {
    .set = block_frames_set,
    .get = param_get_bool,
};

// Define block_frames parameter with custom handler
module_param_cb(block_frames, &block_frames_ops, &block_frames, 0644);
MODULE_PARM_DESC(block_frames, "Enable frame blocking (default: false)");

// Module initialization
static int __init drm_fb_blocker_init(void)
{
    int ret;

    pr_info("DRM Frame Blocker with Auto-Recovery loading...\n");
    pr_info("Settings: blocking=%s, auto_recovery=%s, timeout=%ds\n",
            block_frames ? "ENABLED" : "DISABLED",
            auto_recovery_enabled ? "ENABLED" : "DISABLED", 
            recovery_timeout);

    // Initialize recovery timer
    timer_setup(&recovery_timer, recovery_timer_callback, 0);

    // Register kprobe
    ret = register_kprobe(&kp_drm_fb_init);
    if (ret < 0) {
        pr_err("Failed to register kprobe: %d\n", ret);
        return ret;
    }

    pr_info("DRM Frame Blocker loaded successfully\n");
    pr_info("Commands:\n");
    pr_info("  Enable blocking:  echo 1 > /sys/module/drm_frame_blocker/parameters/block_frames\n");
    pr_info("  Disable blocking: echo 0 > /sys/module/drm_frame_blocker/parameters/block_frames\n");
    pr_info("  Set timeout:      echo N > /sys/module/drm_frame_blocker/parameters/recovery_timeout\n");
    pr_info("  Toggle recovery:  echo 0/1 > /sys/module/drm_frame_blocker/parameters/auto_recovery_enabled\n");
    
    return 0;
}

// Module cleanup
static void __exit drm_fb_blocker_exit(void)
{
    pr_info("DRM Frame Blocker unloading (blocked %d frames total)\n", blocked_frame_count);

    // Clean up timer
    del_timer_sync(&recovery_timer);
    
    // Unregister kprobe
    unregister_kprobe(&kp_drm_fb_init);

    pr_info("DRM Frame Blocker unloaded\n");
}

module_init(drm_fb_blocker_init);
module_exit(drm_fb_blocker_exit);
