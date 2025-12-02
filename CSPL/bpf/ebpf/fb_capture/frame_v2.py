#!/usr/bin/env python3
from bcc import BPF
import ctypes, os, sys

# Enhanced BPF program using available fentry probes
bpf_program = """
#include <linux/bpf.h>
#include <linux/ptrace.h>

struct fb_capture_event_t {
    u64 ts_ns;
    u64 fb_ptr;
    u64 plane_state_ptr;
    u64 atomic_state_ptr;
    u32 pid;
    u32 cpu;
    u32 event_type; // 1=set_fb, 2=disable_plane, 3=atomic_commit, 4=check_only
};

BPF_PERF_OUTPUT(events);

// Hook drm_atomic_set_fb_for_plane - this is where framebuffer is assigned to plane
SEC("fentry/drm_atomic_set_fb_for_plane")
int BPF_PROG(trace_set_fb_for_plane, void *plane_state, void *fb)
{
    struct fb_capture_event_t evt = {};
    
    evt.ts_ns = bpf_ktime_get_ns();
    evt.fb_ptr = (u64)fb;
    evt.plane_state_ptr = (u64)plane_state;
    evt.pid = bpf_get_current_pid_tgid() >> 32;
    evt.cpu = bpf_get_smp_processor_id();
    evt.event_type = 1;

    events.perf_submit(ctx, &evt, sizeof(evt));
    return 0;
}

// Hook drm_atomic_check_only - validation before commit
SEC("fentry/drm_atomic_check_only")
int BPF_PROG(trace_atomic_check, void *state)
{
    struct fb_capture_event_t evt = {};
    
    evt.ts_ns = bpf_ktime_get_ns();
    evt.atomic_state_ptr = (u64)state;
    evt.pid = bpf_get_current_pid_tgid() >> 32;
    evt.cpu = bpf_get_smp_processor_id();
    evt.event_type = 4;

    events.perf_submit(ctx, &evt, sizeof(evt));
    return 0;
}

// Hook drm_atomic_commit - the actual frame submission
SEC("fentry/drm_atomic_commit")
int BPF_PROG(trace_atomic_commit, void *state)
{
    struct fb_capture_event_t evt = {};
    
    evt.ts_ns = bpf_ktime_get_ns();
    evt.atomic_state_ptr = (u64)state;
    evt.pid = bpf_get_current_pid_tgid() >> 32;
    evt.cpu = bpf_get_smp_processor_id();
    evt.event_type = 3;

    events.perf_submit(ctx, &evt, sizeof(evt));
    return 0;
}

// Hook drm_atomic_nonblocking_commit - async frame submission
SEC("fentry/drm_atomic_nonblocking_commit")
int BPF_PROG(trace_nonblocking_commit, void *state)
{
    struct fb_capture_event_t evt = {};
    
    evt.ts_ns = bpf_ktime_get_ns();
    evt.atomic_state_ptr = (u64)state;
    evt.pid = bpf_get_current_pid_tgid() >> 32;
    evt.cpu = bpf_get_smp_processor_id();
    evt.event_type = 3;

    events.perf_submit(ctx, &evt, sizeof(evt));
    return 0;
}
"""

class FBCaptureEvent(ctypes.Structure):
    _fields_ = [
        ("ts_ns", ctypes.c_ulonglong),
        ("fb_ptr", ctypes.c_ulonglong),
        ("plane_state_ptr", ctypes.c_ulonglong),
        ("atomic_state_ptr", ctypes.c_ulonglong),
        ("pid", ctypes.c_uint),
        ("cpu", ctypes.c_uint),
        ("event_type", ctypes.c_uint),
    ]

event_types = {
    1: "SET_FB_FOR_PLANE",
    2: "DISABLE_PLANE", 
    3: "ATOMIC_COMMIT",
    4: "ATOMIC_CHECK"
}

def handle_event(cpu, data, size):
    evt = ctypes.cast(data, ctypes.POINTER(FBCaptureEvent)).contents
    event_name = event_types.get(evt.event_type, "UNKNOWN")
    
    print(f"[{evt.ts_ns:016x}] CPU{evt.cpu:02d} PID{evt.pid:05d} {event_name}")
    if evt.fb_ptr:
        print(f"  FB: {evt.fb_ptr:016x}")
    if evt.plane_state_ptr:
        print(f"  PlaneState: {evt.plane_state_ptr:016x}")
    if evt.atomic_state_ptr:
        print(f"  AtomicState: {evt.atomic_state_ptr:016x}")
    print()

def print_drm_info():
    """Print available DRM devices"""
    try:
        drm_devices = [f for f in os.listdir("/dev/dri") if f.startswith("card")]
        print(f"Available DRM devices: {drm_devices}")
        
        for device in drm_devices:
            path = f"/dev/dri/{device}"
            try:
                stat = os.stat(path)
                print(f"  {device}: major={os.major(stat.st_rdev)}, minor={os.minor(stat.st_rdev)}")
            except:
                pass
    except:
        print("No DRM devices found")

if __name__ == "__main__":
    if os.geteuid() != 0:
        print("This program requires root privileges to access kernel tracing")
        sys.exit(1)

    print_drm_info()
    print("\nStarting framebuffer capture monitoring...")
    print("This will trace DRM atomic operations that handle framebuffers")
    print("Use this information to understand the framebuffer flow\n")

    try:
        b = BPF(text=bpf_program, allow_rlimit=True)
        print("BPF program loaded successfully")
        print("Listening for framebuffer events... (Ctrl+C to quit)\n")
        
        b["events"].open_perf_buffer(handle_event)
        
        while True:
            try:
                b.perf_buffer_poll()
            except KeyboardInterrupt:
                print("\nStopping...")
                break
                
    except Exception as e:
        print(f"Error: {e}")
        print("Make sure you have:")
        print("1. Root privileges")
        print("2. BCC installed (python3-bpf)")
        print("3. Kernel headers available")
        print("4. CONFIG_BPF=y and CONFIG_BPF_SYSCALL=y in kernel")
