// SPDX-License-Identifier: MIT
/* intel_y_tile_to_linear.c – copyright Intel Corporation
 * Convert an Intel Y-tiled/X-tiled framebuffer to linear layout.
 *
 * Build :  gcc -O2 intel_y_tile_to_linear.c -o intel_y_tile_to_linear
 * Usage :  intel_y_tile_to_linear <width> <height> <pitch> <X|Y|Yf> <in.raw> <out.raw>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static inline unsigned tile_offset_x(unsigned x, unsigned tile_width)
{
    return (x & (tile_width - 1));
}

static inline unsigned tile_offset_y(unsigned y, unsigned tile_height)
{
    return (y & (tile_height - 1));
}

static void convert(uint8_t *dst, const uint8_t *src,
                    unsigned w, unsigned h, unsigned pitch,
                    unsigned tile_w, unsigned tile_h)
{
    const unsigned tile_size = tile_w * tile_h;
    const unsigned tiles_per_row = pitch / tile_w;

    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w * 4; x++) {
            unsigned tile_x = (x / tile_w);
            unsigned tile_y = (y / tile_h);
            unsigned tile_index = tile_y * tiles_per_row + tile_x;

            unsigned in_tile_x = tile_offset_x(x, tile_w);
            unsigned in_tile_y = tile_offset_y(y, tile_h);

            unsigned offset = tile_index * tile_size +
                              in_tile_y * tile_w + in_tile_x;
            dst[y * w * 4 + x] = src[offset];
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr,
            "usage: %s <width> <height> <pitch> <X|Y|Yf> <in.raw> <out.raw>\n", argv[0]);
        return EXIT_FAILURE;
    }

    unsigned w     = atoi(argv[1]);
    unsigned h     = atoi(argv[2]);
    unsigned pitch = atoi(argv[3]);
    char layout    = argv[4][0];

    unsigned tile_w = (layout == 'X') ? 512 : 128;   /* bytes */
    unsigned tile_h = (layout == 'X') ?  8  :  32;   /* rows  */

    size_t src_size = (size_t)h * pitch;
    size_t dst_size = (size_t)h * w * 4;

    uint8_t *src = malloc(src_size);
    uint8_t *dst = malloc(dst_size);
    if (!src || !dst) { perror("malloc"); return EXIT_FAILURE; }

    FILE *fi = fopen(argv[5], "rb");
    FILE *fo = fopen(argv[6], "wb");
    if (!fi || !fo) { perror("fopen"); return EXIT_FAILURE; }

    fread(src, 1, src_size, fi);
    convert(dst, src, w, h, pitch, tile_w, tile_h);
    fwrite(dst, 1, dst_size, fo);

    return EXIT_SUCCESS;
}