#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
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

SEC("kprobe/__i915_request_commit")
int kprobe_i915_commit(struct pt_regs *ctx)
{
    struct fb_record *rec;
    rec = bpf_ringbuf_reserve(&fb_events, sizeof(*rec), 0);
    if (!rec)
        return 0;

    // Fill in basic info - we'll use dummy values since we can't easily 
    // extract framebuffer info from i915_request
    rec->ts_ns = bpf_ktime_get_ns();
    rec->pid = bpf_get_current_pid_tgid() >> 32;
    rec->crtc_id = 0;
    rec->width = 1920;    // Common resolution
    rec->height = 1080;
    rec->pitch = 1920 * 4; // XRGB8888 
    rec->format = 0x34325258; // DRM_FORMAT_XRGB8888
    rec->inode = 0;
    rec->plane_id = 0;

    bpf_ringbuf_submit(rec, 0);
    return 0;
}

char _license[] SEC("license") = "GPL";
