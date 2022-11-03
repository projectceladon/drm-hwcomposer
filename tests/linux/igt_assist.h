#ifndef IGT_ASSIST_H
#define IGT_ASSIST_H

#define MAX_PIPE_NUM 4
#define MAX_PLANE_NUM 7
#define MAX_LAYER_NUM MAX_PLANE_NUM
#if !defined ANDROID && defined UBUNTU
#include <bits/stdint-uintn.h>
#else
typedef unsigned char uint8_t;
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _LayerInfo
{
    int w;
    int h;
    int x;
    int y;
    float alpha; // this is the alpha for whole plane
    uint8_t A; // this is the alpha in ARGB pixel value
    uint8_t R;
    uint8_t G;
    uint8_t B;
} LayerInfo;

int open_device();
void create_fb(int fd, uint8_t pipe, uint8_t layer);
LayerInfo* get_layer_info(uint8_t pipe, uint8_t layer);
int get_fb(uint8_t pipe, uint8_t layer);
#ifdef __cplusplus
}
#endif
#endif

