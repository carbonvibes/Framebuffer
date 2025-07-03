To load the Module: sudo insmod *.ko
To unload the Module: sudo rmmod *
To view kernel stats: sudo dmesg
To view the output: cat /proc/drm_fb_raw and one more too.
ffmpeg -f rawvideo -pixel_format bgr0 -video_size 3840x1080 -i /proc/drm_fb_raw -frames:v 1 framebuffer.png
ffplay -f rawvideo -pixel_format bgr0 -video_size 3840x1080 frame_linear.raw
./intel_y_tile_to_linear 3840 1080 15360 X /proc/drm_fb_raw frame_linear.raw



Modifier	Layout	Tile size (bytes)	Tile size (pixels, 32 bpp)
0x…0001	X	512 × 8 B	128 px × 8 px
0x…0009	Y	128 × 32 B	128 px × 32 px
0x…000a	Yf	256 × 32 B	256 px × 32 px