/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include "applypatch_modes.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <openssl/sha.h>

#include "applypatch/applypatch.h"
#include "edify/expr.h"

static int CheckMode(const std::string& target_emmc) {
  std::string err;
  auto target = Partition::Parse(target_emmc, &err);
  if (!target) {
    LOG(ERROR) << "Failed to parse target \"" << target_emmc << "\": " << err;
    return 2;
  }
  return CheckPartition(target) ? 0 : 1;
}

static int FlashMode(const std::string& target_emmc, const std::string& source_file) {
  std::string err;
  auto target = Partition::Parse(target_emmc, &err);
  if (!target) {
    LOG(ERROR) << "Failed to parse target \"" << target_emmc << "\": " << err;
    return 2;
  }
  return FlashPartition(target, source_file) ? 0 : 1;
}

static int PatchMode(const std::string& target_emmc, const std::string& source_emmc,
                     const std::string& patch_file, const std::string& bonus_file) {
  std::string err;
  auto target = Partition::Parse(target_emmc, &err);
  if (!target) {
    LOG(ERROR) << "Failed to parse target \"" << target_emmc << "\": " << err;
    return 2;
  }

  auto source = Partition::Parse(source_emmc, &err);
  if (!source) {
    LOG(ERROR) << "Failed to parse source \"" << source_emmc << "\": " << err;
    return 2;
  }

  std::string patch_contents;
  if (!android::base::ReadFileToString(patch_file, &patch_contents)) {
    PLOG(ERROR) << "Failed to read patch file \"" << patch_file << "\"";
    return 1;
  }

  Value patch(Value::Type::BLOB, std::move(patch_contents));
  std::unique_ptr<Value> bonus;
  if (!bonus_file.empty()) {
    std::string bonus_contents;
    if (!android::base::ReadFileToString(bonus_file, &bonus_contents)) {
      PLOG(ERROR) << "Failed to read bonus file \"" << bonus_file << "\"";
      return 1;
    }
    bonus = std::make_unique<Value>(Value::Type::BLOB, std::move(bonus_contents));
  }

  return PatchPartition(target, source, patch, bonus.get()) ? 0 : 1;
}

static void Usage() {
  printf(
      "Usage: \n"
      "check mode\n"
      "  applypatch --check EMMC:<target-file>:<target-size>:<target-sha1>\n\n"
      "flash mode\n"
      "  applypatch --flash <source-file>\n"
      "             --target EMMC:<target-file>:<target-size>:<target-sha1>\n\n"
      "patch mode\n"
      "  applypatch [--bonus <bonus-file>]\n"
      "             --patch <patch-file>\n"
      "             --target EMMC:<target-file>:<target-size>:<target-sha1>\n"
      "             --source EMMC:<source-file>:<source-size>:<source-sha1>\n\n"
      "show license\n"
      "  applypatch --license\n"
      "\n\n");
}

int applypatch_modes(int argc, char* argv[]) {
  static constexpr struct option OPTIONS[]{
    // clang-format off
    { "bonus", required_argument, nullptr, 0 },
    { "check", required_argument, nullptr, 0 },
    { "flash", required_argument, nullptr, 0 },
    { "license", no_argument, nullptr, 0 },
    { "patch", required_argument, nullptr, 0 },
    { "source", required_argument, nullptr, 0 },
    { "target", required_argument, nullptr, 0 },
    { nullptr, 0, nullptr, 0 },
    // clang-format on
  };

  std::string check_target;
  std::string source;
  std::string target;
  std::string patch;
  std::string bonus;

  bool check_mode = false;
  bool flash_mode = false;
  bool patch_mode = false;

  optind = 1;

  int arg;
  int option_index;
  while ((arg = getopt_long(argc, argv, "", OPTIONS, &option_index)) != -1) {
    switch (arg) {
      case 0: {
        std::string option = OPTIONS[option_index].name;
        if (option == "bonus") {
          bonus = optarg;
        } else if (option == "check") {
          check_target = optarg;
          check_mode = true;
        } else if (option == "flash") {
          source = optarg;
          flash_mode = true;
        } else if (option == "license") {
          return ShowLicenses();
        } else if (option == "patch") {
          patch = optarg;
          patch_mode = true;
        } else if (option == "source") {
          source = optarg;
        } else if (option == "target") {
          target = optarg;
        }
        break;
      }
      case '?':
      default:
        LOG(ERROR) << "Invalid argument";
        Usage();
        return 2;
    }
  }

  if (check_mode) {
    return CheckMode(check_target);
  }
  if (flash_mode) {
    if (!bonus.empty()) {
      LOG(ERROR) << "bonus file not supported in flash mode";
      return 1;
    }
    return FlashMode(target, source);
  }
  if (patch_mode) {
    return PatchMode(target, source, patch, bonus);
  }

  Usage();
  return 2;
}
