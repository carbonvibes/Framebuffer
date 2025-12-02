#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>

struct fb_record {
    u64 ts_ns;
    u32 crtc_id;
    u32 width;
    u32 height;
    u32 pitch;
    u32 format;
    u64 inode;
    u32 pid;
    u32 plane_id;
    u64 obj_inode;  // GEM object inode
    u64 dma_addr;   // DMA buffer address
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} fb_events SEC(".maps");

// Hook into drm_atomic_helper_commit_planes with fentry
SEC("fentry/drm_atomic_helper_commit_planes")
int fentry_commit_planes(struct drm_device *dev, struct drm_atomic_state *state)
{
    if (!dev || !state)
        return 0;
    
    struct fb_record *rec;
    rec = bpf_ringbuf_reserve(&fb_events, sizeof(*rec), 0);
    if (!rec)
        return 0;

    // Fill basic information
    rec->ts_ns = bpf_ktime_get_ns();
    rec->pid = bpf_get_current_pid_tgid() >> 32;
    
    // Try to extract plane information from the atomic state
    // Note: This is a simplified approach - we'd need to iterate through planes
    // For now, use reasonable defaults and indicate this is from commit_planes
    rec->crtc_id = 0;  // We'd extract this from state->crtcs
    rec->width = 1920;
    rec->height = 1080;
    rec->pitch = 1920 * 4;
    rec->format = 0x34325258; // DRM_FORMAT_XRGB8888
    rec->plane_id = 0;
    rec->inode = 0;        // Will be filled by more detailed extraction
    rec->obj_inode = 0;
    rec->dma_addr = 0;

    bpf_ringbuf_submit(rec, 0);
    return 0;
}

// Also hook drm_framebuffer_init to catch when framebuffers are created
SEC("fentry/drm_framebuffer_init")
int fentry_fb_init(struct drm_device *dev, struct drm_framebuffer *fb, void *funcs)
{
    if (!fb)
        return 0;
    
    struct fb_record *rec;
    rec = bpf_ringbuf_reserve(&fb_events, sizeof(*rec), 0);
    if (!rec)
        return 0;

    // Fill information from framebuffer
    rec->ts_ns = bpf_ktime_get_ns();
    rec->pid = bpf_get_current_pid_tgid() >> 32;
    
    // Read framebuffer properties safely
    u32 width = 0, height = 0, format = 0;
    u32 pitch = 0;
    
    bpf_probe_read_kernel(&width, sizeof(width), &fb->width);
    bpf_probe_read_kernel(&height, sizeof(height), &fb->height);
    bpf_probe_read_kernel(&pitch, sizeof(pitch), &fb->pitches[0]);
    
    rec->width = width;
    rec->height = height;
    rec->format = 0x34325258; // Default to XRGB8888
    rec->pitch = pitch;
    rec->crtc_id = 999;  // Mark as framebuffer init event
    rec->plane_id = 999;
    rec->inode = 0;
    rec->obj_inode = 0;
    rec->dma_addr = 0;

    bpf_ringbuf_submit(rec, 0);
    return 0;
}

char _license[] SEC("license") = "GPL";
