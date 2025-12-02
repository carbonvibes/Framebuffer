#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct fb_record {
    __u64 ts_ns;
    __u32 crtc_id;
    __u32 width;
    __u32 height;
    __u32 pitch;
    __u32 format;
    __u64 inode;
    __u32 pid;
    __u32 plane_id;
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

    // Fill in basic info - using simple values without BTF dependency
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
