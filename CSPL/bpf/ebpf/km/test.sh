# Check if Secure Boot is enabled
mokutil --sb-state 2>/dev/null || echo "mokutil not found - checking other ways"

# Check for Secure Boot via EFI
ls /sys/firmware/efi/efivars/*SecureBoot* 2>/dev/null || echo "EFI SecureBoot vars not found"

# Check kernel lockdown status
cat /sys/kernel/security/lockdown 2>/dev/null || echo "Lockdown status not available"

# Check module signature verification
cat /proc/sys/kernel/modules_disabled 2>/dev/null || echo "0"

# Check dmesg for specific error messages
dmesg | tail -10

# Try to get more detailed error info
modprobe -v drm_fb_extractor 2>&1 || echo "modprobe failed"
