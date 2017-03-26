/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

struct RangeSet {
  size_t count;             // Limit is INT_MAX.
  size_t size;              // The number of blocks in the RangeSet.
  std::vector<size_t> pos;  // Actual limit is INT_MAX.

  static RangeSet Parse(const std::string& range_text) {
    std::vector<std::string> pieces = android::base::Split(range_text, ",");
    CHECK_GE(pieces.size(), static_cast<size_t>(3)) << "Invalid range text: " << range_text;

    size_t num;
    CHECK(android::base::ParseUint(pieces[0], &num, static_cast<size_t>(INT_MAX)))
        << "Failed to parse the number of tokens: " << range_text;

    CHECK_NE(num, static_cast<size_t>(0)) << "Invalid number of tokens: " << range_text;
    CHECK_EQ(num % 2, static_cast<size_t>(0)) << "Number of tokens must be even: " << range_text;
    CHECK_EQ(num, pieces.size() - 1) << "Mismatching number of tokens: " << range_text;

    std::vector<size_t> pairs(num);
    size_t size = 0;
    for (size_t i = 0; i < num; i += 2) {
      CHECK(android::base::ParseUint(pieces[i + 1], &pairs[i], static_cast<size_t>(INT_MAX)));
      CHECK(android::base::ParseUint(pieces[i + 2], &pairs[i + 1], static_cast<size_t>(INT_MAX)));
      CHECK_LT(pairs[i], pairs[i + 1])
          << "Empty or negative range: " << pairs[i] << ", " << pairs[i + 1];

      size_t sz = pairs[i + 1] - pairs[i];
      CHECK_LE(size, SIZE_MAX - sz) << "RangeSet size overflow";
      size += sz;
    }

    return RangeSet{ num / 2, size, std::move(pairs) };
  }

  // Get the block number for the i-th (starting from 0) block in the RangeSet.
  size_t GetBlockNumber(size_t idx) const {
    CHECK_LT(idx, size) << "Index " << idx << " is greater than RangeSet size " << size;
    for (size_t i = 0; i < pos.size(); i += 2) {
      if (idx < pos[i + 1] - pos[i]) {
        return pos[i] + idx;
      }
      idx -= (pos[i + 1] - pos[i]);
    }
    CHECK(false);
    return 0;  // Unreachable, but to make compiler happy.
  }

  // RangeSet has half-closed half-open bounds. For example, "3,5" contains blocks 3 and 4. So "3,5"
  // and "5,7" are not overlapped.
  bool Overlaps(const RangeSet& other) const {
    for (size_t i = 0; i < count; ++i) {
      size_t start = pos[i * 2];
      size_t end = pos[i * 2 + 1];
      for (size_t j = 0; j < other.count; ++j) {
        size_t other_start = other.pos[j * 2];
        size_t other_end = other.pos[j * 2 + 1];
        // [start, end) vs [other_start, other_end)
        if (!(other_start >= end || start >= other_end)) {
          return true;
        }
      }
    }
    return false;
  }

  bool operator==(const RangeSet& other) const {
    return (count == other.count && size == other.size && pos == other.pos);
  }
};
