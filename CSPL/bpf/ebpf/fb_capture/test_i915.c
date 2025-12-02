#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

struct fb_record {
    uint64_t ts_ns;
    uint32_t crtc_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;
    uint64_t inode;
    uint32_t pid;
    uint32_t plane_id;
};

static volatile int running = 1;

static void sig_handler(int sig) {
    running = 0;
}

static int handle_event(void *ctx, void *data, size_t data_sz) {
    const struct fb_record *rec = data;
    
    if (data_sz != sizeof(*rec)) {
        fprintf(stderr, "Invalid data size: %zu\n", data_sz);
        return 0;
    }

    printf("i915 commit event: %ux%u pid=%u ts=%lu\n",
           rec->width, rec->height, rec->pid, rec->ts_ns / 1000000);

    return 0;
}

int main(int argc, char **argv) {
    struct bpf_object *obj;
    struct bpf_link *link;
    struct ring_buffer *rb;
    int map_fd;
    int err;

    signal(SIGINT, sig_handler);

    obj = bpf_object__open_file("flip_meta_i915.bpf.o", NULL);
    if (libbpf_get_error(obj)) {
        fprintf(stderr, "Failed to open BPF object: %s\n", strerror(errno));
        return 1;
    }

    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "Failed to load BPF object: %s\n", strerror(-err));
        bpf_object__close(obj);
        return 1;
    }

    printf("✓ BPF object loaded successfully\n");

    struct bpf_program *prog = bpf_object__find_program_by_name(obj, "kprobe_i915_commit");
    if (!prog) {
        fprintf(stderr, "Failed to find BPF program\n");
        bpf_object__close(obj);
        return 1;
    }

    link = bpf_program__attach(prog);
    if (libbpf_get_error(link)) {
        err = libbpf_get_error(link);
        fprintf(stderr, "Failed to attach BPF program: %s (%d)\n", strerror(-err), err);
        bpf_object__close(obj);
        return 1;
    }

    printf("✓ BPF program attached to __i915_request_commit\n");

    map_fd = bpf_object__find_map_fd_by_name(obj, "fb_events");
    if (map_fd < 0) {
        fprintf(stderr, "Failed to find ring buffer map\n");
        bpf_link__destroy(link);
        bpf_object__close(obj);
        return 1;
    }

    rb = ring_buffer__new(map_fd, handle_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "Failed to create ring buffer\n");
        bpf_link__destroy(link);
        bpf_object__close(obj);
        return 1;
    }

    printf("🎯 Intel i915 commit tracking active. Try moving windows...\n");
    printf("Press Ctrl+C to stop.\n\n");

    while (running) {
        err = ring_buffer__poll(rb, 1000);
        if (err == -EINTR) {
            break;
        }
        if (err < 0) {
            fprintf(stderr, "Error polling ring buffer: %s\n", strerror(-err));
            break;
        }
    }

    printf("\n🛑 Shutting down...\n");

    ring_buffer__free(rb);
    bpf_link__destroy(link);
    bpf_object__close(obj);
    return 0;
}
