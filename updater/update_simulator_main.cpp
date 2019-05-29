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

#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>

#include "otautil/error_code.h"
#include "otautil/paths.h"
#include "updater/blockimg.h"
#include "updater/install.h"
#include "updater/simulator_runtime.h"
#include "updater/target_files.h"
#include "updater/updater.h"

int main(int argc, char** argv) {
  // Write the logs to stdout.
  android::base::InitLogging(argv, &android::base::StderrLogger);

  if (argc != 3 && argc != 4) {
    LOG(ERROR) << "unexpected number of arguments: " << argc << std::endl
               << "Usage: " << argv[0] << " <source_target-file> <ota_package>";
    return 1;
  }

  // TODO(xunchang) implement a commandline parser, e.g. it can take an oem property so that the
  // file_getprop() will return correct value.

  std::string source_target_file = argv[1];
  std::string package_name = argv[2];

  // Configure edify's functions.
  RegisterBuiltins();
  RegisterInstallFunctions();
  RegisterBlockImageFunctions();

  TemporaryFile temp_saved_source;
  TemporaryFile temp_last_command;
  TemporaryDir temp_stash_base;

  Paths::Get().set_cache_temp_source(temp_saved_source.path);
  Paths::Get().set_last_command_file(temp_last_command.path);
  Paths::Get().set_stash_directory_base(temp_stash_base.path);

  TemporaryFile cmd_pipe;

  TemporaryDir source_temp_dir;
  TargetFiles source(source_target_file, source_temp_dir.path);

  Updater updater(std::make_unique<SimulatorRuntime>(&source));
  if (!updater.Init(cmd_pipe.release(), package_name, false)) {
    return 1;
  }

  if (!updater.RunUpdate()) {
    return 1;
  }

  LOG(INFO) << "\nscript succeeded, result: " << updater.GetResult();

  return 0;
}
