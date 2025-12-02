#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/slab.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("VT Overlay Test");
MODULE_DESCRIPTION("Virtual Terminal overlay test - WILL BE VISIBLE");
MODULE_VERSION("1.0");

#define PROC_NAME "vt_overlay_test"

struct vt_overlay_state {
    bool overlay_active;
    struct timer_list remove_timer;
    struct work_struct apply_work;
    struct work_struct remove_work;
    u16 *saved_screen;
    int screen_size;
};

static struct vt_overlay_state vt_state;
static struct proc_dir_entry *proc_entry;
static DEFINE_MUTEX(vt_mutex);

// Apply blue overlay to VT
static void apply_vt_blue_overlay(void)
{
    struct vc_data *vc;
    u16 blue_char;
    int i;
    
    pr_info("Applying VT blue overlay\n");
    
    console_lock();
    
    vc = vc_cons[fg_console].d;
    if (vc && vc->vc_screenbuf) {
        
        // Save original screen if not saved yet
        if (!vt_state.saved_screen) {
            vt_state.screen_size = vc->vc_screenbuf_size;
            vt_state.saved_screen = kmalloc(vt_state.screen_size, GFP_KERNEL);
            if (vt_state.saved_screen) {
                memcpy(vt_state.saved_screen, vc->vc_screenbuf, vt_state.screen_size);
                pr_info("Saved original screen: %d bytes\n", vt_state.screen_size);
            }
        }
        
        // Blue background (0x1), white text (0xF), space character
        blue_char = 0x1F00 | ' ';
        
        // Fill entire screen with blue
        for (i = 0; i < vc->vc_screenbuf_size / 2; i++) {
            vc->vc_screenbuf[i] = blue_char;
        }
        
        // Add message in center
        if (vc->vc_cols > 20 && vc->vc_rows > 10) {
            char *message = "*** SAFETY OVERLAY ACTIVE ***";
            int msg_len = strlen(message);
            int start_pos = (vc->vc_rows / 2) * vc->vc_cols + (vc->vc_cols - msg_len) / 2;
            
            for (i = 0; i < msg_len && (start_pos + i) < (vc->vc_screenbuf_size / 2); i++) {
                vc->vc_screenbuf[start_pos + i] = 0x1F00 | message[i];
            }
        }
        
        // Force display update
        update_screen(vc);
        
        pr_info("*** VT BLUE OVERLAY APPLIED - SHOULD BE VISIBLE! ***\n");
        pr_info("Screen size: %dx%d, filled %d characters\n", 
                vc->vc_cols, vc->vc_rows, vc->vc_screenbuf_size / 2);
    } else {
        pr_err("No VT console available\n");
    }
    
    console_unlock();
}

// Restore original VT content
static void restore_vt_screen(void)
{
    struct vc_data *vc;
    
    pr_info("Restoring VT screen\n");
    
    console_lock();
    
    vc = vc_cons[fg_console].d;
    if (vc && vc->vc_screenbuf && vt_state.saved_screen) {
        
        // Restore original content
        memcpy(vc->vc_screenbuf, vt_state.saved_screen, vt_state.screen_size);
        
        // Force display update
        update_screen(vc);
        
        pr_info("VT screen restored\n");
    }
    
    console_unlock();
}

// Work function to apply overlay
static void apply_overlay_work(struct work_struct *work)
{
    mutex_lock(&vt_mutex);
    
    pr_info("=== APPLYING VT OVERLAY ===\n");
    
    apply_vt_blue_overlay();
    vt_state.overlay_active = true;
    
    // Set timer to remove after 5 seconds
    mod_timer(&vt_state.remove_timer, jiffies + msecs_to_jiffies(5000));
    
    mutex_unlock(&vt_mutex);
}

// Work function to remove overlay
static void remove_overlay_work(struct work_struct *work)
{
    mutex_lock(&vt_mutex);
    
    if (!vt_state.overlay_active) {
        goto unlock;
    }
    
    pr_info("=== REMOVING VT OVERLAY ===\n");
    
    restore_vt_screen();
    vt_state.overlay_active = false;
    
    pr_info("=== VT OVERLAY REMOVED ===\n");
    
unlock:
    mutex_unlock(&vt_mutex);
}

// Timer callback
static void remove_timer_callback(struct timer_list *timer)
{
    pr_info("Timer expired - removing VT overlay\n");
    schedule_work(&vt_state.remove_work);
}

// Alternative method - just fill with red for testing
static void apply_red_overlay(void)
{
    struct vc_data *vc;
    u16 red_char;
    int i;
    
    pr_info("Applying RED overlay test\n");
    
    console_lock();
    
    vc = vc_cons[fg_console].d;
    if (vc && vc->vc_screenbuf) {
        // Red background (0x4), white text (0xF)
        red_char = 0x4F00 | '#';
        
        // Fill screen with red '#' characters
        for (i = 0; i < vc->vc_screenbuf_size / 2; i++) {
            vc->vc_screenbuf[i] = red_char;
        }
        
        update_screen(vc);
        pr_info("*** RED OVERLAY APPLIED! ***\n");
    }
    
    console_unlock();
}

// Proc file operations
static int vt_overlay_proc_show(struct seq_file *m, void *v)
{
    struct vc_data *vc;
    
    mutex_lock(&vt_mutex);
    
    seq_printf(m, "VT Overlay Test Module\n");
    seq_printf(m, "======================\n\n");
    
    seq_printf(m, "Purpose: Create VISIBLE overlay on Virtual Terminal\n");
    seq_printf(m, "Method: Direct VT screen buffer manipulation\n\n");
    
    console_lock();
    vc = vc_cons[fg_console].d;
    if (vc) {
        seq_printf(m, "Current VT: %d\n", fg_console);
        seq_printf(m, "VT size: %dx%d\n", vc->vc_cols, vc->vc_rows);
        seq_printf(m, "Screen buffer: %p\n", vc->vc_screenbuf);
        seq_printf(m, "Buffer size: %d bytes\n", vc->vc_screenbuf_size);
    }
    console_unlock();
    
    seq_printf(m, "Overlay status: %s\n", vt_state.overlay_active ? "ACTIVE" : "Inactive");
    seq_printf(m, "Saved screen: %s\n", vt_state.saved_screen ? "Yes" : "No");
    
    seq_printf(m, "\nCommands:\n");
    seq_printf(m, "  echo 'blue' > /proc/%s   - Apply blue overlay (5 seconds)\n", PROC_NAME);
    seq_printf(m, "  echo 'red' > /proc/%s    - Apply red overlay (permanent)\n", PROC_NAME);
    seq_printf(m, "  echo 'clear' > /proc/%s  - Remove overlay immediately\n", PROC_NAME);
    
    seq_printf(m, "\nThis WILL be visible on your current console/terminal!\n");
    
    mutex_unlock(&vt_mutex);
    return 0;
}

static ssize_t vt_overlay_proc_write(struct file *file, const char __user *buffer, 
                                    size_t count, loff_t *pos)
{
    char input[32];
    
    if (count >= sizeof(input))
        return -EINVAL;
    
    if (copy_from_user(input, buffer, count))
        return -EFAULT;
    
    input[count] = '\0';
    
    if (strncmp(input, "blue", 4) == 0) {
        pr_info("=== USER REQUESTED BLUE VT OVERLAY ===\n");
        schedule_work(&vt_state.apply_work);
    } else if (strncmp(input, "red", 3) == 0) {
        pr_info("=== USER REQUESTED RED VT OVERLAY ===\n");
        apply_red_overlay();
    } else if (strncmp(input, "clear", 5) == 0) {
        pr_info("=== USER REQUESTED OVERLAY REMOVAL ===\n");
        del_timer(&vt_state.remove_timer);
        schedule_work(&vt_state.remove_work);
    } else {
        pr_info("Unknown command: %s\n", input);
        pr_info("Available commands: 'blue', 'red', 'clear'\n");
    }
    
    return count;
}

static int vt_overlay_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, vt_overlay_proc_show, NULL);
}

static const struct proc_ops vt_overlay_proc_ops = {
    .proc_open = vt_overlay_proc_open,
    .proc_read = seq_read,
    .proc_write = vt_overlay_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Module initialization
static int __init vt_overlay_init(void)
{
    pr_info("VT Overlay Test Module loading\n");
    
    // Initialize state
    memset(&vt_state, 0, sizeof(vt_state));
    
    // Initialize work and timer
    INIT_WORK(&vt_state.apply_work, apply_overlay_work);
    INIT_WORK(&vt_state.remove_work, remove_overlay_work);
    timer_setup(&vt_state.remove_timer, remove_timer_callback, 0);
    
    // Create proc entry
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &vt_overlay_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry\n");
        return -ENOMEM;
    }
    
    pr_info("VT Overlay Test loaded successfully\n");
    pr_info("*** TO TEST: echo 'blue' > /proc/%s ***\n", PROC_NAME);
    pr_info("*** OR: echo 'red' > /proc/%s ***\n", PROC_NAME);
    pr_info("*** THIS WILL BE VISIBLE ON YOUR CONSOLE! ***\n");
    
    return 0;
}

// Module cleanup
static void __exit vt_overlay_exit(void)
{
    pr_info("VT Overlay Test unloading\n");
    
    // Cancel work and timer
    cancel_work_sync(&vt_state.apply_work);
    cancel_work_sync(&vt_state.remove_work);
    del_timer_sync(&vt_state.remove_timer);
    
    // Remove overlay if active
    if (vt_state.overlay_active) {
        remove_overlay_work(&vt_state.remove_work);
    }
    
    // Free saved screen
    if (vt_state.saved_screen) {
        kfree(vt_state.saved_screen);
    }
    
    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }
    
    pr_info("VT Overlay Test unloaded\n");
}

module_init(vt_overlay_init);
module_exit(vt_overlay_exit);
