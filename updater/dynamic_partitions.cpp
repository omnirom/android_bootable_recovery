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

#include "updater/dynamic_partitions.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "edify/expr.h"
#include "edify/updater_runtime_interface.h"
#include "otautil/error_code.h"
#include "otautil/paths.h"
#include "private/utils.h"

static std::vector<std::string> ReadStringArgs(const char* name, State* state,
                                               const std::vector<std::unique_ptr<Expr>>& argv,
                                               const std::vector<std::string>& arg_names) {
  if (argv.size() != arg_names.size()) {
    ErrorAbort(state, kArgsParsingFailure, "%s expects %zu arguments, got %zu", name,
               arg_names.size(), argv.size());
    return {};
  }

  std::vector<std::unique_ptr<Value>> args;
  if (!ReadValueArgs(state, argv, &args)) {
    return {};
  }

  CHECK_EQ(args.size(), arg_names.size());

  for (size_t i = 0; i < arg_names.size(); ++i) {
    if (args[i]->type != Value::Type::STRING) {
      ErrorAbort(state, kArgsParsingFailure, "%s argument to %s must be string",
                 arg_names[i].c_str(), name);
      return {};
    }
  }

  std::vector<std::string> ret;
  std::transform(args.begin(), args.end(), std::back_inserter(ret),
                 [](const auto& arg) { return arg->data; });
  return ret;
}

Value* UnmapPartitionFn(const char* name, State* state,
                        const std::vector<std::unique_ptr<Expr>>& argv) {
  auto args = ReadStringArgs(name, state, argv, { "name" });
  if (args.empty()) return StringValue("");

  auto updater_runtime = state->updater->GetRuntime();
  return updater_runtime->UnmapPartitionOnDeviceMapper(args[0]) ? StringValue("t")
                                                                : StringValue("");
}

Value* MapPartitionFn(const char* name, State* state,
                      const std::vector<std::unique_ptr<Expr>>& argv) {
  auto args = ReadStringArgs(name, state, argv, { "name" });
  if (args.empty()) return StringValue("");

  std::string path;
  auto updater_runtime = state->updater->GetRuntime();
  bool result = updater_runtime->MapPartitionOnDeviceMapper(args[0], &path);
  return result ? StringValue(path) : StringValue("");
}

static constexpr char kMetadataUpdatedMarker[] = "/dynamic_partition_metadata.UPDATED";

Value* UpdateDynamicPartitionsFn(const char* name, State* state,
                                 const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 1) {
    ErrorAbort(state, kArgsParsingFailure, "%s expects 1 arguments, got %zu", name, argv.size());
    return StringValue("");
  }
  std::vector<std::unique_ptr<Value>> args;
  if (!ReadValueArgs(state, argv, &args)) {
    return nullptr;
  }
  const std::unique_ptr<Value>& op_list_value = args[0];
  if (op_list_value->type != Value::Type::BLOB) {
    ErrorAbort(state, kArgsParsingFailure, "op_list argument to %s must be blob", name);
    return StringValue("");
  }

  std::string updated_marker = Paths::Get().stash_directory_base() + kMetadataUpdatedMarker;
  if (state->is_retry) {
    struct stat sb;
    int result = stat(updated_marker.c_str(), &sb);
    if (result == 0) {
      LOG(INFO) << "Skipping already updated dynamic partition metadata based on marker";
      return StringValue("t");
    }
  } else {
    // Delete the obsolete marker if any.
    std::string err;
    if (!android::base::RemoveFileIfExists(updated_marker, &err)) {
      LOG(ERROR) << "Failed to remove dynamic partition metadata updated marker " << updated_marker
                 << ": " << err;
      return StringValue("");
    }
  }

  auto updater_runtime = state->updater->GetRuntime();
  if (!updater_runtime->UpdateDynamicPartitions(op_list_value->data)) {
    return StringValue("");
  }

  if (!SetUpdatedMarker(updated_marker)) {
    LOG(ERROR) << "Failed to set metadata updated marker.";
    return StringValue("");
  }

  return StringValue("t");
}

void RegisterDynamicPartitionsFunctions() {
  RegisterFunction("unmap_partition", UnmapPartitionFn);
  RegisterFunction("map_partition", MapPartitionFn);
  RegisterFunction("update_dynamic_partitions", UpdateDynamicPartitionsFn);
}
