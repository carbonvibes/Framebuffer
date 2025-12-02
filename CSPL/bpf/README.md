# Framebuffer Extraction with eBPF

This toolkit provides comprehensive solutions for extracting framebuffer data from the Linux kernel using eBPF (Extended Berkeley Packet Filter) and direct framebuffer access.

## Overview

The toolkit includes multiple approaches to framebuffer extraction:

1. **eBPF-based tracing** - Traces graphics operations in the kernel
2. **Direct framebuffer access** - Reads framebuffer device directly
3. **Memory mapping** - Maps graphics memory for real-time access
4. **Combined monitoring** - Uses both eBPF and direct access simultaneously

## Files in this toolkit

### eBPF Scripts (`.bt` files)
- `framebuffer_tracer.bt` - Basic DRM and graphics event tracing
- `framebuffer_data_extractor.bt` - Advanced framebuffer operation analysis
- `pixel_data_capture.bt` - Attempts to capture actual pixel data
- `trace.bt` - Original VBlank tracing script
- `trace_with_filename.bt` - VBlank tracing with filename extraction

### C Programs
- `framebuffer_extractor.c` - Direct framebuffer device access program
- `framebuffer_extractor` - Compiled executable (after building)

### Build and Setup
- `Makefile` - Build system for C programs and script management
- `framebuffer_toolkit.sh` - Interactive setup and usage script
- `README.md` - This documentation

## Quick Start

### 1. Interactive Mode (Recommended)
```bash
cd /home/carbon/Documents/WashU/bpf/
sudo ./framebuffer_toolkit.sh
```

This will start an interactive menu with all available options.

### 2. Command Line Usage

Check system requirements:
```bash
sudo ./framebuffer_toolkit.sh --check
```

Run basic framebuffer extraction:
```bash
sudo ./framebuffer_toolkit.sh --extract
```

Run eBPF tracing for 30 seconds:
```bash
sudo ./framebuffer_toolkit.sh --trace 30
```

Monitor framebuffer changes:
```bash
sudo ./framebuffer_toolkit.sh --monitor
```

### 3. Manual Usage

Build the C extractor:
```bash
make all
```

Run eBPF tracing:
```bash
sudo bpftrace framebuffer_tracer.bt
```

Extract current framebuffer:
```bash
sudo ./framebuffer_extractor --save
```

## How It Works

### eBPF Approach

The eBPF scripts hook into various kernel tracepoints and function calls:

1. **DRM Tracepoints**:
   - `drm:drm_vblank_event` - Frame boundary detection
   - `drm:drm_vblank_event_queued` - Application framebuffer requests
   - `drm:drm_vblank_event_delivered` - Framebuffer delivery to applications

2. **i915 Graphics Tracepoints** (Intel graphics):
   - `i915:i915_gem_object_create` - Graphics memory allocation
   - `i915:i915_gem_object_pwrite` - Graphics memory writes (potential pixel data)
   - `i915:i915_gem_object_pread` - Graphics memory reads
   - `i915:intel_update_plane` - Display plane updates
   - `i915:intel_disable_plane` - Display plane disable events

3. **Kernel Function Hooks**:
   - Memory allocation functions (`__kmalloc`, `kfree`)
   - Memory copy functions (`__memcpy`, `copy_from_user`)
   - DRM framebuffer functions (`drm_framebuffer_init`)

### Direct Access Approach

The C program uses several methods to access framebuffer data:

1. **Framebuffer Device**: Direct access to `/dev/fb0`
2. **Memory Mapping**: Maps framebuffer memory into user space
3. **IOCTL Calls**: Gets framebuffer configuration and properties
4. **Content Analysis**: Analyzes pixel data patterns and formats

## System Requirements

- Linux kernel with eBPF support (4.4+)
- Root privileges (required for framebuffer and eBPF access)
- bpftrace installed
- Framebuffer device available (`/dev/fb0`)
- Build tools (gcc, make)

### Installing Dependencies

Ubuntu/Debian:
```bash
sudo apt update
sudo apt install bpftrace build-essential linux-tools-generic
```

CentOS/RHEL:
```bash
sudo yum install bpftrace gcc make kernel-devel
```

## Usage Examples

### Capture Current Screen
```bash
# Build and run basic extraction
make test

# This creates:
# - framebuffer_dump.raw (raw pixel data)
# - framebuffer_dump.ppm (viewable image)
```

### Monitor Graphics Activity
```bash
# Start eBPF tracing
sudo bpftrace framebuffer_tracer.bt

# In another terminal, generate graphics activity:
# - Move windows around
# - Play a video
# - Run a game or graphics application
```

### Real-time Framebuffer Monitoring
```bash
# Monitor framebuffer for changes
sudo ./framebuffer_extractor --monitor

# This will:
# - Detect framebuffer changes
# - Save frames when significant changes occur
# - Calculate frame rates
```

### Combined Analysis
```bash
# Run both eBPF tracing and direct monitoring
make combined-test

# This provides comprehensive analysis from both approaches
```

## Output Files

The toolkit generates various output files:

- `*.raw` - Raw framebuffer data (binary)
- `*.ppm` - PPM image format (viewable)
- `framebuffer_capture_N.raw/ppm` - Individual captured frames
- Console output with timing and analysis data

### Viewing Images

View PPM files with any image viewer:
```bash
# Using ImageMagick
display framebuffer_dump.ppm

# Using GIMP
gimp framebuffer_dump.ppm

# Convert to other formats
convert framebuffer_dump.ppm screenshot.png
```

## Technical Details

### eBPF Memory Access Limitations

eBPF has strict memory access controls:
- Cannot directly read arbitrary kernel memory
- Must use `bpf_probe_read_kernel()` for safe kernel memory access
- Memory access is limited in size and scope
- Some kernel structures may not be accessible

### Framebuffer Formats Supported

The C extractor supports common framebuffer formats:
- 16-bit RGB565
- 24-bit RGB888/BGR888
- 32-bit RGBA8888/BGRA8888

### Graphics Hardware Support

Tested with:
- Intel graphics (i915 driver) - Full eBPF tracepoint support
- Other graphics drivers may have limited tracepoint availability

## Troubleshooting

### Permission Denied
```bash
# Ensure you're running as root
sudo ./framebuffer_toolkit.sh

# Check framebuffer device permissions
ls -la /dev/fb0
```

### No Framebuffer Device
```bash
# Try loading framebuffer modules
sudo modprobe fb

# Check if graphics driver provides framebuffer
ls /dev/fb*
```

### eBPF Tracing Not Working
```bash
# Check if debugfs is mounted
mount | grep debugfs

# Mount debugfs if needed
sudo mount -t debugfs debugfs /sys/kernel/debug

# Check available tracepoints
sudo ls /sys/kernel/debug/tracing/events/drm/
sudo ls /sys/kernel/debug/tracing/events/i915/
```

### No Graphics Activity Detected
- Make sure to generate graphics activity while tracing
- Move windows, play videos, or run graphics applications
- Some desktop environments may use different graphics paths

## Advanced Usage

### Custom eBPF Scripts

You can modify the eBPF scripts to:
- Add new tracepoints
- Filter specific processes or applications
- Capture different types of graphics data
- Export data to user space programs

### Integration with Other Tools

The toolkit can be integrated with:
- `perf` for performance analysis
- `ftrace` for additional kernel tracing
- Custom applications for real-time processing
- Image processing pipelines

## Limitations

1. **eBPF Constraints**: Limited memory access and processing capabilities
2. **Hardware Specific**: Some features require specific graphics hardware
3. **Performance Impact**: eBPF tracing may impact system performance
4. **Root Required**: All operations require root privileges
5. **Format Dependencies**: Framebuffer format detection is heuristic

## Security Considerations

- This toolkit requires root access to kernel memory and devices
- eBPF scripts can trace all system graphics operations
- Captured framebuffers may contain sensitive visual information
- Use only on systems where you have authorization

## Contributing

To extend this toolkit:
1. Add new eBPF tracepoints in the `.bt` scripts
2. Extend the C extractor for new framebuffer formats
3. Add support for additional graphics drivers
4. Improve pixel data extraction methods

## License

This toolkit is provided for educational and research purposes. Ensure you have proper authorization before using on any system.

## References

- [eBPF Documentation](https://ebpf.io/)
- [bpftrace Reference](https://github.com/iovisor/bpftrace)
- [Linux Framebuffer Documentation](https://www.kernel.org/doc/Documentation/fb/framebuffer.txt)
- [DRM Subsystem](https://dri.freedesktop.org/wiki/DRM/)
