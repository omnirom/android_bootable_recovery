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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>

#include <string>

#include <android-base/unique_fd.h>
#include <gtest/gtest.h>

#include "adb_io.h"
#include "fuse_adb_provider.h"

TEST(fuse_adb_provider, read_block_adb) {
  android::base::unique_fd device_socket;
  android::base::unique_fd host_socket;

  ASSERT_TRUE(android::base::Socketpair(AF_UNIX, SOCK_STREAM, 0, &device_socket, &host_socket));
  FuseAdbDataProvider data(std::move(device_socket), 0, 0);

  fcntl(host_socket, F_SETFL, O_NONBLOCK);

  const char expected_data[] = "foobar";
  char block_data[sizeof(expected_data)] = {};

  // If we write the result of read_block_adb's request before the request is
  // actually made we can avoid needing an extra thread for this test.
  ASSERT_TRUE(WriteFdExactly(host_socket, expected_data,
                             strlen(expected_data)));

  uint32_t block = 1234U;
  const char expected_block[] = "00001234";
  ASSERT_TRUE(data.ReadBlockAlignedData(reinterpret_cast<uint8_t*>(block_data),
                                        sizeof(expected_data) - 1, block));

  // Check that read_block_adb requested the right block.
  char block_req[sizeof(expected_block)] = {};
  ASSERT_TRUE(ReadFdExactly(host_socket, block_req, 8));
  ASSERT_EQ(0, block_req[8]);
  ASSERT_EQ(8U, strlen(block_req));
  ASSERT_STREQ(expected_block, block_req);

  // Check that read_block_adb returned the right data.
  ASSERT_EQ(0, block_req[8]);
  ASSERT_STREQ(expected_data, block_data);

  // Check that nothing else was written to the socket.
  char tmp;
  errno = 0;
  ASSERT_EQ(-1, read(host_socket, &tmp, 1));
  ASSERT_EQ(EWOULDBLOCK, errno);
}

TEST(fuse_adb_provider, read_block_adb_fail_write) {
  android::base::unique_fd device_socket;
  android::base::unique_fd host_socket;

  ASSERT_TRUE(android::base::Socketpair(AF_UNIX, SOCK_STREAM, 0, &device_socket, &host_socket));
  FuseAdbDataProvider data(std::move(device_socket), 0, 0);

  host_socket.reset();

  // write(2) raises SIGPIPE since the reading end has been closed. Ignore the signal to avoid
  // failing the test.
  signal(SIGPIPE, SIG_IGN);

  char buf[1];
  ASSERT_FALSE(data.ReadBlockAlignedData(reinterpret_cast<uint8_t*>(buf), 1, 0));
}
