#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define MAX_PLANES 4
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

static __always_inline int extract_fb_info(struct drm_atomic_state *state)
{
    struct drm_plane_state **planes;
    struct drm_plane_state *plane_state;
    struct drm_framebuffer *fb;
    struct drm_gem_object *gem_obj;
    struct dma_buf *dma_buf;
    struct file *file;
    struct inode *inode;
    struct drm_crtc *crtc;
    struct fb_record *rec;
    u32 plane_id, crtc_id;
    u64 ino;
    int i;

    // Read planes array from atomic state
    if (bpf_core_read(&planes, sizeof(planes), &state->planes) != 0)
        return 0;

    if (!planes)
        return 0;

    // Unroll loop for up to 4 planes
    #pragma unroll
    for (i = 0; i < MAX_PLANES; i++) {
        // Read plane state pointer
        if (bpf_core_read(&plane_state, sizeof(plane_state), &planes[i]) != 0)
            continue;

        if (!plane_state)
            continue;

        // Get framebuffer
        if (bpf_core_read(&fb, sizeof(fb), &plane_state->fb) != 0)
            continue;

        if (!fb)
            continue;

        // Check if plane is visible/enabled
        if (bpf_core_read(&crtc, sizeof(crtc), &plane_state->crtc) != 0)
            continue;

        if (!crtc)
            continue;

        // Get CRTC ID
        if (bpf_core_read(&crtc_id, sizeof(crtc_id), &crtc->base.id) != 0)
            continue;

        // Get plane ID
        struct drm_plane *plane;
        if (bpf_core_read(&plane, sizeof(plane), &plane_state->plane) != 0)
            continue;
        if (!plane)
            continue;
        if (bpf_core_read(&plane_id, sizeof(plane_id), &plane->base.id) != 0)
            continue;

        // Only capture common 32-bit formats
        u32 format;
        if (bpf_core_read(&format, sizeof(format), &fb->format->format) != 0)
            continue;

        if (format != DRM_FORMAT_XRGB8888 && 
            format != DRM_FORMAT_ARGB8888 &&
            format != DRM_FORMAT_XBGR8888 && 
            format != DRM_FORMAT_ABGR8888)
            continue;

        // Get GEM object (first one, obj[0])
        if (bpf_core_read(&gem_obj, sizeof(gem_obj), &fb->obj[0]) != 0)
            continue;

        if (!gem_obj)
            continue;

        // Try to get dma_buf
        if (bpf_core_read(&dma_buf, sizeof(dma_buf), &gem_obj->dma_buf) != 0)
            continue;

        if (!dma_buf)
            continue;

        // Get file from dma_buf
        if (bpf_core_read(&file, sizeof(file), &dma_buf->file) != 0)
            continue;

        if (!file)
            continue;

        // Get inode number
        if (bpf_core_read(&inode, sizeof(inode), &file->f_inode) != 0)
            continue;

        if (!inode)
            continue;

        if (bpf_core_read(&ino, sizeof(ino), &inode->i_ino) != 0)
            continue;

        if (!ino)
            continue;

        // Reserve space in ring buffer
        rec = bpf_ringbuf_reserve(&fb_events, sizeof(*rec), 0);
        if (!rec)
            continue;

        // Fill record
        rec->ts_ns = bpf_ktime_get_ns();
        rec->crtc_id = crtc_id;
        if (bpf_core_read(&rec->width, sizeof(rec->width), &fb->width) != 0) {
            bpf_ringbuf_discard(rec, 0);
            continue;
        }
        if (bpf_core_read(&rec->height, sizeof(rec->height), &fb->height) != 0) {
            bpf_ringbuf_discard(rec, 0);
            continue;
        }
        if (bpf_core_read(&rec->pitch, sizeof(rec->pitch), &fb->pitches[0]) != 0) {
            bpf_ringbuf_discard(rec, 0);
            continue;
        }
        rec->format = format;
        rec->inode = ino;
        rec->pid = bpf_get_current_pid_tgid() >> 32;
        rec->plane_id = plane_id;

        // Submit to ring buffer
        bpf_ringbuf_submit(rec, 0);
    }

    return 0;
}

SEC("fentry/drm_atomic_helper_prepare_planes")
int BPF_PROG(trace_prepare_planes, struct drm_device *dev, struct drm_atomic_state *state)
{
    return extract_fb_info(state);
}

char LICENSE[] SEC("license") = "GPL";
