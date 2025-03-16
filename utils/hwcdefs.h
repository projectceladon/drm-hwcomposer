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

#ifndef PUBLIC_HWCDEFS_H_
#define PUBLIC_HWCDEFS_H_

#include <stdint.h>

#ifdef __cplusplus

#include <unordered_map>
#include <vector>

namespace hwcomposer {

// Enumerations for content protection.
enum HWCContentProtection {
  kUnSupported = 0,  // Content Protection is not supported.
  kUnDesired = 1,    // Content Protection is not required.
  kDesired = 2       // Content Protection is desired.
};

enum HWCContentType {
  kCONTENT_TYPE0,  // Can support any HDCP specifiction.
  kCONTENT_TYPE1,  // Can support only HDCP 2.2 and higher specification.
  kInvalid,        // Used when disabling HDCP.
};
#endif  //__cplusplus
}  // namespace hwcomposer

#endif  // PUBLIC_HWCDEFS_H_
