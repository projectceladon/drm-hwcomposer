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

#ifndef OS_ANDROID_HWC_HWCSERVICEAPI_H_
#define OS_ANDROID_HWC_HWCSERVICEAPI_H_

#include <stdint.h>

#ifdef __cplusplus
#include <vector>
extern "C" {
#endif

// Header file version.  Please increment on any API additions.
// NOTE: Additions ONLY! No API modifications allowed (to maintain
// compatability).
#define HWCS_VERSION 1

typedef void *HWCSHANDLE;

typedef enum _EHwcsBool {
  HWCS_FALSE = 0,
  HWCS_TRUE = 1,
} EHwcsBool;

typedef int status_t;

HWCSHANDLE HwcService_Connect();
void HwcService_Disconnect(HWCSHANDLE hwcs);

const char *HwcService_GetHwcVersion(HWCSHANDLE hwcs);

// DisplayControl
// Enumerations for content type.
typedef enum _EHwcsContentType {
  HWCS_CP_CONTENT_TYPE0,  // Can support any HDCP specifiction.
  HWCS_CP_CONTENT_TYPE1,  // Can support only HDCP 2.2 and higher specification.
} EHwcsContentType;

// VideoControl

// The control enables the usage of HDCP for all planes supporting this feature
// on display. Some displays can support latest HDCP specification and also
// have ability to fallback to older specifications i.e. HDCP 2.2 and 1.4
// in case latest specification cannot be supported for some reason. Type
// of content can be set by content_type.
status_t HwcService_Video_EnableHDCPSession_ForDisplay(
    HWCSHANDLE hwcs, uint32_t connector, EHwcsContentType content_type);

// The control enables the usage of HDCP for all planes supporting this
// feature on all connected displays. Some displays can support latest HDCP
// specification and also have ability to fallback to older specifications
// i.e. HDCP 2.2 and 1.4 in case latest specification cannot be supported
// for some reason. Type of content can be set by content_type.
status_t HwcService_Video_EnableHDCPSession_AllDisplays(
    HWCSHANDLE hwcs, EHwcsContentType content_type);

// The control disables the usage of HDCP for all planes supporting this feature
// on display.
status_t HwcService_Video_DisableHDCPSession_ForDisplay(HWCSHANDLE hwcs,
                                                        uint32_t connector);

// The control disables the usage of HDCP for all planes supporting this feature
// on all connected displays.
status_t HwcService_Video_DisableHDCPSession_AllDisplays(HWCSHANDLE hwcs);

status_t HwcService_Video_SetHDCPSRM_ForDisplay(HWCSHANDLE hwcs,
                                                uint32_t connector,
                                                const int8_t *SRM,
                                                uint32_t SRMLengh);

status_t HwcService_Video_SetHDCPSRM_AllDisplays(HWCSHANDLE hwcs,
                                                 const int8_t *SRM,
                                                 uint32_t SRMLengh);
#ifdef __cplusplus
}
#endif

#endif  // OS_ANDROID_HWC_HWCSERVICEAPI_H_
