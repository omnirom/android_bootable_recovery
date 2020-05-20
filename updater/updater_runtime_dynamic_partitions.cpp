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

#include "updater/updater_runtime.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fs_mgr.h>
#include <fs_mgr_dm_linear.h>
#include <libdm/dm.h>
#include <liblp/builder.h>

using android::dm::DeviceMapper;
using android::dm::DmDeviceState;
using android::fs_mgr::CreateLogicalPartition;
using android::fs_mgr::CreateLogicalPartitionParams;
using android::fs_mgr::DestroyLogicalPartition;
using android::fs_mgr::LpMetadata;
using android::fs_mgr::MetadataBuilder;
using android::fs_mgr::Partition;
using android::fs_mgr::PartitionOpener;
using android::fs_mgr::SlotNumberForSlotSuffix;

static constexpr std::chrono::milliseconds kMapTimeout{ 1000 };

static std::string GetSuperDevice() {
  return "/dev/block/by-name/" + fs_mgr_get_super_partition_name();
}

static std::string AddSlotSuffix(const std::string& partition_name) {
  return partition_name + fs_mgr_get_slot_suffix();
}

static bool UnmapPartitionWithSuffixOnDeviceMapper(const std::string& partition_name_suffix) {
  auto state = DeviceMapper::Instance().GetState(partition_name_suffix);
  if (state == DmDeviceState::INVALID) {
    return true;
  }
  if (state == DmDeviceState::ACTIVE) {
    return DestroyLogicalPartition(partition_name_suffix);
  }
  LOG(ERROR) << "Unknown device mapper state: "
             << static_cast<std::underlying_type_t<DmDeviceState>>(state);
  return false;
}

bool UpdaterRuntime::MapPartitionOnDeviceMapper(const std::string& partition_name,
                                                std::string* path) {
  auto partition_name_suffix = AddSlotSuffix(partition_name);
  auto state = DeviceMapper::Instance().GetState(partition_name_suffix);
  if (state == DmDeviceState::INVALID) {
    CreateLogicalPartitionParams params = {
      .block_device = GetSuperDevice(),
      // If device supports A/B, apply non-A/B update to the partition at current slot. Otherwise,
      // SlotNumberForSlotSuffix("") returns 0.
      .metadata_slot = SlotNumberForSlotSuffix(fs_mgr_get_slot_suffix()),
      // If device supports A/B, apply non-A/B update to the partition at current slot. Otherwise,
      // fs_mgr_get_slot_suffix() returns empty string.
      .partition_name = partition_name_suffix,
      .force_writable = true,
      .timeout_ms = kMapTimeout,
    };
    return CreateLogicalPartition(params, path);
  }

  if (state == DmDeviceState::ACTIVE) {
    return DeviceMapper::Instance().GetDmDevicePathByName(partition_name_suffix, path);
  }
  LOG(ERROR) << "Unknown device mapper state: "
             << static_cast<std::underlying_type_t<DmDeviceState>>(state);
  return false;
}

bool UpdaterRuntime::UnmapPartitionOnDeviceMapper(const std::string& partition_name) {
  return ::UnmapPartitionWithSuffixOnDeviceMapper(AddSlotSuffix(partition_name));
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
    if (!android::base::ParseUint(str, &ret)) {
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
  const auto& partition_name_suffix = AddSlotSuffix(params.arg(0));
  auto size = params.uint_arg(1, "size");
  if (!size.has_value()) return false;

  auto partition = params.builder->FindPartition(partition_name_suffix);
  if (partition == nullptr) {
    LOG(ERROR) << "Failed to find partition " << partition_name_suffix
               << " in dynamic partition metadata.";
    return false;
  }
  if (!UnmapPartitionWithSuffixOnDeviceMapper(partition_name_suffix)) {
    LOG(ERROR) << "Cannot unmap " << partition_name_suffix << " before resizing.";
    return false;
  }
  if (!params.builder->ResizePartition(partition, size.value())) {
    LOG(ERROR) << "Failed to resize partition " << partition_name_suffix << " to size " << *size
               << ".";
    return false;
  }
  return true;
}

bool PerformOpRemove(const OpParameters& params) {
  if (!params.ExpectArgSize(1)) return false;
  const auto& partition_name_suffix = AddSlotSuffix(params.arg(0));

  if (!UnmapPartitionWithSuffixOnDeviceMapper(partition_name_suffix)) {
    LOG(ERROR) << "Cannot unmap " << partition_name_suffix << " before removing.";
    return false;
  }
  params.builder->RemovePartition(partition_name_suffix);
  return true;
}

bool PerformOpAdd(const OpParameters& params) {
  if (!params.ExpectArgSize(2)) return false;
  const auto& partition_name_suffix = AddSlotSuffix(params.arg(0));
  const auto& group_name_suffix = AddSlotSuffix(params.arg(1));

  if (params.builder->AddPartition(partition_name_suffix, group_name_suffix,
                                   LP_PARTITION_ATTR_READONLY) == nullptr) {
    LOG(ERROR) << "Failed to add partition " << partition_name_suffix << " to group "
               << group_name_suffix << ".";
    return false;
  }
  return true;
}

bool PerformOpMove(const OpParameters& params) {
  if (!params.ExpectArgSize(2)) return false;
  const auto& partition_name_suffix = AddSlotSuffix(params.arg(0));
  const auto& new_group_name_suffix = AddSlotSuffix(params.arg(1));

  auto partition = params.builder->FindPartition(partition_name_suffix);
  if (partition == nullptr) {
    LOG(ERROR) << "Cannot move partition " << partition_name_suffix << " to group "
               << new_group_name_suffix << " because it is not found.";
    return false;
  }

  auto old_group_name_suffix = partition->group_name();
  if (old_group_name_suffix != new_group_name_suffix) {
    if (!params.builder->ChangePartitionGroup(partition, new_group_name_suffix)) {
      LOG(ERROR) << "Cannot move partition " << partition_name_suffix << " from group "
                 << old_group_name_suffix << " to group " << new_group_name_suffix << ".";
      return false;
    }
  }
  return true;
}

bool PerformOpAddGroup(const OpParameters& params) {
  if (!params.ExpectArgSize(2)) return false;
  const auto& group_name_suffix = AddSlotSuffix(params.arg(0));
  auto maximum_size = params.uint_arg(1, "maximum_size");
  if (!maximum_size.has_value()) return false;

  auto group = params.builder->FindGroup(group_name_suffix);
  if (group != nullptr) {
    LOG(ERROR) << "Cannot add group " << group_name_suffix << " because it already exists.";
    return false;
  }

  if (maximum_size.value() == 0) {
    LOG(WARNING) << "Adding group " << group_name_suffix << " with no size limits.";
  }

  if (!params.builder->AddGroup(group_name_suffix, maximum_size.value())) {
    LOG(ERROR) << "Failed to add group " << group_name_suffix << " with maximum size "
               << maximum_size.value() << ".";
    return false;
  }
  return true;
}

bool PerformOpResizeGroup(const OpParameters& params) {
  if (!params.ExpectArgSize(2)) return false;
  const auto& group_name_suffix = AddSlotSuffix(params.arg(0));
  auto new_size = params.uint_arg(1, "maximum_size");
  if (!new_size.has_value()) return false;

  auto group = params.builder->FindGroup(group_name_suffix);
  if (group == nullptr) {
    LOG(ERROR) << "Cannot resize group " << group_name_suffix << " because it is not found.";
    return false;
  }

  auto old_size = group->maximum_size();
  if (old_size != new_size.value()) {
    if (!params.builder->ChangeGroupSize(group_name_suffix, new_size.value())) {
      LOG(ERROR) << "Cannot resize group " << group_name_suffix << " from " << old_size << " to "
                 << new_size.value() << ".";
      return false;
    }
  }
  return true;
}

std::vector<std::string> ListPartitionNamesInGroup(MetadataBuilder* builder,
                                                   const std::string& group_name_suffix) {
  auto partitions = builder->ListPartitionsInGroup(group_name_suffix);
  std::vector<std::string> partition_names;
  std::transform(partitions.begin(), partitions.end(), std::back_inserter(partition_names),
                 [](Partition* partition) { return partition->name(); });
  return partition_names;
}

bool PerformOpRemoveGroup(const OpParameters& params) {
  if (!params.ExpectArgSize(1)) return false;
  const auto& group_name_suffix = AddSlotSuffix(params.arg(0));

  auto partition_names = ListPartitionNamesInGroup(params.builder, group_name_suffix);
  if (!partition_names.empty()) {
    LOG(ERROR) << "Cannot remove group " << group_name_suffix
               << " because it still contains partitions ["
               << android::base::Join(partition_names, ", ") << "]";
    return false;
  }
  params.builder->RemoveGroupAndPartitions(group_name_suffix);
  return true;
}

bool PerformOpRemoveAllGroups(const OpParameters& params) {
  if (!params.ExpectArgSize(0)) return false;

  auto group_names = params.builder->ListGroups();
  for (const auto& group_name_suffix : group_names) {
    auto partition_names = ListPartitionNamesInGroup(params.builder, group_name_suffix);
    for (const auto& partition_name_suffix : partition_names) {
      if (!UnmapPartitionWithSuffixOnDeviceMapper(partition_name_suffix)) {
        LOG(ERROR) << "Cannot unmap " << partition_name_suffix << " before removing group "
                   << group_name_suffix << ".";
        return false;
      }
    }
    params.builder->RemoveGroupAndPartitions(group_name_suffix);
  }
  return true;
}

}  // namespace

bool UpdaterRuntime::UpdateDynamicPartitions(const std::string_view op_list_value) {
  auto super_device = GetSuperDevice();
  auto builder = MetadataBuilder::New(PartitionOpener(), super_device, 0);
  if (builder == nullptr) {
    LOG(ERROR) << "Failed to load dynamic partition metadata.";
    return false;
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

  std::vector<std::string> lines = android::base::Split(std::string(op_list_value), "\n");
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
      return false;
    }
    OpParameters params;
    params.tokens = tokens;
    params.builder = builder.get();
    if (!it->second(params)) {
      return false;
    }
  }

  auto metadata = builder->Export();
  if (metadata == nullptr) {
    LOG(ERROR) << "Failed to export metadata.";
    return false;
  }

  if (!UpdatePartitionTable(super_device, *metadata, 0)) {
    LOG(ERROR) << "Failed to write metadata.";
    return false;
  }

  return true;
}
