/*
 * Copyright (C) 2022 The Android Open Source Project
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

#define LOG_TAG "drmhwc"

#include "DrmDisplayPipeline.h"

#include "DrmAtomicStateManager.h"
#include "DrmConnector.h"
#include "DrmCrtc.h"
#include "DrmDevice.h"
#include "DrmEncoder.h"
#include "DrmPlane.h"
#include "utils/log.h"
#include "utils/properties.h"
#include <xf86drmMode.h>
namespace android {

template <class O>
auto PipelineBindable<O>::BindPipeline(DrmDisplayPipeline *pipeline,
                                       bool return_object_if_bound)
    -> std::shared_ptr<BindingOwner<O>> {
  auto owner_object = owner_object_.lock();
  if (owner_object) {
    if (bound_pipeline_ == pipeline && return_object_if_bound) {
      return owner_object;
    }

    return {};
  }
  owner_object = std::make_shared<BindingOwner<O>>(static_cast<O *>(this));

  owner_object_ = owner_object;
  bound_pipeline_ = pipeline;
  return owner_object;
}

static auto TryCreatePipeline(DrmDevice &dev, DrmConnector &connector,
                              DrmEncoder &enc, DrmCrtc &crtc)
    -> std::unique_ptr<DrmDisplayPipeline> {
  /* Check if resources are available */

  auto pipe = std::make_unique<DrmDisplayPipeline>();
  pipe->device = &dev;

  pipe->connector = connector.BindPipeline(pipe.get());
  pipe->encoder = enc.BindPipeline(pipe.get());
  pipe->crtc = crtc.BindPipeline(pipe.get());

  if (!pipe->connector || !pipe->encoder || !pipe->crtc) {
    return {};
  }

  std::vector<DrmPlane *> primary_planes;

  /* Attach necessary resources */
  auto display_planes = std::vector<DrmPlane *>();
  for (const auto &plane : dev.GetPlanes()) {
    if (plane->IsCrtcSupported(crtc)) {
      switch (plane->GetType()) {
        case DRM_PLANE_TYPE_PRIMARY:
          primary_planes.emplace_back(plane.get());
          break;
        case DRM_PLANE_TYPE_OVERLAY:
        case DRM_PLANE_TYPE_CURSOR:
          break;
        default:
          ALOGE("Unknown type for plane %d", plane->GetId());
          break;
      }
    }
  }

  if (primary_planes.empty()) {
    ALOGE("Primary plane for CRTC %d not found", crtc.GetId());
    return {};
  }

  for (const auto &plane : primary_planes) {
    pipe->primary_plane = plane->BindPipeline(pipe.get());
    if (pipe->primary_plane) {
      break;
    }
  }

  if (!pipe->primary_plane) {
    ALOGE("Failed to bind primary plane");
    return {};
  }

  pipe->atomic_state_manager = DrmAtomicStateManager::CreateInstance(
      pipe.get());

  return pipe;
}

static auto TryCreatePipelineUsingEncoder(DrmDevice &dev, DrmConnector &conn,
                                          DrmEncoder &enc)
    -> std::unique_ptr<DrmDisplayPipeline> {
  /* First try to use the currently-bound crtc */
  auto *crtc = dev.FindCrtcById(enc.GetCurrentCrtcId());
  if (crtc != nullptr) {
    auto pipeline = TryCreatePipeline(dev, conn, enc, *crtc);
    if (pipeline) {
      crtc->BindConnector(conn.GetId());
      return pipeline;
    }
  }

  /* Try to find a possible crtc which will work */
  for (const auto &crtc : dev.GetCrtcs()) {
    if (enc.SupportsCrtc(*crtc) && crtc->CanBind(conn.GetId())) {
      auto pipeline = TryCreatePipeline(dev, conn, enc, *crtc);
      if (pipeline) {
        crtc->BindConnector(conn.GetId());
        return pipeline;
      }
    }
  }

  /* We can't use this encoder, but nothing went wrong, try another one */
  return {};
}

auto DrmDisplayPipeline::AtomicDisablePipeline() -> int {
  auto pset = MakeDrmModeAtomicReqUnique();
  if (!pset) {
    ALOGE("Failed to allocate property set");
    return -EINVAL;
  }

  if (!connector->Get()->GetCrtcIdProperty().AtomicSet(*pset, 0) ||
      !crtc->Get()->GetActiveProperty().AtomicSet(*pset,  0)||
      !crtc->Get()->GetModeProperty().AtomicSet(*pset, 0)){
        ALOGE("Failed to atomic disable connector & crtc property set");
        return -EINVAL;
  }

  int err = drmModeAtomicCommit(*(device->GetFd()), pset.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, device);
  if (err != 0) {
    ALOGE("Failed to commit pset ret=%d\n", err);
    return -EINVAL;
  }

  return 0;
}

auto DrmDisplayPipeline::CreatePipeline(DrmConnector &connector)
    -> std::unique_ptr<DrmDisplayPipeline> {
  auto &dev = connector.GetDev();
  /* Try to use current setup first */
  auto *encoder = dev.FindEncoderById(connector.GetCurrentEncoderId());

  if (encoder != nullptr) {
    auto pipeline = TryCreatePipelineUsingEncoder(dev, connector, *encoder);
    if (pipeline) {
      return pipeline;
    }
  }

  for (const auto &enc : dev.GetEncoders()) {
    if (connector.SupportsEncoder(*enc)) {
      auto pipeline = TryCreatePipelineUsingEncoder(dev, connector, *enc);
      if (pipeline) {
        return pipeline;
      }
    }
  }

  ALOGE("Could not find a suitable encoder/crtc for connector %s",
        connector.GetName().c_str());

  return {};
}

auto DrmDisplayPipeline::GetUsablePlanes() -> UsablePlanes {
  UsablePlanes pair;
  auto &[planes, cursor] = pair;

  planes.emplace_back(primary_plane);

  for (const auto &plane : device->GetPlanes()) {
    if (plane->IsCrtcSupported(*crtc->Get())) {
      if (Properties::UseOverlayPlanes() &&
          plane->GetType() == DRM_PLANE_TYPE_OVERLAY) {
        auto op = plane->BindPipeline(this, true);
        if (op) {
          planes.emplace_back(op);
        }
      } else if (plane->GetType() == DRM_PLANE_TYPE_CURSOR) {
        if (cursor) {
          ALOGW(
              "Encountered multiple cursor planes for CRTC %d. Ignoring "
              "plane %d",
              crtc->Get()->GetId(), plane->GetId());
        } else {
          cursor = plane->BindPipeline(this, true);
        }
      }
    }
  }

  return pair;
}

DrmDisplayPipeline::~DrmDisplayPipeline() {
  if (atomic_state_manager)
    atomic_state_manager->StopThread();
}

}  // namespace android
