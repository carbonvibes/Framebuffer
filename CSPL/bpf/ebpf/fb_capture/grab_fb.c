#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#define MAX_PATH 512
#define DRM_FORMAT_XRGB8888 0x34325258
#define DRM_FORMAT_ARGB8888 0x34325241
#define DRM_FORMAT_XBGR8888 0x34324258
#define DRM_FORMAT_ABGR8888 0x34324241

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
        case DRM_FORMAT_XRGB8888: return "XR24";
        case DRM_FORMAT_ARGB8888: return "AR24";
        case DRM_FORMAT_XBGR8888: return "XB24";
        case DRM_FORMAT_ABGR8888: return "AB24";
        default: return "UNKN";
    }
}

static int find_fd_by_inode(uint64_t target_inode, uint32_t hint_pid) {
    char path[MAX_PATH];
    char link_target[MAX_PATH];
    struct stat st;
    DIR *fd_dir;
    struct dirent *entry;
    ssize_t link_len;
    int fd;

    // First try the hint PID
    if (hint_pid > 0) {
        snprintf(path, sizeof(path), "/proc/%u/fd", hint_pid);
        fd_dir = opendir(path);
        if (fd_dir) {
            while ((entry = readdir(fd_dir)) != NULL) {
                if (entry->d_name[0] == '.')
                    continue;

                snprintf(path, sizeof(path), "/proc/%u/fd/%s", hint_pid, entry->d_name);
                
                link_len = readlink(path, link_target, sizeof(link_target) - 1);
                if (link_len <= 0)
                    continue;
                
                link_target[link_len] = '\0';
                
                // Check if it's a DMA-BUF
                if (strstr(link_target, "dmabuf")) {
                    if (stat(path, &st) == 0 && st.st_ino == target_inode) {
                        fd = atoi(entry->d_name);
                        closedir(fd_dir);
                        return dup(fd); // Return a dup'd fd we can safely use
                    }
                }
            }
            closedir(fd_dir);
        }
    }

    // Search all processes
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir)
        return -1;

    struct dirent *proc_entry;
    while ((proc_entry = readdir(proc_dir)) != NULL) {
        if (!isdigit(proc_entry->d_name[0]))
            continue;

        uint32_t pid = atoi(proc_entry->d_name);
        if (pid == hint_pid) // Already tried this one
            continue;

        snprintf(path, sizeof(path), "/proc/%u/fd", pid);
        fd_dir = opendir(path);
        if (!fd_dir)
            continue;

        while ((entry = readdir(fd_dir)) != NULL) {
            if (entry->d_name[0] == '.')
                continue;

            snprintf(path, sizeof(path), "/proc/%u/fd/%s", pid, entry->d_name);
            
            link_len = readlink(path, link_target, sizeof(link_target) - 1);
            if (link_len <= 0)
                continue;
            
            link_target[link_len] = '\0';
            
            if (strstr(link_target, "dmabuf")) {
                if (stat(path, &st) == 0 && st.st_ino == target_inode) {
                    fd = atoi(entry->d_name);
                    closedir(fd_dir);
                    closedir(proc_dir);
                    return dup(fd);
                }
            }
        }
        closedir(fd_dir);
    }
    closedir(proc_dir);
    return -1;
}

static int save_framebuffer(const struct fb_record *rec) {
    char filename[256];
    int dma_fd = -1;
    void *mapped_data = NULL;
    FILE *output_file = NULL;
    size_t buffer_size;
    int ret = -1;

    // Find the DMA-BUF file descriptor
    dma_fd = find_fd_by_inode(rec->inode, rec->pid);
    if (dma_fd < 0) {
        fprintf(stderr, "Failed to find fd for inode %lu\n", rec->inode);
        return -1;
    }

    // Calculate buffer size
    buffer_size = rec->pitch * rec->height;

    // Map the buffer
    mapped_data = mmap(NULL, buffer_size, PROT_READ, MAP_SHARED, dma_fd, 0);
    if (mapped_data == MAP_FAILED) {
        fprintf(stderr, "mmap failed for %ux%u buffer: %s\n", 
                rec->width, rec->height, strerror(errno));
        if (errno == EPERM) {
            fprintf(stderr, "EPERM suggests VRAM-only buffer. Intel iGPU should usually work.\n");
            fprintf(stderr, "Workaround: Use BLT commands to copy to system memory.\n");
        }
        goto cleanup;
    }

    // Create output filename
    snprintf(filename, sizeof(filename), "pre_%lu_%ux%u_crtc%u_plane%u.raw",
             rec->ts_ns / 1000000, rec->width, rec->height, rec->crtc_id, rec->plane_id);

    // Write the framebuffer data
    output_file = fopen(filename, "wb");
    if (!output_file) {
        fprintf(stderr, "Failed to create output file %s: %s\n", filename, strerror(errno));
        goto cleanup;
    }

    if (fwrite(mapped_data, 1, buffer_size, output_file) != buffer_size) {
        fprintf(stderr, "Failed to write framebuffer data: %s\n", strerror(errno));
        goto cleanup;
    }

    printf("✓ Saved %s (%s format, CRTC-%u, plane-%u)\n", 
           filename, format_to_string(rec->format), rec->crtc_id, rec->plane_id);
    ret = 0;

cleanup:
    if (mapped_data && mapped_data != MAP_FAILED)
        munmap(mapped_data, buffer_size);
    if (output_file)
        fclose(output_file);
    if (dma_fd >= 0)
        close(dma_fd);
    return ret;
}

static int handle_event(void *ctx, void *data, size_t data_sz) {
    const struct fb_record *rec = data;
    
    if (data_sz != sizeof(*rec)) {
        fprintf(stderr, "Invalid data size: %zu\n", data_sz);
        return 0;
    }

    printf("FB event: %ux%u fmt=%s inode=%lu pid=%u crtc=%u plane=%u\n",
           rec->width, rec->height, format_to_string(rec->format),
           rec->inode, rec->pid, rec->crtc_id, rec->plane_id);

    return save_framebuffer(rec);
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

    // Load BPF object
    obj = bpf_object__open_file("flip_meta_i915.bpf.o", NULL);
    if (libbpf_get_error(obj)) {
        fprintf(stderr, "Failed to open BPF object: %s\n", strerror(errno));
        return 1;
    }

    // Load BPF program
    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "Failed to load BPF object: %s\n", strerror(-err));
        fprintf(stderr, "This could be due to:\n");
        fprintf(stderr, "1. Missing kernel symbols or BTF\n");
        fprintf(stderr, "2. Incompatible kernel version\n");
        fprintf(stderr, "3. Missing kernel modules (drm_kms_helper)\n");
        bpf_object__close(obj);
        return 1;
    }

    printf("✓ BPF object loaded successfully\n");

    // Attach BPF program
    struct bpf_program *prog = bpf_object__find_program_by_name(obj, "kprobe_i915_commit");
    if (!prog) {
        fprintf(stderr, "Failed to find BPF program 'kprobe_i915_commit'\n");
        fprintf(stderr, "Available programs:\n");
        struct bpf_program *p;
        bpf_object__for_each_program(p, obj) {
            fprintf(stderr, "  - %s\n", bpf_program__name(p));
        }
        bpf_object__close(obj);
        return 1;
    }

    printf("✓ Found BPF program: %s\n", bpf_program__name(prog));

    link = bpf_program__attach(prog);
    if (libbpf_get_error(link)) {
        err = libbpf_get_error(link);
        fprintf(stderr, "Failed to attach BPF program: %s (%d)\n", strerror(-err), err);
        fprintf(stderr, "This could be due to:\n");
        fprintf(stderr, "1. Function symbol not found in kernel\n");
        fprintf(stderr, "2. Missing module (drm_kms_helper not loaded)\n");
        fprintf(stderr, "3. Insufficient permissions\n");
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

    printf("🎯 Framebuffer capture active. Move a window to trigger events...\n");
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

    printf("\n🛑 Shutting down...\n");

    ring_buffer__free(rb);
    bpf_link__destroy(link);
    bpf_object__close(obj);
    return 0;
}
