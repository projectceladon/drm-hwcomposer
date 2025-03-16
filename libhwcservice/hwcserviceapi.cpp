/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "hwcserviceapi.h"

#include "icontrols.h"
#include "iservice.h"

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>

#include <utils/RefBase.h>
#include <utils/String8.h>

using namespace std;
using namespace android;

using namespace hwcomposer;

extern "C" {
struct HwcsContext {
  sp<IService> mHwcService;
  sp<IControls> mControls;
};

HWCSHANDLE HwcService_Connect() {
  ProcessState::self()
      ->startThreadPool();  // Required for starting binder threads
  ALOGD("%s start",__FUNCTION__);
  HwcsContext context;
  context.mHwcService = interface_cast<IService>(
      defaultServiceManager()->waitForService(String16(HWC_SERVICE_NAME)));
  if (context.mHwcService == NULL) {
    printf("%d\n", __LINE__);
    ALOGE("%s context.mHwcService == NULL",__FUNCTION__);
    return NULL;
  }

  ALOGD("%s IService ok",__FUNCTION__);
  context.mControls = context.mHwcService->GetControls();
  ALOGD("%s context.mControls",__FUNCTION__);
  if (context.mControls == NULL) {
    printf("%d\n", __LINE__);
    ALOGE("%s context.mControls == NULL",__FUNCTION__);
    return NULL;
  }
  printf("%d\n", __LINE__);
  ALOGD("HDcPD_ %s ok",__FUNCTION__);
  return new HwcsContext(context);
}

void HwcService_Disconnect(HWCSHANDLE hwcs) {
  if (hwcs != NULL) {
    delete static_cast<HwcsContext*>(hwcs);
  }
}

const char* HwcService_GetHwcVersion(HWCSHANDLE hwcs) {
  HwcsContext* pContext = static_cast<HwcsContext*>(hwcs);
  if (!pContext) {
    return NULL;
  }

  static String8 version = pContext->mHwcService->GetHwcVersion();
  if (version.length() == 0) {
    return NULL;
  }
  return version;
}

status_t HwcService_Video_EnableHDCPSession_ForDisplay(
    HWCSHANDLE hwcs, uint32_t connector, EHwcsContentType content_type) {
  HwcsContext* pContext = static_cast<HwcsContext*>(hwcs);
  if (!pContext) {
    return android::BAD_VALUE;
  }

  return pContext->mControls->EnableHDCPSessionForDisplay(connector,
                                                          content_type);
}

status_t HwcService_Video_EnableHDCPSession_AllDisplays(
    HWCSHANDLE hwcs, EHwcsContentType content_type) {
  HwcsContext* pContext = static_cast<HwcsContext*>(hwcs);
  if (!pContext) {
    return android::BAD_VALUE;
  }

  return pContext->mControls->EnableHDCPSessionForAllDisplays(content_type);
}

status_t HwcService_Video_SetHDCPSRM_AllDisplays(HWCSHANDLE hwcs,
                                                 const int8_t* SRM,
                                                 uint32_t SRMLength) {
  HwcsContext* pContext = static_cast<HwcsContext*>(hwcs);
  if (!pContext) {
    return android::BAD_VALUE;
  }

  return pContext->mControls->SetHDCPSRMForAllDisplays(SRM, SRMLength);
}

status_t HwcService_Video_SetHDCPSRM_ForDisplay(HWCSHANDLE hwcs,
                                                uint32_t connector,
                                                const int8_t* SRM,
                                                uint32_t SRMLength) {
  HwcsContext* pContext = static_cast<HwcsContext*>(hwcs);

  if (!pContext) {
    return android::BAD_VALUE;
  }
  return pContext->mControls->SetHDCPSRMForDisplay(connector, SRM, SRMLength);
}

status_t HwcService_Video_DisableHDCPSession_ForDisplay(HWCSHANDLE hwcs,
                                                        uint32_t connector) {
  HwcsContext* pContext = static_cast<HwcsContext*>(hwcs);
  if (!pContext) {
    return android::BAD_VALUE;
  }

  return pContext->mControls->DisableHDCPSessionForDisplay(connector);
}
}
