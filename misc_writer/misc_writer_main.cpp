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

#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <android-base/logging.h>
#include <android-base/parseint.h>

#include "misc_writer/misc_writer.h"

using namespace std::string_literals;
using android::hardware::google::pixel::MiscWriter;
using android::hardware::google::pixel::MiscWriterActions;

static int Usage(std::string_view name) {
  std::cerr << name << " usage:\n";
  std::cerr << name << " [--override-vendor-space-offset <offset>] --<misc_writer_action>\n";
  std::cerr << "Supported misc_writer_action is one of: \n";
  std::cerr << "  --set-dark-theme     Write the dark theme flag\n";
  std::cerr << "  --clear-dark-theme   Clear the dark theme flag\n";
  std::cerr << "  --set-sota           Write the silent OTA flag\n";
  std::cerr << "  --clear-sota         Clear the silent OTA flag\n";
  std::cerr << "Writes the given hex string to the specified offset in vendor space in /misc "
               "partition.\nDefault offset is used for each action unless "
               "--override-vendor-space-offset is specified.\n";
  return EXIT_FAILURE;
}

// misc_writer is a vendor tool that writes data to the vendor space in /misc.
int main(int argc, char** argv) {
  constexpr struct option OPTIONS[] = {
    { "set-dark-theme", no_argument, nullptr, 0 },
    { "clear-dark-theme", no_argument, nullptr, 0 },
    { "set-sota", no_argument, nullptr, 0 },
    { "clear-sota", no_argument, nullptr, 0 },
    { "override-vendor-space-offset", required_argument, nullptr, 0 },
    { nullptr, 0, nullptr, 0 },
  };

  std::map<std::string, MiscWriterActions> action_map{
    { "set-dark-theme", MiscWriterActions::kSetDarkThemeFlag },
    { "clear-dark-theme", MiscWriterActions::kClearDarkThemeFlag },
    { "set-sota", MiscWriterActions::kSetSotaFlag },
    { "clear-sota", MiscWriterActions::kClearSotaFlag },
  };

  std::unique_ptr<MiscWriter> misc_writer;
  std::optional<size_t> override_offset;

  int arg;
  int option_index = 0;
  while ((arg = getopt_long(argc, argv, "", OPTIONS, &option_index)) != -1) {
    if (arg != 0) {
      LOG(ERROR) << "Invalid command argument";
      return Usage(argv[0]);
    }
    auto option_name = OPTIONS[option_index].name;
    if (option_name == "override-vendor-space-offset"s) {
      LOG(WARNING) << "Overriding the vendor space offset in misc partition to " << optarg;
      size_t offset;
      if (!android::base::ParseUint(optarg, &offset)) {
        LOG(ERROR) << "Failed to parse the offset: " << optarg;
        return Usage(argv[0]);
      }
      override_offset = offset;
    } else if (auto iter = action_map.find(option_name); iter != action_map.end()) {
      if (misc_writer) {
        LOG(ERROR) << "Misc writer action has already been set";
        return Usage(argv[0]);
      }
      misc_writer = std::make_unique<MiscWriter>(iter->second);
    } else {
      LOG(FATAL) << "Unreachable path, option_name: " << option_name;
    }
  }

  if (!misc_writer) {
    LOG(ERROR) << "An action must be specified for misc writer";
    return Usage(argv[0]);
  }

  if (!misc_writer->PerformAction(override_offset)) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
