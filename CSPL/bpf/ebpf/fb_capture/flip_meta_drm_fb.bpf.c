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
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} fb_events SEC(".maps");

// Hook into DRM framebuffer operations to get real framebuffer data
SEC("kprobe/drm_framebuffer_get")
int kprobe_drm_fb_get(struct pt_regs *ctx)
{
    struct drm_framebuffer *fb = (struct drm_framebuffer *)PT_REGS_PARM1(ctx);
    struct fb_record *rec;
    struct drm_gem_object *gem_obj;
    struct dma_buf *dmabuf;
    struct file *file;
    
    if (!fb)
        return 0;
        
    rec = bpf_ringbuf_reserve(&fb_events, sizeof(*rec), 0);
    if (!rec)
        return 0;

    // Extract framebuffer dimensions and format
    rec->ts_ns = bpf_ktime_get_ns();
    rec->pid = bpf_get_current_pid_tgid() >> 32;
    
    // Read framebuffer properties
    bpf_core_read(&rec->width, sizeof(rec->width), &fb->width);
    bpf_core_read(&rec->height, sizeof(rec->height), &fb->height);
    bpf_core_read(&rec->format, sizeof(rec->format), &fb->format->format);
    
    // Try to get pitch from the first plane
    if (fb->pitches && fb->pitches[0] > 0) {
        bpf_core_read(&rec->pitch, sizeof(rec->pitch), &fb->pitches[0]);
    } else {
        rec->pitch = rec->width * 4; // Default to 32bpp
    }
    
    // Try to extract DMA-BUF inode if possible
    rec->inode = 0;
    rec->crtc_id = 0;
    rec->plane_id = 0;
    
    // Attempt to get the underlying gem object and dma-buf
    if (fb->obj && fb->obj[0]) {
        bpf_core_read(&gem_obj, sizeof(gem_obj), &fb->obj[0]);
        if (gem_obj) {
            // Try to get dma_buf from gem object
            if (bpf_core_field_exists(gem_obj->dma_buf)) {
                bpf_core_read(&dmabuf, sizeof(dmabuf), &gem_obj->dma_buf);
                if (dmabuf) {
                    bpf_core_read(&file, sizeof(file), &dmabuf->file);
                    if (file && file->f_inode) {
                        struct inode *inode;
                        bpf_core_read(&inode, sizeof(inode), &file->f_inode);
                        if (inode) {
                            bpf_core_read(&rec->inode, sizeof(rec->inode), &inode->i_ino);
                        }
                    }
                }
            }
        }
    }

    bpf_ringbuf_submit(rec, 0);
    return 0;
}

// Alternative hook for plane updates
SEC("kprobe/drm_atomic_helper_update_plane")
int kprobe_drm_plane_update(struct pt_regs *ctx)
{
    struct drm_plane *plane = (struct drm_plane *)PT_REGS_PARM1(ctx);
    struct drm_framebuffer *fb = (struct drm_framebuffer *)PT_REGS_PARM2(ctx);
    struct fb_record *rec;
    
    if (!fb || !plane)
        return 0;
        
    rec = bpf_ringbuf_reserve(&fb_events, sizeof(*rec), 0);
    if (!rec)
        return 0;

    rec->ts_ns = bpf_ktime_get_ns();
    rec->pid = bpf_get_current_pid_tgid() >> 32;
    
    // Read plane and framebuffer info
    bpf_core_read(&rec->width, sizeof(rec->width), &fb->width);
    bpf_core_read(&rec->height, sizeof(rec->height), &fb->height);
    bpf_core_read(&rec->format, sizeof(rec->format), &fb->format->format);
    
    if (fb->pitches && fb->pitches[0] > 0) {
        bpf_core_read(&rec->pitch, sizeof(rec->pitch), &fb->pitches[0]);
    } else {
        rec->pitch = rec->width * 4;
    }
    
    // Try to get plane info
    if (plane->base.id) {
        bpf_core_read(&rec->plane_id, sizeof(rec->plane_id), &plane->base.id);
    }
    
    rec->inode = 0; // Will be filled by framebuffer extraction logic
    rec->crtc_id = 0;

    bpf_ringbuf_submit(rec, 0);
    return 0;
}

char _license[] SEC("license") = "GPL";
