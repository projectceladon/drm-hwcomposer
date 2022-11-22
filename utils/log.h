#ifndef UTILS_LOG_H_
#define UTILS_LOG_H_

#ifdef ANDROID

#include <log/log.h>

#else

#include <cinttypes>
#include <cstdio>
#ifdef ALOGE
#undef ALOGE
#undef ALOGW
#undef ALOGI
#undef ALOGD
#undef ALOGV
#endif
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ALOGE(args...) printf("ERR: " args);printf("\n")
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ALOGW(args...) printf("WARN: " args);printf("\n")
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ALOGI(args...) printf("INFO: " args);printf("\n")
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ALOGD(args...) printf("DBG:" args);printf("\n")
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ALOGV(args...) printf("VERBOSE: " args);printf("\n")

#endif

#endif