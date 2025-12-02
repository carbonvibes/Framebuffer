#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safe Frame Freezer - Atomic Commit");
MODULE_DESCRIPTION("Safely freeze display updates by blocking atomic commits");
MODULE_VERSION("1.0");

static bool freeze_active = false;
static int freeze_duration = 5; // seconds
static struct timer_list unfreeze_timer;
static int blocked_commits = 0;
static unsigned long freeze_start_time = 0;

module_param(freeze_duration, int, 0644);
MODULE_PARM_DESC(freeze_duration, "Freeze duration in seconds (default: 5)");

// Unfreeze callback
static void unfreeze_callback(struct timer_list *t)
{
    if (freeze_active) {
        freeze_active = false;
        pr_info("ATOMIC_BLOCKER: Display unfrozen after %d seconds, blocked %d commits\n", 
                freeze_duration, blocked_commits);
    }
}

// Block DRM atomic commit operations
static int handler_drm_atomic_commit(struct kprobe *p, struct pt_regs *regs)
{
    if (freeze_active) {
        blocked_commits++;
        pr_info("ATOMIC_BLOCKER: Blocking display commit #%d\n", blocked_commits);
        return 1; // Block the atomic commit
    }
    return 0; // Allow normal operation
}

static struct kprobe kp_atomic_commit = {
    .symbol_name = "drm_atomic_commit",
    .pre_handler = handler_drm_atomic_commit,
};

// Alternative probe for non-blocking atomic commits
static int handler_drm_atomic_nonblocking_commit(struct kprobe *p, struct pt_regs *regs)
{
    if (freeze_active) {
        blocked_commits++;
        pr_info("ATOMIC_BLOCKER: Blocking non-blocking commit #%d\n", blocked_commits);
        return 1;
    }
    return 0;
}

static struct kprobe kp_atomic_nonblocking = {
    .symbol_name = "drm_atomic_nonblocking_commit",
    .pre_handler = handler_drm_atomic_nonblocking_commit,
};

// Sysfs interface for triggering freeze
static ssize_t trigger_freeze_store(struct kobject *kobj, struct kobj_attribute *attr,
                                  const char *buf, size_t count)
{
    if (freeze_active) {
        pr_info("ATOMIC_BLOCKER: Freeze already active (%d commits blocked so far)\n", blocked_commits);
        return count;
    }
    
    freeze_active = true;
    blocked_commits = 0;
    freeze_start_time = jiffies;
    
    pr_info("ATOMIC_BLOCKER: Freezing display for %d seconds via atomic commit blocking...\n", 
            freeze_duration);
    mod_timer(&unfreeze_timer, jiffies + msecs_to_jiffies(freeze_duration * 1000));
    
    return count;
}

static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "Status: %s\nDuration: %d seconds\nBlocked commits: %d\n",
                   freeze_active ? "FROZEN" : "NORMAL",
                   freeze_duration, 
                   blocked_commits);
}

static struct kobj_attribute trigger_attr = __ATTR(trigger, 0200, NULL, trigger_freeze_store);
static struct kobj_attribute status_attr = __ATTR(status, 0444, status_show, NULL);

static struct kobject *atomic_freeze_kobj;

static int __init atomic_blocker_init(void)
{
    int ret;
    
    pr_info("ATOMIC_BLOCKER: Loading safe frame freezer (atomic commit method)\n");
    
    // Setup timer
    timer_setup(&unfreeze_timer, unfreeze_callback, 0);
    
    // Register primary kprobe
    ret = register_kprobe(&kp_atomic_commit);
    if (ret < 0) {
        pr_warn("ATOMIC_BLOCKER: Failed to register drm_atomic_commit probe: %d\n", ret);
    } else {
        pr_info("ATOMIC_BLOCKER: Registered drm_atomic_commit probe\n");
    }
    
    // Register secondary kprobe (optional)
    ret = register_kprobe(&kp_atomic_nonblocking);
    if (ret < 0) {
        pr_warn("ATOMIC_BLOCKER: Failed to register drm_atomic_nonblocking_commit probe: %d\n", ret);
    } else {
        pr_info("ATOMIC_BLOCKER: Registered drm_atomic_nonblocking_commit probe\n");
    }
    
    // Create sysfs interface
    atomic_freeze_kobj = kobject_create_and_add("atomic_freezer", kernel_kobj);
    if (!atomic_freeze_kobj) {
        pr_err("ATOMIC_BLOCKER: Failed to create sysfs directory\n");
        unregister_kprobe(&kp_atomic_commit);
        unregister_kprobe(&kp_atomic_nonblocking);
        return -ENOMEM;
    }
    
    ret = sysfs_create_file(atomic_freeze_kobj, &trigger_attr.attr);
    if (ret) {
        pr_err("ATOMIC_BLOCKER: Failed to create trigger file\n");
        kobject_put(atomic_freeze_kobj);
        unregister_kprobe(&kp_atomic_commit);
        unregister_kprobe(&kp_atomic_nonblocking);
        return ret;
    }
    
    ret = sysfs_create_file(atomic_freeze_kobj, &status_attr.attr);
    if (ret) {
        pr_err("ATOMIC_BLOCKER: Failed to create status file\n");
        sysfs_remove_file(atomic_freeze_kobj, &trigger_attr.attr);
        kobject_put(atomic_freeze_kobj);
        unregister_kprobe(&kp_atomic_commit);
        unregister_kprobe(&kp_atomic_nonblocking);
        return ret;
    }
    
    pr_info("ATOMIC_BLOCKER: Module loaded successfully\n");
    pr_info("ATOMIC_BLOCKER: Usage:\n");
    pr_info("  Trigger freeze: echo 1 > /sys/kernel/atomic_freezer/trigger\n");
    pr_info("  Check status:   cat /sys/kernel/atomic_freezer/status\n");
    pr_info("  Set duration:   echo N > /sys/module/atomic_commit_blocker/parameters/freeze_duration\n");
    
    return 0;
}

static void __exit atomic_blocker_exit(void)
{
    pr_info("ATOMIC_BLOCKER: Unloading (blocked %d commits total)\n", blocked_commits);
    
    // Clean up timer
    del_timer_sync(&unfreeze_timer);
    
    // Remove sysfs interface
    if (atomic_freeze_kobj) {
        sysfs_remove_file(atomic_freeze_kobj, &trigger_attr.attr);
        sysfs_remove_file(atomic_freeze_kobj, &status_attr.attr);
        kobject_put(atomic_freeze_kobj);
    }
    
    // Unregister kprobes
    unregister_kprobe(&kp_atomic_commit);
    unregister_kprobe(&kp_atomic_nonblocking);
    
    pr_info("ATOMIC_BLOCKER: Module unloaded\n");
}

module_init(atomic_blocker_init);
module_exit(atomic_blocker_exit);
