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
#include <linux/selection.h>
#include <linux/tty.h>
#include <linux/kd.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Console Overlay Test");
MODULE_DESCRIPTION("Direct console overlay test - WILL BE VISIBLE");
MODULE_VERSION("1.0");

#define PROC_NAME "console_overlay_test"

struct console_overlay_state {
    bool overlay_active;
    struct timer_list remove_timer;
    struct work_struct apply_work;
    struct work_struct remove_work;
    int original_mode;
    char *saved_screen;
    int screen_size;
};

static struct console_overlay_state overlay_state;
static struct proc_dir_entry *proc_entry;
static DEFINE_MUTEX(overlay_mutex);

// Clear screen with color (using ANSI escape codes)
static void clear_screen_with_color(const char *color_code)
{
    struct tty_struct *tty;
    char command[64];
    int len;
    
    // Get the current console tty
    tty = get_current_tty();
    if (!tty) {
        pr_err("No current TTY available\n");
        return;
    }
    
    // Create ANSI command to clear screen with color
    len = snprintf(command, sizeof(command), 
                   "\033[2J\033[H\033[%sm%*s\033[0m", 
                   color_code, 80*25, "");
    
    // Send to console
    if (tty->ops && tty->ops->write) {
        tty->ops->write(tty, command, len);
    }
    
    tty_kref_put(tty);
}

// Print large colored message
static void print_overlay_message(const char *bg_color, const char *text_color, const char *message)
{
    struct tty_struct *tty;
    char buffer[1024];
    int len;
    int i;
    
    tty = get_current_tty();
    if (!tty) {
        pr_err("No current TTY available\n");
        return;
    }
    
    // Clear screen first
    len = snprintf(buffer, sizeof(buffer), "\033[2J\033[H");
    if (tty->ops && tty->ops->write) {
        tty->ops->write(tty, buffer, len);
    }
    
    // Print colored background
    for (i = 0; i < 25; i++) {  // 25 lines
        len = snprintf(buffer, sizeof(buffer), 
                       "\033[%s;%sm%80s\033[0m\n", 
                       bg_color, text_color, "");
        if (tty->ops && tty->ops->write) {
            tty->ops->write(tty, buffer, len);
        }
    }
    
    // Position cursor in middle and print message
    len = snprintf(buffer, sizeof(buffer), 
                   "\033[12;20H\033[%s;%sm%s\033[0m", 
                   bg_color, text_color, message);
    if (tty->ops && tty->ops->write) {
        tty->ops->write(tty, buffer, len);
    }
    
    tty_kref_put(tty);
}

// Switch to text console
static void switch_to_console(void)
{
    // Try to switch to console 1
    console_lock();
    set_console(0);  // Switch to tty0/console1
    console_unlock();
}

// Work function to apply overlay
static void apply_overlay_work(struct work_struct *work)
{
    mutex_lock(&overlay_mutex);
    
    pr_info("=== APPLYING CONSOLE OVERLAY ===\n");
    
    // Switch to console mode
    switch_to_console();
    
    // Wait a bit for console switch
    msleep(100);
    
    // Apply blue background overlay
    print_overlay_message("44", "37", "*** SAFETY OVERLAY ACTIVE ***");
    
    overlay_state.overlay_active = true;
    
    // Set timer to remove after 5 seconds
    mod_timer(&overlay_state.remove_timer, jiffies + msecs_to_jiffies(5000));
    
    pr_info("=== BLUE CONSOLE OVERLAY APPLIED ===\n");
    pr_info("*** YOU SHOULD SEE BLUE SCREEN ON CONSOLE! ***\n");
    
    mutex_unlock(&overlay_mutex);
}

// Work function to remove overlay
static void remove_overlay_work(struct work_struct *work)
{
    struct tty_struct *tty;
    char clear_cmd[] = "\033[2J\033[H\033[0m";
    
    mutex_lock(&overlay_mutex);
    
    if (!overlay_state.overlay_active) {
        goto unlock;
    }
    
    pr_info("=== REMOVING CONSOLE OVERLAY ===\n");
    
    // Clear screen and reset
    tty = get_current_tty();
    if (tty) {
        if (tty->ops && tty->ops->write) {
            tty->ops->write(tty, clear_cmd, strlen(clear_cmd));
        }
        tty_kref_put(tty);
    }
    
    overlay_state.overlay_active = false;
    
    pr_info("=== CONSOLE OVERLAY REMOVED ===\n");
    
unlock:
    mutex_unlock(&overlay_mutex);
}

// Timer callback
static void remove_timer_callback(struct timer_list *timer)
{
    pr_info("Timer expired - removing console overlay\n");
    schedule_work(&overlay_state.remove_work);
}

// Alternative method using VT switching
static void apply_vt_overlay(void)
{
    struct vc_data *vc;
    int i;
    u16 blue_char;
    
    pr_info("Applying VT overlay method\n");
    
    console_lock();
    
    vc = vc_cons[fg_console].d;
    if (vc && vc->vc_screenbuf) {
        // Blue background, white text
        blue_char = (0x1F << 8) | ' ';  // Blue bg, white fg, space char
        
        // Fill screen with blue
        for (i = 0; i < vc->vc_screenbuf_size / 2; i++) {
            vc->vc_screenbuf[i] = blue_char;
        }
        
        // Update display
        if (vc->vc_sw && vc->vc_sw->con_putcs) {
            vc->vc_sw->con_putcs(vc, vc->vc_screenbuf, 
                                vc->vc_cols * vc->vc_rows, 0, 0);
        }
        
        // Force update
        if (vc->vc_sw && vc->vc_sw->con_switch) {
            vc->vc_sw->con_switch(vc);
        }
        
        pr_info("VT overlay applied: filled %d chars with blue\n", 
                vc->vc_screenbuf_size / 2);
    }
    
    console_unlock();
}

// Proc file operations
static int console_overlay_proc_show(struct seq_file *m, void *v)
{
    mutex_lock(&overlay_mutex);
    
    seq_printf(m, "Console Overlay Test Module\n");
    seq_printf(m, "===========================\n\n");
    
    seq_printf(m, "Purpose: Create VISIBLE overlay using console/VT system\n");
    seq_printf(m, "Method: Direct console manipulation and VT switching\n\n");
    
    seq_printf(m, "Current console: %d\n", fg_console);
    seq_printf(m, "Overlay status: %s\n", overlay_state.overlay_active ? "ACTIVE" : "Inactive");
    
    seq_printf(m, "\nCommands:\n");
    seq_printf(m, "  echo 'overlay' > /proc/%s  - Apply console overlay\n", PROC_NAME);
    seq_printf(m, "  echo 'vt' > /proc/%s       - Apply VT overlay\n", PROC_NAME);
    seq_printf(m, "  echo 'clear' > /proc/%s    - Remove overlay\n", PROC_NAME);
    
    seq_printf(m, "\nNote: This WILL be visible on your screen!\n");
    seq_printf(m, "The overlay will appear on the console/terminal.\n");
    
    mutex_unlock(&overlay_mutex);
    return 0;
}

static ssize_t console_overlay_proc_write(struct file *file, const char __user *buffer, 
                                         size_t count, loff_t *pos)
{
    char input[32];
    
    if (count >= sizeof(input))
        return -EINVAL;
    
    if (copy_from_user(input, buffer, count))
        return -EFAULT;
    
    input[count] = '\0';
    
    if (strncmp(input, "overlay", 7) == 0) {
        pr_info("=== USER REQUESTED CONSOLE OVERLAY ===\n");
        schedule_work(&overlay_state.apply_work);
    } else if (strncmp(input, "vt", 2) == 0) {
        pr_info("=== USER REQUESTED VT OVERLAY ===\n");
        apply_vt_overlay();
        overlay_state.overlay_active = true;
        mod_timer(&overlay_state.remove_timer, jiffies + msecs_to_jiffies(5000));
    } else if (strncmp(input, "clear", 5) == 0) {
        pr_info("=== USER REQUESTED OVERLAY REMOVAL ===\n");
        del_timer(&overlay_state.remove_timer);
        schedule_work(&overlay_state.remove_work);
    } else {
        pr_info("Unknown command: %s\n", input);
        pr_info("Available commands: 'overlay', 'vt', 'clear'\n");
    }
    
    return count;
}

static int console_overlay_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, console_overlay_proc_show, NULL);
}

static const struct proc_ops console_overlay_proc_ops = {
    .proc_open = console_overlay_proc_open,
    .proc_read = seq_read,
    .proc_write = console_overlay_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Module initialization
static int __init console_overlay_init(void)
{
    pr_info("Console Overlay Test Module loading\n");
    
    // Initialize state
    memset(&overlay_state, 0, sizeof(overlay_state));
    
    // Initialize work and timer
    INIT_WORK(&overlay_state.apply_work, apply_overlay_work);
    INIT_WORK(&overlay_state.remove_work, remove_overlay_work);
    timer_setup(&overlay_state.remove_timer, remove_timer_callback, 0);
    
    // Create proc entry
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &console_overlay_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry\n");
        return -ENOMEM;
    }
    
    pr_info("Console Overlay Test loaded successfully\n");
    pr_info("*** TO TEST: echo 'vt' > /proc/%s ***\n", PROC_NAME);
    pr_info("*** THIS WILL ACTUALLY BE VISIBLE! ***\n");
    
    return 0;
}

// Module cleanup
static void __exit console_overlay_exit(void)
{
    pr_info("Console Overlay Test unloading\n");
    
    // Cancel work and timer
    cancel_work_sync(&overlay_state.apply_work);
    cancel_work_sync(&overlay_state.remove_work);
    del_timer_sync(&overlay_state.remove_timer);
    
    // Remove overlay if active
    if (overlay_state.overlay_active) {
        remove_overlay_work(&overlay_state.remove_work);
    }
    
    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }
    
    pr_info("Console Overlay Test unloaded\n");
}

module_init(console_overlay_init);
module_exit(console_overlay_exit);
