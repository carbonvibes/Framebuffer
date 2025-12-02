from bcc import BPF
import ctypes, os, mmap, glob

bpf_program = """
#include <uapi/linux/ptrace.h>
#include <linux/sched.h>

struct fb_data_t {
    u64 ts_ns;
    u64 fb_ptr;
    u64 gem_obj_ptr;
    u64 plane_state_ptr;
    u32 pid;
    u32 width;
    u32 height;
    u32 format;
    u64 gem_size;
    u64 gem_handle;
    u64 physical_addr;
    u32 stride;
    char event_type[16];
};

BPF_PERF_OUTPUT(fb_events);
BPF_HASH(framebuffer_cache, u64, struct fb_data_t);

// Intercept framebuffer being set for plane - direct access to framebuffer struct
KFUNC_PROBE(drm_atomic_set_fb_for_plane, struct drm_plane_state *plane_state, struct drm_framebuffer *fb)
{
    if (!plane_state || !fb)
        return 0;

    struct fb_data_t data = {};
    data.ts_ns = bpf_ktime_get_ns();
    data.fb_ptr = (u64)fb;
    data.plane_state_ptr = (u64)plane_state;
    data.pid = bpf_get_current_pid_tgid() >> 32;
    
    // Read framebuffer properties using bpf_probe_read_kernel
    bpf_probe_read_kernel(&data.width, sizeof(data.width), &fb->width);
    bpf_probe_read_kernel(&data.height, sizeof(data.height), &fb->height);
    
    // Read format - need to read the format pointer first, then the format value
    struct drm_format_info *format_info;
    bpf_probe_read_kernel(&format_info, sizeof(format_info), &fb->format);
    if (format_info) {
        bpf_probe_read_kernel(&data.format, sizeof(data.format), &format_info->format);
    }
    
    bpf_probe_read_kernel(&data.stride, sizeof(data.stride), &fb->pitches[0]);
    
    __builtin_memcpy(data.event_type, "SET_FB_PLANE", 12);

    // Cache this for correlation with other events
    u64 key = data.fb_ptr;
    framebuffer_cache.update(&key, &data);

    fb_events.perf_submit(ctx, &data, sizeof(data));
    return 0;
}

// Intercept GEM object creation - this is where actual pixel data lives
KFUNC_PROBE(drm_gem_object_init, struct drm_device *dev, struct drm_gem_object *obj, size_t size)
{
    if (!obj)
        return 0;

    struct fb_data_t data = {};
    data.ts_ns = bpf_ktime_get_ns();
    data.gem_obj_ptr = (u64)obj;
    data.gem_size = size;
    data.pid = bpf_get_current_pid_tgid() >> 32;
    __builtin_memcpy(data.event_type, "GEM_INIT", 8);

    fb_events.perf_submit(ctx, &data, sizeof(data));
    return 0;
}

// Intercept GEM handle creation to correlate userspace handles with kernel objects
KFUNC_PROBE(drm_gem_handle_create, struct drm_file *file, struct drm_gem_object *obj, u32 *handlep)
{
    if (!obj)
        return 0;

    struct fb_data_t data = {};
    data.ts_ns = bpf_ktime_get_ns();
    data.gem_obj_ptr = (u64)obj;
    data.pid = bpf_get_current_pid_tgid() >> 32;
    
    // Read the handle value
    if (handlep) {
        bpf_probe_read_kernel(&data.gem_handle, sizeof(data.gem_handle), handlep);
    }
    
    __builtin_memcpy(data.event_type, "GEM_HANDLE", 10);

    fb_events.perf_submit(ctx, &data, sizeof(data));
    return 0;
}

// Intercept atomic commit - the final step before scanout
KFUNC_PROBE(drm_atomic_commit, struct drm_atomic_state *state)
{
    if (!state)
        return 0;

    struct fb_data_t data = {};
    data.ts_ns = bpf_ktime_get_ns();
    data.fb_ptr = (u64)state;  // atomic state pointer
    data.pid = bpf_get_current_pid_tgid() >> 32;
    __builtin_memcpy(data.event_type, "ATOMIC_COMMIT", 13);

    fb_events.perf_submit(ctx, &data, sizeof(data));
    return 0;
}

// Intercept GEM mmap operations to catch when userspace maps framebuffer
KFUNC_PROBE(drm_gem_mmap_obj, struct drm_gem_object *obj, unsigned long obj_size, struct vm_area_struct *vma)
{
    if (!obj)
        return 0;

    struct fb_data_t data = {};
    data.ts_ns = bpf_ktime_get_ns();
    data.gem_obj_ptr = (u64)obj;
    data.gem_size = obj_size;
    data.pid = bpf_get_current_pid_tgid() >> 32;
    __builtin_memcpy(data.event_type, "GEM_MMAP", 8);

    fb_events.perf_submit(ctx, &data, sizeof(data));
    return 0;
}

// Intercept CMA buffer creation - these often contain framebuffer data
KRETFUNC_PROBE(__drm_gem_cma_create, struct drm_gem_cma_object *retval)
{
    if (!retval)
        return 0;

    struct fb_data_t data = {};
    data.ts_ns = bpf_ktime_get_ns();
    data.gem_obj_ptr = (u64)retval;
    data.pid = bpf_get_current_pid_tgid() >> 32;
    __builtin_memcpy(data.event_type, "CMA_CREATE", 10);

    // Read CMA object properties using bpf_probe_read_kernel
    bpf_probe_read_kernel(&data.physical_addr, sizeof(data.physical_addr), &retval->paddr);
    bpf_probe_read_kernel(&data.gem_size, sizeof(data.gem_size), &retval->base.size);

    fb_events.perf_submit(ctx, &data, sizeof(data));
    return 0;
}
"""

class FBEvent(ctypes.Structure):
    _fields_ = [
        ("ts_ns", ctypes.c_ulonglong),
        ("fb_ptr", ctypes.c_ulonglong),
        ("gem_obj_ptr", ctypes.c_ulonglong),
        ("plane_state_ptr", ctypes.c_ulonglong),
        ("pid", ctypes.c_uint),
        ("width", ctypes.c_uint),
        ("height", ctypes.c_uint),
        ("format", ctypes.c_uint),
        ("gem_size", ctypes.c_ulonglong),
        ("gem_handle", ctypes.c_ulonglong),
        ("physical_addr", ctypes.c_ulonglong),
        ("stride", ctypes.c_uint),
        ("event_type", ctypes.c_char * 16),
    ]

def handle_event(cpu, data, size):
    evt = ctypes.cast(data, ctypes.POINTER(FBEvent)).contents
    event_type = evt.event_type.decode('utf-8', 'ignore').rstrip('\x00')
    
    # Decode DRM format (if available)
    format_str = "UNKNOWN"
    if evt.format != 0:
        # Common DRM formats
        format_map = {
            0x34325258: "XR24",  # XRGB8888
            0x34324752: "RG24",  # RGB888
            0x36314752: "RG16",  # RGB565
            0x34324142: "BA24",  # ABGR8888
            0x34325241: "AR24",  # ARGB8888
        }
        format_str = format_map.get(evt.format, f"0x{evt.format:08x}")
    
    print(f"[{evt.ts_ns}] {event_type} PID:{evt.pid} FB@{hex(evt.fb_ptr)} GEM@{hex(evt.gem_obj_ptr)} "
          f"Size:{evt.gem_size} {evt.width}x{evt.height} Format:{format_str} "
          f"PlaneState@{hex(evt.plane_state_ptr)} PhysAddr@{hex(evt.physical_addr)}")
    
    # If we have framebuffer data with size, this is where we'd extract pixels
    if evt.gem_obj_ptr != 0 and evt.gem_size > 0 and event_type == "SET_FB_PLANE":
        print(f"  -> Framebuffer ready for capture: {evt.width}x{evt.height} {evt.gem_size} bytes")
    
    # TODO: Use the captured pointers to extract actual framebuffer data
    # The gem_obj_ptr points to the actual pixel data buffer
    
def find_dma_buf_fd(inode, pid):
    # This function is kept for potential future use with different approach
    paths = [f"/proc/{pid}/fd"] + glob.glob("/proc/*/fd")
    for path in paths:
        try:
            for fd in os.listdir(path):
                full_path = os.path.join(path, fd)
                if os.stat(full_path).st_ino == inode:
                    return os.open(full_path, os.O_RDONLY)
        except:
            continue
    return -1

if __name__ == "__main__":
    headers = f"/usr/src/linux-headers-{os.uname().release}/include"
    if not os.path.exists(headers):
        print(f"Install kernel headers: sudo apt install linux-headers-{os.uname().release}")
        exit(1)

    b = BPF(text=bpf_program, cflags=[f"-I{headers}"], allow_rlimit=True)
    print("Listening for framebuffer events... (Ctrl+C to quit)")
    b["fb_events"].open_perf_buffer(handle_event)

    while True:
        b.perf_buffer_poll()
