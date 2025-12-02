#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>

#define DRM_FORMAT_XRGB8888 0x34325258
#define DRM_FORMAT_ARGB8888 0x34325241
#define DRM_FORMAT_XBGR8888 0x34324258
#define DRM_FORMAT_ABGR8888 0x34324241

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
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} fb_events SEC(".maps");

// Minimal struct definitions for kernel data access
struct drm_gem_object {
    struct dma_buf *dma_buf;
} __attribute__((packed));

struct drm_i915_gem_object {
    struct drm_gem_object base;
} __attribute__((packed));

struct drm_framebuffer {
    u32 width;
    u32 height;
    u32 pitches[4];
    u32 format;
} __attribute__((packed));

struct drm_plane_state {
    struct drm_plane *plane;
    struct drm_crtc *crtc;
    struct drm_framebuffer *fb;
} __attribute__((packed));

struct drm_plane {
    u32 index;
} __attribute__((packed));

struct drm_crtc {
    u32 index;
} __attribute__((packed));

struct drm_atomic_state {
    struct drm_plane_state **plane_states;
    int num_planes;
} __attribute__((packed));

SEC("kprobe/drm_atomic_helper_commit_planes")
int kprobe_commit_planes(struct pt_regs *ctx)
{
    struct drm_device *dev = (struct drm_device *)PT_REGS_PARM1(ctx);
    struct drm_atomic_state *old_state = (struct drm_atomic_state *)PT_REGS_PARM2(ctx);
    
    if (!dev || !old_state)
        return 0;

    struct fb_record *rec;
    rec = bpf_ringbuf_reserve(&fb_events, sizeof(*rec), 0);
    if (!rec)
        return 0;

    rec->ts_ns = bpf_ktime_get_ns();
    rec->pid = bpf_get_current_pid_tgid() >> 32;

    // Read the number of planes from atomic state
    int num_planes = 0;
    if (bpf_probe_read_kernel(&num_planes, sizeof(num_planes), &old_state->num_planes) != 0) {
        bpf_ringbuf_discard(rec, 0);
        return 0;
    }

    // We'll process the first plane we find with a framebuffer
    // In a more complete implementation, we'd iterate through all planes
    if (num_planes > 0) {
        struct drm_plane_state **plane_states;
        if (bpf_probe_read_kernel(&plane_states, sizeof(plane_states), &old_state->plane_states) == 0 && plane_states) {
            
            struct drm_plane_state *plane_state;
            if (bpf_probe_read_kernel(&plane_state, sizeof(plane_state), &plane_states[0]) == 0 && plane_state) {
                
                // Read framebuffer info
                struct drm_framebuffer *fb;
                if (bpf_probe_read_kernel(&fb, sizeof(fb), &plane_state->fb) == 0 && fb) {
                    
                    // Extract framebuffer properties
                    bpf_probe_read_kernel(&rec->width, sizeof(rec->width), &fb->width);
                    bpf_probe_read_kernel(&rec->height, sizeof(rec->height), &fb->height);
                    bpf_probe_read_kernel(&rec->pitch, sizeof(rec->pitch), &fb->pitches[0]);
                    bpf_probe_read_kernel(&rec->format, sizeof(rec->format), &fb->format);
                    
                    // Get CRTC info
                    struct drm_crtc *crtc;
                    if (bpf_probe_read_kernel(&crtc, sizeof(crtc), &plane_state->crtc) == 0 && crtc) {
                        bpf_probe_read_kernel(&rec->crtc_id, sizeof(rec->crtc_id), &crtc->index);
                    }
                    
                    // Get plane info
                    struct drm_plane *plane;
                    if (bpf_probe_read_kernel(&plane, sizeof(plane), &plane_state->plane) == 0 && plane) {
                        bpf_probe_read_kernel(&rec->plane_id, sizeof(rec->plane_id), &plane->index);
                    }
                    
                    // Try to get inode from DMA-BUF
                    // This is tricky and might not always work depending on how the buffer was allocated
                    rec->inode = 0; // Default value
                    
                    // Only submit if we have valid dimensions and format
                    if (rec->width > 0 && rec->height > 0 && 
                        (rec->format == DRM_FORMAT_XRGB8888 || rec->format == DRM_FORMAT_ARGB8888 ||
                         rec->format == DRM_FORMAT_XBGR8888 || rec->format == DRM_FORMAT_ABGR8888)) {
                        bpf_ringbuf_submit(rec, 0);
                        return 0;
                    }
                }
            }
        }
    }

    bpf_ringbuf_discard(rec, 0);
    return 0;
}

char _license[] SEC("license") = "GPL";
