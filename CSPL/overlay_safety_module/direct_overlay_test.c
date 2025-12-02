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
MODULE_AUTHOR("Direct FB Overlay Test");
MODULE_DESCRIPTION("Direct framebuffer access for visible overlay test");
MODULE_VERSION("1.0");

#define PROC_NAME "direct_overlay_test"

struct fb_overlay_state {
    struct fb_info *fb_info;
    void *original_screen;
    size_t screen_size;
    bool overlay_active;
    struct timer_list restore_timer;
    struct work_struct overlay_work;
    int width;
    int height;
    int bpp;
};

static struct fb_overlay_state overlay_state;
static struct proc_dir_entry *proc_entry;
static DEFINE_MUTEX(overlay_mutex);

// Find the framebuffer device
static struct fb_info *find_framebuffer(void)
{
    struct fb_info *info = NULL;
    int i;
    
    pr_info("Searching for framebuffer devices...\n");
    
    // Look through registered framebuffers
    for (i = 0; i < FB_MAX; i++) {
        info = registered_fb[i];
        if (info && info->screen_base) {
            pr_info("Found framebuffer %d: %dx%d, %d bpp\n", 
                    i, info->var.xres, info->var.yres, info->var.bits_per_pixel);
            
            // Use the first valid framebuffer
            overlay_state.width = info->var.xres;
            overlay_state.height = info->var.yres;
            overlay_state.bpp = info->var.bits_per_pixel;
            overlay_state.screen_size = info->screen_size;
            
            if (overlay_state.screen_size == 0) {
                overlay_state.screen_size = overlay_state.width * overlay_state.height * (overlay_state.bpp / 8);
            }
            
            pr_info("Using framebuffer: %dx%d, %d bpp, %zu bytes\n",
                    overlay_state.width, overlay_state.height, 
                    overlay_state.bpp, overlay_state.screen_size);
            
            return info;
        }
    }
    
    pr_warn("No framebuffer found\n");
    return NULL;
}

// Fill framebuffer with solid color
static void fill_framebuffer_color(struct fb_info *info, uint32_t color)
{
    uint32_t *screen32;
    uint16_t *screen16;
    int i, pixel_count;
    
    if (!info || !info->screen_base) {
        pr_err("Invalid framebuffer\n");
        return;
    }
    
    pr_info("Filling framebuffer with color 0x%08x\n", color);
    
    pixel_count = overlay_state.width * overlay_state.height;
    
    // Console subsystem might interfere, so we try to handle that
    console_lock();
    
    if (overlay_state.bpp == 32) {
        screen32 = (uint32_t *)info->screen_base;
        for (i = 0; i < pixel_count; i++) {
            screen32[i] = color;
        }
    } else if (overlay_state.bpp == 16) {
        screen16 = (uint16_t *)info->screen_base;
        uint16_t color16 = (uint16_t)((color >> 8) & 0xFFFF); // Convert to 16-bit
        for (i = 0; i < pixel_count; i++) {
            screen16[i] = color16;
        }
    } else {
        pr_warn("Unsupported bit depth: %d\n", overlay_state.bpp);
    }
    
    console_unlock();
    
    pr_info("Framebuffer filled with color\n");
}

// Save current framebuffer content
static int save_framebuffer_content(struct fb_info *info)
{
    if (!info || !info->screen_base) {
        return -EINVAL;
    }
    
    pr_info("Saving original framebuffer content (%zu bytes)\n", overlay_state.screen_size);
    
    overlay_state.original_screen = vmalloc(overlay_state.screen_size);
    if (!overlay_state.original_screen) {
        pr_err("Failed to allocate backup buffer\n");
        return -ENOMEM;
    }
    
    console_lock();
    memcpy(overlay_state.original_screen, info->screen_base, overlay_state.screen_size);
    console_unlock();
    
    pr_info("Original content saved\n");
    return 0;
}

// Restore original framebuffer content
static void restore_framebuffer_content(struct fb_info *info)
{
    if (!info || !info->screen_base || !overlay_state.original_screen) {
        pr_warn("Cannot restore - missing data\n");
        return;
    }
    
    pr_info("Restoring original framebuffer content\n");
    
    console_lock();
    memcpy(info->screen_base, overlay_state.original_screen, overlay_state.screen_size);
    console_unlock();
    
    vfree(overlay_state.original_screen);
    overlay_state.original_screen = NULL;
    
    pr_info("Original content restored\n");
}

// Work function to apply blue overlay
static void apply_blue_overlay_work(struct work_struct *work)
{
    struct fb_overlay_state *state = container_of(work, struct fb_overlay_state, overlay_work);
    
    pr_info("=== APPLYING BLUE OVERLAY ===\n");
    
    mutex_lock(&overlay_mutex);
    
    if (!state->fb_info) {
        pr_err("No framebuffer available\n");
        mutex_unlock(&overlay_mutex);
        return;
    }
    
    // Save original content
    if (save_framebuffer_content(state->fb_info) != 0) {
        pr_err("Failed to save original content\n");
        mutex_unlock(&overlay_mutex);
        return;
    }
    
    // Fill with blue color (RGB: 0, 0, 255)
    uint32_t blue_color = 0xFF0000FF; // ARGB: Alpha=255, Red=0, Green=0, Blue=255
    fill_framebuffer_color(state->fb_info, blue_color);
    
    state->overlay_active = true;
    
    // Set timer to restore after 3 seconds
    mod_timer(&state->restore_timer, jiffies + msecs_to_jiffies(3000));
    
    mutex_unlock(&overlay_mutex);
    
    pr_info("=== BLUE OVERLAY ACTIVE FOR 3 SECONDS ===\n");
}

// Timer callback to restore screen
static void restore_timer_callback(struct timer_list *timer)
{
    struct fb_overlay_state *state = container_of(timer, struct fb_overlay_state, restore_timer);
    
    pr_info("=== RESTORING ORIGINAL SCREEN ===\n");
    
    mutex_lock(&overlay_mutex);
    
    if (state->overlay_active && state->fb_info) {
        restore_framebuffer_content(state->fb_info);
        state->overlay_active = false;
    }
    
    mutex_unlock(&overlay_mutex);
    
    pr_info("=== OVERLAY TEST COMPLETE ===\n");
}

// Proc file operations
static int direct_overlay_proc_show(struct seq_file *m, void *v)
{
    mutex_lock(&overlay_mutex);
    
    seq_printf(m, "Direct Framebuffer Overlay Test\n");
    seq_printf(m, "===============================\n\n");
    
    seq_printf(m, "Purpose: Direct framebuffer access for visible blue overlay\n");
    seq_printf(m, "Status: %s\n", overlay_state.overlay_active ? "BLUE OVERLAY ACTIVE" : "Normal display");
    
    seq_printf(m, "\nFramebuffer Info:\n");
    seq_printf(m, "  Device: %p\n", overlay_state.fb_info);
    seq_printf(m, "  Resolution: %dx%d\n", overlay_state.width, overlay_state.height);
    seq_printf(m, "  Bits per pixel: %d\n", overlay_state.bpp);
    seq_printf(m, "  Screen size: %zu bytes\n", overlay_state.screen_size);
    seq_printf(m, "  Backup buffer: %s\n", overlay_state.original_screen ? "Allocated" : "None");
    
    seq_printf(m, "\nCommands:\n");
    seq_printf(m, "  echo 'blue' > /proc/%s    - Apply blue overlay for 3 seconds\n", PROC_NAME);
    seq_printf(m, "  echo 'red' > /proc/%s     - Apply red overlay for 3 seconds\n", PROC_NAME);
    seq_printf(m, "  echo 'restore' > /proc/%s - Restore immediately\n", PROC_NAME);
    
    seq_printf(m, "\nNote: This version directly modifies framebuffer memory\n");
    seq_printf(m, "      Should produce visible color changes on screen\n");
    
    mutex_unlock(&overlay_mutex);
    return 0;
}

static ssize_t direct_overlay_proc_write(struct file *file, const char __user *buffer, 
                                        size_t count, loff_t *pos)
{
    char input[32];
    uint32_t color;
    
    if (count >= sizeof(input))
        return -EINVAL;
    
    if (copy_from_user(input, buffer, count))
        return -EFAULT;
    
    input[count] = '\0';
    
    if (!overlay_state.fb_info) {
        pr_err("No framebuffer available\n");
        return -ENODEV;
    }
    
    if (strncmp(input, "blue", 4) == 0) {
        pr_info("User requested blue overlay\n");
        schedule_work(&overlay_state.overlay_work);
    } else if (strncmp(input, "red", 3) == 0) {
        pr_info("User requested red overlay\n");
        // Save and apply red color
        if (save_framebuffer_content(overlay_state.fb_info) == 0) {
            color = 0xFFFF0000; // Red
            fill_framebuffer_color(overlay_state.fb_info, color);
            overlay_state.overlay_active = true;
            mod_timer(&overlay_state.restore_timer, jiffies + msecs_to_jiffies(3000));
        }
    } else if (strncmp(input, "restore", 7) == 0) {
        pr_info("User requested immediate restore\n");
        del_timer(&overlay_state.restore_timer);
        restore_timer_callback(&overlay_state.restore_timer);
    } else {
        pr_info("Unknown command: %s\n", input);
        pr_info("Available: 'blue', 'red', 'restore'\n");
    }
    
    return count;
}

static int direct_overlay_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, direct_overlay_proc_show, NULL);
}

static const struct proc_ops direct_overlay_proc_ops = {
    .proc_open = direct_overlay_proc_open,
    .proc_read = seq_read,
    .proc_write = direct_overlay_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init direct_overlay_init(void)
{
    pr_info("Direct Framebuffer Overlay Test Module loading\n");
    
    // Initialize state
    memset(&overlay_state, 0, sizeof(overlay_state));
    
    // Initialize work and timer
    INIT_WORK(&overlay_state.overlay_work, apply_blue_overlay_work);
    timer_setup(&overlay_state.restore_timer, restore_timer_callback, 0);
    
    // Find framebuffer
    overlay_state.fb_info = find_framebuffer();
    if (!overlay_state.fb_info) {
        pr_err("No framebuffer found\n");
        return -ENODEV;
    }
    
    // Create proc entry
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &direct_overlay_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry\n");
        return -ENOMEM;
    }
    
    pr_info("Direct Framebuffer Overlay Test Module loaded\n");
    pr_info("Found framebuffer: %dx%d, %d bpp\n", 
            overlay_state.width, overlay_state.height, overlay_state.bpp);
    pr_info("Commands:\n");
    pr_info("  echo 'blue' > /proc/%s   - Blue overlay for 3 seconds\n", PROC_NAME);
    pr_info("  echo 'red' > /proc/%s    - Red overlay for 3 seconds\n", PROC_NAME);
    pr_info("  cat /proc/%s             - Show status\n", PROC_NAME);
    
    return 0;
}

static void __exit direct_overlay_exit(void)
{
    pr_info("Direct Framebuffer Overlay Test Module unloading\n");
    
    // Cancel work and timer
    cancel_work_sync(&overlay_state.overlay_work);
    del_timer_sync(&overlay_state.restore_timer);
    
    // Restore original content if overlay is active
    if (overlay_state.overlay_active && overlay_state.fb_info) {
        restore_framebuffer_content(overlay_state.fb_info);
    }
    
    // Clean up backup buffer
    if (overlay_state.original_screen) {
        vfree(overlay_state.original_screen);
    }
    
    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }
    
    pr_info("Direct Framebuffer Overlay Test Module unloaded\n");
}

module_init(direct_overlay_init);
module_exit(direct_overlay_exit);
