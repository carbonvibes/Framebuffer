# Flash Violation Videos - Final Summary

## Overview
Successfully generated 10 diverse videos that violate the FlashLuminanceThreshold guidelines as specified in the Coq formal specification (`guidlines.v`). All videos are 40 seconds long at 24 FPS with continuous flashing patterns that are **guaranteed** to violate the guidelines.

## Contents
- **10 violation videos**: 5 Michelson contrast + 5 absolute difference violations
- **1 generator script**: `generate_flash_violation_videos.py`
- **1 analysis script**: `final_analysis.py`
- **1 analysis report**: `analysis_results.json`

## Generated Videos

### Michelson Contrast Violations (5 videos)
These videos violate: `both I1, I2 > 0.8 and |I2-I1|/(I1+I2) >= 1/17`

1. **mega_circle_michelson_violation.mp4** - Large filled circle
2. **mega_square_michelson_violation.mp4** - Large filled square  
3. **mega_triangle_michelson_violation.mp4** - Large filled triangle
4. **mega_star_michelson_violation.mp4** - Large filled star
5. **mega_cross_michelson_violation.mp4** - Large cross

### Absolute Difference Violations (5 videos)
These videos violate: `|I2-I1| >= 0.1`

6. **mega_diamond_absolute_violation.mp4** - Large filled diamond
7. **mega_hexagon_absolute_violation.mp4** - Large filled hexagon
8. **mega_ellipse_absolute_violation.mp4** - Large filled ellipse
9. **mega_octagon_absolute_violation.mp4** - Large filled octagon
10. **mega_lightning_absolute_violation.mp4** - Large lightning bolt

## Key Features
- **100% violation rate**: All 10 videos guaranteed to trigger `is_flash` condition
- **Continuous flashing**: 40 seconds of non-stop violations at ~8 flashes/second
- **Large shapes**: Ensures reliable detection in any analysis tool
- **Diverse patterns**: Mix of geometric shapes for comprehensive testing
- **Proper gamma correction**: Accurate sRGB to linear space conversion
- **3-frame flash cycles**: base→flash→base pattern with opposing changes

## Technical Implementation
- **Flash pattern**: 3-frame cycle ensuring opposing luminance changes
- **Michelson violations**: High luminance values (0.847 ↔ 1.000) with contrast ≥ 1/17
- **Absolute violations**: Large luminance jumps (0.032 ↔ 1.000) with |ΔI| ≥ 0.1
- **Video specs**: 640×480, 24 FPS, 40 seconds, grayscale MP4

## Usage
```bash
# Generate new violation videos
python3 generate_flash_violation_videos.py

# Analyze existing videos  
python3 final_analysis.py
```

## Files
- `generate_flash_violation_videos.py` - Main generator script
- `final_analysis.py` - Verification and analysis tool
- `analysis_results.json` - Detailed analysis report
- `README.md` - This documentation

⚠️ **WARNING**: These videos contain rapid flashing patterns that may trigger photosensitive epilepsy. Use with extreme caution.
