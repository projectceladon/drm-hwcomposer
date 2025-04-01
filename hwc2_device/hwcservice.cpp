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

#include "hwcservice.h"
#include <binder/IInterface.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>
#include "utils/hwcdefs.h"
#include "DrmHwcTwo.h"

#ifdef LOG_TAG
#undef LOG_TAG
#define LOG_TAG "hwc-service"
#endif
#define HWC_VERSION_STRING                                             \
  "VERSION:HWC 2.0 GIT Branch & Latest Commit:" HWC_VERSION_GIT_BRANCH \
  " " HWC_VERSION_GIT_SHA " " __DATE__ " " __TIME__

namespace android {
using namespace hwcomposer;

HwcService::HwcService() : mpHwc(NULL), initialized_(false) {
}

HwcService::~HwcService() {
}

bool HwcService::Start(DrmHwcTwo *hwc) {
  if (initialized_)
    return true;

  mpHwc = hwc;
  sp<IServiceManager> sm(defaultServiceManager());
  if (sm->addService(String16(HWC_SERVICE_NAME), this, false)) {
    ALOGE("Failed to start %s service", HWC_SERVICE_NAME);
    return false;
  }

  initialized_ = true;
  ALOGD("success to start %s service", HWC_SERVICE_NAME);
  return true;
}

String8 HwcService::GetHwcVersion() {
  return String8("");
}

status_t HwcService::SetOption(String8 option, String8 value) {
  return OK;
}

void HwcService::DumpOptions(void) {
  // TO DO
}

status_t HwcService::EnableLogviewToLogcat(bool enable) {
  // TO DO
  return OK;
}

sp<IDiagnostic> HwcService::GetDiagnostic() {
  lock_.lock();
  ALOG_ASSERT(mpHwc);
  if (mpDiagnostic == NULL)
    mpDiagnostic = new Diagnostic(*mpHwc);

  lock_.unlock();

  return mpDiagnostic;
}

sp<IControls> HwcService::GetControls() {
  // TODO: Check the need for lock
  ALOG_ASSERT(mpHwc);
  return new Controls(*mpHwc, *this);
}

status_t HwcService::Diagnostic::ReadLogParcel(Parcel *parcel) {
  // TO DO
  return OK;
}

void HwcService::Diagnostic::EnableDisplay(uint32_t) { /* nothing */
}
void HwcService::Diagnostic::DisableDisplay(uint32_t, bool) { /* nothing */
}
void HwcService::Diagnostic::MaskLayer(uint32_t, uint32_t, bool) { /* nothing */
}
void HwcService::Diagnostic::DumpFrames(uint32_t, int32_t, bool) { /* nothing */
}

HwcService::Controls::Controls(DrmHwcTwo &hwc, HwcService &hwcService)
    : mHwc(hwc),
      mHwcService(hwcService),
      mbHaveSessionsEnabled(false) {
}

HwcService::Controls::~Controls() {
}

#define HWCS_ENTRY_FMT(fname, fmt, ...) \
  const char *___HWCS_FUNCTION = fname; \
  Log::add(fname " " fmt " -->", __VA_ARGS__)

#define HWCS_ENTRY(fname)               \
  const char *___HWCS_FUNCTION = fname; \
  Log::add(fname " -->")

#define HWCS_ERROR(code) Log::add("%s ERROR %d <--", ___HWCS_FUNCTION, code)

#define HWCS_EXIT_ERROR(code) \
  do {                        \
    int ___code = code;       \
    HWCS_ERROR(___code);      \
    return ___code;           \
  } while (0)

#define HWCS_OK_FMT(fmt, ...) \
  Log::add("%s OK " fmt " <--", ___HWCS_FUNCTION, __VA_ARGS__);

#define HWCS_EXIT_OK_FMT(fmt, ...) \
  do {                             \
    HWCS_OK_FMT(fmt, __VA_ARGS__); \
    return OK;                     \
  } while (0)

#define HWCS_EXIT_OK()                       \
  do {                                       \
    Log::add("%s OK <--", ___HWCS_FUNCTION); \
    return OK;                               \
  } while (0)

#define HWCS_EXIT_VAR(code)    \
  do {                         \
    int ____code = code;       \
    if (____code == OK)        \
      HWCS_EXIT_OK();          \
    HWCS_EXIT_ERROR(____code); \
  } while (0)

#define HWCS_EXIT_VAR_FMT(code, fmt, ...) \
  do {                                    \
    int ____code = code;                  \
    if (____code == OK)                   \
      HWCS_EXIT_OK_FMT(fmt, __VA_ARGS__); \
    HWCS_EXIT_ERROR(____code);            \
  } while (0)

status_t HwcService::Controls::EnableHDCPSessionForDisplay(
    uint32_t connector, EHwcsContentType content_type) {
   mHwc.EnableHDCPSessionForDisplay(connector, content_type);
  return OK;
}

status_t HwcService::Controls::EnableHDCPSessionForAllDisplays(
    EHwcsContentType content_type) {
   mHwc.EnableHDCPSessionForAllDisplays(content_type);
  return OK;
}

status_t HwcService::Controls::DisableHDCPSessionForDisplay(
    uint32_t connector) {
   mHwc.DisableHDCPSessionForDisplay(connector);
  return OK;
}

status_t HwcService::Controls::DisableHDCPSessionForAllDisplays() {
   mHwc.DisableHDCPSessionForAllDisplays();
  return OK;
}

status_t HwcService::Controls::SetHDCPSRMForAllDisplays(const int8_t *SRM,
                                                        uint32_t SRMLength) {
  // TO DO
  return OK;
}

status_t HwcService::Controls::SetHDCPSRMForDisplay(uint32_t connector,
                                                    const int8_t *SRM,
                                                    uint32_t SRMLength) {
  // TO DO
  return OK;
}

void HwcService::RegisterListener(ENotification notify,
                                  NotifyCallback *pCallback) {
  // TO DO
}

void HwcService::UnregisterListener(ENotification notify,
                                    NotifyCallback *pCallback) {
  // TO DO
}

void HwcService::Notify(ENotification notify, int32_t paraCnt, int64_t para[]) {
  // TO DO
}

}  // namespace android
