#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <drm/drm_device.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_fourcc.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simple Overlay Test");
MODULE_DESCRIPTION("Simple test for DRM overlay plane creation - no detection logic");
MODULE_VERSION("1.0");

#define PROC_NAME "simple_overlay_test"

// Simple state tracking
struct simple_overlay_state {
    struct drm_device *dev;
    struct drm_crtc *crtc;
    struct drm_plane *overlay_plane;
    struct drm_plane *cursor_plane;
    bool overlay_available;
    bool cursor_available;
    int plane_count;
};

static struct simple_overlay_state test_state;
static struct proc_dir_entry *proc_entry;
static DEFINE_MUTEX(test_mutex);

// Simple plane discovery - NO KPROBES, NO FRAME INTERCEPTION
static void discover_drm_planes(void)
{
    struct drm_device *dev;
    struct drm_plane *plane;
    struct drm_crtc *crtc;
    
    mutex_lock(&test_mutex);
    
    // Reset state
    memset(&test_state, 0, sizeof(test_state));
    
    // Try to find a DRM device by checking /sys/class/drm
    // This is a safe way without using kprobes
    
    pr_info("Simple overlay test: Looking for DRM devices...\n");
    
    // For now, we'll just log what we're trying to do
    // In a real implementation, we'd get the device safely
    
    pr_info("Note: This is a proof-of-concept showing overlay plane concepts\n");
    pr_info("In production, we would:\n");
    pr_info("1. Safely get DRM device reference\n");
    pr_info("2. Enumerate available planes\n");
    pr_info("3. Create safety overlay framebuffer\n");
    pr_info("4. Apply overlay when needed\n");
    
    test_state.plane_count = 0;
    test_state.overlay_available = false;
    test_state.cursor_available = false;
    
    mutex_unlock(&test_mutex);
}

// Simulate overlay application (safe version)
static int simulate_overlay_application(void)
{
    pr_info("=== SIMULATING SAFETY OVERLAY APPLICATION ===\n");
    pr_info("Step 1: Create 1x1 black safety framebuffer\n");
    pr_info("Step 2: Configure overlay plane to cover entire screen\n");
    pr_info("  - src_w = display_width << 16\n");
    pr_info("  - src_h = display_height << 16\n");
    pr_info("  - crtc_w = display_width\n");
    pr_info("  - crtc_h = display_height\n");
    pr_info("  - crtc_x = 0, crtc_y = 0\n");
    pr_info("Step 3: Atomic commit to apply overlay\n");
    pr_info("Step 4: Timer to remove overlay after 1 second\n");
    pr_info("=== OVERLAY SIMULATION COMPLETE ===\n");
    
    return 0;
}

// Proc file to show status and test overlay
static int simple_overlay_proc_show(struct seq_file *m, void *v)
{
    mutex_lock(&test_mutex);
    
    seq_printf(m, "Simple DRM Overlay Test Module\n");
    seq_printf(m, "=============================\n\n");
    
    seq_printf(m, "Purpose: Test if we can create overlay planes with safe content\n");
    seq_printf(m, "Status: Safe simulation mode (no actual DRM operations)\n\n");
    
    seq_printf(m, "Overlay Plane Concept:\n");
    seq_printf(m, "  - Use higher-priority DRM planes (overlay or cursor)\n");
    seq_printf(m, "  - Create 1x1 black pixel framebuffer\n");
    seq_printf(m, "  - Stretch to cover entire screen\n");
    seq_printf(m, "  - Apply via atomic commit for speed\n");
    seq_printf(m, "  - Remove after timeout\n\n");
    
    seq_printf(m, "Test State:\n");
    seq_printf(m, "  Device: %p\n", test_state.dev);
    seq_printf(m, "  CRTC: %p\n", test_state.crtc);
    seq_printf(m, "  Overlay Plane: %s\n", test_state.overlay_available ? "Available" : "Not found");
    seq_printf(m, "  Cursor Plane: %s\n", test_state.cursor_available ? "Available" : "Not found");
    seq_printf(m, "  Total Planes: %d\n", test_state.plane_count);
    
    seq_printf(m, "\nTo test overlay simulation:\n");
    seq_printf(m, "  echo 'test' > /proc/%s\n", PROC_NAME);
    
    seq_printf(m, "\nKey Insight:\n");
    seq_printf(m, "  Every modern GPU has overlay/cursor planes that sit above\n");
    seq_printf(m, "  the primary framebuffer. These can be updated independently\n");
    seq_printf(m, "  and very quickly (sub-10ms) to cover malicious content.\n");
    
    mutex_unlock(&test_mutex);
    return 0;
}

// Proc write function to trigger test
static ssize_t simple_overlay_proc_write(struct file *file, const char __user *buffer, 
                                        size_t count, loff_t *pos)
{
    char input[32];
    
    if (count >= sizeof(input))
        return -EINVAL;
    
    if (copy_from_user(input, buffer, count))
        return -EFAULT;
    
    input[count] = '\0';
    
    if (strncmp(input, "test", 4) == 0) {
        pr_info("User requested overlay test simulation\n");
        simulate_overlay_application();
    } else if (strncmp(input, "discover", 8) == 0) {
        pr_info("User requested plane discovery\n");
        discover_drm_planes();
    } else {
        pr_info("Unknown command: %s\n", input);
        pr_info("Available commands: 'test', 'discover'\n");
    }
    
    return count;
}

static int simple_overlay_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, simple_overlay_proc_show, NULL);
}

static const struct proc_ops simple_overlay_proc_ops = {
    .proc_open = simple_overlay_proc_open,
    .proc_read = seq_read,
    .proc_write = simple_overlay_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Module initialization - NO KPROBES, VERY SAFE
static int __init simple_overlay_init(void)
{
    pr_info("Simple DRM Overlay Test Module loading\n");

    // Initialize state
    memset(&test_state, 0, sizeof(test_state));

    // Create proc entry
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &simple_overlay_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry %s\n", PROC_NAME);
        return -ENOMEM;
    }

    // Safe discovery without kprobes
    discover_drm_planes();

    pr_info("Simple DRM Overlay Test Module loaded successfully\n");
    pr_info("View status: cat /proc/%s\n", PROC_NAME);
    pr_info("Test overlay: echo 'test' > /proc/%s\n", PROC_NAME);
    pr_info("This module is SAFE - no kprobes, no frame interception\n");
    
    return 0;
}

// Module cleanup
static void __exit simple_overlay_exit(void)
{
    pr_info("Simple DRM Overlay Test Module unloading\n");

    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }

    pr_info("Simple DRM Overlay Test Module unloaded safely\n");
}

module_init(simple_overlay_init);
module_exit(simple_overlay_exit);
