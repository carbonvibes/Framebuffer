# DRM Framebuffer Blocking Module

This kernel module extends the existing DRM framebuffer pixel extractor with frame blocking functionality. It intercepts calls to `drm_framebuffer_init` and can optionally block them from executing.

## Features

1. **Frame Blocking**: Can prevent framebuffer initialization calls from completing
2. **Real-time Control**: Enable/disable blocking via proc filesystem
3. **Statistics**: Track how many frames have been blocked
4. **Monitoring**: View blocking status and frame information

## Frame Blocking Implementation

The blocking functionality works by:

1. **Boolean Control Variable**: Uses a `block_frames` boolean to control blocking state
2. **Kprobe Handler**: Intercepts `drm_framebuffer_init` function calls
3. **Conditional Blocking**: When blocking is enabled, the handler returns 1 to prevent the original function from executing
4. **Statistics Tracking**: Counts how many frames have been blocked

## Usage

### Building and Installing

```bash
# Build the module
make

# Install the module (loads it)
sudo make install

# Uninstall the module
sudo make uninstall
```

### Controlling Frame Blocking

```bash
# Enable frame blocking
echo 1 | sudo tee /proc/drm_fb_block

# Disable frame blocking
echo 0 | sudo tee /proc/drm_fb_block

# Reset blocked frame counter
echo reset | sudo tee /proc/drm_fb_block

# View current status
cat /proc/drm_fb_block
```

### Monitoring

```bash
# View frame capture information (includes blocking status)
cat /proc/drm_fb_pixels

# Monitor kernel messages for blocking events
sudo dmesg -w | grep "BLOCKING framebuffer"

# Run the test script
./test_frame_blocking.sh
```

## Proc Filesystem Interface

The module creates three proc files:

1. **`/proc/drm_fb_pixels`**: Shows captured framebuffer information and blocking status
2. **`/proc/drm_fb_raw`**: Provides access to raw pixel data
3. **`/proc/drm_fb_block`**: Controls frame blocking functionality

## Testing

Use the provided test script to verify functionality:

```bash
./test_frame_blocking.sh
```

This script will:
- Check if the module is loaded
- Enable blocking and wait for activity
- Show statistics
- Disable blocking
- Reset counters

## How Frame Blocking Works

When a frame is blocked:

1. The kprobe handler intercepts `drm_framebuffer_init`
2. If `block_frames` is true, it logs a blocking message
3. The handler returns 1, preventing the original function from executing
4. The blocked frame counter is incremented
5. No framebuffer initialization occurs for that frame

## Expected Behavior

- **When blocking is disabled**: Normal framebuffer operations continue
- **When blocking is enabled**: New framebuffer initialization calls are prevented
- **Performance impact**: Minimal, only affects framebuffer initialization (not rendering)
- **Reversible**: Can be enabled/disabled in real-time without reloading the module

## Safety Notes

- Frame blocking may cause visual glitches or display issues
- Use primarily for testing and research purposes
- Always disable blocking when done testing
- Monitor system stability during testing

## Debugging

Check kernel messages for detailed information:

```bash
# View recent kernel messages
sudo dmesg | tail -20

# Monitor in real-time
sudo dmesg -w
```

Look for messages like:
- "BLOCKING framebuffer init: ..."
- "Frame blocking ENABLED"
- "Frame blocking DISABLED"
