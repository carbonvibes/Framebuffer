#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

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

// Minimal struct definitions for our use case
struct drm_atomic_state_minimal {
    void **planes;  // drm_plane_state **planes
} __attribute__((packed));

struct drm_plane_state_minimal {
    void *plane;        // struct drm_plane *
    void *crtc;         // struct drm_crtc *
    void *fb;           // struct drm_framebuffer *
} __attribute__((packed));

struct drm_framebuffer_minimal {
    u32 width;
    u32 height;
    u32 pitches[4];
    void *format;       // struct drm_format_info *
    void *obj[4];       // struct drm_gem_object *
} __attribute__((packed));

struct drm_format_info_minimal {
    u32 format;
} __attribute__((packed));

struct drm_gem_object_minimal {
    void *dma_buf;      // struct dma_buf *
} __attribute__((packed));

struct dma_buf_minimal {
    void *file;         // struct file *
} __attribute__((packed));

struct file_minimal {
    void *f_inode;      // struct inode *
} __attribute__((packed));

struct inode_minimal {
    unsigned long i_ino;
} __attribute__((packed));

struct drm_mode_object_minimal {
    u32 id;
} __attribute__((packed));

struct drm_crtc_minimal {
    struct drm_mode_object_minimal base;
} __attribute__((packed));

struct drm_plane_minimal {
    struct drm_mode_object_minimal base;
} __attribute__((packed));

static __always_inline int extract_fb_info_from_state(void *state_ptr)
{
    struct drm_atomic_state_minimal *state = (struct drm_atomic_state_minimal *)state_ptr;
    struct drm_plane_state_minimal *plane_state;
    struct drm_framebuffer_minimal *fb;
    struct drm_gem_object_minimal *gem_obj;
    struct dma_buf_minimal *dma_buf;
    struct file_minimal *file;
    struct inode_minimal *inode;
    struct drm_crtc_minimal *crtc;
    struct drm_plane_minimal *plane;
    struct drm_format_info_minimal *format_info;
    struct fb_record *rec;
    void **planes_array;
    u32 plane_id, crtc_id, format;
    u64 ino;
    int i;

    // Read planes array pointer
    if (bpf_probe_read_kernel(&planes_array, sizeof(planes_array), &state->planes) != 0)
        return 0;

    if (!planes_array)
        return 0;

    // Unroll loop for up to 4 planes
    #pragma unroll
    for (i = 0; i < MAX_PLANES; i++) {
        // Read plane state pointer
        if (bpf_probe_read_kernel(&plane_state, sizeof(plane_state), &planes_array[i]) != 0)
            continue;

        if (!plane_state)
            continue;

        // Get framebuffer
        if (bpf_probe_read_kernel(&fb, sizeof(fb), &plane_state->fb) != 0)
            continue;

        if (!fb)
            continue;

        // Check if plane is visible/enabled
        if (bpf_probe_read_kernel(&crtc, sizeof(crtc), &plane_state->crtc) != 0)
            continue;

        if (!crtc)
            continue;

        // Get CRTC ID
        if (bpf_probe_read_kernel(&crtc_id, sizeof(crtc_id), &crtc->base.id) != 0)
            continue;

        // Get plane ID
        if (bpf_probe_read_kernel(&plane, sizeof(plane), &plane_state->plane) != 0)
            continue;
        if (!plane)
            continue;
        if (bpf_probe_read_kernel(&plane_id, sizeof(plane_id), &plane->base.id) != 0)
            continue;

        // Get format
        if (bpf_probe_read_kernel(&format_info, sizeof(format_info), &fb->format) != 0)
            continue;
        if (!format_info)
            continue;
        if (bpf_probe_read_kernel(&format, sizeof(format), &format_info->format) != 0)
            continue;

        // Only capture common 32-bit formats
        if (format != DRM_FORMAT_XRGB8888 && 
            format != DRM_FORMAT_ARGB8888 &&
            format != DRM_FORMAT_XBGR8888 && 
            format != DRM_FORMAT_ABGR8888)
            continue;

        // Get GEM object (first one, obj[0])
        if (bpf_probe_read_kernel(&gem_obj, sizeof(gem_obj), &fb->obj[0]) != 0)
            continue;

        if (!gem_obj)
            continue;

        // Try to get dma_buf
        if (bpf_probe_read_kernel(&dma_buf, sizeof(dma_buf), &gem_obj->dma_buf) != 0)
            continue;

        if (!dma_buf)
            continue;

        // Get file from dma_buf
        if (bpf_probe_read_kernel(&file, sizeof(file), &dma_buf->file) != 0)
            continue;

        if (!file)
            continue;

        // Get inode number
        if (bpf_probe_read_kernel(&inode, sizeof(inode), &file->f_inode) != 0)
            continue;

        if (!inode)
            continue;

        if (bpf_probe_read_kernel(&ino, sizeof(ino), &inode->i_ino) != 0)
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
        if (bpf_probe_read_kernel(&rec->width, sizeof(rec->width), &fb->width) != 0) {
            bpf_ringbuf_discard(rec, 0);
            continue;
        }
        if (bpf_probe_read_kernel(&rec->height, sizeof(rec->height), &fb->height) != 0) {
            bpf_ringbuf_discard(rec, 0);
            continue;
        }
        if (bpf_probe_read_kernel(&rec->pitch, sizeof(rec->pitch), &fb->pitches[0]) != 0) {
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

SEC("kprobe/drm_atomic_helper_prepare_planes")
int BPF_KPROBE(kprobe_prepare_planes, void *dev, void *state)
{
    return extract_fb_info_from_state(state);
}

char LICENSE[] SEC("license") = "GPL";
