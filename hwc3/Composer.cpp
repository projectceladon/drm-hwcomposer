/*
 * Copyright (C) 2024 The Android Open Source Project
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
#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include "Composer.h"

#include <android-base/logging.h>
#include <android/binder_ibinder_platform.h>

#include "hwc3/ComposerClient.h"
#include "hwc3/Utils.h"
#include "utils/log.h"

namespace aidl::android::hardware::graphics::composer3::impl {

ndk::ScopedAStatus Composer::createClient(
    std::shared_ptr<IComposerClient>* out_client) {
  DEBUG_FUNC();

  auto client = ndk::SharedRefBase::make<ComposerClient>();
  if (!client || !client->Init()) {
    *out_client = nullptr;
    return ToBinderStatus(hwc3::Error::kNoResources);
  }

  *out_client = client;
  client_ = client;

  return ndk::ScopedAStatus::ok();
}

binder_status_t Composer::dump(int fd, const char** /*args*/,
                               uint32_t /*numArgs*/) {
  std::stringstream output;
  output << "hwc3-drm\n\n";

  auto client_instance = client_.lock();
  if (!client_instance) {
    return STATUS_OK;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto* client = static_cast<ComposerClient*>(client_instance.get());
  output << client->Dump();

  auto output_str = output.str();
  write(fd, output_str.c_str(), output_str.size());
  return STATUS_OK;
}

ndk::ScopedAStatus Composer::getCapabilities(std::vector<Capability>* caps) {
  DEBUG_FUNC();
  /* No capabilities advertised */
  caps->clear();
  return ndk::ScopedAStatus::ok();
}

::ndk::SpAIBinder Composer::createBinder() {
  auto binder = BnComposer::createBinder();
  AIBinder_setInheritRt(binder.get(), true);
  return binder;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
