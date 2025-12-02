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

// Hook into drm_atomic_helper_commit_planes 
SEC("kprobe/drm_atomic_helper_commit_planes")
int kprobe_commit_planes(struct pt_regs *ctx)
{
    struct fb_record *rec;
    rec = bpf_ringbuf_reserve(&fb_events, sizeof(*rec), 0);
    if (!rec)
        return 0;

    // Fill basic information
    rec->ts_ns = bpf_ktime_get_ns();
    rec->pid = bpf_get_current_pid_tgid() >> 32;
    
    // Mark this as coming from commit_planes
    rec->crtc_id = 1;  
    rec->width = 1920;
    rec->height = 1080;
    rec->pitch = 1920 * 4;
    rec->format = 0x34325258; // DRM_FORMAT_XRGB8888
    rec->plane_id = 1;
    rec->inode = 12345;     // Test inode - will be replaced with actual extraction
    rec->obj_inode = 0;
    rec->dma_addr = 0;

    bpf_ringbuf_submit(rec, 0);
    return 0;
}

char _license[] SEC("license") = "GPL";
