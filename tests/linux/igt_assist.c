#include "igt_assist.h"
#include "i915_drm.h"
#include "igt.h"


struct igt_fb g_fb[MAX_PIPE_NUM][MAX_PLANE_NUM];

LayerInfo layers[MAX_PIPE_NUM][MAX_PLANE_NUM] =
{
    {
        {1920 * 2, 1080 * 2, 0, 0, 0.5, 0x32, 0x80, 0x00, 0},
        {(1920 - 50 * 2) * 1, (1080 - 50 * 2) * 1, 50, 50, 0.5, 0x50, 0, 0, 0x20},
        {(1920 - 100 * 2) * 1, (1080 - 100 * 2) * 1, 100, 100, 0.5, 0x50, 0x20, 0, 0},
        {1920 - 150 * 2, 1080 - 150 * 2, 150, 150, 0.5, 0x50, 0x34, 0x23, 0},
        {1920 - 200 * 2, 1080 - 200 * 2, 200, 200, 0.5, 0x50, 0, 0x43, 0x80},
        {1920 - 250 * 2, 1080 - 250 * 2, 250, 250, 0.5, 0x50, 0x40, 0, 0},
        {1920 - 300 * 2, 1080 - 300 * 2, 300, 300, 0.5, 0xff, 0, 0x40, 0},
    },
    {
        {1920 * 2, 1080 * 2, 0, 0, 0.5, 0x32, 0x80, 0x00, 0},
        {(1920 - 50 * 2) * 1, (1080 - 50 * 2) * 1, 50, 50, 0.5, 0x50, 0, 0, 0x20},
        {(1920 - 100 * 2) * 1, (1080 - 100 * 2) * 1, 100, 100, 0.5, 0x50, 0x20, 0, 0},
        {1920 - 150 * 2, 1080 - 150 * 2, 150, 150, 0.5, 0x50, 0x34, 0x23, 0},
        {1920 - 200 * 2, 1080 - 200 * 2, 200, 200, 0.5, 0x50, 0, 0x43, 0x80},
        {1920 - 250 * 2, 1080 - 250 * 2, 250, 250, 0.5, 0x50, 0x40, 0, 0},
        {1920 - 300 * 2, 1080 - 300 * 2, 300, 300, 0.5, 0xff, 0, 0x40, 0},
    },
    {
        {1920 * 2, 1080 * 2, 0, 0, 0.5, 0x32, 0x80, 0x00, 0},
        {(1920 - 50 * 2) * 1, (1080 - 50 * 2) * 1, 50, 50, 0.5, 0x50, 0, 0, 0x20},
        {(1920 - 100 * 2) * 1, (1080 - 100 * 2) * 1, 100, 100, 0.5, 0x50, 0x20, 0, 0},
        {1920 - 150 * 2, 1080 - 150 * 2, 150, 150, 0.5, 0x50, 0x34, 0x23, 0},
        {1920 - 200 * 2, 1080 - 200 * 2, 200, 200, 0.5, 0x50, 0, 0x43, 0x80},
        {1920 - 250 * 2, 1080 - 250 * 2, 250, 250, 0.5, 0x50, 0x40, 0, 0},
        {1920 - 300 * 2, 1080 - 300 * 2, 300, 300, 0.5, 0xff, 0, 0x40, 0},
    },
    {
        {1920 * 2, 1080 * 2, 0, 0, 0.5, 0x32, 0x80, 0x00, 0},
        {(1920 - 50 * 2) * 1, (1080 - 50 * 2) * 1, 50, 50, 0.5, 0x50, 0, 0, 0x20},
        {(1920 - 100 * 2) * 1, (1080 - 100 * 2) * 1, 100, 100, 0.5, 0x50, 0x20, 0, 0},
        {1920 - 150 * 2, 1080 - 150 * 2, 150, 150, 0.5, 0x50, 0x34, 0x23, 0},
        {1920 - 200 * 2, 1080 - 200 * 2, 200, 200, 0.5, 0x50, 0, 0x43, 0x80},
        {1920 - 250 * 2, 1080 - 250 * 2, 250, 250, 0.5, 0x50, 0x40, 0, 0},
        {1920 - 300 * 2, 1080 - 300 * 2, 300, 300, 0.5, 0xff, 0, 0x40, 0},
    }
};

LayerInfo* get_layer_info(uint8_t pipe, uint8_t layer) {
    return &layers[pipe][layer];
}

int open_device() {
    int drm_fd = drm_open_driver_master(DRIVER_INTEL);
    return drm_fd;
}

void create_fb(int fd, uint8_t pipe, uint8_t layer)
{
    int width = layers[pipe][layer].w;
    int height = layers[pipe][layer].h;
    uint8_t A = layers[pipe][layer].A;
    uint8_t R = layers[pipe][layer].R;
    uint8_t G = layers[pipe][layer].G;
    uint8_t B = layers[pipe][layer].B;
    struct igt_fb* fb = &g_fb[pipe][layer];
    igt_create_fb(fd, width, height, DRM_FORMAT_ARGB8888, 0, fb);

    uint8_t *pbuffer = igt_fb_map_buffer(fd, fb);
    uint32_t *pintbuffer = (uint32_t *)pbuffer;
    uint32_t value = (A << 24) | (R << 16) | (G << 8) | B;
    for (int i = 0; i < width * height; i++)
    {
        pintbuffer[i] = value;
    }

    igt_fb_unmap_buffer(fb, pbuffer);
}

int get_fb(uint8_t pipe, uint8_t layer) {
    return g_fb[pipe][layer].fb_id;
}
