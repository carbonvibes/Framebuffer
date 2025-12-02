#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kprobes.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_atomic.h>
#include <drm/drm_device.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Overlay Safety Module");
MODULE_DESCRIPTION("DRM overlay safety module for malicious frame protection");
MODULE_VERSION("1.0");

#define PROC_NAME "overlay_safety"
#define MAX_CAPTURES 10
#define SAFETY_OVERLAY_COLOR 0xFF000000  // Solid black overlay
#define MALICIOUS_THRESHOLD 50           // Simple threshold for demo

// Safety overlay state
struct safety_overlay_state {
    struct drm_device *dev;
    struct drm_crtc *crtc;
    struct drm_plane *overlay_plane;
    struct drm_plane *cursor_plane;
    struct drm_framebuffer *safety_fb;
    bool overlay_active;
    bool emergency_mode;
    struct work_struct overlay_work;
    struct timer_list safety_timer;
};

// Frame analysis data
struct frame_analysis {
    struct drm_framebuffer *fb;
    struct drm_device *dev;
    uint64_t timestamp;
    bool is_malicious;
    int malicious_score;
    uint32_t width, height;
    uint32_t format;
    bool analyzed;
};

static struct safety_overlay_state safety_state;
static struct frame_analysis frame_captures[MAX_CAPTURES];
static int capture_count = 0;
static int current_index = 0;
static DEFINE_MUTEX(safety_mutex);
static struct proc_dir_entry *proc_entry;

// Simple malicious frame detection (placeholder)
static bool is_frame_malicious(struct drm_framebuffer *fb)
{
    // This is a placeholder for actual detection logic
    // In a real implementation, this would analyze pixel data for:
    // - Excessive brightness changes
    // - Rapid color flashing
    // - High contrast patterns
    // - Seizure-inducing patterns
    
    if (!fb) return false;
    
    // Simple heuristic: consider frames with certain dimensions potentially problematic
    if (fb->width > 1920 && fb->height > 1080) {
        return false; // Large frames are typically safe
    }
    
    // Simulate detection based on format (for demo purposes)
    if (fb->format && fb->format->format == DRM_FORMAT_RGB565) {
        return true; // Flag RGB565 as potentially malicious for demo
    }
    
    // Random detection for demonstration (replace with real logic)
    return (capture_count % 5 == 0); // Every 5th frame for demo
}

// Create a safety overlay framebuffer (1x1 black pixel that will be stretched)
static struct drm_framebuffer *create_safety_framebuffer(struct drm_device *dev)
{
    // This is a simplified version - in practice, you'd need to:
    // 1. Allocate GEM buffer
    // 2. Create framebuffer with drm_framebuffer_init
    // 3. Fill with safety color
    
    pr_info("Creating safety overlay framebuffer (placeholder)\n");
    
    // Placeholder - real implementation would create actual FB
    return NULL;
}

// Work function to apply safety overlay
static void apply_safety_overlay_work(struct work_struct *work)
{
    struct safety_overlay_state *state = container_of(work, struct safety_overlay_state, overlay_work);
    struct drm_atomic_state *atomic_state;
    struct drm_plane_state *plane_state;
    int ret;
    
    if (!state->dev || !state->crtc) {
        pr_warn("No device or CRTC available for safety overlay\n");
        return;
    }
    
    pr_info("Applying emergency safety overlay\n");
    
    // Create atomic state for the overlay commit
    atomic_state = drm_atomic_state_alloc(state->dev);
    if (!atomic_state) {
        pr_err("Failed to allocate atomic state\n");
        return;
    }
    
    // Try to use overlay plane first, then cursor plane as fallback
    if (state->overlay_plane) {
        plane_state = drm_atomic_get_plane_state(atomic_state, state->overlay_plane);
        if (!IS_ERR(plane_state)) {
            // Configure overlay plane to cover entire screen
            plane_state->src_w = state->crtc->mode.hdisplay << 16;
            plane_state->src_h = state->crtc->mode.vdisplay << 16;
            plane_state->crtc_w = state->crtc->mode.hdisplay;
            plane_state->crtc_h = state->crtc->mode.vdisplay;
            plane_state->crtc_x = 0;
            plane_state->crtc_y = 0;
            plane_state->src_x = 0;
            plane_state->src_y = 0;
            
            // Set the safety framebuffer (if available)
            if (state->safety_fb) {
                plane_state->fb = state->safety_fb;
                plane_state->crtc = state->crtc;
            }
            
            pr_info("Configured overlay plane: %dx%d covering entire screen\n",
                    state->crtc->mode.hdisplay, state->crtc->mode.vdisplay);
        }
    } else if (state->cursor_plane) {
        plane_state = drm_atomic_get_plane_state(atomic_state, state->cursor_plane);
        if (!IS_ERR(plane_state)) {
            // Configure cursor plane as emergency overlay
            plane_state->src_w = state->crtc->mode.hdisplay << 16;
            plane_state->src_h = state->crtc->mode.vdisplay << 16;
            plane_state->crtc_w = state->crtc->mode.hdisplay;
            plane_state->crtc_h = state->crtc->mode.vdisplay;
            plane_state->crtc_x = 0;
            plane_state->crtc_y = 0;
            
            pr_info("Using cursor plane as emergency overlay: %dx%d\n",
                    state->crtc->mode.hdisplay, state->crtc->mode.vdisplay);
        }
    }
    
    // Commit the atomic state (this would be the actual DRM commit)
    // In a real implementation, you'd call drm_atomic_commit()
    pr_info("Emergency overlay atomic commit would be executed here\n");
    
    // For demo purposes, just log the action
    state->overlay_active = true;
    state->emergency_mode = true;
    
    // Clean up
    drm_atomic_state_put(atomic_state);
    
    // Set timer to remove overlay after safety period
    mod_timer(&state->safety_timer, jiffies + msecs_to_jiffies(1000)); // 1 second
}

// Timer callback to remove safety overlay
static void safety_timer_callback(struct timer_list *timer)
{
    struct safety_overlay_state *state = container_of(timer, struct safety_overlay_state, safety_timer);
    
    pr_info("Safety timer expired, removing emergency overlay\n");
    
    mutex_lock(&safety_mutex);
    state->overlay_active = false;
    state->emergency_mode = false;
    mutex_unlock(&safety_mutex);
    
    // In real implementation, would commit atomic state to disable overlay
    pr_info("Emergency overlay would be disabled here\n");
}

// Find available overlay and cursor planes
static void discover_available_planes(struct drm_device *dev)
{
    struct drm_plane *plane;
    
    safety_state.overlay_plane = NULL;
    safety_state.cursor_plane = NULL;
    
    drm_for_each_plane(plane, dev) {
        if (plane->type == DRM_PLANE_TYPE_OVERLAY && !safety_state.overlay_plane) {
            safety_state.overlay_plane = plane;
            pr_info("Found overlay plane: %s\n", plane->name ? plane->name : "unnamed");
        } else if (plane->type == DRM_PLANE_TYPE_CURSOR && !safety_state.cursor_plane) {
            safety_state.cursor_plane = plane;
            pr_info("Found cursor plane: %s\n", plane->name ? plane->name : "unnamed");
        }
    }
    
    if (!safety_state.overlay_plane && !safety_state.cursor_plane) {
        pr_warn("No overlay or cursor planes found for safety mechanism\n");
    }
}

// Analyze and potentially trigger safety overlay
static void analyze_and_protect(struct drm_framebuffer *fb, struct drm_device *dev)
{
    struct frame_analysis *analysis;
    bool trigger_protection = false;
    
    mutex_lock(&safety_mutex);
    
    // Get current analysis slot
    analysis = &frame_captures[current_index];
    
    // Initialize analysis
    memset(analysis, 0, sizeof(*analysis));
    analysis->fb = fb;
    analysis->dev = dev;
    analysis->timestamp = ktime_get_ns();
    analysis->width = fb->width;
    analysis->height = fb->height;
    analysis->format = fb->format ? fb->format->format : 0;
    
    // Perform malicious frame detection
    analysis->is_malicious = is_frame_malicious(fb);
    analysis->malicious_score = analysis->is_malicious ? MALICIOUS_THRESHOLD + 10 : 10;
    analysis->analyzed = true;
    
    // Update counters
    current_index = (current_index + 1) % MAX_CAPTURES;
    if (capture_count < MAX_CAPTURES) {
        capture_count++;
    }
    
    // Check if we should trigger protection
    if (analysis->is_malicious && !safety_state.emergency_mode) {
        trigger_protection = true;
        pr_warn("MALICIOUS FRAME DETECTED! Triggering safety overlay\n");
        pr_info("Frame: %dx%d, format=0x%08x, score=%d\n",
                analysis->width, analysis->height, analysis->format, analysis->malicious_score);
    }
    
    mutex_unlock(&safety_mutex);
    
    // Trigger safety overlay if needed (outside of mutex to avoid deadlock)
    if (trigger_protection) {
        schedule_work(&safety_state.overlay_work);
    }
}

// Kprobe handler for DRM framebuffer operations
static int handler_drm_framebuffer_init(struct kprobe *p, struct pt_regs *regs)
{
    struct drm_device *dev;
    struct drm_framebuffer *fb;
    
    // Extract parameters based on architecture
#ifdef CONFIG_X86_64
    dev = (struct drm_device *)regs->di;
    fb = (struct drm_framebuffer *)regs->si;
#elif defined(CONFIG_ARM64)
    dev = (struct drm_device *)regs->regs[0];
    fb = (struct drm_framebuffer *)regs->regs[1];
#else
    return 0;
#endif

    if (!dev || !fb) {
        return 0;
    }

    // Store device reference for safety overlay
    if (!safety_state.dev) {
        safety_state.dev = dev;
        
        // Discover available CRTCs and planes
        if (!list_empty(&dev->mode_config.crtc_list)) {
            safety_state.crtc = list_first_entry(&dev->mode_config.crtc_list, 
                                                struct drm_crtc, head);
            pr_info("Using CRTC: %s\n", safety_state.crtc->name ? safety_state.crtc->name : "unnamed");
        }
        
        discover_available_planes(dev);
    }

    pr_info("Intercepted framebuffer: %dx%d, format=0x%08x\n", 
            fb->width, fb->height, fb->format ? fb->format->format : 0);

    // Analyze frame and potentially trigger protection
    analyze_and_protect(fb, dev);
    
    return 0;
}

// Kprobe structure
static struct kprobe kp_drm_fb_init = {
    .symbol_name = "drm_framebuffer_init",
    .pre_handler = handler_drm_framebuffer_init,
};

// Proc file to show status and statistics
static int overlay_safety_proc_show(struct seq_file *m, void *v)
{
    int i;
    int malicious_count = 0;
    
    mutex_lock(&safety_mutex);
    
    seq_printf(m, "DRM Overlay Safety Module Status\n");
    seq_printf(m, "=================================\n\n");
    
    seq_printf(m, "Safety State:\n");
    seq_printf(m, "  Device: %p\n", safety_state.dev);
    seq_printf(m, "  CRTC: %p (%s)\n", safety_state.crtc, 
               safety_state.crtc && safety_state.crtc->name ? safety_state.crtc->name : "unnamed");
    seq_printf(m, "  Overlay Plane: %p (%s)\n", safety_state.overlay_plane,
               safety_state.overlay_plane && safety_state.overlay_plane->name ? safety_state.overlay_plane->name : "none");
    seq_printf(m, "  Cursor Plane: %p (%s)\n", safety_state.cursor_plane,
               safety_state.cursor_plane && safety_state.cursor_plane->name ? safety_state.cursor_plane->name : "none");
    seq_printf(m, "  Emergency Mode: %s\n", safety_state.emergency_mode ? "ACTIVE" : "inactive");
    seq_printf(m, "  Overlay Active: %s\n", safety_state.overlay_active ? "YES" : "no");
    
    if (safety_state.crtc) {
        seq_printf(m, "  Display Mode: %dx%d\n", 
                   safety_state.crtc->mode.hdisplay, safety_state.crtc->mode.vdisplay);
    }
    
    seq_printf(m, "\nFrame Analysis (%d captures):\n", capture_count);
    
    for (i = 0; i < capture_count; i++) {
        struct frame_analysis *analysis = &frame_captures[i];
        
        if (!analysis->analyzed) continue;
        
        if (analysis->is_malicious) malicious_count++;
        
        seq_printf(m, "  Frame %d:\n", i);
        seq_printf(m, "    Timestamp: %llu ns\n", analysis->timestamp);
        seq_printf(m, "    Dimensions: %dx%d\n", analysis->width, analysis->height);
        seq_printf(m, "    Format: 0x%08x\n", analysis->format);
        seq_printf(m, "    Malicious: %s (score: %d)\n", 
                   analysis->is_malicious ? "YES" : "no", analysis->malicious_score);
    }
    
    seq_printf(m, "\nStatistics:\n");
    seq_printf(m, "  Total frames analyzed: %d\n", capture_count);
    seq_printf(m, "  Malicious frames detected: %d\n", malicious_count);
    seq_printf(m, "  Detection rate: %d%%\n", 
               capture_count > 0 ? (malicious_count * 100) / capture_count : 0);
    
    seq_printf(m, "\nSafety Mechanism:\n");
    seq_printf(m, "  Available planes: %s\n", 
               safety_state.overlay_plane ? "Overlay + Cursor" :
               safety_state.cursor_plane ? "Cursor only" : "None");
    seq_printf(m, "  Emergency responses: Available\n");
    seq_printf(m, "  Response time: <10ms (target)\n");
    
    mutex_unlock(&safety_mutex);
    return 0;
}

static int overlay_safety_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, overlay_safety_proc_show, NULL);
}

static const struct proc_ops overlay_safety_proc_ops = {
    .proc_open = overlay_safety_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Module initialization
static int __init overlay_safety_init(void)
{
    int ret;

    pr_info("DRM Overlay Safety Module loading\n");

    // Initialize safety state
    memset(&safety_state, 0, sizeof(safety_state));
    memset(frame_captures, 0, sizeof(frame_captures));
    
    // Initialize work queue and timer
    INIT_WORK(&safety_state.overlay_work, apply_safety_overlay_work);
    timer_setup(&safety_state.safety_timer, safety_timer_callback, 0);

    // Register kprobe
    ret = register_kprobe(&kp_drm_fb_init);
    if (ret < 0) {
        pr_err("Failed to register kprobe: %d\n", ret);
        return ret;
    }

    // Create proc entry
    proc_entry = proc_create(PROC_NAME, 0644, NULL, &overlay_safety_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create proc entry %s\n", PROC_NAME);
        unregister_kprobe(&kp_drm_fb_init);
        return -ENOMEM;
    }

    pr_info("DRM Overlay Safety Module loaded successfully\n");
    pr_info("Monitor status: cat /proc/%s\n", PROC_NAME);
    pr_info("Emergency overlay system ready\n");
    
    return 0;
}

// Module cleanup
static void __exit overlay_safety_exit(void)
{
    pr_info("DRM Overlay Safety Module unloading\n");

    // Cancel any pending work
    cancel_work_sync(&safety_state.overlay_work);
    
    // Delete timer
    del_timer_sync(&safety_state.safety_timer);

    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }

    // Unregister kprobe
    unregister_kprobe(&kp_drm_fb_init);

    // Clean up safety state
    mutex_lock(&safety_mutex);
    memset(&safety_state, 0, sizeof(safety_state));
    capture_count = 0;
    current_index = 0;
    mutex_unlock(&safety_mutex);

    pr_info("DRM Overlay Safety Module unloaded\n");
}

module_init(overlay_safety_init);
module_exit(overlay_safety_exit);
