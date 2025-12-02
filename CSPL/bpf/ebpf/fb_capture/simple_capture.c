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

static const char* format_to_string(uint32_t format) {
    switch (format) {
        case 0x34325258: return "XR24"; // DRM_FORMAT_XRGB8888
        case 0x34325241: return "AR24"; // DRM_FORMAT_ARGB8888
        case 0x34324258: return "XB24"; // DRM_FORMAT_XBGR8888
        case 0x34324241: return "AB24"; // DRM_FORMAT_ABGR8888
        default: return "UNKN";
    }
}

static int event_count = 0;

static int handle_event(void *ctx, void *data, size_t data_sz) {
    const struct fb_record *rec = data;
    
    if (data_sz != sizeof(*rec)) {
        fprintf(stderr, "Invalid data size: %zu\n", data_sz);
        return 0;
    }

    event_count++;
    printf("[%d] FB event: %ux%u fmt=%s pid=%u ts=%lu\n",
           event_count, rec->width, rec->height, format_to_string(rec->format),
           rec->pid, rec->ts_ns / 1000000);

    // For now, just log the event without trying to access framebuffer data
    printf("    └─ Would save framebuffer data to: fb_%lu_%ux%u.raw\n",
           rec->ts_ns / 1000000, rec->width, rec->height);

    return 0;
}

int main(int argc, char **argv) {
    struct bpf_object *obj;
    struct bpf_link *link;
    struct ring_buffer *rb;
    int map_fd;
    int err;

    // Setup signal handler
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("Loading BPF program flip_meta_i915_no_btf.bpf.o...\n");

    // Load BPF object
    obj = bpf_object__open_file("flip_meta_i915_no_btf.bpf.o", NULL);
    if (libbpf_get_error(obj)) {
        fprintf(stderr, "Failed to open BPF object: %s\n", strerror(errno));
        return 1;
    }

    // Load BPF program
    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "Failed to load BPF object: %s\n", strerror(-err));
        bpf_object__close(obj);
        return 1;
    }

    printf("✓ BPF object loaded successfully\n");

    // Attach BPF program
    struct bpf_program *prog = bpf_object__find_program_by_name(obj, "kprobe_i915_commit");
    if (!prog) {
        fprintf(stderr, "Failed to find BPF program 'kprobe_i915_commit'\n");
        bpf_object__close(obj);
        return 1;
    }

    printf("✓ Found BPF program: %s\n", bpf_program__name(prog));

    link = bpf_program__attach(prog);
    if (libbpf_get_error(link)) {
        err = libbpf_get_error(link);
        fprintf(stderr, "Failed to attach BPF program: %s (%d)\n", strerror(-err), err);
        bpf_object__close(obj);
        return 1;
    }

    printf("✓ BPF program attached successfully\n");

    // Get ring buffer map
    map_fd = bpf_object__find_map_fd_by_name(obj, "fb_events");
    if (map_fd < 0) {
        fprintf(stderr, "Failed to find ring buffer map\n");
        bpf_link__destroy(link);
        bpf_object__close(obj);
        return 1;
    }

    // Create ring buffer
    rb = ring_buffer__new(map_fd, handle_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "Failed to create ring buffer\n");
        bpf_link__destroy(link);
        bpf_object__close(obj);
        return 1;
    }

    printf("\n🎯 Framebuffer event capture active (simplified mode)\n");
    printf("🖥️  This will show Intel i915 graphics events but not save framebuffer data\n");
    printf("📊 Events should appear when you move windows, switch apps, etc.\n");
    printf("Press Ctrl+C to stop.\n\n");

    // Poll for events
    while (running) {
        err = ring_buffer__poll(rb, 1000); // 1 second timeout
        if (err == -EINTR) {
            break;
        }
        if (err < 0) {
            fprintf(stderr, "Error polling ring buffer: %s\n", strerror(-err));
            break;
        }
    }

    printf("\n🛑 Shutting down... Captured %d events total\n", event_count);

    ring_buffer__free(rb);
    bpf_link__destroy(link);
    bpf_object__close(obj);
    return 0;
}
