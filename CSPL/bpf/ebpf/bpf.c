/* SPDX-License-Identifier: GPL-2.0 */
/* flip_meta.bpf.c  —  emit one record for every plane armed
 *                    in drm_atomic_helper_prepare_planes()
 *                    (≈80 µs before vblank)
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

struct rec {
    __u64 ts_ns;
    __u32 crtc_id, w, h, pitch, fmt;
    __u64 inode;
    __u32 pid;
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20);
} rb SEC(".maps");

static __always_inline void push(struct drm_plane_state *pst)
{
    struct drm_framebuffer *fb  = BPF_CORE_READ(pst, fb);
    if (!fb) return;

    struct drm_gem_object *obj  = BPF_CORE_READ(fb, obj, 0);
    if (!obj) return;

    struct dma_buf *dbuf        = BPF_CORE_READ(obj, base.dma_buf);
    if (!dbuf) return;

    struct rec *r = bpf_ringbuf_reserve(&rb, sizeof(*r), 0);
    if (!r) return;

    r->ts_ns  = bpf_ktime_get_ns();
    r->crtc_id= BPF_CORE_READ(pst, crtc, base.id);
    r->w      = BPF_CORE_READ(fb, width);
    r->h      = BPF_CORE_READ(fb, height);
    r->pitch  = BPF_CORE_READ(fb, pitches, 0);
    r->fmt    = BPF_CORE_READ(fb, format, format);
    r->inode  = BPF_CORE_READ(dbuf, file, f_inode, i_ino);
    r->pid    = bpf_get_current_pid_tgid() >> 32;

    bpf_ringbuf_submit(r, 0);
}

#define MAX_PLANES 4

SEC("fentry:drm_kms_helper:drm_atomic_helper_prepare_planes")
int grab(struct drm_atomic_state *state)
{
    __u32 n = BPF_CORE_READ(state, num_planes);
#pragma unroll
    for (int i = 0; i < MAX_PLANES; i++) {
        if (i >= n) break;
        struct drm_plane_state *pst = BPF_CORE_READ(state, planes, i);
        if (pst) push(pst);
    }
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
