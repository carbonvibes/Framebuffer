#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/backlight.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Screen Dimmer");
MODULE_DESCRIPTION("Control screen brightness from kernel");
MODULE_VERSION("1.0");

static struct backlight_device *backlight_dev = NULL;
static int original_brightness = -1;
static int current_brightness_percent = 100;

module_param(current_brightness_percent, int, 0644);
MODULE_PARM_DESC(current_brightness_percent, "Current brightness level (0-100)");

// Find the first available backlight device
static struct backlight_device *find_backlight_device(void)
{
    struct backlight_device *bd = NULL;
    struct device *dev;
    struct class *backlight_class;
    
    // Try to find backlight class
    backlight_class = class_find("backlight");
    if (!backlight_class) {
        pr_err("SCREEN_DIMMER: Backlight class not found\n");
        return NULL;
    }
    
    // Find first backlight device
    dev = class_find_device(backlight_class, NULL, NULL, NULL);
    if (dev) {
        bd = dev_get_drvdata(dev);
        put_device(dev);
    }
    
    return bd;
}

// Set screen brightness
static int set_brightness(int brightness_percent)
{
    int max_brightness, target_brightness;
    
    if (!backlight_dev) {
        pr_err("SCREEN_DIMMER: No backlight device available\n");
        return -ENODEV;
    }
    
    if (brightness_percent < 0) brightness_percent = 0;
    if (brightness_percent > 100) brightness_percent = 100;
    
    max_brightness = backlight_dev->props.max_brightness;
    target_brightness = (max_brightness * brightness_percent) / 100;
    
    // Ensure minimum brightness (avoid complete black screen)
    if (target_brightness == 0 && brightness_percent > 0) {
        target_brightness = 1;
    }
    
    backlight_dev->props.brightness = target_brightness;
    backlight_update_status(backlight_dev);
    
    current_brightness_percent = brightness_percent;
    
    pr_info("SCREEN_DIMMER: Set brightness to %d%% (%d/%d)\n",
            brightness_percent, target_brightness, max_brightness);
    
    return 0;
}

// Get current brightness
static int get_brightness_percent(void)
{
    if (!backlight_dev)
        return -1;
    
    return (backlight_dev->props.brightness * 100) / backlight_dev->props.max_brightness;
}

// Sysfs interface for brightness control
static ssize_t brightness_store(struct kobject *kobj, struct kobj_attribute *attr,
                               const char *buf, size_t count)
{
    int brightness;
    
    if (kstrtoint(buf, 10, &brightness) != 0) {
        pr_err("SCREEN_DIMMER: Invalid brightness value\n");
        return -EINVAL;
    }
    
    if (brightness < 0 || brightness > 100) {
        pr_err("SCREEN_DIMMER: Brightness must be 0-100\n");
        return -EINVAL;
    }
    
    set_brightness(brightness);
    return count;
}

static ssize_t brightness_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int current = get_brightness_percent();
    if (current < 0)
        return sprintf(buf, "ERROR: No backlight device\n");
    
    return sprintf(buf, "%d%%\n", current);
}

// Sysfs interface for device info
static ssize_t info_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    if (!backlight_dev)
        return sprintf(buf, "No backlight device found\n");
    
    return sprintf(buf, "Device: %s\nMax brightness: %d\nCurrent: %d (%d%%)\nOriginal: %d\n",
                   backlight_dev->dev.init_name ?: "unknown",
                   backlight_dev->props.max_brightness,
                   backlight_dev->props.brightness,
                   get_brightness_percent(),
                   original_brightness);
}

// Sysfs interface for quick dim presets
static ssize_t dim_store(struct kobject *kobj, struct kobj_attribute *attr,
                        const char *buf, size_t count)
{
    char cmd[10];
    
    if (sscanf(buf, "%9s", cmd) != 1)
        return -EINVAL;
    
    if (strcmp(cmd, "dark") == 0) {
        set_brightness(20);
    } else if (strcmp(cmd, "dim") == 0) {
        set_brightness(50);
    } else if (strcmp(cmd, "normal") == 0) {
        set_brightness(100);
    } else if (strcmp(cmd, "restore") == 0) {
        if (original_brightness >= 0) {
            int original_percent = (original_brightness * 100) / backlight_dev->props.max_brightness;
            set_brightness(original_percent);
        }
    } else {
        pr_err("SCREEN_DIMMER: Invalid command. Use: dark, dim, normal, restore\n");
        return -EINVAL;
    }
    
    return count;
}

static struct kobj_attribute brightness_attr = __ATTR(brightness, 0644, brightness_show, brightness_store);
static struct kobj_attribute info_attr = __ATTR(info, 0444, info_show, NULL);
static struct kobj_attribute dim_attr = __ATTR(dim, 0200, NULL, dim_store);

static struct kobject *dimmer_kobj;

static int __init screen_dimmer_init(void)
{
    int ret;
    
    pr_info("SCREEN_DIMMER: Loading screen brightness control module\n");
    
    // Find backlight device
    backlight_dev = find_backlight_device();
    if (!backlight_dev) {
        pr_err("SCREEN_DIMMER: No backlight device found\n");
        pr_info("SCREEN_DIMMER: Try: ls /sys/class/backlight/\n");
        return -ENODEV;
    }
    
    // Save original brightness
    original_brightness = backlight_dev->props.brightness;
    current_brightness_percent = get_brightness_percent();
    
    pr_info("SCREEN_DIMMER: Found backlight device: %s\n", 
            backlight_dev->dev.init_name ?: "unknown");
    pr_info("SCREEN_DIMMER: Max brightness: %d\n", backlight_dev->props.max_brightness);
    pr_info("SCREEN_DIMMER: Current brightness: %d (%d%%)\n", 
            original_brightness, current_brightness_percent);
    
    // Create sysfs interface
    dimmer_kobj = kobject_create_and_add("screen_dimmer", kernel_kobj);
    if (!dimmer_kobj) {
        pr_err("SCREEN_DIMMER: Failed to create sysfs directory\n");
        return -ENOMEM;
    }
    
    ret = sysfs_create_file(dimmer_kobj, &brightness_attr.attr);
    if (ret) goto cleanup;
    
    ret = sysfs_create_file(dimmer_kobj, &info_attr.attr);
    if (ret) goto cleanup;
    
    ret = sysfs_create_file(dimmer_kobj, &dim_attr.attr);
    if (ret) goto cleanup;
    
    pr_info("SCREEN_DIMMER: Module loaded successfully!\n");
    pr_info("SCREEN_DIMMER: Usage:\n");
    pr_info("  Set brightness: echo N > /sys/kernel/screen_dimmer/brightness (0-100)\n");
    pr_info("  Get brightness: cat /sys/kernel/screen_dimmer/brightness\n");
    pr_info("  Device info:    cat /sys/kernel/screen_dimmer/info\n");
    pr_info("  Quick presets:  echo dark|dim|normal|restore > /sys/kernel/screen_dimmer/dim\n");
    
    return 0;
    
cleanup:
    if (dimmer_kobj) {
        sysfs_remove_file(dimmer_kobj, &brightness_attr.attr);
        sysfs_remove_file(dimmer_kobj, &info_attr.attr);
        sysfs_remove_file(dimmer_kobj, &dim_attr.attr);
        kobject_put(dimmer_kobj);
    }
    return ret;
}

static void __exit screen_dimmer_exit(void)
{
    // Restore original brightness
    if (backlight_dev && original_brightness >= 0) {
        backlight_dev->props.brightness = original_brightness;
        backlight_update_status(backlight_dev);
        pr_info("SCREEN_DIMMER: Restored original brightness: %d\n", original_brightness);
    }
    
    // Clean up sysfs
    if (dimmer_kobj) {
        sysfs_remove_file(dimmer_kobj, &brightness_attr.attr);
        sysfs_remove_file(dimmer_kobj, &info_attr.attr);
        sysfs_remove_file(dimmer_kobj, &dim_attr.attr);
        kobject_put(dimmer_kobj);
    }
    
    pr_info("SCREEN_DIMMER: Module unloaded\n");
}

module_init(screen_dimmer_init);
module_exit(screen_dimmer_exit);
