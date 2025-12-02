#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simple Frame Freezer");
MODULE_DESCRIPTION("Simple time-based frame freezing");
MODULE_VERSION("1.0");

static bool freeze_active = false;
static int freeze_duration = 3;
static int frame_delay_ms = 2000;
static struct timer_list unfreeze_timer;

module_param(freeze_duration, int, 0644);
MODULE_PARM_DESC(freeze_duration, "Freeze duration in seconds");

module_param(frame_delay_ms, int, 0644);
MODULE_PARM_DESC(frame_delay_ms, "Delay per frame in milliseconds");

// Timer callback - stops the freeze
static void unfreeze_callback(struct timer_list *t)
{
    freeze_active = false;
    pr_info("SIMPLE_FREEZE: Freeze ended after %d seconds\n", freeze_duration);
}

// Simple page flip handler - just delay based on time
static int handler_page_flip(struct kprobe *p, struct pt_regs *regs)
{
    if (freeze_active) {
        msleep(frame_delay_ms);
    }
    return 0;
}

static struct kprobe kp_page_flip = {
    .symbol_name = "drm_mode_page_flip_ioctl",
    .pre_handler = handler_page_flip,
};

// Simple sysfs interface - just start/stop
static ssize_t freeze_store(struct kobject *kobj, struct kobj_attribute *attr,
                           const char *buf, size_t count)
{
    if (freeze_active) {
        pr_info("SIMPLE_FREEZE: Already active\n");
        return count;
    }
    
    freeze_active = true;
    pr_info("SIMPLE_FREEZE: Starting %d second freeze with %dms delays\n", 
            freeze_duration, frame_delay_ms);
    
    // Set timer to stop freeze
    mod_timer(&unfreeze_timer, jiffies + msecs_to_jiffies(freeze_duration * 1000));
    
    return count;
}

static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", freeze_active ? "ACTIVE" : "INACTIVE");
}

static struct kobj_attribute freeze_attr = __ATTR(freeze, 0200, NULL, freeze_store);
static struct kobj_attribute status_attr = __ATTR(status, 0444, status_show, NULL);

static struct kobject *freeze_kobj;

static int __init simple_freeze_init(void)
{
    int ret;
    
    pr_info("SIMPLE_FREEZE: Loading simple frame freezer\n");
    
    // Initialize timer
    timer_setup(&unfreeze_timer, unfreeze_callback, 0);
    
    // Register kprobe
    ret = register_kprobe(&kp_page_flip);
    if (ret < 0) {
        pr_err("SIMPLE_FREEZE: Failed to register kprobe: %d\n", ret);
        return ret;
    }
    
    // Create sysfs interface
    freeze_kobj = kobject_create_and_add("simple_freeze", kernel_kobj);
    if (!freeze_kobj) {
        unregister_kprobe(&kp_page_flip);
        return -ENOMEM;
    }
    
    sysfs_create_file(freeze_kobj, &freeze_attr.attr);
    sysfs_create_file(freeze_kobj, &status_attr.attr);
    
    pr_info("SIMPLE_FREEZE: Ready!\n");
    pr_info("  Start:  echo 1 > /sys/kernel/simple_freeze/freeze\n");
    pr_info("  Duration: echo N > /sys/module/simple_freeze/parameters/freeze_duration\n");
    pr_info("  Delay:    echo N > /sys/module/simple_freeze/parameters/frame_delay_ms\n");
    
    return 0;
}

static void __exit simple_freeze_exit(void)
{
    freeze_active = false;
    del_timer_sync(&unfreeze_timer);
    unregister_kprobe(&kp_page_flip);
    
    if (freeze_kobj) {
        sysfs_remove_file(freeze_kobj, &freeze_attr.attr);
        sysfs_remove_file(freeze_kobj, &status_attr.attr);
        kobject_put(freeze_kobj);
    }
    
    pr_info("SIMPLE_FREEZE: Unloaded\n");
}

module_init(simple_freeze_init);
module_exit(simple_freeze_exit);