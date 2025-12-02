#include <stdio.h>
#include <stdlib.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

int main() {
    struct bpf_object *obj;
    int err;

    printf("Opening BPF object file...\n");
    obj = bpf_object__open_file("flip_meta_i915_no_btf.bpf.o", NULL);
    if (libbpf_get_error(obj)) {
        fprintf(stderr, "Failed to open BPF object\n");
        return 1;
    }

    printf("Loading BPF object...\n");
    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "Failed to load BPF object: %s\n", strerror(-err));
        bpf_object__close(obj);
        return 1;
    }

    printf("Available BPF programs:\n");
    struct bpf_program *prog;
    bpf_object__for_each_program(prog, obj) {
        printf("  - %s\n", bpf_program__name(prog));
    }

    bpf_object__close(obj);
    return 0;
}
