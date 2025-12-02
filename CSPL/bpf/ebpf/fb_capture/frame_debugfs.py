#!/usr/bin/env python3
"""
Framebuffer extraction using DRM debugfs and sysfs interfaces
This approach avoids deep kernel structure access by using exposed interfaces
"""

import os
import sys
import time
import glob
import mmap
import struct
from pathlib import Path

class DRMFramebufferExtractor:
    def __init__(self):
        self.drm_cards = []
        self.debugfs_path = "/sys/kernel/debug/dri"
        self.sysfs_path = "/sys/class/drm"
        self.find_drm_devices()

    def find_drm_devices(self):
        """Find available DRM devices"""
        try:
            cards = [f for f in os.listdir("/dev/dri") if f.startswith("card")]
            for card in sorted(cards):
                card_num = card.replace("card", "")
                self.drm_cards.append({
                    'name': card,
                    'number': int(card_num),
                    'device_path': f"/dev/dri/{card}",
                    'debugfs_path': f"{self.debugfs_path}/{card_num}",
                    'sysfs_path': f"{self.sysfs_path}/{card}"
                })
            print(f"Found DRM cards: {[c['name'] for c in self.drm_cards]}")
        except Exception as e:
            print(f"Error finding DRM devices: {e}")

    def read_debugfs_info(self, card):
        """Read framebuffer info from debugfs"""
        try:
            debugfs_files = [
                "framebuffer",
                "gem_names", 
                "clients",
                "state",
                "vblank"
            ]
            
            info = {}
            for filename in debugfs_files:
                filepath = f"{card['debugfs_path']}/{filename}"
                if os.path.exists(filepath):
                    try:
                        with open(filepath, 'r') as f:
                            info[filename] = f.read().strip()
                    except:
                        info[filename] = "Permission denied or not readable"
                        
            return info
        except Exception as e:
            print(f"Error reading debugfs for {card['name']}: {e}")
            return {}

    def find_framebuffer_gems(self, card):
        """Find GEM objects that might be framebuffers"""
        gems = []
        try:
            gem_path = f"{card['debugfs_path']}/gem_names"
            if os.path.exists(gem_path):
                with open(gem_path, 'r') as f:
                    for line in f:
                        if 'size' in line and 'name' in line:
                            gems.append(line.strip())
        except:
            pass
        return gems

    def monitor_framebuffers(self):
        """Monitor framebuffer changes"""
        print("Monitoring framebuffer activity...")
        print("This shows DRM state information that's available through debugfs\n")
        
        last_state = {}
        
        try:
            while True:
                for card in self.drm_cards:
                    # Read current debugfs state
                    current_info = self.read_debugfs_info(card)
                    
                    # Check for changes
                    card_name = card['name']
                    if card_name not in last_state:
                        last_state[card_name] = {}
                    
                    changes_detected = False
                    for key, value in current_info.items():
                        if key not in last_state[card_name] or last_state[card_name][key] != value:
                            changes_detected = True
                            last_state[card_name][key] = value
                    
                    if changes_detected:
                        print(f"\n=== {card_name} State Update ===")
                        for key, value in current_info.items():
                            if value and value != "Permission denied or not readable":
                                print(f"{key}:")
                                # Limit output length for readability
                                if len(value) > 200:
                                    print(f"  {value[:200]}...")
                                else:
                                    print(f"  {value}")
                        
                        # Show GEM objects
                        gems = self.find_framebuffer_gems(card)
                        if gems:
                            print("Active GEM objects:")
                            for gem in gems[:5]:  # Limit to first 5
                                print(f"  {gem}")
                
                time.sleep(0.5)  # Check every 500ms
                
        except KeyboardInterrupt:
            print("\nStopping monitor...")

    def dump_current_state(self):
        """Dump current DRM state for all cards"""
        print("=== Current DRM State ===\n")
        
        for card in self.drm_cards:
            print(f"Card: {card['name']}")
            print(f"Device: {card['device_path']}")
            print(f"Debugfs: {card['debugfs_path']}")
            
            # Check if debugfs is accessible
            if not os.path.exists(card['debugfs_path']):
                print("  Debugfs not available (may need CONFIG_DEBUG_FS=y)")
                continue
                
            info = self.read_debugfs_info(card)
            for key, value in info.items():
                print(f"  {key}: {value[:100]}{'...' if len(value) > 100 else ''}")
            
            gems = self.find_framebuffer_gems(card)
            print(f"  GEM objects: {len(gems)} found")
            
            print()

def main():
    if len(sys.argv) > 1 and sys.argv[1] == "dump":
        # Just dump current state and exit
        extractor = DRMFramebufferExtractor()
        extractor.dump_current_state()
    else:
        # Monitor for changes
        if os.geteuid() != 0:
            print("Root privileges may be required to access debugfs")
            print("Try: sudo python3 frame_debugfs.py")
            
        extractor = DRMFramebufferExtractor()
        extractor.monitor_framebuffers()

if __name__ == "__main__":
    main()
