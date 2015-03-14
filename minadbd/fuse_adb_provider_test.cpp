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

#include "fuse_adb_provider.h"

#include <gtest/gtest.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <string>

#include "adb_io.h"

TEST(fuse_adb_provider, read_block_adb) {
  adb_data data = {};
  int sockets[2];

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  data.sfd = sockets[0];

  int host_socket = sockets[1];
  fcntl(host_socket, F_SETFL, O_NONBLOCK);

  const char expected_data[] = "foobar";
  char block_data[sizeof(expected_data)] = {};

  // If we write the result of read_block_adb's request before the request is
  // actually made we can avoid needing an extra thread for this test.
  ASSERT_TRUE(WriteFdExactly(host_socket, expected_data,
                             strlen(expected_data)));

  uint32_t block = 1234U;
  const char expected_block[] = "00001234";
  ASSERT_EQ(0, read_block_adb(reinterpret_cast<void*>(&data), block,
                              reinterpret_cast<uint8_t*>(block_data),
                              sizeof(expected_data) - 1));

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

  close(sockets[0]);
  close(sockets[1]);
}

TEST(fuse_adb_provider, read_block_adb_fail_write) {
  adb_data data = {};
  int sockets[2];

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  data.sfd = sockets[0];

  ASSERT_EQ(0, close(sockets[1]));

  char buf[1];
  ASSERT_EQ(-EIO, read_block_adb(reinterpret_cast<void*>(&data), 0,
                                 reinterpret_cast<uint8_t*>(buf), 1));

  close(sockets[0]);
}
