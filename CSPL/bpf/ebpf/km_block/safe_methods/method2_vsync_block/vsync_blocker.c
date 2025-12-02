#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/delay.h>  // For msleep function
#include <linux/workqueue.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Controlled Frame Freezer");
MODULE_DESCRIPTION("Safely freeze display for specific time with guaranteed recovery");
MODULE_VERSION("2.0");

static bool freeze_active = false;
static int freeze_duration = 3; // Start with shorter duration
static struct timer_list unfreeze_timer;
static struct timer_list safety_timer;
static struct work_struct recovery_work;
static int blocked_pageflips = 0;
static int blocked_vsyncs = 0;
static unsigned long freeze_start_time = 0;

// Hardcoded count solution - auto-recovery after N blocks
static int max_blocks = 50;  // Much larger limit for longer freeze effect
static int current_block_count = 0;

module_param(freeze_duration, int, 0644);
MODULE_PARM_DESC(freeze_duration, "Freeze duration in seconds (1-10 max for safety)");

module_param(max_blocks, int, 0644);
MODULE_PARM_DESC(max_blocks, "Maximum operations to delay before auto-recovery (default: 20)");

// Recovery work function
static void recovery_work_fn(struct work_struct *work)
{
    freeze_active = false;
    pr_warn("CONTROLLED_FREEZER: EMERGENCY RECOVERY activated!\n");
}

// Multiple recovery mechanisms
static void unfreeze_callback(struct timer_list *t)
{
    if (freeze_active) {
        freeze_active = false;
        pr_info("CONTROLLED_FREEZER: Normal recovery after %d seconds\n", freeze_duration);
        pr_info("CONTROLLED_FREEZER: Blocked %d pageflips, %d vsyncs\n", 
                blocked_pageflips, blocked_vsyncs);
    }
}

// Safety timer (backup recovery)
static void safety_callback(struct timer_list *t)
{
    if (freeze_active) {
        pr_warn("CONTROLLED_FREEZER: Safety timer triggered - forcing recovery!\n");
        schedule_work(&recovery_work);
    }
}

// SAFE: Delay instead of blocking page flip operations (creates freeze effect)
static int handler_drm_mode_page_flip_ioctl(struct kprobe *p, struct pt_regs *regs)
{
    if (freeze_active) {
        current_block_count++;
        blocked_pageflips++;
        
        pr_info("CONTROLLED_FREEZER: Blocking page flip #%d (total blocks: %d/%d)\n", 
                blocked_pageflips, current_block_count, max_blocks);
        
        // SAFE: Add delay instead of blocking completely
        msleep(1000); // 1 second delay creates VERY visible freeze effect

        // GUARANTEED RECOVERY: Force unfreeze after count limit
        if (current_block_count >= max_blocks) {
            pr_warn("CONTROLLED_FREEZER: RECOVERY! Reached %d delays - UNFREEZING NOW!\n", max_blocks);
            freeze_active = false;
            current_block_count = 0; // Reset counter
            blocked_pageflips = 0;   // Reset stats
            pr_info("CONTROLLED_FREEZER: RECOVERY COMPLETE - normal operations resumed\n");
        }
        
        return 0; // ALWAYS allow the operation to proceed (SAFE!)
    }
    return 0; // Normal operation when not frozen
}

static struct kprobe kp_page_flip_ioctl = {
    .symbol_name = "drm_mode_page_flip_ioctl",
    .pre_handler = handler_drm_mode_page_flip_ioctl,
};

// SAFE: Delay instead of blocking plane updates 
static int handler_drm_plane_commit(struct kprobe *p, struct pt_regs *regs)
{
    if (freeze_active) {
        current_block_count++;
        blocked_vsyncs++;
        
        pr_info("CONTROLLED_FREEZER: Delaying plane update #%d (total delays: %d/%d)\n", 
                blocked_vsyncs, current_block_count, max_blocks);
        
        msleep(1000); // 1 second delay for plane updates
        
        // Auto-recovery based on count
        if (current_block_count >= max_blocks) {
            pr_warn("CONTROLLED_FREEZER: Reached max delays (%d) - forcing recovery!\n", max_blocks);
            freeze_active = false;
            current_block_count = 0; // Reset counter
            pr_info("CONTROLLED_FREEZER: Display should recover now!\n");
        }
        
        return 0; // ALWAYS allow the operation (SAFE!)
    }
    return 0;
}

static struct kprobe kp_plane_update = {
    .symbol_name = "drm_mode_setplane",
    .pre_handler = handler_drm_plane_commit,
};

// Sysfs interface with safety limits
static ssize_t freeze_store(struct kobject *kobj, struct kobj_attribute *attr,
                           const char *buf, size_t count)
{
    if (freeze_active) {
        pr_info("CONTROLLED_FREEZER: Already frozen (%d pageflips, %d updates blocked)\n",
                blocked_pageflips, blocked_vsyncs);
        return count;
    }
    
    // Safety limits
    if (freeze_duration > 10) {
        pr_warn("CONTROLLED_FREEZER: Duration limited to 10 seconds for safety\n");
        freeze_duration = 10;
    }
    if (freeze_duration < 1) {
        pr_warn("CONTROLLED_FREEZER: Minimum duration is 1 second\n");
        freeze_duration = 1;
    }
    
    freeze_active = true;
    blocked_pageflips = 0;
    blocked_vsyncs = 0;
    current_block_count = 0;  // Reset block counter
    freeze_start_time = jiffies;
    
    pr_info("CONTROLLED_FREEZER: Freezing display for %d seconds (CONTROLLED MODE)\n", 
            freeze_duration);
    pr_info("CONTROLLED_FREEZER: Multiple recovery timers active for safety\n");
    
    // Set multiple recovery timers
    mod_timer(&unfreeze_timer, jiffies + msecs_to_jiffies(freeze_duration * 1000));
    mod_timer(&safety_timer, jiffies + msecs_to_jiffies((freeze_duration + 2) * 1000)); // Backup
    
    return count;
}

static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    if (freeze_active) {
        unsigned long elapsed = (jiffies - freeze_start_time) / HZ;
        return sprintf(buf, "FROZEN: %lu seconds elapsed, %d pageflips, %d updates blocked (blocks: %d/%d)\n",
                      elapsed, blocked_pageflips, blocked_vsyncs, current_block_count, max_blocks);
    } else {
        return sprintf(buf, "UNFROZEN: Ready for freeze operation\n");
    }
}

static ssize_t emergency_store(struct kobject *kobj, struct kobj_attribute *attr,
                              const char *buf, size_t count)
{
    if (freeze_active) {
        pr_warn("CONTROLLED_FREEZER: EMERGENCY UNFREEZE activated by user!\n");
        freeze_active = false;
        current_block_count = 0;
        blocked_pageflips = 0;
        blocked_vsyncs = 0;
        del_timer(&unfreeze_timer);
        del_timer(&safety_timer);
        pr_info("CONTROLLED_FREEZER: Emergency recovery complete\n");
    }
    return count;
}

static struct kobj_attribute freeze_attr = __ATTR(freeze, 0200, NULL, freeze_store);
static struct kobj_attribute status_attr = __ATTR(status, 0444, status_show, NULL);
static struct kobj_attribute emergency_attr = __ATTR(emergency_unfreeze, 0200, NULL, emergency_store);

static struct kobject *controlled_kobj;

static int __init controlled_freezer_init(void)
{
    int ret;
    
    pr_info("CONTROLLED_FREEZER: Loading controlled frame freezer with safety features\n");
    
    // Initialize timers and work
    timer_setup(&unfreeze_timer, unfreeze_callback, 0);
    timer_setup(&safety_timer, safety_callback, 0);
    INIT_WORK(&recovery_work, recovery_work_fn);
    
    // Register page flip probe
    ret = register_kprobe(&kp_page_flip_ioctl);
    if (ret < 0) {
        pr_err("CONTROLLED_FREEZER: Failed to register page flip probe: %d\n", ret);
        return ret;
    }
    pr_info("CONTROLLED_FREEZER: Registered page flip ioctl probe\n");
    
    // Register plane update probe
    ret = register_kprobe(&kp_plane_update);
    if (ret < 0) {
        pr_err("CONTROLLED_FREEZER: Failed to register plane update probe: %d\n", ret);
        unregister_kprobe(&kp_page_flip_ioctl);
        return ret;
    }
    pr_info("CONTROLLED_FREEZER: Registered plane update probe\n");
    
    // Create sysfs interface
    controlled_kobj = kobject_create_and_add("controlled_freezer", kernel_kobj);
    if (!controlled_kobj) {
        pr_err("CONTROLLED_FREEZER: Failed to create sysfs directory\n");
        unregister_kprobe(&kp_page_flip_ioctl);
        unregister_kprobe(&kp_plane_update);
        return -ENOMEM;
    }
    
    // Create sysfs files
    sysfs_create_file(controlled_kobj, &freeze_attr.attr);
    sysfs_create_file(controlled_kobj, &status_attr.attr);
    sysfs_create_file(controlled_kobj, &emergency_attr.attr);
    
    pr_info("CONTROLLED_FREEZER: Module loaded with 2 probes and safety features\n");
    pr_info("CONTROLLED_FREEZER: Usage:\n");
    pr_info("  Freeze:     echo 1 > /sys/kernel/controlled_freezer/freeze\n");
    pr_info("  Status:     cat /sys/kernel/controlled_freezer/status\n");
    pr_info("  Emergency:  echo 1 > /sys/kernel/controlled_freezer/emergency_unfreeze\n");
    pr_info("  Duration:   echo N > /sys/module/vsync_blocker/parameters/freeze_duration\n");
    pr_info("CONTROLLED_FREEZER: Max duration limited to 10 seconds for safety\n");
    
    return 0;
}

static void __exit controlled_freezer_exit(void)
{
    // Force unfreeze
    freeze_active = false;
    
    // Clean up timers and work
    del_timer_sync(&unfreeze_timer);
    del_timer_sync(&safety_timer);
    cancel_work_sync(&recovery_work);
    
    // Unregister probes
    unregister_kprobe(&kp_page_flip_ioctl);
    unregister_kprobe(&kp_plane_update);
    
    // Clean up sysfs
    if (controlled_kobj) {
        sysfs_remove_file(controlled_kobj, &freeze_attr.attr);
        sysfs_remove_file(controlled_kobj, &status_attr.attr);
        sysfs_remove_file(controlled_kobj, &emergency_attr.attr);
        kobject_put(controlled_kobj);
    }
    
    pr_info("CONTROLLED_FREEZER: Module unloaded safely\n");
}

module_init(controlled_freezer_init);
module_exit(controlled_freezer_exit);
