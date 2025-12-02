#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Page Flip Blocker");
MODULE_DESCRIPTION("Freeze display by blocking page flip operations");
MODULE_VERSION("1.0");

static bool freeze_active = false;
static int freeze_duration = 5;
static struct timer_list unfreeze_timer;
static int blocked_flips = 0;
static int blocked_updates = 0;

module_param(freeze_duration, int, 0644);
MODULE_PARM_DESC(freeze_duration, "Freeze duration in seconds");

static void unfreeze_callback(struct timer_list *t)
{
    if (freeze_active) {
        freeze_active = false;
        pr_info("PAGEFLIP_BLOCKER: Unfrozen - blocked %d flips, %d updates\n",
                blocked_flips, blocked_updates);
    }
}

// Block DRM CRTC page flip events
static int handler_drm_mode_page_flip_ioctl(struct kprobe *p, struct pt_regs *regs)
{
    if (freeze_active) {
        blocked_flips++;
        pr_info("PAGEFLIP_BLOCKER: Blocking page flip ioctl #%d\n", blocked_flips);
        return 1;
    }
    return 0;
}

static struct kprobe kp_page_flip_ioctl = {
    .symbol_name = "drm_mode_page_flip_ioctl",
    .pre_handler = handler_drm_mode_page_flip_ioctl,
};

// Block CRTC updates
static int handler_drm_crtc_update(struct kprobe *p, struct pt_regs *regs)
{
    if (freeze_active) {
        blocked_updates++;
        pr_info("PAGEFLIP_BLOCKER: Blocking CRTC update #%d\n", blocked_updates);
        return 1;
    }
    return 0;
}

static struct kprobe kp_crtc_update = {
    .symbol_name = "drm_mode_setcrtc",
    .pre_handler = handler_drm_crtc_update,
};

// Block primary plane updates
static int handler_drm_primary_plane_update(struct kprobe *p, struct pt_regs *regs)
{
    if (freeze_active) {
        blocked_updates++;
        pr_info("PAGEFLIP_BLOCKER: Blocking primary plane update #%d\n", blocked_updates);
        return 1;
    }
    return 0;
}

static struct kprobe kp_primary_plane = {
    .symbol_name = "drm_mode_setplane",
    .pre_handler = handler_drm_primary_plane_update,
};

// Block cursor updates (optional - allows cursor to still move)
static int handler_drm_cursor_update(struct kprobe *p, struct pt_regs *regs)
{
    if (freeze_active) {
        // Uncomment to also block cursor:
        // blocked_updates++;
        // pr_info("PAGEFLIP_BLOCKER: Blocking cursor update #%d\n", blocked_updates);
        // return 1;
    }
    return 0; // Allow cursor updates even when frozen
}

static struct kprobe kp_cursor_update = {
    .symbol_name = "drm_mode_cursor_universal",
    .pre_handler = handler_drm_cursor_update,
};

// Sysfs interface
static ssize_t freeze_store(struct kobject *kobj, struct kobj_attribute *attr,
                           const char *buf, size_t count)
{
    if (!freeze_active) {
        freeze_active = true;
        blocked_flips = 0;
        blocked_updates = 0;
        
        pr_info("PAGEFLIP_BLOCKER: Freezing display for %d seconds via page flip blocking\n", 
                freeze_duration);
        pr_info("PAGEFLIP_BLOCKER: Cursor should still move (if working correctly)\n");
        
        mod_timer(&unfreeze_timer, jiffies + msecs_to_jiffies(freeze_duration * 1000));
    } else {
        pr_info("PAGEFLIP_BLOCKER: Already frozen (%d flips, %d updates blocked)\n",
                blocked_flips, blocked_updates);
    }
    return count;
}

static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, 
                   "Status: %s\n"
                   "Duration: %d seconds\n"
                   "Blocked page flips: %d\n"
                   "Blocked updates: %d\n"
                   "Note: Cursor movement should still work\n",
                   freeze_active ? "FROZEN" : "NORMAL",
                   freeze_duration, 
                   blocked_flips,
                   blocked_updates);
}

static struct kobj_attribute freeze_attr = __ATTR(freeze, 0200, NULL, freeze_store);
static struct kobj_attribute status_attr = __ATTR(status, 0444, status_show, NULL);
static struct kobject *pageflip_kobj;

static int __init pageflip_blocker_init(void)
{
    int ret;
    int probes_registered = 0;
    
    pr_info("PAGEFLIP_BLOCKER: Loading page flip freezer\n");
    
    timer_setup(&unfreeze_timer, unfreeze_callback, 0);
    
    // Register page flip ioctl probe
    ret = register_kprobe(&kp_page_flip_ioctl);
    if (ret == 0) {
        pr_info("PAGEFLIP_BLOCKER: Registered page flip ioctl probe\n");
        probes_registered++;
    } else {
        pr_warn("PAGEFLIP_BLOCKER: Failed to register page flip ioctl probe: %d\n", ret);
    }
    
    // Register CRTC update probe
    ret = register_kprobe(&kp_crtc_update);
    if (ret == 0) {
        pr_info("PAGEFLIP_BLOCKER: Registered CRTC update probe\n");
        probes_registered++;
    } else {
        pr_warn("PAGEFLIP_BLOCKER: Failed to register CRTC update probe: %d\n", ret);
    }
    
    // Register primary plane probe
    ret = register_kprobe(&kp_primary_plane);
    if (ret == 0) {
        pr_info("PAGEFLIP_BLOCKER: Registered primary plane probe\n");
        probes_registered++;
    } else {
        pr_warn("PAGEFLIP_BLOCKER: Failed to register primary plane probe: %d\n", ret);
    }
    
    // Register cursor probe (optional)
    ret = register_kprobe(&kp_cursor_update);
    if (ret == 0) {
        pr_info("PAGEFLIP_BLOCKER: Registered cursor probe (cursor will still work)\n");
        probes_registered++;
    } else {
        pr_warn("PAGEFLIP_BLOCKER: Failed to register cursor probe: %d\n", ret);
    }
    
    if (probes_registered == 0) {
        pr_err("PAGEFLIP_BLOCKER: No probes registered successfully\n");
        return -ENODEV;
    }
    
    // Create sysfs interface
    pageflip_kobj = kobject_create_and_add("pageflip_freezer", kernel_kobj);
    if (pageflip_kobj) {
        sysfs_create_file(pageflip_kobj, &freeze_attr.attr);
        sysfs_create_file(pageflip_kobj, &status_attr.attr);
    }
    
    pr_info("PAGEFLIP_BLOCKER: Module loaded with %d active probes\n", probes_registered);
    pr_info("PAGEFLIP_BLOCKER: Usage: echo 1 > /sys/kernel/pageflip_freezer/freeze\n");
    pr_info("PAGEFLIP_BLOCKER: Status: cat /sys/kernel/pageflip_freezer/status\n");
    
    return 0;
}

static void __exit pageflip_blocker_exit(void)
{
    del_timer_sync(&unfreeze_timer);
    
    unregister_kprobe(&kp_page_flip_ioctl);
    unregister_kprobe(&kp_crtc_update);
    unregister_kprobe(&kp_primary_plane);
    unregister_kprobe(&kp_cursor_update);
    
    if (pageflip_kobj) {
        sysfs_remove_file(pageflip_kobj, &freeze_attr.attr);
        sysfs_remove_file(pageflip_kobj, &status_attr.attr);
        kobject_put(pageflip_kobj);
    }
    
    pr_info("PAGEFLIP_BLOCKER: Module unloaded (total: %d flips, %d updates blocked)\n",
            blocked_flips, blocked_updates);
}

module_init(pageflip_blocker_init);
module_exit(pageflip_blocker_exit);
