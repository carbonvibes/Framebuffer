#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/fb.h>
#include <linux/console.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simple Visual Test");
MODULE_DESCRIPTION("Simple visual test using framebuffer console");
MODULE_VERSION("1.0");

#define PROC_NAME "simple_visual_test"

struct visual_test_state {
    struct fb_info *fb_info;
    void *original_screen;
    size_t screen_size;
    bool overlay_active;
    struct timer_list remove_timer;
    struct work_struct apply_work;
    struct work_struct remove_work;
};

static struct visual_test_state test_state;
static struct proc_dir_entry *proc_entry;
static DEFINE_MUTEX(test_mutex);

// Fill screen with blue color
static void fill_screen_blue(struct fb_info *info)
{
    void __iomem *screen_base;
    u32 *pixels;
    u32 blue_color;
    int i, total_pixels;
    
    if (!info || !info->screen_base) {
        pr_err("No framebuffer screen base\n");
        return;
    }
    
    screen_base = info->screen_base;
    total_pixels = info->var.xres * info->var.yres;
    
    // Calculate blue color based on bit depth
    switch (info->var.bits_per_pixel) {
        case 32:
            blue_color = 0xFF0000FF;  // ARGB: Opaque Blue
            break;
        case 24:
            blue_color = 0x0000FF;    // RGB: Blue
            break;
        case 16:
            blue_color = 0x001F;      // RGB565: Blue
            break;
        default:
            blue_color = 0xFF;        // Fallback
            break;
    }
    
    pr_info("Filling screen with blue: %dx%d, %d bpp, color=0x%08x\n",
            info->var.xres, info->var.yres, info->var.bits_per_pixel, blue_color);
    
    // Fill framebuffer with blue
    if (info->var.bits_per_pixel == 32) {
        pixels = (u32 *)screen_base;
        for (i = 0; i < total_pixels; i++) {
            pixels[i] = blue_color;
        }
    } else {
        // For other bit depths, use memset as approximation
        memset_io(screen_base, blue_color & 0xFF, 
                  info->var.xres * info->var.yres * (info->var.bits_per_pixel / 8));
    }
    
    // Force screen update if available
    if (info->fbops && info->fbops->fb_pan_display) {
        info->fbops->fb_pan_display(&info->var, info);
    }
}

// Restore original screen content
static void restore_screen(struct fb_info *info)
{
    if (!info || !info->screen_base || !test_state.original_screen) {
        pr_err("Cannot restore screen - missing data\n");
        return;
    }
    
    pr_info("Restoring original screen content\n");
    
    memcpy_toio(info->screen_base, test_state.original_screen, test_state.screen_size);
    
    // Force screen update
    if (info->fbops && info->fbops->fb_pan_display) {
        info->fbops->fb_pan_display(&info->var, info);
    }
}

// Work function to apply blue overlay
static void apply_blue_work(struct work_struct *work)
{
    mutex_lock(&test_mutex);
    
    if (!test_state.fb_info) {
        pr_err("No framebuffer available\n");
        goto unlock;
    }
    
    pr_info("=== APPLYING BLUE OVERLAY TO SCREEN ===\n");
    
    // Save original screen content
    if (!test_state.original_screen) {
        test_state.screen_size = test_state.fb_info->var.xres * 
                                 test_state.fb_info->var.yres * 
                                 (test_state.fb_info->var.bits_per_pixel / 8);
        
        test_state.original_screen = vmalloc(test_state.screen_size);
        if (test_state.original_screen) {
            memcpy_fromio(test_state.original_screen, 
                         test_state.fb_info->screen_base, 
                         test_state.screen_size);
            pr_info("Saved original screen content (%zu bytes)\n", test_state.screen_size);
        }
    }
    
    // Fill screen with blue
    fill_screen_blue(test_state.fb_info);
    
    test_state.overlay_active = true;
    
    // Set timer to remove overlay after 3 seconds
    mod_timer(&test_state.remove_timer, jiffies + msecs_to_jiffies(3000));
    
    pr_info("=== BLUE OVERLAY ACTIVE - SHOULD BE VISIBLE NOW! ===\n");
    
unlock:
    mutex_unlock(&test_mutex);
}

// Work function to remove overlay
static void remove_blue_work(struct work_struct *work)
{
    mutex_lock(&test_mutex);
    
    if (!test_state.overlay_active) {
        goto unlock;
    }
    
    pr_info("=== REMOVING BLUE OVERLAY ===\n");
    
    if (test_state.fb_info) {
        restore_screen(test_state.fb_info);
    }
    
    test_state.overlay_active = false;
    
    pr_info("=== BLUE OVERLAY REMOVED ===\n");
    
unlock:
    mutex_unlock(&test_mutex);
}

// Timer callback
static void remove_timer_callback(struct timer_list *timer)
{
    pr_info("Timer expired - removing blue overlay\n");
    schedule_work(&test_state.remove_work);
}

// Find framebuffer
static int find_framebuffer(void)
{
    int i;
    
    pr_info("Looking for framebuffer devices...\n");
    
    // Try to find an active framebuffer
    for (i = 0; i < FB_MAX; i++) {
        test_state.fb_info = registered_fb[i];
        if (test_state.fb_info && test_state.fb_info->screen_base) {
            pr_info("Found framebuffer %d: %dx%d, %d bpp\n", i,
                    test_state.fb_info->var.xres, test_state.fb_info->var.yres,
                    test_state.fb_info->var.bits_per_pixel);
            return 0;
        }
    }
    
    pr_err("No usable framebuffer found\n");
    test_state.fb_info = NULL;
    return -ENODEV;
}

// Proc file operations
static int visual_test_proc_show(struct seq_file *m, void *v)
{
    mutex_lock(&test_mutex);
    
    seq_printf(m, "Simple Visual Overlay Test\n");
    seq_printf(m, "=========================\n\n");
    
    seq_printf(m, "Purpose: Display blue overlay directly on framebuffer for visual test\n");
    seq_printf(m, "Method: Direct framebuffer manipulation (simple and safe)\n\n");
    
    if (test_state.fb_info) {
        seq_printf(m, "Framebuffer Info:\n");
        seq_printf(m, "  Resolution: %dx%d\n", 
                   test_state.fb_info->var.xres, test_state.fb_info->var.yres);
        seq_printf(m, "  Bits per pixel: %d\n", test_state.fb_info->var.bits_per_pixel);
        seq_printf(m, "  Screen base: %p\n", test_state.fb_info->screen_base);
        seq_printf(m, "  Screen size: %zu bytes\n", test_state.screen_size);
    } else {
        seq_printf(m, "Framebuffer: Not found\n");
    }
    
    seq_printf(m, "Overlay Status: %s\n", test_state.overlay_active ? "ACTIVE" : "Inactive");
    seq_printf(m, "Original Content: %s\n", test_state.original_screen ? "Saved" : "Not saved");
    
    seq_printf(m, "\nCommands:\n");
    seq_printf(m, "  echo 'blue' > /proc/%s   - Show blue overlay for 3 seconds\n", PROC_NAME);
    seq_printf(m, "  echo 'clear' > /proc/%s  - Remove overlay immediately\n", PROC_NAME);
    
    seq_printf(m, "\nNote: You should see a BLUE SCREEN when 'blue' command is used!\n");
    
    mutex_unlock(&test_mutex);
    return 0;
}

static ssize_t visual_test_proc_write(struct file *file, const char __user *buffer, 
                                     size_t count, loff_t *pos)
{
    char input[32];
    
    if (count >= sizeof(input))
        return -EINVAL;
    
    if (copy_from_user(input, buffer, count))
        return -EFAULT;
    
    input[count] = '\0';
    
    if (strncmp(input, "blue", 4) == 0) {
        pr_info("=== USER REQUESTED BLUE OVERLAY ===\n");
        schedule_work(&test_state.apply_work);
    } else if (strncmp(input, "clear", 5) == 0) {
        pr_info("=== USER REQUESTED OVERLAY REMOVAL ===\n");
        del_timer(&test_state.remove_timer);
        schedule_work(&test_state.remove_work);
    } else {
        pr_info("Unknown command: %s\n", input);
        pr_info("Available commands: 'blue', 'clear'\n");
    }
    
    return count;
}

static int visual_test_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, visual_test_proc_show, NULL);
}

static const struct proc_ops visual_test_proc_ops = {
    .proc_open = visual_test_proc_open,
    .proc_read = seq_read,
    .proc_write = visual_test_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Module initialization
static int __init visual_test_init(void)
{
    int ret;
    
    pr_info("Simple Visual Overlay Test loading\n");
    
    // Initialize state
    memset(&test_state, 0, sizeof(test_state));
    
    // Initialize work and timer
    INIT_WORK(&test_state.apply_work, apply_blue_work);
    INIT_WORK(&test_state.remove_work, remove_blue_work);
    timer_setup(&test_state.remove_timer, remove_timer_callback, 0);
    
    // Find framebuffer
    ret = find_framebuffer();
    if (ret) {
        pr_err("Failed to find framebuffer: %d\n", ret);
        return ret;
    }
    
    // Create proc entry
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &visual_test_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry\n");
        return -ENOMEM;
    }
    
    pr_info("Simple Visual Test loaded successfully\n");
    pr_info("*** TO TEST: echo 'blue' > /proc/%s ***\n", PROC_NAME);
    pr_info("*** YOU SHOULD SEE BLUE SCREEN FOR 3 SECONDS! ***\n");
    
    return 0;
}

// Module cleanup
static void __exit visual_test_exit(void)
{
    pr_info("Simple Visual Test unloading\n");
    
    // Cancel work and timer
    cancel_work_sync(&test_state.apply_work);
    cancel_work_sync(&test_state.remove_work);
    del_timer_sync(&test_state.remove_timer);
    
    // Restore screen if overlay is active
    if (test_state.overlay_active) {
        remove_blue_work(&test_state.remove_work);
    }
    
    // Free saved screen content
    if (test_state.original_screen) {
        vfree(test_state.original_screen);
    }
    
    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }
    
    pr_info("Simple Visual Test unloaded\n");
}

module_init(visual_test_init);
module_exit(visual_test_exit);
