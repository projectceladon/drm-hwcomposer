/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gralloc1bufferhandler.h"

#include <cutils/native_handle.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <ui/GraphicBuffer.h>
#include "vautils.h"
#include <cutils/native_handle.h>
#include "cros_gralloc_handle.h"
#include "drm_fourcc.h"


namespace android {

// static
NativeBufferHandler *NativeBufferHandler::CreateInstance(uint32_t fd) {
  Gralloc1BufferHandler *handler = new Gralloc1BufferHandler(fd);
  if (!handler)
    return NULL;

  if (!handler->Init()) {
    ALOGE("Failed to initialize GralocBufferHandlers.");
    delete handler;
    return NULL;
  }
  return handler;
}

Gralloc1BufferHandler::Gralloc1BufferHandler(uint32_t fd)
    : fd_(fd),
      gralloc_(nullptr),
      device_(nullptr),
      register_(nullptr),
      release_(nullptr),
      dimensions_(nullptr),
      lock_(nullptr),
      unlock_(nullptr),
      create_descriptor_(nullptr),
      destroy_descriptor_(nullptr),
      set_consumer_usage_(nullptr),
      set_dimensions_(nullptr),
      set_format_(nullptr),
      set_producer_usage_(nullptr),
      allocate_(nullptr){
}

Gralloc1BufferHandler::~Gralloc1BufferHandler() {
  gralloc1_device_t *gralloc1_dvc =
      reinterpret_cast<gralloc1_device_t *>(device_);
  gralloc1_dvc->common.close(device_);
}

bool Gralloc1BufferHandler::Init() {
  int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                          (const hw_module_t **)&gralloc_);
  if (ret) {
    ALOGE("Failed to get gralloc module");
    return false;
  }

  ret = gralloc_->methods->open(gralloc_, GRALLOC_HARDWARE_MODULE_ID, &device_);
  if (ret) {
    ALOGE("Failed to open gralloc module");
    return false;
  }

  gralloc1_device_t *gralloc1_dvc =
      reinterpret_cast<gralloc1_device_t *>(device_);
  register_ = reinterpret_cast<GRALLOC1_PFN_RETAIN>(
      gralloc1_dvc->getFunction(gralloc1_dvc, GRALLOC1_FUNCTION_RETAIN));
  release_ = reinterpret_cast<GRALLOC1_PFN_RELEASE>(
      gralloc1_dvc->getFunction(gralloc1_dvc, GRALLOC1_FUNCTION_RELEASE));
  lock_ = reinterpret_cast<GRALLOC1_PFN_LOCK>(
      gralloc1_dvc->getFunction(gralloc1_dvc, GRALLOC1_FUNCTION_LOCK));
  unlock_ = reinterpret_cast<GRALLOC1_PFN_UNLOCK>(
      gralloc1_dvc->getFunction(gralloc1_dvc, GRALLOC1_FUNCTION_UNLOCK));

  dimensions_ =
      reinterpret_cast<GRALLOC1_PFN_GET_DIMENSIONS>(gralloc1_dvc->getFunction(
          gralloc1_dvc, GRALLOC1_FUNCTION_GET_DIMENSIONS));

  create_descriptor_ = reinterpret_cast<GRALLOC1_PFN_CREATE_DESCRIPTOR>(
      gralloc1_dvc->getFunction(gralloc1_dvc,
                                GRALLOC1_FUNCTION_CREATE_DESCRIPTOR));
  destroy_descriptor_ = reinterpret_cast<GRALLOC1_PFN_DESTROY_DESCRIPTOR>(
      gralloc1_dvc->getFunction(gralloc1_dvc,
                                GRALLOC1_FUNCTION_DESTROY_DESCRIPTOR));

  set_consumer_usage_ = reinterpret_cast<GRALLOC1_PFN_SET_CONSUMER_USAGE>(
      gralloc1_dvc->getFunction(gralloc1_dvc,
                                GRALLOC1_FUNCTION_SET_CONSUMER_USAGE));
  set_dimensions_ =
      reinterpret_cast<GRALLOC1_PFN_SET_DIMENSIONS>(gralloc1_dvc->getFunction(
          gralloc1_dvc, GRALLOC1_FUNCTION_SET_DIMENSIONS));
  set_format_ = reinterpret_cast<GRALLOC1_PFN_SET_FORMAT>(
      gralloc1_dvc->getFunction(gralloc1_dvc, GRALLOC1_FUNCTION_SET_FORMAT));
  set_producer_usage_ = reinterpret_cast<GRALLOC1_PFN_SET_PRODUCER_USAGE>(
      gralloc1_dvc->getFunction(gralloc1_dvc,
                                GRALLOC1_FUNCTION_SET_PRODUCER_USAGE));
  allocate_ = reinterpret_cast<GRALLOC1_PFN_ALLOCATE>(
      gralloc1_dvc->getFunction(gralloc1_dvc, GRALLOC1_FUNCTION_ALLOCATE));
#ifdef USE_GRALLOC1
  set_modifier_ = reinterpret_cast<GRALLOC1_PFN_SET_MODIFIER>(
      gralloc1_dvc->getFunction(gralloc1_dvc, GRALLOC1_FUNCTION_SET_MODIFIER));
#endif
  return true;
}

bool Gralloc1BufferHandler::CreateBuffer(uint32_t w, uint32_t h, int format,
                                         DRMHwcNativeHandle *handle,
                                         uint32_t layer_type,
                                         bool *modifier_used,
                                         int64_t preferred_modifier,
                                         bool /*raw_pixel_buffer*/) const {
 struct gralloc_handle *temp = new struct gralloc_handle();
 (void)preferred_modifier;
 gralloc1_device_t *gralloc1_dvc =
  reinterpret_cast<gralloc1_device_t *>(device_);
  uint32_t usage = 0;
  uint32_t pixel_format = 0;
  bool force_normal_usage = false;

  create_descriptor_(gralloc1_dvc, &temp->gralloc1_buffer_descriptor_t_);
  if (format != 0) {
    pixel_format = DrmFormatToHALFormat(format);
  }
  if (pixel_format == 0) {
    pixel_format = HAL_PIXEL_FORMAT_RGBA_8888;
  }
  set_format_(gralloc1_dvc, temp->gralloc1_buffer_descriptor_t_, pixel_format);
#ifdef ENABLE_RBC
	if (preferred_modifier != 0) {
	  uint64_t modifier = 0;
	  if (set_modifier_) {
		if (preferred_modifier != -1) {
		  modifier = preferred_modifier;
		}
		set_modifier_(gralloc1_dvc, temp->gralloc1_buffer_descriptor_t_,
					  modifier);
	  }
	  if (modifier_used && modifier != DRM_FORMAT_MOD_NONE) {
		*modifier_used = true;
	  }
	} else {
	  *modifier_used = false;
	}
#else
	if (modifier_used) {
	  *modifier_used = false;
	}
#endif
  if (layer_type == 3){
//      !IsSupportedMediaFormat(format)) {
    ALOGD("Forcing normal usage for Video Layer. \n");
    force_normal_usage = true;
  }

  if ((layer_type == 0) || force_normal_usage) {
    usage |= GRALLOC1_CONSUMER_USAGE_HWCOMPOSER |
             GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET |
             GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
    layer_type = 0;
  } else if (layer_type == 3 ||
             layer_type == 2) {
    switch (pixel_format) {
      case HAL_PIXEL_FORMAT_YCbCr_422_I:
      case HAL_PIXEL_FORMAT_Y8:
        usage |= GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE |
                 GRALLOC1_PRODUCER_USAGE_VIDEO_DECODER;
        break;
      default:
        usage |= GRALLOC1_PRODUCER_USAGE_CAMERA |
                 GRALLOC1_CONSUMER_USAGE_CAMERA |
                 GRALLOC1_PRODUCER_USAGE_VIDEO_DECODER |
                 GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
    }
  } else if (layer_type == 1) {
    usage |= GRALLOC1_CONSUMER_USAGE_CURSOR;
  }
  set_consumer_usage_(gralloc1_dvc, temp->gralloc1_buffer_descriptor_t_, usage);
  set_producer_usage_(gralloc1_dvc, temp->gralloc1_buffer_descriptor_t_, usage);
  set_dimensions_(gralloc1_dvc, temp->gralloc1_buffer_descriptor_t_, w, h);
  allocate_(gralloc1_dvc, 1, &temp->gralloc1_buffer_descriptor_t_,
            &temp->handle_);
  if (!temp) {
    ALOGE("Failed to allocate buffer \n");
  }
  *handle = temp;
  return true;
}

bool Gralloc1BufferHandler::ReleaseBuffer(DRMHwcNativeHandle handle) const {
  gralloc1_device_t *gralloc1_dvc =
      reinterpret_cast<gralloc1_device_t *>(device_);
  release_(gralloc1_dvc, handle->handle_);

  if (handle->gralloc1_buffer_descriptor_t_ > 0)
    destroy_descriptor_(gralloc1_dvc, handle->gralloc1_buffer_descriptor_t_);

  return true;
}

void Gralloc1BufferHandler::DestroyHandle(DRMHwcNativeHandle handle) const {
  if (handle) {
    int ret = native_handle_close((native_handle_t* )handle->handle_);
    if (ret){
      ALOGE("Failed to close native handle %d", ret);
      return;
    }
    if (NULL != handle->handle_)
      ret = native_handle_delete((native_handle_t* )handle->handle_);
      if (NULL != handle->handle_){
      delete handle->handle_;
      handle->handle_= NULL;
      }
    }
}

bool Gralloc1BufferHandler::ImportBuffer(DRMHwcNativeHandle handle) const {
  gralloc1_device_t *gralloc1_dvc =
      reinterpret_cast<gralloc1_device_t *>(device_);
  register_(gralloc1_dvc, handle->handle_);

  return true;
}

uint32_t Gralloc1BufferHandler::GetTotalPlanes(DRMHwcNativeHandle handle) const {
 // return handle->meta_data_.num_planes_;
 (void)handle;
 return 0;
}

void Gralloc1BufferHandler::CopyHandle(DRMHwcNativeHandle source,
                                       DRMHwcNativeHandle target) const {
 // *target = source;
 cros_gralloc_handle *source_handle = (cros_gralloc_handle *)source->handle_;
 cros_gralloc_handle *target_handle = (cros_gralloc_handle *)target->handle_;
	target_handle->format = source_handle->format;
	// target_handle->tiling_mode = source_handle->tiling_mode;
	target_handle->width = source_handle->width;
	target_handle->height = source_handle->height;
	target_handle->droid_format = source_handle->droid_format;
	// target_handle->is_interlaced = source_handle->is_interlaced;
	int32_t numplanes = source_handle->numFds;
	target_handle->numFds = source_handle->numFds;
	for (int32_t p = 0; p < numplanes; p++) {
	target_handle->offsets[p] = source_handle->offsets[p];
		target_handle->strides[p] = source_handle->strides[p];
		target_handle->fds[p] = source_handle->fds[p];
		target_handle->format_modifiers[p] =source_handle->format_modifiers[p];
	}
	target_handle->consumer_usage = source_handle->consumer_usage;
}

void *Gralloc1BufferHandler::Map(DRMHwcNativeHandle handle, uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height,
                                 uint32_t * /*stride*/, void **map_data,
                                 size_t /*plane*/) const {
  cros_gralloc_handle *gr_handle = (cros_gralloc_handle *)handle->handle_;
  if (!gr_handle) {
    ALOGE("could not find gralloc drm handle");
    return NULL;
  }

  int acquireFence = -1;
  gralloc1_rect_t rect{};
  rect.left = x;
  rect.top = y;
  rect.width = width;
  rect.height = height;

  gralloc1_device_t *gralloc1_dvc =
      reinterpret_cast<gralloc1_device_t *>(device_);
  uint32_t status = lock_(gralloc1_dvc, handle->handle_,
                          GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN,
                          GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN, &rect,
                          map_data, acquireFence);
  return (GRALLOC1_ERROR_NONE == status) ? *map_data : NULL;
}

int32_t Gralloc1BufferHandler::UnMap(DRMHwcNativeHandle handle,
                                     void * /*map_data*/) const {
  cros_gralloc_handle *gr_handle = (cros_gralloc_handle *)handle->handle_;
  if (!gr_handle) {
    ALOGE("could not find gralloc drm handle");
    return GRALLOC1_ERROR_BAD_HANDLE;
  }

  int releaseFence = 0;
  gralloc1_device_t *gralloc1_dvc =
      reinterpret_cast<gralloc1_device_t *>(device_);
  return unlock_(gralloc1_dvc, handle->handle_, &releaseFence);
}

bool Gralloc1BufferHandler::GetInterlace(DRMHwcNativeHandle handle) const {
 if(!handle)
  return true;
 return false;
}

#ifdef ENABLE_DUMP_YUV_DATA
void Gralloc1BufferHandler::DumpBuffer(buffer_handle_t handle) {
  if (NULL == handle)
    return;
  char dump_file[256] = {0};
  cros_gralloc_handle *gr_handle = (cros_gralloc_handle *)handle;
  native_handle_t *handle_copy;
  uint8_t* pixels = nullptr;
  GraphicBufferMapper &gm(GraphicBufferMapper::get());
  int ret = gm.importBuffer(handle, gr_handle->width, gr_handle->height, 1,
                          DrmFormatToHALFormat(gr_handle->format), gr_handle->usage,
                          gr_handle->pixel_stride, const_cast<buffer_handle_t *>(&handle_copy));

  if (ret != 0) {
    ALOGE("Failed to import buffer while dumping");
  } else {
    ret = gm.lock(handle_copy, GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_NEVER,
                     Rect(gr_handle->width, gr_handle->height), reinterpret_cast<void**>(&pixels));
    if (ret != 0) {
      ALOGE("Failed to lock buffer while dumping");
    } else {
      char ctime[32];
      time_t t = time(0);
      static int i = 0;
      if (i >= 500)
        i = 0;
      strftime(ctime, sizeof(ctime), "%Y-%m-%d", localtime(&t));
      sprintf(dump_file, "/data/local/traces/dump_%dx%d_0x%x_%s_%d", gr_handle->width, gr_handle->height, gr_handle->format, ctime,i++);
      int file_fd = 0;
      file_fd = open(dump_file, O_RDWR|O_CREAT, 0666);
      if (file_fd == -1) {
        ALOGE("Failed to open %s while dumping", dump_file);
      } else {
        write(file_fd, pixels, gr_handle->sizes[0]);
        close(file_fd);
      }
      gm.unlock(handle_copy);
      gm.freeBuffer(handle_copy);
    }
  }
}
#endif

}  // namespace hwcomposer
