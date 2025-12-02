#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <drm/drm_device.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Visual Overlay Test");
MODULE_DESCRIPTION("Visual test for DRM overlay - displays actual blue overlay");
MODULE_VERSION("1.0");

#define PROC_NAME "visual_overlay_test"

// Visual overlay state
struct visual_overlay_state {
    struct drm_device *dev;
    struct drm_crtc *crtc;
    struct drm_plane *overlay_plane;
    struct drm_plane *cursor_plane;
    struct drm_framebuffer *blue_fb;
    struct drm_gem_object *blue_gem;
    bool overlay_active;
    struct timer_list remove_timer;
    struct work_struct apply_overlay_work;
    struct work_struct remove_overlay_work;
};

static struct visual_overlay_state visual_state;
static struct proc_dir_entry *proc_entry;
static DEFINE_MUTEX(visual_mutex);

// Find the first available DRM device
static struct drm_device *find_drm_device(void)
{
    struct drm_device *dev = NULL;
    struct class *drm_class;
    struct device *device;
    struct class_dev_iter iter;
    
    drm_class = class_find("drm");
    if (!drm_class) {
        pr_err("DRM class not found\n");
        return NULL;
    }
    
    class_dev_iter_init(&iter, drm_class, NULL, NULL);
    device = class_dev_iter_next(&iter);
    
    if (device) {
        dev = dev_get_drvdata(device);
        if (dev) {
            pr_info("Found DRM device: %s\n", dev_name(dev->dev));
        }
    }
    
    class_dev_iter_exit(&iter);
    return dev;
}

// Create a blue framebuffer
static int create_blue_framebuffer(struct drm_device *dev)
{
    struct drm_mode_fb_cmd2 mode_cmd = {};
    struct drm_gem_shmem_object *shmem_obj;
    void *vaddr;
    uint32_t *pixels;
    int ret;
    int i, total_pixels;
    
    if (!dev) return -EINVAL;
    
    pr_info("Creating blue overlay framebuffer\n");
    
    // Create a small gem buffer (128x128 pixels should be enough)
    visual_state.blue_gem = drm_gem_shmem_create(dev, 128 * 128 * 4);
    if (IS_ERR(visual_state.blue_gem)) {
        pr_err("Failed to create GEM object\n");
        return PTR_ERR(visual_state.blue_gem);
    }
    
    shmem_obj = to_drm_gem_shmem_obj(visual_state.blue_gem);
    
    // Map the buffer and fill with blue
    vaddr = drm_gem_shmem_vmap(shmem_obj);
    if (IS_ERR(vaddr)) {
        pr_err("Failed to map GEM buffer\n");
        drm_gem_object_put(visual_state.blue_gem);
        return PTR_ERR(vaddr);
    }
    
    pixels = (uint32_t *)vaddr;
    total_pixels = 128 * 128;
    
    // Fill with blue color (ARGB: 0xFF0000FF)
    for (i = 0; i < total_pixels; i++) {
        pixels[i] = 0xFF0000FF;  // Opaque blue
    }
    
    drm_gem_shmem_vunmap(shmem_obj, vaddr);
    
    // Create framebuffer
    mode_cmd.width = 128;
    mode_cmd.height = 128;
    mode_cmd.pixel_format = DRM_FORMAT_ARGB8888;
    mode_cmd.pitches[0] = 128 * 4;
    mode_cmd.handles[0] = visual_state.blue_gem->handle;
    
    visual_state.blue_fb = drm_internal_framebuffer_create(dev, &mode_cmd, NULL);
    if (IS_ERR(visual_state.blue_fb)) {
        pr_err("Failed to create framebuffer\n");
        drm_gem_object_put(visual_state.blue_gem);
        return PTR_ERR(visual_state.blue_fb);
    }
    
    pr_info("Created blue framebuffer: %dx%d\n", mode_cmd.width, mode_cmd.height);
    return 0;
}

// Work function to apply blue overlay
static void apply_blue_overlay_work(struct work_struct *work)
{
    struct drm_atomic_state *state;
    struct drm_plane_state *plane_state;
    struct drm_crtc_state *crtc_state;
    int ret;
    
    mutex_lock(&visual_mutex);
    
    if (!visual_state.dev || !visual_state.crtc || !visual_state.blue_fb) {
        pr_err("Not ready for overlay application\n");
        goto unlock;
    }
    
    pr_info("Applying BLUE OVERLAY to screen!\n");
    
    // Create atomic state
    state = drm_atomic_state_alloc(visual_state.dev);
    if (!state) {
        pr_err("Failed to allocate atomic state\n");
        goto unlock;
    }
    
    // Try overlay plane first
    if (visual_state.overlay_plane) {
        plane_state = drm_atomic_get_plane_state(state, visual_state.overlay_plane);
        if (!IS_ERR(plane_state)) {
            crtc_state = drm_atomic_get_crtc_state(state, visual_state.crtc);
            if (!IS_ERR(crtc_state)) {
                // Configure plane to cover full screen
                plane_state->fb = visual_state.blue_fb;
                plane_state->crtc = visual_state.crtc;
                plane_state->src_w = 128 << 16;  // Source size (fixed point)
                plane_state->src_h = 128 << 16;
                plane_state->src_x = 0;
                plane_state->src_y = 0;
                plane_state->crtc_w = visual_state.crtc->mode.hdisplay;  // Scale to full screen
                plane_state->crtc_h = visual_state.crtc->mode.vdisplay;
                plane_state->crtc_x = 0;
                plane_state->crtc_y = 0;
                
                pr_info("Configured overlay plane: %dx%d -> %dx%d\n",
                        128, 128, visual_state.crtc->mode.hdisplay, visual_state.crtc->mode.vdisplay);
                
                // Commit the atomic state
                ret = drm_atomic_commit(state);
                if (ret) {
                    pr_err("Failed to commit atomic state: %d\n", ret);
                } else {
                    pr_info("BLUE OVERLAY APPLIED SUCCESSFULLY!\n");
                    visual_state.overlay_active = true;
                    
                    // Set timer to remove overlay after 3 seconds
                    mod_timer(&visual_state.remove_timer, jiffies + msecs_to_jiffies(3000));
                }
            }
        }
    } else if (visual_state.cursor_plane) {
        // Fallback to cursor plane
        pr_info("Using cursor plane for overlay\n");
        plane_state = drm_atomic_get_plane_state(state, visual_state.cursor_plane);
        if (!IS_ERR(plane_state)) {
            plane_state->fb = visual_state.blue_fb;
            plane_state->crtc = visual_state.crtc;
            plane_state->src_w = 128 << 16;
            plane_state->src_h = 128 << 16;
            plane_state->src_x = 0;
            plane_state->src_y = 0;
            // Cursor planes might not support full screen scaling
            plane_state->crtc_w = 128;
            plane_state->crtc_h = 128;
            plane_state->crtc_x = 100;  // Position cursor overlay
            plane_state->crtc_y = 100;
            
            ret = drm_atomic_commit(state);
            if (ret) {
                pr_err("Failed to commit cursor atomic state: %d\n", ret);
            } else {
                pr_info("BLUE CURSOR OVERLAY APPLIED!\n");
                visual_state.overlay_active = true;
                mod_timer(&visual_state.remove_timer, jiffies + msecs_to_jiffies(3000));
            }
        }
    }
    
    // Clean up state object
    drm_atomic_state_put(state);
    
unlock:
    mutex_unlock(&visual_mutex);
}

// Work function to remove overlay
static void remove_blue_overlay_work(struct work_struct *work)
{
    struct drm_atomic_state *state;
    struct drm_plane_state *plane_state;
    int ret;
    
    mutex_lock(&visual_mutex);
    
    if (!visual_state.overlay_active) {
        goto unlock;
    }
    
    pr_info("Removing blue overlay\n");
    
    state = drm_atomic_state_alloc(visual_state.dev);
    if (!state) {
        pr_err("Failed to allocate atomic state for removal\n");
        goto unlock;
    }
    
    if (visual_state.overlay_plane) {
        plane_state = drm_atomic_get_plane_state(state, visual_state.overlay_plane);
        if (!IS_ERR(plane_state)) {
            // Disable the plane
            plane_state->fb = NULL;
            plane_state->crtc = NULL;
            
            ret = drm_atomic_commit(state);
            if (ret) {
                pr_err("Failed to remove overlay: %d\n", ret);
            } else {
                pr_info("Blue overlay removed successfully\n");
            }
        }
    } else if (visual_state.cursor_plane) {
        plane_state = drm_atomic_get_plane_state(state, visual_state.cursor_plane);
        if (!IS_ERR(plane_state)) {
            plane_state->fb = NULL;
            plane_state->crtc = NULL;
            
            ret = drm_atomic_commit(state);
            if (ret) {
                pr_err("Failed to remove cursor overlay: %d\n", ret);
            } else {
                pr_info("Blue cursor overlay removed\n");
            }
        }
    }
    
    visual_state.overlay_active = false;
    drm_atomic_state_put(state);
    
unlock:
    mutex_unlock(&visual_mutex);
}

// Timer callback to remove overlay
static void remove_timer_callback(struct timer_list *timer)
{
    pr_info("Timer expired - scheduling overlay removal\n");
    schedule_work(&visual_state.remove_overlay_work);
}

// Discover DRM planes
static void discover_planes(struct drm_device *dev)
{
    struct drm_plane *plane;
    int overlay_count = 0, cursor_count = 0;
    
    if (!dev) return;
    
    pr_info("Discovering DRM planes on device %s\n", dev_name(dev->dev));
    
    drm_for_each_plane(plane, dev) {
        pr_info("Found plane: %s (type=%d)\n", 
                plane->name ? plane->name : "unnamed", plane->type);
        
        if (plane->type == DRM_PLANE_TYPE_OVERLAY && !visual_state.overlay_plane) {
            visual_state.overlay_plane = plane;
            overlay_count++;
            pr_info("  -> Selected as overlay plane\n");
        } else if (plane->type == DRM_PLANE_TYPE_CURSOR && !visual_state.cursor_plane) {
            visual_state.cursor_plane = plane;
            cursor_count++;
            pr_info("  -> Selected as cursor plane\n");
        }
    }
    
    pr_info("Plane discovery complete: %d overlay, %d cursor\n", overlay_count, cursor_count);
}

// Initialize visual overlay system
static int init_visual_overlay(void)
{
    struct drm_crtc *crtc;
    int ret;
    
    // Find DRM device
    visual_state.dev = find_drm_device();
    if (!visual_state.dev) {
        pr_err("No DRM device found\n");
        return -ENODEV;
    }
    
    // Find primary CRTC
    drm_for_each_crtc(crtc, visual_state.dev) {
        if (crtc->enabled) {
            visual_state.crtc = crtc;
            pr_info("Using CRTC: %s (%dx%d)\n", 
                    crtc->name ? crtc->name : "unnamed",
                    crtc->mode.hdisplay, crtc->mode.vdisplay);
            break;
        }
    }
    
    if (!visual_state.crtc) {
        pr_err("No enabled CRTC found\n");
        return -ENODEV;
    }
    
    // Discover planes
    discover_planes(visual_state.dev);
    
    if (!visual_state.overlay_plane && !visual_state.cursor_plane) {
        pr_err("No overlay or cursor planes available\n");
        return -ENODEV;
    }
    
    // Create blue framebuffer
    ret = create_blue_framebuffer(visual_state.dev);
    if (ret) {
        pr_err("Failed to create blue framebuffer: %d\n", ret);
        return ret;
    }
    
    pr_info("Visual overlay system initialized successfully\n");
    return 0;
}

// Proc file operations
static int visual_overlay_proc_show(struct seq_file *m, void *v)
{
    mutex_lock(&visual_mutex);
    
    seq_printf(m, "Visual DRM Overlay Test Module\n");
    seq_printf(m, "==============================\n\n");
    
    seq_printf(m, "Purpose: Display actual blue overlay on screen for visual confirmation\n");
    seq_printf(m, "Status: %s\n\n", visual_state.dev ? "Ready" : "Not initialized");
    
    if (visual_state.dev) {
        seq_printf(m, "DRM Device: %s\n", dev_name(visual_state.dev->dev));
        
        if (visual_state.crtc) {
            seq_printf(m, "Display: %dx%d\n", 
                       visual_state.crtc->mode.hdisplay, visual_state.crtc->mode.vdisplay);
        }
        
        seq_printf(m, "Overlay Plane: %s\n", 
                   visual_state.overlay_plane ? "Available" : "None");
        seq_printf(m, "Cursor Plane: %s\n", 
                   visual_state.cursor_plane ? "Available" : "None");
        seq_printf(m, "Blue Framebuffer: %s\n", 
                   visual_state.blue_fb ? "Created" : "None");
        seq_printf(m, "Overlay Active: %s\n", 
                   visual_state.overlay_active ? "YES" : "No");
    }
    
    seq_printf(m, "\nCommands:\n");
    seq_printf(m, "  echo 'show' > /proc/%s  - Display blue overlay for 3 seconds\n", PROC_NAME);
    seq_printf(m, "  echo 'hide' > /proc/%s  - Remove overlay immediately\n", PROC_NAME);
    
    mutex_unlock(&visual_mutex);
    return 0;
}

static ssize_t visual_overlay_proc_write(struct file *file, const char __user *buffer, 
                                        size_t count, loff_t *pos)
{
    char input[32];
    
    if (count >= sizeof(input))
        return -EINVAL;
    
    if (copy_from_user(input, buffer, count))
        return -EFAULT;
    
    input[count] = '\0';
    
    if (strncmp(input, "show", 4) == 0) {
        pr_info("User requested blue overlay display\n");
        schedule_work(&visual_state.apply_overlay_work);
    } else if (strncmp(input, "hide", 4) == 0) {
        pr_info("User requested overlay removal\n");
        del_timer(&visual_state.remove_timer);
        schedule_work(&visual_state.remove_overlay_work);
    } else {
        pr_info("Unknown command: %s\n", input);
        pr_info("Available commands: 'show', 'hide'\n");
    }
    
    return count;
}

static int visual_overlay_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, visual_overlay_proc_show, NULL);
}

static const struct proc_ops visual_overlay_proc_ops = {
    .proc_open = visual_overlay_proc_open,
    .proc_read = seq_read,
    .proc_write = visual_overlay_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Module initialization
static int __init visual_overlay_init(void)
{
    int ret;
    
    pr_info("Visual DRM Overlay Test Module loading\n");
    
    // Initialize state
    memset(&visual_state, 0, sizeof(visual_state));
    
    // Initialize work queues and timer
    INIT_WORK(&visual_state.apply_overlay_work, apply_blue_overlay_work);
    INIT_WORK(&visual_state.remove_overlay_work, remove_blue_overlay_work);
    timer_setup(&visual_state.remove_timer, remove_timer_callback, 0);
    
    // Create proc entry
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &visual_overlay_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry\n");
        return -ENOMEM;
    }
    
    // Initialize overlay system
    ret = init_visual_overlay();
    if (ret) {
        pr_err("Failed to initialize visual overlay system: %d\n", ret);
        proc_remove(proc_entry);
        return ret;
    }
    
    pr_info("Visual DRM Overlay Module loaded successfully\n");
    pr_info("Test blue overlay: echo 'show' > /proc/%s\n", PROC_NAME);
    pr_info("READY TO DISPLAY BLUE OVERLAY!\n");
    
    return 0;
}

// Module cleanup
static void __exit visual_overlay_exit(void)
{
    pr_info("Visual DRM Overlay Module unloading\n");
    
    // Cancel work and timer
    cancel_work_sync(&visual_state.apply_overlay_work);
    cancel_work_sync(&visual_state.remove_overlay_work);
    del_timer_sync(&visual_state.remove_timer);
    
    // Remove overlay if active
    if (visual_state.overlay_active) {
        remove_blue_overlay_work(&visual_state.remove_overlay_work);
    }
    
    // Clean up framebuffer and GEM object
    if (visual_state.blue_fb) {
        drm_framebuffer_put(visual_state.blue_fb);
    }
    if (visual_state.blue_gem) {
        drm_gem_object_put(visual_state.blue_gem);
    }
    
    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }
    
    pr_info("Visual DRM Overlay Module unloaded\n");
}

module_init(visual_overlay_init);
module_exit(visual_overlay_exit);
