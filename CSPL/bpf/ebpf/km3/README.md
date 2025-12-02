# DRM Framebuffer Pixel Extractor v3

This kernel module captures actual rendered framebuffer content by hooking into the DRM atomic commit pipeline at `drm_atomic_helper_commit_planes`. This approach captures the real rendered content as it's being committed to the display hardware.

## Key Improvements

- **Atomic Commit Hooking**: Captures content during actual rendering commits, not just framebuffer creation
- **Primary Plane Focus**: Specifically targets primary display planes that contain the main desktop content
- **Position Tracking**: Records plane positioning information for multi-monitor setups
- **Better Memory Access**: Uses DMA-buf mapping when available for more reliable content extraction
- **Real-time Capture**: Captures content as it's actively being displayed

## Files

- `kernel.c` - The improved kernel module source
- `Makefile` - Build configuration for the module
- `extract_framebuffer_v3.py` - Python script to extract and convert captured framebuffer data to images

## Usage

1. **Build the module:**
   ```bash
   make
   ```

2. **Load the module:**
   ```bash
   sudo make install
   ```

3. **Check capture status:**
   ```bash
   cat /proc/drm_fb_pixels
   ```

4. **Extract images:**
   ```bash
   python3 extract_framebuffer_v3.py
   ```

5. **Unload the module:**
   ```bash
   sudo make uninstall
   ```

## What's Different

### Previous Issues Fixed:
- ❌ **Old**: Captured during framebuffer initialization (empty/wrong content)
- ✅ **New**: Captures during atomic commits (actual rendered content)

- ❌ **Old**: Sequential page reading without geometry respect
- ✅ **New**: DMA-buf mapping with proper stride handling

- ❌ **Old**: Mixed metadata from different framebuffers
- ✅ **New**: Focused on primary planes with position tracking

### Expected Results:
- Clean, non-stripped desktop images
- Proper dual-monitor handling (3840x1080 → two 1920x1080 images)  
- Correct color representation
- Real-time capture of what's actually displayed

## Troubleshooting

If you still see corrupted images:
1. Check `dmesg` for module loading messages
2. Verify the module is capturing data: `cat /proc/drm_fb_pixels`
3. Try triggering screen updates (move windows, etc.) to generate new atomic commits
4. The module captures primary planes only - overlays/cursors are filtered out

## Technical Details

The module hooks `drm_atomic_helper_commit_planes` which is called when the DRM subsystem commits a set of plane updates to the display hardware. This is the moment when the actual pixel content is being sent to the display, making it the optimal capture point for screen content extraction.
