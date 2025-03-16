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

#include "icontrols.h"
#include <binder/IPCThreadState.h>
#include <utils/String8.h>

// For AID_ROOT & AID_MEDIA - various vendor code and utils include this despite
// the path.
#include <cutils/android_filesystem_config.h>

namespace hwcomposer {

using namespace android;

/**
 */
class BpControls : public BpInterface<IControls> {
 public:
  BpControls(const sp<IBinder> &impl) : BpInterface<IControls>(impl) {
  }

  enum {
    // ==============================================
    // Public APIs - try not to reorder these

    TRANSACT_VIDEO_ENABLE_HDCP_SESSION_FOR_DISPLAY = IBinder::FIRST_CALL_TRANSACTION,
    TRANSACT_VIDEO_ENABLE_HDCP_SESSION_FOR_ALL_DISPLAYS,
    TRANSACT_VIDEO_DISABLE_HDCP_SESSION_FOR_DISPLAY,
    TRANSACT_VIDEO_DISABLE_HDCP_SESSION_FOR_ALL_DISPLAYS,
    TRANSACT_VIDEO_SET_HDCP_SRM_FOR_ALL_DISPLAYS,
    TRANSACT_VIDEO_SET_HDCP_SRM_FOR_DISPLAY,
  };

  status_t EnableHDCPSessionForDisplay(uint32_t connector,
                                       EHwcsContentType content_type) override {
    Parcel data;
    Parcel reply;
    data.writeInterfaceToken(IControls::getInterfaceDescriptor());
    data.writeInt32(connector);
    data.writeInt32(content_type);
    status_t ret = remote()->transact(
        TRANSACT_VIDEO_ENABLE_HDCP_SESSION_FOR_DISPLAY, data, &reply);
    if (ret != NO_ERROR) {
      ALOGW("%s() transact failed: %d", __FUNCTION__, ret);
      return ret;
    }
    return reply.readInt32();
  }

  status_t EnableHDCPSessionForAllDisplays(
      EHwcsContentType content_type) override {
    Parcel data;
    Parcel reply;
    data.writeInterfaceToken(IControls::getInterfaceDescriptor());
    data.writeInt32(content_type);
    status_t ret = remote()->transact(
        TRANSACT_VIDEO_ENABLE_HDCP_SESSION_FOR_ALL_DISPLAYS, data, &reply);
    if (ret != NO_ERROR) {
      ALOGW("%s() transact failed: %d", __FUNCTION__, ret);
      return ret;
    }
    return reply.readInt32();
  }

  status_t DisableHDCPSessionForDisplay(uint32_t connector) override {
    Parcel data;
    Parcel reply;
    data.writeInterfaceToken(IControls::getInterfaceDescriptor());
    data.writeInt32(connector);
    status_t ret = remote()->transact(
        TRANSACT_VIDEO_DISABLE_HDCP_SESSION_FOR_DISPLAY, data, &reply);
    if (ret != NO_ERROR) {
      ALOGW("%s() transact failed: %d", __FUNCTION__, ret);
      return ret;
    }
    return reply.readInt32();
  }

  status_t DisableHDCPSessionForAllDisplays() override {
    Parcel data;
    Parcel reply;
    data.writeInterfaceToken(IControls::getInterfaceDescriptor());
    status_t ret = remote()->transact(
        TRANSACT_VIDEO_DISABLE_HDCP_SESSION_FOR_ALL_DISPLAYS, data, &reply);
    if (ret != NO_ERROR) {
      ALOGW("%s() transact failed: %d", __FUNCTION__, ret);
    }
    return reply.readInt32();
  }

  status_t SetHDCPSRMForAllDisplays(const int8_t *SRM,
                                    uint32_t SRMLength) override {
    Parcel data;
    Parcel reply;
    data.writeInterfaceToken(IControls::getInterfaceDescriptor());
    data.writeByteArray(SRMLength, (const uint8_t *)SRM);
    status_t ret = remote()->transact(
        TRANSACT_VIDEO_SET_HDCP_SRM_FOR_ALL_DISPLAYS, data, &reply);
    if (ret != NO_ERROR) {
      ALOGW("%s() transact failed: %d", __FUNCTION__, ret);
    }
    return reply.readInt32();
  }

  status_t SetHDCPSRMForDisplay(uint32_t connector, const int8_t *SRM,
                                uint32_t SRMLength) override {
    Parcel data;
    Parcel reply;
    data.writeInterfaceToken(IControls::getInterfaceDescriptor());
    data.writeInt32(connector);
    data.writeByteArray(SRMLength, (const uint8_t *)SRM);
    status_t ret = remote()->transact(TRANSACT_VIDEO_SET_HDCP_SRM_FOR_DISPLAY,
                                      data, &reply);
    if (ret != NO_ERROR) {
      ALOGW("%s() transact failed: %d", __FUNCTION__, ret);
    }
    return reply.readInt32();
  }
};

IMPLEMENT_META_INTERFACE(Controls, "hwc.controls");

status_t BnControls::onTransact(uint32_t code, const Parcel &data,
                                Parcel *reply, uint32_t flags) {
  switch (code) {
    case BpControls::TRANSACT_VIDEO_ENABLE_HDCP_SESSION_FOR_DISPLAY: {
      CHECK_INTERFACE(IControls, data, reply);
      uint32_t connector = data.readInt32();
      EHwcsContentType content_type = (EHwcsContentType)data.readInt32();
      status_t ret = this->EnableHDCPSessionForDisplay(connector, content_type);
      reply->writeInt32(ret);
      return NO_ERROR;
    }
    case BpControls::TRANSACT_VIDEO_ENABLE_HDCP_SESSION_FOR_ALL_DISPLAYS: {
      CHECK_INTERFACE(IControls, data, reply);
      EHwcsContentType content_type = (EHwcsContentType)data.readInt32();
      status_t ret = this->EnableHDCPSessionForAllDisplays(content_type);
      reply->writeInt32(ret);
      return NO_ERROR;
    }
    case BpControls::TRANSACT_VIDEO_DISABLE_HDCP_SESSION_FOR_DISPLAY: {
      CHECK_INTERFACE(IControls, data, reply);
      uint32_t connector = data.readInt32();
      status_t ret = this->DisableHDCPSessionForDisplay(connector);
      reply->writeInt32(ret);
      return NO_ERROR;
    }
    case BpControls::TRANSACT_VIDEO_DISABLE_HDCP_SESSION_FOR_ALL_DISPLAYS: {
      CHECK_INTERFACE(IControls, data, reply);
      status_t ret = this->DisableHDCPSessionForAllDisplays();
      reply->writeInt32(ret);
      return NO_ERROR;
    }
    case BpControls::TRANSACT_VIDEO_SET_HDCP_SRM_FOR_ALL_DISPLAYS: {
      CHECK_INTERFACE(IControls, data, reply);
      std::vector<int8_t> srmvec;
      data.readByteVector(&srmvec);
      status_t ret = this->SetHDCPSRMForAllDisplays(
          (const int8_t *)(srmvec.data()), srmvec.size());
      reply->writeInt32(ret);
      return NO_ERROR;
    }
    case BpControls::TRANSACT_VIDEO_SET_HDCP_SRM_FOR_DISPLAY: {
      CHECK_INTERFACE(IControls, data, reply);
      std::vector<int8_t> srmvec;
      uint32_t connector = data.readInt32();
      data.readByteVector(&srmvec);
      status_t ret = this->SetHDCPSRMForDisplay(
          connector, (const int8_t *)(srmvec.data()), srmvec.size());
      reply->writeInt32(ret);
      return NO_ERROR;
    }

    default:
      return BBinder::onTransact(code, data, reply, flags);
  }
}

}  // namespace hwcomposer
