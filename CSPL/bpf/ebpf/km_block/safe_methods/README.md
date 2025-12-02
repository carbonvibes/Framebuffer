# Safe Frame Freezing Methods

This directory contains **4 safe alternatives** to blocking `drm_framebuffer_init` that won't cause kernel panics.

## **Safety Ranking (Safest to Riskiest)**

1. **🟢 Method 3: Input Blocker** - Safest, simulates freeze by blocking user input
2. **🟡 Method 1: Atomic Commit Blocker** - Blocks display updates, generally safe
3. **🟡 Method 4: Page Flip Blocker** - Blocks page flips, cursor may still work
4. **🟠 Method 2: VSync Blocker** - Blocks VSync signals, GPU-dependent

## **Quick Start**

```bash
# Run the interactive test suite
./test_all_methods.sh

# Or test individual methods:
cd method3_input_block
make test

cd ../method1_atomic_commit  
make test
```

## **Method Details**

### **Method 1: Atomic Commit Blocker**
- **File**: `method1_atomic_commit/atomic_commit_blocker.c`
- **Target**: `drm_atomic_commit`, `drm_atomic_nonblocking_commit`
- **Effect**: Blocks display updates, freezes screen content
- **Safety**: Generally safe, doesn't break initialization
- **Usage**: `echo 1 > /sys/kernel/atomic_freezer/trigger`

### **Method 2: VSync/Page Flip Blocker**
- **File**: `method2_vsync_block/vsync_blocker.c`
- **Target**: `intel_pipe_update_end`, `amdgpu_display_commit_tail`, page flip functions
- **Effect**: Blocks vertical sync and page flip operations
- **Safety**: GPU-dependent, may not work on all systems
- **Usage**: `echo 1 > /sys/kernel/vsync_freezer/freeze`

### **Method 3: Input Blocker (Recommended)**
- **File**: `method3_input_block/input_blocker.c`
- **Target**: `input_event`, `input_handle_event`
- **Effect**: Blocks all user input (keyboard, mouse) - **simulates** freeze
- **Safety**: Safest method, doesn't affect display system
- **Usage**: `echo 1 > /sys/kernel/input_freezer/freeze`
- **Emergency**: `echo 1 > /sys/kernel/input_freezer/emergency_unblock`

### **Method 4: Page Flip Blocker**
- **File**: `method4_page_flip/pageflip_blocker.c`
- **Target**: `drm_mode_page_flip_ioctl`, `drm_mode_setcrtc`
- **Effect**: Blocks page flips, cursor may still move
- **Safety**: Moderate, focuses on frame updates
- **Usage**: `echo 1 > /sys/kernel/pageflip_freezer/freeze`

## **Common Features**

All methods include:
- **Auto-recovery timer** - Automatic unfreeze after configurable timeout
- **Sysfs interface** - Easy control via `/sys/kernel/`
- **Status monitoring** - Real-time status and statistics
- **Configurable duration** - Set freeze time via module parameters
- **Detailed logging** - Monitor via `dmesg`

## **Testing Workflow**

1. **Start with Method 3** (Input Blocker) - safest
2. **Try Method 1** (Atomic Commit) - most effective for display
3. **Test Method 4** (Page Flip) - good balance
4. **Use Method 2** (VSync) only if others don't work

## **Recovery Mechanisms**

Each method has multiple recovery options:
- **Automatic timer** - Default 5-second auto-recovery
- **Manual disable** - Write 0 to control files
- **Module unload** - `sudo rmmod <module_name>`
- **Emergency unblock** - Method 3 has emergency override

## **Example Usage**

```bash
# Method 3 (Safest): Input blocking
cd method3_input_block
make install
echo 3 | sudo tee /sys/module/input_blocker/parameters/freeze_duration
echo 1 | sudo tee /sys/kernel/input_freezer/freeze
# Try typing/moving mouse - should be blocked for 3 seconds

# Method 1: Atomic commit blocking  
cd ../method1_atomic_commit
make install
echo 2 | sudo tee /sys/module/atomic_commit_blocker/parameters/freeze_duration
echo 1 | sudo tee /sys/kernel/atomic_freezer/trigger
# Screen should freeze for 2 seconds

# Check status anytime
cat /sys/kernel/*/status
```

## **Troubleshooting**

**If a method causes issues:**
1. Wait for auto-recovery (default 5 seconds)
2. Try emergency unblock: `echo 1 > /sys/kernel/*/emergency_unblock` (Method 3)
3. Unload module: `sudo rmmod <module_name>`
4. Check logs: `dmesg | tail -20`

**If build fails:**
- Check kernel headers: `sudo apt install linux-headers-$(uname -r)`
- Verify function symbols: `cat /proc/kallsyms | grep drm_atomic`

## **Why These Are Safer**

Unlike blocking `drm_framebuffer_init`:
- ✅ Don't break graphics initialization
- ✅ Don't corrupt framebuffer objects  
- ✅ Have automatic recovery
- ✅ Target specific operations, not core functions
- ✅ Allow graceful degradation

**Method 3 (Input Blocker)** is the safest because it doesn't touch the graphics subsystem at all - it only blocks user input to simulate an unresponsive system.

---

**⚠️ Always test in a safe environment first!**
