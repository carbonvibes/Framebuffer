# DRM Atomic Helper Commit Planes Tracer

This directory contains bpftrace scripts to trace and analyze the `drm_atomic_helper_commit_planes` kernel function.

## Scripts

### 1. `drm_atomic_commit_planes_tracer.bt` (Verbose Version)
The comprehensive version that provides detailed information about:
- DRM device structure details (driver name, node pointers, open count, etc.)
- Mode configuration (number of planes, CRTCs, connectors, encoders)
- Atomic state information (modeset flags, async updates, state pointers)
- Function execution timing and statistics
- Related atomic helper function calls

### 2. `drm_atomic_commit_planes_simple.bt` (Simple Version)
A simplified version that shows:
- Basic function call information
- Driver name and device pointer
- Key atomic state flags
- Hardware configuration summary
- Execution timing

## Usage

### Run the verbose tracer:
```bash
sudo bpftrace drm_atomic_commit_planes_tracer.bt
```

### Run the simple tracer:
```bash
sudo bpftrace drm_atomic_commit_planes_simple.bt
```

### Run with output to file:
```bash
sudo bpftrace drm_atomic_commit_planes_tracer.bt > commit_planes_trace.log 2>&1
```

### Run for a specific duration:
```bash
sudo timeout 30s bpftrace drm_atomic_commit_planes_tracer.bt
```

## Function Signature

The traced function has the following signature:
```c
void drm_atomic_helper_commit_planes(struct drm_device *dev, 
                                     struct drm_atomic_state *old_state)
```

Where:
- `dev`: Pointer to the DRM device structure
- `old_state`: Pointer to the atomic state being committed

## Sample Output

### Verbose Version:
```
┌─ TRACE #1 ─────────────────────────────────────────────────────
│ [14:30:15.123456] drm_atomic_helper_commit_planes() ENTRY
│ Process: Xorg (PID: 1234, TID: 1234)
│ CPU: 2
│
│ ┌─ DRM Device (struct drm_device *) ──────────────────────────
│ │ Address: 0xffff888123456789
│ │ Driver Name: i915
│ │ Open Count: 3
│ │ ├─ Num Connector: 2
│ │ ├─ Num Encoder: 2
│ │ ├─ Num Crtc: 3
│ │ └─ Num Plane: 12
│ └─────────────────────────────────────────────────────────────
│
│ ┌─ Atomic State (struct drm_atomic_state *) ──────────────────
│ │ Address: 0xffff888987654321
│ │ Allow Modeset: false
│ │ Async Update: false
│ │ ├─ Plane States Available
│ │ ├─ CRTC States Available
│ │ └─ Connector States Available
│ └─────────────────────────────────────────────────────────────
│
│ [14:30:15.123789] drm_atomic_helper_commit_planes() EXIT
│ Duration: 333000 ns (333 μs)
└─────────────────────────────────────────────────────────────────
```

### Simple Version:
```
[14:30:15] COMMIT_PLANES #1: Xorg (PID: 1234)
  Device: 0xffff888123456789 (Driver: i915)
  Atomic State: 0xffff888987654321 (Modeset: N, Async: N)
  Planes: 12, CRTCs: 3, Connectors: 2, Encoders: 2
  → Completed in 333 μs
```

## Prerequisites

- Root privileges (for bpftrace)
- Linux kernel with DRM KMS helper module loaded
- bpftrace installed
- Kernel with CONFIG_KPROBES and CONFIG_BPF enabled

## Notes

- The scripts trace the function in the `drm_kms_helper` module
- Some struct field access may fail on different kernel versions
- Use Ctrl+C to stop tracing and see statistics
- The verbose version includes related atomic helper function calls for context
