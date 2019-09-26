/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/unique_fd.h>
#include <gtest/gtest.h>

#include "adb.h"
#include "adb_io.h"
#include "fuse_adb_provider.h"
#include "fuse_sideload.h"
#include "minadbd/types.h"
#include "minadbd_services.h"
#include "socket.h"

class MinadbdServicesTest : public ::testing::Test {
 protected:
  static constexpr int EXIT_TIME_OUT = 10;

  void SetUp() override {
    ASSERT_TRUE(
        android::base::Socketpair(AF_UNIX, SOCK_STREAM, 0, &minadbd_socket_, &recovery_socket_));
    SetMinadbdSocketFd(minadbd_socket_);
    SetSideloadMountPoint(mount_point_.path);

    package_path_ = std::string(mount_point_.path) + "/" + FUSE_SIDELOAD_HOST_FILENAME;
    exit_flag_ = std::string(mount_point_.path) + "/" + FUSE_SIDELOAD_HOST_EXIT_FLAG;

    signal(SIGPIPE, SIG_IGN);
  }

  void TearDown() override {
    // Umount in case the test fails. Ignore the result.
    umount(mount_point_.path);

    signal(SIGPIPE, SIG_DFL);
  }

  void ReadAndCheckCommandMessage(int fd, MinadbdCommand expected_command) {
    std::vector<uint8_t> received(kMinadbdMessageSize, '\0');
    ASSERT_TRUE(android::base::ReadFully(fd, received.data(), kMinadbdMessageSize));

    std::vector<uint8_t> expected(kMinadbdMessageSize, '\0');
    memcpy(expected.data(), kMinadbdCommandPrefix, strlen(kMinadbdCommandPrefix));
    memcpy(expected.data() + strlen(kMinadbdCommandPrefix), &expected_command,
           sizeof(expected_command));
    ASSERT_EQ(expected, received);
  }

  void WaitForFusePath() {
    constexpr int TIME_OUT = 10;
    for (int i = 0; i < TIME_OUT; ++i) {
      struct stat sb;
      if (stat(package_path_.c_str(), &sb) == 0) {
        return;
      }

      if (errno == ENOENT) {
        sleep(1);
        continue;
      }
      FAIL() << "Timed out waiting for the fuse-provided package " << strerror(errno);
    }
  }

  void StatExitFlagAndExitProcess(int exit_code) {
    struct stat sb;
    if (stat(exit_flag_.c_str(), &sb) != 0) {
      PLOG(ERROR) << "Failed to stat " << exit_flag_;
    }

    exit(exit_code);
  }

  void WriteMinadbdCommandStatus(MinadbdCommandStatus status) {
    std::string status_message(kMinadbdMessageSize, '\0');
    memcpy(status_message.data(), kMinadbdStatusPrefix, strlen(kMinadbdStatusPrefix));
    memcpy(status_message.data() + strlen(kMinadbdStatusPrefix), &status, sizeof(status));
    ASSERT_TRUE(
        android::base::WriteFully(recovery_socket_, status_message.data(), kMinadbdMessageSize));
  }

  void ExecuteCommandAndWaitForExit(const std::string& command) {
    unique_fd fd = daemon_service_to_fd(command, nullptr);
    ASSERT_NE(-1, fd);
    sleep(EXIT_TIME_OUT);
  }

  android::base::unique_fd minadbd_socket_;
  android::base::unique_fd recovery_socket_;

  TemporaryDir mount_point_;
  std::string package_path_;
  std::string exit_flag_;
};

TEST_F(MinadbdServicesTest, SideloadHostService_wrong_size_argument) {
  ASSERT_EXIT(ExecuteCommandAndWaitForExit("sideload-host:abc:4096"),
              ::testing::ExitedWithCode(kMinadbdHostCommandArgumentError), "");
}

TEST_F(MinadbdServicesTest, SideloadHostService_wrong_block_size) {
  ASSERT_EXIT(ExecuteCommandAndWaitForExit("sideload-host:10:20"),
              ::testing::ExitedWithCode(kMinadbdFuseStartError), "");
}

TEST_F(MinadbdServicesTest, SideloadHostService_broken_minadbd_socket) {
  SetMinadbdSocketFd(-1);
  ASSERT_EXIT(ExecuteCommandAndWaitForExit("sideload-host:4096:4096"),
              ::testing::ExitedWithCode(kMinadbdSocketIOError), "");
}

TEST_F(MinadbdServicesTest, SideloadHostService_broken_recovery_socket) {
  recovery_socket_.reset();
  ASSERT_EXIT(ExecuteCommandAndWaitForExit("sideload-host:4096:4096"),
              ::testing::ExitedWithCode(kMinadbdSocketIOError), "");
}

TEST_F(MinadbdServicesTest, SideloadHostService_wrong_command_format) {
  auto test_body = [&](const std::string& command) {
    unique_fd fd = daemon_service_to_fd(command, nullptr);
    ASSERT_NE(-1, fd);
    WaitForFusePath();
    ReadAndCheckCommandMessage(recovery_socket_, MinadbdCommand::kInstall);

    struct stat sb;
    ASSERT_EQ(0, stat(exit_flag_.c_str(), &sb));
    ASSERT_TRUE(android::base::WriteStringToFd("12345678", recovery_socket_));
    sleep(EXIT_TIME_OUT);
  };

  ASSERT_EXIT(test_body("sideload-host:4096:4096"),
              ::testing::ExitedWithCode(kMinadbdMessageFormatError), "");
}

TEST_F(MinadbdServicesTest, SideloadHostService_read_data_from_fuse) {
  auto test_body = [&]() {
    std::vector<uint8_t> content(4096, 'a');
    // Start a new process instead of a thread to read from the package mounted by FUSE. Because
    // the test may not exit and report failures correctly when the thread blocks by a syscall.
    pid_t pid = fork();
    if (pid == 0) {
      WaitForFusePath();
      android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(package_path_.c_str(), O_RDONLY)));
      // Do not use assertion here because we want to stat the exit flag and exit the process.
      // Otherwise the test will wait for the time out instead of failing immediately.
      if (fd == -1) {
        PLOG(ERROR) << "Failed to open " << package_path_;
        StatExitFlagAndExitProcess(1);
      }
      std::vector<uint8_t> content_from_fuse(4096);
      if (!android::base::ReadFully(fd, content_from_fuse.data(), 4096)) {
        PLOG(ERROR) << "Failed to read from " << package_path_;
        StatExitFlagAndExitProcess(1);
      }
      if (content_from_fuse != content) {
        LOG(ERROR) << "Content read from fuse doesn't match with the expected value";
        StatExitFlagAndExitProcess(1);
      }
      StatExitFlagAndExitProcess(0);
    }

    unique_fd fd = daemon_service_to_fd("sideload-host:4096:4096", nullptr);
    ASSERT_NE(-1, fd);
    ReadAndCheckCommandMessage(recovery_socket_, MinadbdCommand::kInstall);

    // Mimic the response from adb host.
    std::string adb_message(8, '\0');
    ASSERT_TRUE(android::base::ReadFully(fd, adb_message.data(), 8));
    ASSERT_EQ(android::base::StringPrintf("%08u", 0), adb_message);
    ASSERT_TRUE(android::base::WriteFully(fd, content.data(), 4096));

    // Check that we read the correct data from fuse.
    int child_status;
    waitpid(pid, &child_status, 0);
    ASSERT_TRUE(WIFEXITED(child_status));
    ASSERT_EQ(0, WEXITSTATUS(child_status));

    WriteMinadbdCommandStatus(MinadbdCommandStatus::kSuccess);

    // TODO(xunchang) check if adb host-side receives "DONEDONE", there's a race condition between
    // receiving the message and exit of test body (by detached thread in minadbd service).
    exit(kMinadbdSuccess);
  };

  ASSERT_EXIT(test_body(), ::testing::ExitedWithCode(kMinadbdSuccess), "");
}
