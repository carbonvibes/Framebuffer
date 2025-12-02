#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Userspace Overlay Test");
MODULE_DESCRIPTION("Creates userspace overlay using call_usermodehelper");
MODULE_VERSION("1.0");

#define PROC_NAME "userspace_overlay_test"

struct overlay_test_state {
    bool overlay_active;
    struct timer_list remove_timer;
    struct work_struct apply_work;
    struct work_struct remove_work;
    pid_t overlay_pid;
};

static struct overlay_test_state test_state;
static struct proc_dir_entry *proc_entry;
static DEFINE_MUTEX(test_mutex);

// Work function to apply overlay via userspace helper
static void apply_overlay_work(struct work_struct *work)
{
    char *argv[] = {
        "/bin/bash",
        "-c",
        "DISPLAY=:0 zenity --info --width=800 --height=600 --title='SAFETY OVERLAY ACTIVE' --text='<span font=\"48\" color=\"red\">*** MALICIOUS CONTENT BLOCKED ***</span>\n\nThis overlay demonstrates the safety mechanism.\nIt will auto-close in 5 seconds.' --timeout=5 2>/dev/null || "
        "DISPLAY=:0 xmessage -center -timeout 5 'SAFETY OVERLAY ACTIVE - MALICIOUS CONTENT BLOCKED' 2>/dev/null || "
        "notify-send -u critical -t 5000 'SAFETY OVERLAY' 'Malicious content blocked' 2>/dev/null || "
        "echo 'SAFETY OVERLAY: Malicious content blocked' > /dev/console",
        NULL
    };
    char *envp[] = {
        "HOME=/root",
        "PATH=/sbin:/bin:/usr/bin",
        "DISPLAY=:0",
        NULL
    };

    mutex_lock(&test_mutex);
    
    pr_info("=== APPLYING USERSPACE OVERLAY ===\n");
    pr_info("Launching overlay dialog...\n");
    
    // Try to launch overlay dialog
    call_usermodehelper(argv[0], argv, envp, UMH_NO_WAIT);
    
    test_state.overlay_active = true;
    
    // Set timer to mark overlay as removed after 5 seconds
    mod_timer(&test_state.remove_timer, jiffies + msecs_to_jiffies(5000));
    
    pr_info("=== OVERLAY DIALOG LAUNCHED - SHOULD BE VISIBLE NOW! ===\n");
    
    mutex_unlock(&test_mutex);
}

// Work function to handle overlay removal
static void remove_overlay_work(struct work_struct *work)
{
    mutex_lock(&test_mutex);
    
    if (!test_state.overlay_active) {
        goto unlock;
    }
    
    pr_info("=== OVERLAY TIMEOUT - MARKING AS REMOVED ===\n");
    test_state.overlay_active = false;
    
unlock:
    mutex_unlock(&test_mutex);
}

// Timer callback
static void remove_timer_callback(struct timer_list *timer)
{
    pr_info("Overlay timer expired\n");
    schedule_work(&test_state.remove_work);
}

// Alternative overlay using desktop notification
static void apply_notification_overlay(void)
{
    char *argv[] = {
        "/bin/bash",
        "-c",
        "for i in {1..10}; do "
        "DISPLAY=:0 notify-send -u critical -t 1000 'SAFETY OVERLAY' 'Frame $i - Malicious content blocked!' 2>/dev/null; "
        "sleep 0.5; "
        "done",
        NULL
    };
    char *envp[] = {
        "HOME=/root",
        "PATH=/sbin:/bin:/usr/bin",
        "DISPLAY=:0",
        NULL
    };

    pr_info("=== APPLYING NOTIFICATION OVERLAY ===\n");
    call_usermodehelper(argv[0], argv, envp, UMH_NO_WAIT);
    pr_info("=== NOTIFICATION OVERLAY LAUNCHED ===\n");
}

// Emergency console overlay (always visible)
static void apply_console_overlay(void)
{
    char *argv[] = {
        "/bin/bash",
        "-c",
        "echo -e '\\033[41;37m*** SAFETY OVERLAY ACTIVE - MALICIOUS CONTENT BLOCKED ***\\033[0m' > /dev/console; "
        "echo -e '\\033[41;37m*** THIS MESSAGE CONFIRMS THE OVERLAY SYSTEM WORKS ***\\033[0m' > /dev/console; "
        "for tty in /dev/tty*; do echo -e '\\033[41;37m*** SAFETY OVERLAY ***\\033[0m' > \"$tty\" 2>/dev/null || true; done",
        NULL
    };
    char *envp[] = {
        "HOME=/root",
        "PATH=/sbin:/bin:/usr/bin",
        NULL
    };

    pr_info("=== APPLYING CONSOLE OVERLAY ===\n");
    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
    pr_info("=== CONSOLE OVERLAY APPLIED ===\n");
}

// Proc file operations
static int overlay_test_proc_show(struct seq_file *m, void *v)
{
    mutex_lock(&test_mutex);
    
    seq_printf(m, "Userspace Overlay Test Module\n");
    seq_printf(m, "=============================\n\n");
    
    seq_printf(m, "Purpose: Create visible overlay using userspace helpers\n");
    seq_printf(m, "Method: call_usermodehelper() with GUI applications\n\n");
    
    seq_printf(m, "Overlay Status: %s\n", test_state.overlay_active ? "ACTIVE" : "Inactive");
    seq_printf(m, "PID: %d\n", test_state.overlay_pid);
    
    seq_printf(m, "\nCommands:\n");
    seq_printf(m, "  echo 'dialog' > /proc/%s    - Show GUI dialog overlay\n", PROC_NAME);
    seq_printf(m, "  echo 'notify' > /proc/%s    - Show notification overlay\n", PROC_NAME);
    seq_printf(m, "  echo 'console' > /proc/%s   - Show console overlay\n", PROC_NAME);
    seq_printf(m, "  echo 'all' > /proc/%s       - Try all overlay methods\n", PROC_NAME);
    
    seq_printf(m, "\nNotes:\n");
    seq_printf(m, "- GUI overlays require X11/Wayland display\n");
    seq_printf(m, "- Console overlay should always be visible in terminal\n");
    seq_printf(m, "- This demonstrates emergency overlay capabilities\n");
    
    mutex_unlock(&test_mutex);
    return 0;
}

static ssize_t overlay_test_proc_write(struct file *file, const char __user *buffer, 
                                      size_t count, loff_t *pos)
{
    char input[32];
    
    if (count >= sizeof(input))
        return -EINVAL;
    
    if (copy_from_user(input, buffer, count))
        return -EFAULT;
    
    input[count] = '\0';
    
    if (strncmp(input, "dialog", 6) == 0) {
        pr_info("=== USER REQUESTED GUI DIALOG OVERLAY ===\n");
        schedule_work(&test_state.apply_work);
    } else if (strncmp(input, "notify", 6) == 0) {
        pr_info("=== USER REQUESTED NOTIFICATION OVERLAY ===\n");
        apply_notification_overlay();
    } else if (strncmp(input, "console", 7) == 0) {
        pr_info("=== USER REQUESTED CONSOLE OVERLAY ===\n");
        apply_console_overlay();
    } else if (strncmp(input, "all", 3) == 0) {
        pr_info("=== USER REQUESTED ALL OVERLAY METHODS ===\n");
        apply_console_overlay();
        apply_notification_overlay();
        schedule_work(&test_state.apply_work);
    } else {
        pr_info("Unknown command: %s\n", input);
        pr_info("Available commands: 'dialog', 'notify', 'console', 'all'\n");
    }
    
    return count;
}

static int overlay_test_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, overlay_test_proc_show, NULL);
}

static const struct proc_ops overlay_test_proc_ops = {
    .proc_open = overlay_test_proc_open,
    .proc_read = seq_read,
    .proc_write = overlay_test_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Module initialization
static int __init overlay_test_init(void)
{
    pr_info("Userspace Overlay Test Module loading\n");
    
    // Initialize state
    memset(&test_state, 0, sizeof(test_state));
    
    // Initialize work and timer
    INIT_WORK(&test_state.apply_work, apply_overlay_work);
    INIT_WORK(&test_state.remove_work, remove_overlay_work);
    timer_setup(&test_state.remove_timer, remove_timer_callback, 0);
    
    // Create proc entry
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &overlay_test_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry\n");
        return -ENOMEM;
    }
    
    pr_info("Userspace Overlay Test Module loaded successfully\n");
    pr_info("*** THIS WILL DEFINITELY BE VISIBLE! ***\n");
    pr_info("Commands:\n");
    pr_info("  echo 'console' > /proc/%s  - Show red text in terminal\n", PROC_NAME);
    pr_info("  echo 'dialog' > /proc/%s   - Show GUI dialog box\n", PROC_NAME);
    pr_info("  echo 'all' > /proc/%s      - Try all methods at once\n", PROC_NAME);
    
    return 0;
}

// Module cleanup
static void __exit overlay_test_exit(void)
{
    pr_info("Userspace Overlay Test Module unloading\n");
    
    // Cancel work and timer
    cancel_work_sync(&test_state.apply_work);
    cancel_work_sync(&test_state.remove_work);
    del_timer_sync(&test_state.remove_timer);
    
    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }
    
    pr_info("Userspace Overlay Test Module unloaded\n");
}

module_init(overlay_test_init);
module_exit(overlay_test_exit);
