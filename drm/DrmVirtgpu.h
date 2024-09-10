/*
 * Copyright 2013 Red Hat
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef DRM_VIRTGPU_H
#define DRM_VIRTGPU_H

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints.
 *
 * Do not use pointers, use __u64 instead for 32 bit / 64 bit user/kernel
 * compatibility Keep fields aligned to their size
 */

#define VIRTGPU_PARAM_QUERY_DEV 11 /* Query the virtio device name. */
#define DRM_VIRTGPU_GETPARAM    0x03
#define ARRAY_SIZE(A) (sizeof(A) / sizeof(*(A)))

struct drm_virtgpu_getparam {
  __u64 param;
  __u64 value;
};

struct virtgpu_param {
  uint64_t param;
  const char *name;
  uint32_t value;
};

#define DRM_IOCTL_VIRTGPU_GETPARAM \
  DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_GETPARAM,\
           struct drm_virtgpu_getparam)

#define PARAM(x)                                                                                   \
  (struct virtgpu_param)                                                                     \
  {                                                                                          \
    x, #x, 0                                                                           \
  }

static struct virtgpu_param params[] = {
  PARAM(VIRTGPU_PARAM_QUERY_DEV),
};

#if defined(__cplusplus)
}
#endif

#endif
