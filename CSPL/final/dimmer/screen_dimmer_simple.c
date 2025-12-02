#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/string.h>

static int original_brightness = -1;
static int current_brightness = 100;
static char brightness_path[256] = "/sys/class/backlight/intel_backlight/brightness";
static char max_brightness_path[256] = "/sys/class/backlight/intel_backlight/max_brightness";

module_param(current_brightness, int, 0644);
MODULE_PARM_DESC(current_brightness, "Current brightness level (0-100)");

module_param_string(brightness_path, brightness_path, sizeof(brightness_path), 0644);
MODULE_PARM_DESC(brightness_path, "Path to brightness control file");

module_param_string(max_brightness_path, max_brightness_path, sizeof(max_brightness_path), 0644);
MODULE_PARM_DESC(max_brightness_path, "Path to max brightness file");

// Read a value from sysfs file
static int read_sysfs_int(const char *path)
{
    struct file *file;
    char buffer[32];
    int value = -1;
    loff_t pos = 0;
    
    file = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(file)) {
        pr_err("SCREEN_DIMMER: Cannot open %s\n", path);
        return -1;
    }
    
    if (kernel_read(file, buffer, sizeof(buffer) - 1, &pos) > 0) {
        buffer[31] = '\0';
        if (kstrtoint(buffer, 10, &value) != 0) {
            value = -1;
        }
    }
    
    filp_close(file, NULL);
    return value;
}

// Try to find a working backlight device
static int find_backlight_device(void)
{
    const char *possible_paths[][2] = {
        {"/sys/class/backlight/intel_backlight/brightness", "/sys/class/backlight/intel_backlight/max_brightness"},
        {"/sys/class/backlight/acpi_video0/brightness", "/sys/class/backlight/acpi_video0/max_brightness"},
        {"/sys/class/backlight/amdgpu_bl0/brightness", "/sys/class/backlight/amdgpu_bl0/max_brightness"},
        {"/sys/class/backlight/radeon_bl0/brightness", "/sys/class/backlight/radeon_bl0/max_brightness"},
        {"/sys/class/backlight/nvidia_0/brightness", "/sys/class/backlight/nvidia_0/max_brightness"},
        {NULL, NULL}
    };
    
    int i;
    for (i = 0; possible_paths[i][0] != NULL; i++) {
        if (read_sysfs_int(possible_paths[i][1]) > 0) {
            strcpy(brightness_path, possible_paths[i][0]);
            strcpy(max_brightness_path, possible_paths[i][1]);
            pr_info("SCREEN_DIMMER: Found backlight device: %s\n", brightness_path);
            return 0;
        }
    }
    
    pr_err("SCREEN_DIMMER: No backlight device found\n");
    pr_info("SCREEN_DIMMER: You can manually set the paths using:\n");
    pr_info("  echo '/path/to/brightness' > /sys/module/screen_dimmer_simple/parameters/brightness_path\n");
    pr_info("  echo '/path/to/max_brightness' > /sys/module/screen_dimmer_simple/parameters/max_brightness_path\n");
    return -1;
}

// Write a value to sysfs file.
static int write_sysfs_int(const char *path, int value)
{
    struct file *file;
    char buffer[32];
    loff_t pos = 0;
    int len;
    
    file = filp_open(path, O_WRONLY, 0);
    if (IS_ERR(file)) {
        pr_err("SCREEN_DIMMER: Cannot open %s for writing\n", path);
        return -1;
    }
    
    len = snprintf(buffer, sizeof(buffer), "%d", value);
    if (kernel_write(file, buffer, len, &pos) != len) {
        filp_close(file, NULL);
        return -1;
    }
    
    filp_close(file, NULL);
    return 0;
}

// Get current brightness as percentage (0-100)
static int get_brightness_percent(void)
{
    int curr_brightness = read_sysfs_int(brightness_path);
    int max_brightness = read_sysfs_int(max_brightness_path);
    
    if (curr_brightness < 0 || max_brightness <= 0) {
        return -1;
    }
    
    return (curr_brightness * 100) / max_brightness;
}

// Set brightness as percentage
static int set_brightness_percent(int percent)
{
    int max_brightness = read_sysfs_int(max_brightness_path);
    int target;
    
    if (max_brightness <= 0) {
        pr_err("SCREEN_DIMMER: Cannot read max brightness\n");
        return -1;
    }
    
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    target = (max_brightness * percent) / 100;
    
    if (write_sysfs_int(brightness_path, target) != 0) {
        pr_err("SCREEN_DIMMER: Cannot set brightness\n");
        return -1;
    }
    
    pr_info("SCREEN_DIMMER: Set brightness to %d%% (%d/%d)\n", percent, target, max_brightness);
    return 0;
}

// Sysfs interface
static ssize_t brightness_store(struct kobject *kobj, struct kobj_attribute *attr,
                               const char *buf, size_t count)
{
    int brightness;
    
    if (kstrtoint(buf, 10, &brightness) != 0) {
        return -EINVAL;
    }
    
    if (brightness < 0 || brightness > 100) {
        return -EINVAL;
    }
    
    if (set_brightness_percent(brightness) == 0) {
        current_brightness = brightness;
    }
    
    return count;
}

static ssize_t brightness_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int current_percent = get_brightness_percent();
    
    if (current_percent < 0) {
        return sprintf(buf, "ERROR\n");
    }
    
    return sprintf(buf, "%d%%\n", current_percent);
}

static struct kobj_attribute brightness_attr = __ATTR(brightness, 0644, brightness_show, brightness_store);
static struct kobject *dimmer_kobj;

static int __init screen_dimmer_init(void)
{
    int ret;
    
    pr_info("SCREEN_DIMMER: Loading screen dimmer module\n");
    
    // Try to find a working backlight device
    if (find_backlight_device() != 0) {
        pr_warn("SCREEN_DIMMER: No backlight device found, but continuing anyway\n");
        pr_info("SCREEN_DIMMER: This system might not support brightness control\n");
        pr_info("SCREEN_DIMMER: Or you might need to manually set the paths\n");
    }
    
    // Save original brightness
    original_brightness = get_brightness_percent();
    if (original_brightness < 0) {
        pr_warn("SCREEN_DIMMER: Could not read original brightness\n");
        original_brightness = 100;
    }
    
    // Create sysfs interface
    dimmer_kobj = kobject_create_and_add("screen_dimmer", kernel_kobj);
    if (!dimmer_kobj) {
        return -ENOMEM;
    }
    
    ret = sysfs_create_file(dimmer_kobj, &brightness_attr.attr);
    if (ret) {
        kobject_put(dimmer_kobj);
        return ret;
    }
    
    pr_info("SCREEN_DIMMER: Loaded successfully\n");
    pr_info("SCREEN_DIMMER: Original brightness: %d%%\n", original_brightness);
    pr_info("SCREEN_DIMMER: Usage:\n");
    pr_info("  Set brightness: echo N > /sys/kernel/screen_dimmer/brightness\n");
    pr_info("  Get brightness: cat /sys/kernel/screen_dimmer/brightness\n");
    
    return 0;
}

static void __exit screen_dimmer_exit(void)
{
    // Restore original brightness
    if (original_brightness >= 0) {
        set_brightness_percent(original_brightness);
        pr_info("SCREEN_DIMMER: Restored original brightness (%d%%)\n", original_brightness);
    }
    
    // Clean up sysfs
    if (dimmer_kobj) {
        sysfs_remove_file(dimmer_kobj, &brightness_attr.attr);
        kobject_put(dimmer_kobj);
    }
    
    pr_info("SCREEN_DIMMER: Unloaded\n");
}

module_init(screen_dimmer_init);
module_exit(screen_dimmer_exit);
