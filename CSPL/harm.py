import cv2
import numpy as np
import os

# Video parameters
fps = 24
duration_sec = 40
total_frames = fps * duration_sec
frame_size = (640, 480)

# Output directory
output_dir = '/home/carbon/Documents/WashU/flash_videos'
os.makedirs(output_dir, exist_ok=True)

# Shape names
shape_names = [
    "circle", "square", "triangle", "pentagon",
    "hexagon", "star", "ellipse", "cross",
    "diamond", "line"
]

# Intensity settings
# Michelson branch: both intensities > 0.8 after gamma expansion
branch1_base = 240   # ~0.94 → I ≈ 0.88 (>0.8)
branch1_flash = 255  # I = 1.0
# Absolute-diff branch: any base < flash for |ΔI| ≥ 0.1
branch2_base = 100   # ~0.392 → I ≈ 0.127
branch2_flash = 255  # I = 1.0

def draw_shape(img, shape, intensity):
    h, w = img.shape
    center = (w // 2, h // 2)
    if shape == "circle":
        cv2.circle(img, center, 80, intensity, -1)
    elif shape == "square":
        cv2.rectangle(img, (center[0]-80, center[1]-80),
                      (center[0]+80, center[1]+80), intensity, -1)
    elif shape == "triangle":
        pts = np.array([[center[0], center[1]-90],
                        [center[0]-80, center[1]+60],
                        [center[0]+80, center[1]+60]])
        cv2.fillPoly(img, [pts], intensity)
    elif shape == "pentagon":
        angles = np.linspace(0, 2*np.pi, 6, endpoint=False)
        pts = np.array([[int(center[0] + 80*np.cos(a)),
                         int(center[1] + 80*np.sin(a))] for a in angles])
        cv2.fillPoly(img, [pts], intensity)
    elif shape == "hexagon":
        angles = np.linspace(0, 2*np.pi, 7, endpoint=False)
        pts = np.array([[int(center[0] + 70*np.cos(a)),
                         int(center[1] + 70*np.sin(a))] for a in angles])
        cv2.fillPoly(img, [pts], intensity)
    elif shape == "star":
        pts = np.array([
            [center[0], center[1]-90], [center[0]+25, center[1]-30],
            [center[0]+90, center[1]-30], [center[0]+40, center[1]+10],
            [center[0]+60, center[1]+80], [center[0], center[1]+40],
            [center[0]-60, center[1]+80], [center[0]-40, center[1]+10],
            [center[0]-90, center[1]-30], [center[0]-25, center[1]-30]
        ])
        cv2.fillPoly(img, [pts], intensity)
    elif shape == "ellipse":
        cv2.ellipse(img, center, (80, 50), 0, 0, 360, intensity, -1)
    elif shape == "cross":
        cv2.line(img, (center[0]-80, center[1]),
                 (center[0]+80, center[1]), intensity, 30)
        cv2.line(img, (center[0], center[1]-80),
                 (center[0], center[1]+80), intensity, 30)
    elif shape == "diamond":
        pts = np.array([[center[0], center[1]-90],
                        [center[0]+80, center[1]],
                        [center[0], center[1]+90],
                        [center[0]-80, center[1]]])
        cv2.fillPoly(img, [pts], intensity)
    elif shape == "line":
        cv2.line(img, (center[0]-90, center[1]-90),
                 (center[0]+90, center[1]+90), intensity, 10)

# Generate continuous flash videos
for idx, shape in enumerate(shape_names):
    branch1 = (idx < 5)
    base_intensity = branch1_base if branch1 else branch2_base
    flash_intensity = branch1_flash if branch1 else branch2_flash
    fname = f"{shape}_{'mich_true' if branch1 else 'abs_true'}.mp4"
    path = os.path.join(output_dir, fname)

    writer = cv2.VideoWriter(path, cv2.VideoWriter_fourcc(*'mp4v'),
                             fps, frame_size, isColor=False)

    for f in range(total_frames):
        # Background with base intensity
        frame = np.full((frame_size[1], frame_size[0]), base_intensity, dtype=np.uint8)
        # Toggle flashing every frame
        if f % 2 == 0:
            intensity = flash_intensity
        else:
            intensity = base_intensity
        draw_shape(frame, shape, intensity)
        writer.write(frame)

    writer.release()

print("Generated true continuous-flash videos in:", output_dir)
