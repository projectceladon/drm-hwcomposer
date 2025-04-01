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

#ifndef OS_ANDROID_HWCSERVICE_H_
#define OS_ANDROID_HWCSERVICE_H_

#include <list>

#include <binder/IInterface.h>
#include <utils/String8.h>
#include "libhwcservice/icontrols.h"
#include "libhwcservice/idiagnostic.h"
#include "libhwcservice/iservice.h"
#include "utils/spinlock.h"

#define HWC_UNUSED(x) ((void)&(x))
namespace android {
class DrmHwcTwo;
using namespace hwcomposer;

class HwcService : public BnService {
 public:
  class Diagnostic : public BnDiagnostic {
   public:
    Diagnostic(DrmHwcTwo& DrmHwcTwo) : mHwc(DrmHwcTwo) {
      HWC_UNUSED(mHwc);
    }

    status_t ReadLogParcel(Parcel* parcel) override;
    void EnableDisplay(uint32_t d) override;
    void DisableDisplay(uint32_t d, bool bBlank) override;
    void MaskLayer(uint32_t d, uint32_t layer, bool bHide) override;
    void DumpFrames(uint32_t d, int32_t frames, bool bSync) override;

   private:
    DrmHwcTwo& mHwc;
  };

  bool Start(DrmHwcTwo* hwc);

  sp<IDiagnostic> GetDiagnostic();
  sp<IControls> GetControls();

  android::String8 GetHwcVersion();

  void DumpOptions(void);
  status_t SetOption(android::String8 option, android::String8 optionValue);
  status_t EnableLogviewToLogcat(bool enable = true);

  class Controls : public BnControls {
   public:
    Controls(DrmHwcTwo& hwc, HwcService& hwcService);
    virtual ~Controls();

    status_t EnableHDCPSessionForDisplay(uint32_t connector,
                                         EHwcsContentType content_type);

    status_t EnableHDCPSessionForAllDisplays(EHwcsContentType content_type);

    status_t DisableHDCPSessionForDisplay(uint32_t connector);

    status_t DisableHDCPSessionForAllDisplays();

    status_t SetHDCPSRMForAllDisplays(const int8_t* SRM, uint32_t SRMLength);

    status_t SetHDCPSRMForDisplay(uint32_t connector, const int8_t* SRM,
                                  uint32_t SRMLength);

   private:
    DrmHwcTwo& mHwc;
    HwcService& mHwcService;
    bool mbHaveSessionsEnabled;
  };

  enum ENotification {
    eInvalidNofiy = 0,
    eOptimizationMode,
    eMdsUpdateVideoState,
    eMdsUpdateInputState,
    eMdsUpdateVideoFps,
    ePavpEnableEncryptedSession,
    ePavpDisableEncryptedSession,
    ePavpDisableAllEncryptedSessions,
    ePavpIsEncryptedSessionEnabled,
    eWidiGetSingleDisplay,
    eWidiSetSingleDisplay,
    eNeedSetKeyFrameHint,
  };

  class NotifyCallback {
   public:
    virtual ~NotifyCallback() {
    }
    virtual void notify(ENotification notify, int32_t paraCnt,
                        int64_t para[]) = 0;
  };

  void RegisterListener(ENotification notify, NotifyCallback* pCallback);
  void UnregisterListener(ENotification notify, NotifyCallback* pCallback);
  void Notify(ENotification notify, int32_t paraCnt, int64_t para[]);
  HwcService();
  virtual ~HwcService();
 private:
  friend class DrmHwcTwo;

  struct Notification {
    Notification() : mWhat(eInvalidNofiy), mpCallback(NULL) {
    }
    Notification(ENotification what, NotifyCallback* pCallback)
        : mWhat(what), mpCallback(pCallback) {
    }
    ENotification mWhat;
    NotifyCallback* mpCallback;
  };

  SpinLock lock_;
  DrmHwcTwo* mpHwc;
  bool initialized_;

  sp<IDiagnostic> mpDiagnostic;

  std::vector<Notification> mNotifications;
};
}  // namespace android

#endif  // OS_ANDROID_HWCSERVICE_H_
