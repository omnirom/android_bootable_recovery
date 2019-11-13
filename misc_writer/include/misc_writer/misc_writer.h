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

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

enum class MiscWriterActions : int32_t {
  kSetDarkThemeFlag = 0,
  kClearDarkThemeFlag,
  kSetSotaFlag,
  kClearSotaFlag,

  kUnset = -1,
};

class MiscWriter {
 public:
  static constexpr uint32_t kThemeFlagOffsetInVendorSpace = 0;
  static constexpr char kDarkThemeFlag[] = "theme-dark";
  static constexpr uint32_t kSotaFlagOffsetInVendorSpace = 32;
  static constexpr char kSotaFlag[] = "enable-sota";

  // Returns true of |size| bytes data starting from |offset| is fully inside the vendor space.
  static bool OffsetAndSizeInVendorSpace(size_t offset, size_t size);
  // Writes the given data to the vendor space in /misc partition, at the given offset. Note that
  // offset is in relative to the start of the vendor space.
  static bool WriteMiscPartitionVendorSpace(const void* data, size_t size, size_t offset,
                                            std::string* err);

  explicit MiscWriter(const MiscWriterActions& action) : action_(action) {}

  // Performs the stored MiscWriterActions. If |override_offset| is set, writes to the input offset
  // in the vendor space of /misc instead of the default offset.
  bool PerformAction(std::optional<size_t> override_offset = std::nullopt);

 private:
  MiscWriterActions action_{ MiscWriterActions::kUnset };
};

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android
