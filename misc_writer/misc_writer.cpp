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
#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <bootloader_message/bootloader_message.h>

using namespace std::string_literals;

static std::vector<uint8_t> ParseHexString(std::string_view hex_string) {
  auto length = hex_string.size();
  if (length % 2 != 0 || length == 0) {
    return {};
  }

  std::vector<uint8_t> result(length / 2);
  for (size_t i = 0; i < length / 2; i++) {
    auto sub = "0x" + std::string(hex_string.substr(i * 2, 2));
    if (!android::base::ParseUint(sub, &result[i])) {
      return {};
    }
  }
  return result;
}

static int Usage(std::string_view name) {
  std::cerr << name << " usage:\n";
  std::cerr << name << " [--vendor-space-offset <offset>] --hex-string 0xABCDEF\n";
  std::cerr << "Writes the given hex string to the specified offset in vendor space in /misc "
               "partition. Offset defaults to 0 if unspecified.\n";
  return EXIT_FAILURE;
}

// misc_writer is a vendor tool that writes data to the vendor space in /misc.
int main(int argc, char** argv) {
  constexpr struct option OPTIONS[] = {
    { "vendor-space-offset", required_argument, nullptr, 0 },
    { "hex-string", required_argument, nullptr, 0 },
    { nullptr, 0, nullptr, 0 },
  };

  // Offset defaults to 0 if unspecified.
  size_t offset = 0;
  std::string_view hex_string;

  int arg;
  int option_index;
  while ((arg = getopt_long(argc, argv, "", OPTIONS, &option_index)) != -1) {
    if (arg != 0) {
      LOG(ERROR) << "Invalid command argument";
      return Usage(argv[0]);
    }
    auto option_name = OPTIONS[option_index].name;
    if (option_name == "vendor-space-offset"s) {
      if (!android::base::ParseUint(optarg, &offset)) {
        LOG(ERROR) << "Failed to parse the offset: " << optarg;
        return Usage(argv[0]);
      }
    } else if (option_name == "hex-string"s) {
      hex_string = optarg;
    }
  }

  if (hex_string.starts_with("0x") || hex_string.starts_with("0X")) {
    hex_string = hex_string.substr(2);
  }
  if (hex_string.empty()) {
    LOG(ERROR) << "Invalid input hex string: " << hex_string;
    return Usage(argv[0]);
  }

  auto data = ParseHexString(hex_string);
  if (data.empty()) {
    LOG(ERROR) << "Failed to parse the input hex string: " << hex_string;
    return EXIT_FAILURE;
  }
  if (std::string err; !WriteMiscPartitionVendorSpace(data.data(), data.size(), offset, &err)) {
    LOG(ERROR) << "Failed to write to misc partition: " << err;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
