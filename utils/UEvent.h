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

#pragma once

#include <linux/netlink.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include <cerrno>
#include <memory>
#include <optional>
#include <string>

#include "fd.h"
#include "log.h"

namespace android {

class UEvent {
 public:
  static auto CreateInstance() -> std::unique_ptr<UEvent> {
    auto fd = MakeUniqueFd(
        socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT));

    if (!fd) {
      ALOGE("Failed to open uevent socket: errno=%i", errno);
      return {};
    }

    struct sockaddr_nl addr {};
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = 0;
    addr.nl_groups = UINT32_MAX;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    const int ret = bind(*fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret != 0) {
      ALOGE("Failed to bind uevent socket: errno=%i", errno);
      return {};
    }

    auto stop_event_fd = MakeUniqueFd(eventfd(0, EFD_CLOEXEC));
    if (!stop_event_fd) {
      ALOGE("Failed to create eventfd: errno=%i", errno);
      return {};
    }

    return std::unique_ptr<UEvent>(new UEvent(fd, stop_event_fd));
  }

  auto ReadNext() -> std::optional<std::string> {
    constexpr int kUEventBufferSize = 1024;
    char buffer[kUEventBufferSize];

    if (!WaitForData()) {
      return {};
    }

    ssize_t ret = 0;
    ret = read(*fd_, &buffer, sizeof(buffer));
    if (ret == 0)
      return {};

    if (ret < 0) {
      ALOGE("Got error reading uevent %zd", ret);
      return {};
    }

    for (int i = 0; i < ret - 1; i++) {
      if (buffer[i] == '\0') {
        buffer[i] = '\n';
      }
    }

    return std::string(buffer);
  }

  void Stop() {
    // Increment the eventfd by writing 1. All subsequent calls to ReadNext will
    // return false.
    const uint64_t value = 1;
    const ssize_t ret = write(*stop_event_fd_, &value, sizeof(value));
    if (ret == -1) {
      ALOGE("Error writing to eventfd. errno: %d", errno);
    } else if (ret != sizeof(value)) {
      ALOGE("Wrote fewer bytes to eventfd than expected: %zd vs %zd", ret,
            sizeof(value));
    }
  }

 private:
  enum { kFdIdx = 0, kStopEventFdIdx, kNumFds };

  UEvent(UniqueFd &fd, UniqueFd &stop_event_fd)
      : fd_(std::move(fd)), stop_event_fd_(std::move(stop_event_fd)) {};

  // Returns true if there is data to be read off of fd_.
  bool WaitForData() {
    struct pollfd poll_fds[kNumFds];
    poll_fds[kFdIdx].fd = *fd_;
    poll_fds[kFdIdx].events = POLLIN;
    poll_fds[kStopEventFdIdx].fd = *stop_event_fd_;
    poll_fds[kStopEventFdIdx].events = POLLIN;

    const int ret = poll(poll_fds, kNumFds, -1);
    if (ret == 0) {
      // Timeout shouldn't happen, but return here anyways.
      ALOGE("Timed out polling uevent.");
      return false;
    }
    if (ret < 1) {
      ALOGE("Error polling uevent. errno: %d", errno);
      return false;
    }

    if ((poll_fds[kStopEventFdIdx].revents & POLLIN) != 0) {
      // Stop event has been signalled. Return without reading from the fd to
      // ensure that this fd stays in a readable state.
      ALOGI("Stop event signalled.");
      return false;
    }

    // Return true if there is data to read.
    return (poll_fds[kFdIdx].revents & POLLIN) != 0;
  }

  UniqueFd fd_;
  UniqueFd stop_event_fd_;
};

}  // namespace android
