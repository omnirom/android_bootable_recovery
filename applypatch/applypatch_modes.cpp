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

static int CheckMode(const std::string& target) {
  return applypatch_check(target, {});
}

static int FlashMode(const std::string& target_emmc, const std::string& source_file) {
  std::vector<std::string> pieces = android::base::Split(target_emmc, ":");
  if (pieces.size() != 4 || pieces[0] != "EMMC") {
    return 2;
  }
  size_t target_size;
  if (!android::base::ParseUint(pieces[2], &target_size) || target_size == 0) {
    LOG(ERROR) << "Failed to parse \"" << pieces[2] << "\" as byte count";
    return 1;
  }
  return applypatch_flash(source_file.c_str(), target_emmc.c_str(), pieces[3].c_str(), target_size);
}

static int PatchMode(const std::string& target_emmc, const std::string& source_emmc,
                     const std::string& patch_file, const std::string& bonus_file) {
  std::vector<std::string> target_pieces = android::base::Split(target_emmc, ":");
  if (target_pieces.size() != 4 || target_pieces[0] != "EMMC") {
    return 2;
  }

  size_t target_size;
  if (!android::base::ParseUint(target_pieces[2], &target_size) || target_size == 0) {
    LOG(ERROR) << "Failed to parse \"" << target_pieces[2] << "\" as byte count";
    return 1;
  }

  std::vector<std::string> source_pieces = android::base::Split(source_emmc, ":");
  if (source_pieces.size() != 4 || source_pieces[0] != "EMMC") {
    return 2;
  }

  size_t source_size;
  if (!android::base::ParseUint(source_pieces[2], &source_size) || source_size == 0) {
    LOG(ERROR) << "Failed to parse \"" << source_pieces[2] << "\" as byte count";
    return 1;
  }

  std::string contents;
  if (!android::base::ReadFileToString(patch_file, &contents)) {
    PLOG(ERROR) << "Failed to read patch file \"" << patch_file << "\"";
    return 1;
  }
  std::vector<std::unique_ptr<Value>> patches;
  patches.push_back(std::make_unique<Value>(Value::Type::BLOB, std::move(contents)));
  std::vector<std::string> sha1s{ source_pieces[3] };

  std::unique_ptr<Value> bonus;
  if (!bonus_file.empty()) {
    std::string bonus_contents;
    if (!android::base::ReadFileToString(bonus_file, &bonus_contents)) {
      PLOG(ERROR) << "Failed to read bonus file \"" << bonus_file << "\"";
      return 1;
    }
    bonus = std::make_unique<Value>(Value::Type::BLOB, std::move(bonus_contents));
  }

  return applypatch(source_emmc.c_str(), target_emmc.c_str(), target_pieces[3].c_str(), target_size,
                    sha1s, patches, bonus.get());
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
