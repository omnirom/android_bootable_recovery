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

#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fs_mgr.h>
#include <fs_mgr_dm_linear.h>
#include <libdm/dm.h>
#include <liblp/builder.h>

#include "edify/expr.h"
#include "otautil/error_code.h"
#include "otautil/paths.h"
#include "private/utils.h"

using android::base::ParseUint;
using android::dm::DeviceMapper;
using android::dm::DmDeviceState;
using android::fs_mgr::CreateLogicalPartition;
using android::fs_mgr::DestroyLogicalPartition;
using android::fs_mgr::LpMetadata;
using android::fs_mgr::MetadataBuilder;
using android::fs_mgr::Partition;
using android::fs_mgr::PartitionOpener;

static constexpr std::chrono::milliseconds kMapTimeout{ 1000 };
static constexpr char kMetadataUpdatedMarker[] = "/dynamic_partition_metadata.UPDATED";

static std::string GetSuperDevice() {
  return "/dev/block/by-name/" + fs_mgr_get_super_partition_name();
}

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

static bool UnmapPartitionOnDeviceMapper(const std::string& partition_name) {
  auto state = DeviceMapper::Instance().GetState(partition_name);
  if (state == DmDeviceState::INVALID) {
    return true;
  }
  if (state == DmDeviceState::ACTIVE) {
    return DestroyLogicalPartition(partition_name, kMapTimeout);
  }
  LOG(ERROR) << "Unknown device mapper state: "
             << static_cast<std::underlying_type_t<DmDeviceState>>(state);
  return false;
}

static bool MapPartitionOnDeviceMapper(const std::string& partition_name, std::string* path) {
  auto state = DeviceMapper::Instance().GetState(partition_name);
  if (state == DmDeviceState::INVALID) {
    return CreateLogicalPartition(GetSuperDevice(), 0 /* metadata slot */, partition_name,
                                  true /* force writable */, kMapTimeout, path);
  }

  if (state == DmDeviceState::ACTIVE) {
    return DeviceMapper::Instance().GetDmDevicePathByName(partition_name, path);
  }
  LOG(ERROR) << "Unknown device mapper state: "
             << static_cast<std::underlying_type_t<DmDeviceState>>(state);
  return false;
}

Value* UnmapPartitionFn(const char* name, State* state,
                        const std::vector<std::unique_ptr<Expr>>& argv) {
  auto args = ReadStringArgs(name, state, argv, { "name" });
  if (args.empty()) return StringValue("");

  return UnmapPartitionOnDeviceMapper(args[0]) ? StringValue("t") : StringValue("");
}

Value* MapPartitionFn(const char* name, State* state,
                      const std::vector<std::unique_ptr<Expr>>& argv) {
  auto args = ReadStringArgs(name, state, argv, { "name" });
  if (args.empty()) return StringValue("");

  std::string path;
  bool result = MapPartitionOnDeviceMapper(args[0], &path);
  return result ? StringValue(path) : StringValue("");
}

namespace {  // Ops

struct OpParameters {
  std::vector<std::string> tokens;
  MetadataBuilder* builder;

  bool ExpectArgSize(size_t size) const {
    CHECK(!tokens.empty());
    auto actual = tokens.size() - 1;
    if (actual != size) {
      LOG(ERROR) << "Op " << op() << " expects " << size << " args, got " << actual;
      return false;
    }
    return true;
  }
  const std::string& op() const {
    CHECK(!tokens.empty());
    return tokens[0];
  }
  const std::string& arg(size_t pos) const {
    CHECK_LE(pos + 1, tokens.size());
    return tokens[pos + 1];
  }
  std::optional<uint64_t> uint_arg(size_t pos, const std::string& name) const {
    auto str = arg(pos);
    uint64_t ret;
    if (!ParseUint(str, &ret)) {
      LOG(ERROR) << "Op " << op() << " expects uint64 for argument " << name << ", got " << str;
      return std::nullopt;
    }
    return ret;
  }
};

using OpFunction = std::function<bool(const OpParameters&)>;
using OpMap = std::map<std::string, OpFunction>;

bool PerformOpResize(const OpParameters& params) {
  if (!params.ExpectArgSize(2)) return false;
  const auto& partition_name = params.arg(0);
  auto size = params.uint_arg(1, "size");
  if (!size.has_value()) return false;

  auto partition = params.builder->FindPartition(partition_name);
  if (partition == nullptr) {
    LOG(ERROR) << "Failed to find partition " << partition_name
               << " in dynamic partition metadata.";
    return false;
  }
  if (!UnmapPartitionOnDeviceMapper(partition_name)) {
    LOG(ERROR) << "Cannot unmap " << partition_name << " before resizing.";
    return false;
  }
  if (!params.builder->ResizePartition(partition, size.value())) {
    LOG(ERROR) << "Failed to resize partition " << partition_name << " to size " << *size << ".";
    return false;
  }
  return true;
}

bool PerformOpRemove(const OpParameters& params) {
  if (!params.ExpectArgSize(1)) return false;
  const auto& partition_name = params.arg(0);

  if (!UnmapPartitionOnDeviceMapper(partition_name)) {
    LOG(ERROR) << "Cannot unmap " << partition_name << " before removing.";
    return false;
  }
  params.builder->RemovePartition(partition_name);
  return true;
}

bool PerformOpAdd(const OpParameters& params) {
  if (!params.ExpectArgSize(2)) return false;
  const auto& partition_name = params.arg(0);
  const auto& group_name = params.arg(1);

  if (params.builder->AddPartition(partition_name, group_name, LP_PARTITION_ATTR_READONLY) ==
      nullptr) {
    LOG(ERROR) << "Failed to add partition " << partition_name << " to group " << group_name << ".";
    return false;
  }
  return true;
}

bool PerformOpMove(const OpParameters& params) {
  if (!params.ExpectArgSize(2)) return false;
  const auto& partition_name = params.arg(0);
  const auto& new_group = params.arg(1);

  auto partition = params.builder->FindPartition(partition_name);
  if (partition == nullptr) {
    LOG(ERROR) << "Cannot move partition " << partition_name << " to group " << new_group
               << " because it is not found.";
    return false;
  }

  auto old_group = partition->group_name();
  if (old_group != new_group) {
    if (!params.builder->ChangePartitionGroup(partition, new_group)) {
      LOG(ERROR) << "Cannot move partition " << partition_name << " from group " << old_group
                 << " to group " << new_group << ".";
      return false;
    }
  }
  return true;
}

bool PerformOpAddGroup(const OpParameters& params) {
  if (!params.ExpectArgSize(2)) return false;
  const auto& group_name = params.arg(0);
  auto maximum_size = params.uint_arg(1, "maximum_size");
  if (!maximum_size.has_value()) return false;

  auto group = params.builder->FindGroup(group_name);
  if (group != nullptr) {
    LOG(ERROR) << "Cannot add group " << group_name << " because it already exists.";
    return false;
  }

  if (maximum_size.value() == 0) {
    LOG(WARNING) << "Adding group " << group_name << " with no size limits.";
  }

  if (!params.builder->AddGroup(group_name, maximum_size.value())) {
    LOG(ERROR) << "Failed to add group " << group_name << " with maximum size "
               << maximum_size.value() << ".";
    return false;
  }
  return true;
}

bool PerformOpResizeGroup(const OpParameters& params) {
  if (!params.ExpectArgSize(2)) return false;
  const auto& group_name = params.arg(0);
  auto new_size = params.uint_arg(1, "maximum_size");
  if (!new_size.has_value()) return false;

  auto group = params.builder->FindGroup(group_name);
  if (group == nullptr) {
    LOG(ERROR) << "Cannot resize group " << group_name << " because it is not found.";
    return false;
  }

  auto old_size = group->maximum_size();
  if (old_size != new_size.value()) {
    if (!params.builder->ChangeGroupSize(group_name, new_size.value())) {
      LOG(ERROR) << "Cannot resize group " << group_name << " from " << old_size << " to "
                 << new_size.value() << ".";
      return false;
    }
  }
  return true;
}

std::vector<std::string> ListPartitionNamesInGroup(MetadataBuilder* builder,
                                                   const std::string& group_name) {
  auto partitions = builder->ListPartitionsInGroup(group_name);
  std::vector<std::string> partition_names;
  std::transform(partitions.begin(), partitions.end(), std::back_inserter(partition_names),
                 [](Partition* partition) { return partition->name(); });
  return partition_names;
}

bool PerformOpRemoveGroup(const OpParameters& params) {
  if (!params.ExpectArgSize(1)) return false;
  const auto& group_name = params.arg(0);

  auto partition_names = ListPartitionNamesInGroup(params.builder, group_name);
  if (!partition_names.empty()) {
    LOG(ERROR) << "Cannot remove group " << group_name << " because it still contains partitions ["
               << android::base::Join(partition_names, ", ") << "]";
    return false;
  }
  params.builder->RemoveGroupAndPartitions(group_name);
  return true;
}

bool PerformOpRemoveAllGroups(const OpParameters& params) {
  if (!params.ExpectArgSize(0)) return false;

  auto group_names = params.builder->ListGroups();
  for (const auto& group_name : group_names) {
    auto partition_names = ListPartitionNamesInGroup(params.builder, group_name);
    for (const auto& partition_name : partition_names) {
      if (!UnmapPartitionOnDeviceMapper(partition_name)) {
        LOG(ERROR) << "Cannot unmap " << partition_name << " before removing group " << group_name
                   << ".";
        return false;
      }
    }
    params.builder->RemoveGroupAndPartitions(group_name);
  }
  return true;
}

}  // namespace

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

  auto super_device = GetSuperDevice();
  auto builder = MetadataBuilder::New(PartitionOpener(), super_device, 0);
  if (builder == nullptr) {
    LOG(ERROR) << "Failed to load dynamic partition metadata.";
    return StringValue("");
  }

  static const OpMap op_map{
    // clang-format off
    {"resize",                PerformOpResize},
    {"remove",                PerformOpRemove},
    {"add",                   PerformOpAdd},
    {"move",                  PerformOpMove},
    {"add_group",             PerformOpAddGroup},
    {"resize_group",          PerformOpResizeGroup},
    {"remove_group",          PerformOpRemoveGroup},
    {"remove_all_groups",     PerformOpRemoveAllGroups},
    // clang-format on
  };

  std::vector<std::string> lines = android::base::Split(op_list_value->data, "\n");
  for (const auto& line : lines) {
    auto comment_idx = line.find('#');
    auto op_and_args = comment_idx == std::string::npos ? line : line.substr(0, comment_idx);
    op_and_args = android::base::Trim(op_and_args);
    if (op_and_args.empty()) continue;

    auto tokens = android::base::Split(op_and_args, " ");
    const auto& op = tokens[0];
    auto it = op_map.find(op);
    if (it == op_map.end()) {
      LOG(ERROR) << "Unknown operation in op_list: " << op;
      return StringValue("");
    }
    OpParameters params;
    params.tokens = tokens;
    params.builder = builder.get();
    if (!it->second(params)) {
      return StringValue("");
    }
  }

  auto metadata = builder->Export();
  if (metadata == nullptr) {
    LOG(ERROR) << "Failed to export metadata.";
    return StringValue("");
  }

  if (!UpdatePartitionTable(super_device, *metadata, 0)) {
    LOG(ERROR) << "Failed to write metadata.";
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
