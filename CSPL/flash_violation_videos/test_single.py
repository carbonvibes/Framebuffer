#!/usr/bin/env python3

# Test just one video generation to debug
import sys
import os
sys.path.append('/home/carbon/Documents/WashU/flash_violation_videos')

from generate_color_flash_violations import *

# Test one sequence
color_sequence = [(255, 255, 255), (0, 0, 255), (255, 255, 255)]  # White -> Red -> White
shape_type = "circle"
params = {"radius": 100}
output_filename = "/home/carbon/Documents/WashU/flash_violation_videos/test_red_flash.mp4"

print("Testing single video generation...")
generate_red_flash_video(shape_type, params, color_sequence, output_filename)
