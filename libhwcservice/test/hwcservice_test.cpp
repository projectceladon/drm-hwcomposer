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

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <binder/TextOutput.h>
#include <utils/String8.h>
#include <cinttypes>
#include "hwcserviceapi.h"
#include "iservice.h"

using namespace android;
using namespace hwcomposer;

static void usage() {
  aout << "Usage: hwcservice_test \n"
          "\t-h: Enable HDCP support for a given Display. \n"
          "\t-i: Disable HDCP support for a given Display. \n"
          "\t-j: Enable HDCP support for all displays. \n"
          "\t-k: Disable HDCP support for all displays. \n";
  exit(-1);
}

int main(int argc, char** argv) {
  uint32_t display = 0;
  uint32_t display_mode_index = 0;
  bool print_mode = false;
  bool get_mode = false;
  bool set_mode = false;
  bool set_hue = false;
  bool set_saturation = false;
  bool set_brightness = false;
  bool set_contrast = false;
  bool set_deinterlace = false;
  bool set_sharpness = false;
  bool set_hdcp_for_display = false;
  bool set_hdcp_for_all_display = false;
  bool disable_hdcp_for_display = false;
  bool disable_hdcp_for_all_display = false;
  bool restore = false;
  int ch;

  while ((ch = getopt(argc, argv, "gsphijkurabcde")) != -1) {

    switch (ch) {
      case 'h':
        set_hdcp_for_display = true;
        break;
      case 'i':
        disable_hdcp_for_display = true;
        break;
      case 'j':
        set_hdcp_for_all_display = true;
        break;
      case 'k':
        disable_hdcp_for_all_display = true;
        break;
      default:
        usage();
    }
  }
  argc -= optind;
  argv += optind;

#ifdef USE_PROCESS_STATE
  // Initialize ProcessState with /dev/vndbinder as HwcService is
  // in the vndbinder context
  android::ProcessState::initWithDriver("/dev/vndbinder");
#endif

  // Connect to HWC service
  HWCSHANDLE hwcs = HwcService_Connect();
  if (hwcs == NULL) {
    aout << "Could not connect to service\n";
    return -1;
  }

  if (set_hdcp_for_display) {
    aout << "Set HDCP For Display: " << atoi(argv[0]) << endl;
    if (atoi(argv[0]) == 0) {
      HwcService_Video_EnableHDCPSession_ForDisplay(hwcs, atoi(argv[0]),
                                                    HWCS_CP_CONTENT_TYPE0);
    } else {
      HwcService_Video_EnableHDCPSession_ForDisplay(hwcs, atoi(argv[0]),
                                                    HWCS_CP_CONTENT_TYPE1);
    }
  }

  if (disable_hdcp_for_display) {
    aout << "Disabling HDCP For Display: " << atoi(argv[0]) << endl;
    HwcService_Video_DisableHDCPSession_ForDisplay(hwcs, atoi(argv[0]));
  }

  if (set_hdcp_for_all_display) {
    aout << "Set HDCP For All Displays Using Fallback: " << atoi(argv[0])
         << endl;
    if (atoi(argv[0]) == 0) {
      HwcService_Video_EnableHDCPSession_AllDisplays(hwcs,
                                                     HWCS_CP_CONTENT_TYPE0);
    } else {
      HwcService_Video_EnableHDCPSession_AllDisplays(hwcs,
                                                     HWCS_CP_CONTENT_TYPE1);
    }
  }

  if (disable_hdcp_for_all_display) {
    aout << "Disabling HDCP For All Displays. " << endl;
    HwcService_Video_DisableHDCPSession_AllDisplays(hwcs);
  }

  HwcService_Disconnect(hwcs);
  return 0;
}
