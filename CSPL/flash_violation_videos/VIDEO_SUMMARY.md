# Flash Violation Test Videos Summary

This directory contains comprehensive test videos designed to violate WCAG 2.1 flash accessibility guidelines for testing compliance tools.

## B.1 Flash Luminance Threshold Violations (10 videos)

**Directory**: `/home/carbon/Documents/WashU/flash_violation_videos/`

These videos violate multiple flash guidelines simultaneously:
- **B.1**: Flash Luminance Threshold (luminance changes exceed safe limits)
- **B.3**: Flash Area Threshold (all shapes exceed 3,655 pixels²)
- **B.4**: Flash Frequency (20 flashes/second >> 4 flashes/second limit)

### Video List:
1. `mega_circle_michelson_20fps_violation.mp4` - Circle (31,416 pixels²)
2. `mega_cross_michelson_20fps_violation.mp4` - Cross shape
3. `mega_diamond_absolute_20fps_violation.mp4` - Diamond shape
4. `mega_ellipse_absolute_20fps_violation.mp4` - Ellipse (23,562 pixels²)
5. `mega_hexagon_absolute_20fps_violation.mp4` - Hexagon (19,486 pixels²)
6. `mega_lightning_absolute_20fps_violation.mp4` - Lightning bolt
7. `mega_octagon_absolute_20fps_violation.mp4` - Octagon (23,431 pixels²)
8. `mega_square_michelson_20fps_violation.mp4` - Square (22,500 pixels²)
9. `mega_star_michelson_20fps_violation.mp4` - Star shape
10. `mega_triangle_michelson_20fps_violation.mp4` - Triangle (19,488 pixels²)

**Technical Specs**:
- Resolution: 640×480
- Duration: 3 seconds
- Flash frequency: 20 fps (violates ≥4 threshold)
- All shapes exceed minimum area threshold of 3,655 pixels²

## B.2 Flash Color Threshold Violations (25 videos)

**Directory**: `/home/carbon/Documents/WashU/flash_violation_videos/color_flash_violations/`

These videos violate B.2 red flash thresholds while maintaining area and frequency violations:
- **B.2**: Flash Color Threshold (red ratio and opposing color changes exceed limits)
- **B.3**: Flash Area Threshold (all shapes exceed area limits)
- **B.4**: Flash Frequency (20 flashes/second)

### Video Categories:
Each of 5 shapes has 5 different color sequences:

#### Shapes:
- Circle
- Ellipse  
- Rectangle
- Square
- Triangle

#### Color Sequences (per shape):
1. `_1.mp4` - Red-cyan opposing flashes
2. `_2.mp4` - Red-green opposing flashes  
3. `_3.mp4` - Red-blue opposing flashes
4. `_4.mp4` - Red-magenta opposing flashes
5. `_5.mp4` - Red-white opposing flashes

**Technical Specs**:
- Resolution: 1920×1080 (HD)
- Duration: 3 seconds
- Flash frequency: 20 fps
- All shapes scaled to exceed area thresholds
- CIE color space calculations for precise B.2 violations

## Usage for Accessibility Testing

These videos are specifically designed to:

1. **Test B.1 compliance tools** - Use the 10 luminance violation videos
2. **Test B.2 compliance tools** - Use the 25 color flash violation videos  
3. **Test B.3 compliance tools** - All videos violate area thresholds
4. **Test B.4 compliance tools** - All videos violate frequency limits
5. **Test multi-guideline detection** - All videos violate multiple guidelines simultaneously

## Technical Implementation

### B.1 Videos:
- Gamma expansion and sRGB to linear conversion
- Michelson contrast calculations
- Absolute luminance difference measurements
- Precise flash area threshold calculations (10° × 7.5° viewing angles)

### B.2 Videos:
- Full CIE XYZ color space pipeline
- CIE 1976 UCS chromaticity coordinate conversion
- Red ratio calculations
- Opposing color change detection
- Color difference measurements in uniform color space

Both sets ensure reliable violations for comprehensive accessibility compliance testing.
