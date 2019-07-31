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
#include <stdlib.h>
#include <unistd.h>

#include <string>
#include <string_view>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "edify/expr.h"
#include "otautil/error_code.h"
#include "otautil/paths.h"
#include "updater/blockimg.h"
#include "updater/build_info.h"
#include "updater/dynamic_partitions.h"
#include "updater/install.h"
#include "updater/simulator_runtime.h"
#include "updater/updater.h"

using namespace std::string_literals;

void Usage(std::string_view name) {
  LOG(INFO) << "Usage: " << name << "[--oem_settings <oem_property_file>]"
            << "[--skip_functions <skip_function_file>]"
            << " --source <source_target_file>"
            << " --ota_package <ota_package>";
}

Value* SimulatorPlaceHolderFn(const char* name, State* /* state */,
                              const std::vector<std::unique_ptr<Expr>>& /* argv */) {
  LOG(INFO) << "Skip function " << name << " in host simulation";
  return StringValue("t");
}

int main(int argc, char** argv) {
  // Write the logs to stdout.
  android::base::InitLogging(argv, &android::base::StderrLogger);

  std::string oem_settings;
  std::string skip_function_file;
  std::string source_target_file;
  std::string package_name;
  std::string work_dir;
  bool keep_images = false;

  constexpr struct option OPTIONS[] = {
    { "keep_images", no_argument, nullptr, 0 },
    { "oem_settings", required_argument, nullptr, 0 },
    { "ota_package", required_argument, nullptr, 0 },
    { "skip_functions", required_argument, nullptr, 0 },
    { "source", required_argument, nullptr, 0 },
    { "work_dir", required_argument, nullptr, 0 },
    { nullptr, 0, nullptr, 0 },
  };

  int arg;
  int option_index;
  while ((arg = getopt_long(argc, argv, "", OPTIONS, &option_index)) != -1) {
    if (arg != 0) {
      LOG(ERROR) << "Invalid command argument";
      Usage(argv[0]);
      return EXIT_FAILURE;
    }
    auto option_name = OPTIONS[option_index].name;
    // The same oem property file used during OTA generation. It's needed for file_getprop() to
    // return the correct value for the source build.
    if (option_name == "oem_settings"s) {
      oem_settings = optarg;
    } else if (option_name == "skip_functions"s) {
      skip_function_file = optarg;
    } else if (option_name == "source"s) {
      source_target_file = optarg;
    } else if (option_name == "ota_package"s) {
      package_name = optarg;
    } else if (option_name == "keep_images"s) {
      keep_images = true;
    } else if (option_name == "work_dir"s) {
      work_dir = optarg;
    } else {
      Usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (source_target_file.empty() || package_name.empty()) {
    Usage(argv[0]);
    return EXIT_FAILURE;
  }

  // Configure edify's functions.
  RegisterBuiltins();
  RegisterInstallFunctions();
  RegisterBlockImageFunctions();
  RegisterDynamicPartitionsFunctions();

  if (!skip_function_file.empty()) {
    std::string content;
    if (!android::base::ReadFileToString(skip_function_file, &content)) {
      PLOG(ERROR) << "Failed to read " << skip_function_file;
      return EXIT_FAILURE;
    }

    auto lines = android::base::Split(content, "\n");
    for (const auto& line : lines) {
      if (line.empty() || android::base::StartsWith(line, "#")) {
        continue;
      }
      RegisterFunction(line, SimulatorPlaceHolderFn);
    }
  }

  TemporaryFile temp_saved_source;
  TemporaryFile temp_last_command;
  TemporaryDir temp_stash_base;

  Paths::Get().set_cache_temp_source(temp_saved_source.path);
  Paths::Get().set_last_command_file(temp_last_command.path);
  Paths::Get().set_stash_directory_base(temp_stash_base.path);

  TemporaryFile cmd_pipe;
  TemporaryDir source_temp_dir;
  if (work_dir.empty()) {
    work_dir = source_temp_dir.path;
  }

  BuildInfo source_build_info(work_dir, keep_images);
  if (!source_build_info.ParseTargetFile(source_target_file, false)) {
    LOG(ERROR) << "Failed to parse the target file " << source_target_file;
    return EXIT_FAILURE;
  }

  if (!oem_settings.empty()) {
    CHECK_EQ(0, access(oem_settings.c_str(), R_OK));
    source_build_info.SetOemSettings(oem_settings);
  }

  Updater updater(std::make_unique<SimulatorRuntime>(&source_build_info));
  if (!updater.Init(cmd_pipe.release(), package_name, false)) {
    return EXIT_FAILURE;
  }

  if (!updater.RunUpdate()) {
    return EXIT_FAILURE;
  }

  LOG(INFO) << "\nscript succeeded, result: " << updater.GetResult();

  return 0;
}
