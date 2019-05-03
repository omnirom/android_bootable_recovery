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

#include "otautil/rangeset.h"

#include <limits.h>
#include <stddef.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

RangeSet::RangeSet(std::vector<Range>&& pairs) {
  blocks_ = 0;
  if (pairs.empty()) {
    LOG(ERROR) << "Invalid number of tokens";
    return;
  }

  for (const auto& range : pairs) {
    if (!PushBack(range)) {
      Clear();
      return;
    }
  }
}

RangeSet RangeSet::Parse(const std::string& range_text) {
  std::vector<std::string> pieces = android::base::Split(range_text, ",");
  if (pieces.size() < 3) {
    LOG(ERROR) << "Invalid range text: " << range_text;
    return {};
  }

  size_t num;
  if (!android::base::ParseUint(pieces[0], &num, static_cast<size_t>(INT_MAX))) {
    LOG(ERROR) << "Failed to parse the number of tokens: " << range_text;
    return {};
  }
  if (num == 0) {
    LOG(ERROR) << "Invalid number of tokens: " << range_text;
    return {};
  }
  if (num % 2 != 0) {
    LOG(ERROR) << "Number of tokens must be even: " << range_text;
    return {};
  }
  if (num != pieces.size() - 1) {
    LOG(ERROR) << "Mismatching number of tokens: " << range_text;
    return {};
  }

  std::vector<Range> pairs;
  for (size_t i = 0; i < num; i += 2) {
    size_t first;
    size_t second;
    if (!android::base::ParseUint(pieces[i + 1], &first, static_cast<size_t>(INT_MAX)) ||
        !android::base::ParseUint(pieces[i + 2], &second, static_cast<size_t>(INT_MAX))) {
      return {};
    }
    pairs.emplace_back(first, second);
  }
  return RangeSet(std::move(pairs));
}

bool RangeSet::PushBack(Range range) {
  if (range.first >= range.second) {
    LOG(ERROR) << "Empty or negative range: " << range.first << ", " << range.second;
    return false;
  }
  size_t sz = range.second - range.first;
  if (blocks_ >= SIZE_MAX - sz) {
    LOG(ERROR) << "RangeSet size overflow";
    return false;
  }

  ranges_.push_back(std::move(range));
  blocks_ += sz;
  return true;
}

void RangeSet::Clear() {
  ranges_.clear();
  blocks_ = 0;
}

std::vector<RangeSet> RangeSet::Split(size_t groups) const {
  if (ranges_.empty() || groups == 0) return {};

  if (blocks_ < groups) {
    groups = blocks_;
  }

  // Evenly distribute blocks, with the first few groups possibly containing one more.
  size_t mean = blocks_ / groups;
  std::vector<size_t> blocks_per_group(groups, mean);
  std::fill_n(blocks_per_group.begin(), blocks_ % groups, mean + 1);

  std::vector<RangeSet> result;

  // Forward iterate Ranges and fill up each group with the desired number of blocks.
  auto it = ranges_.cbegin();
  Range range = *it;
  for (const auto& blocks : blocks_per_group) {
    RangeSet buffer;
    size_t needed = blocks;
    while (needed > 0) {
      size_t range_blocks = range.second - range.first;
      if (range_blocks > needed) {
        // Split the current range and don't advance the iterator.
        buffer.PushBack({ range.first, range.first + needed });
        range.first = range.first + needed;
        break;
      }
      buffer.PushBack(range);
      it++;
      if (it != ranges_.cend()) {
        range = *it;
      }
      needed -= range_blocks;
    }
    result.push_back(std::move(buffer));
  }
  return result;
}

std::string RangeSet::ToString() const {
  if (ranges_.empty()) {
    return "";
  }
  std::string result = std::to_string(ranges_.size() * 2);
  for (const auto& [begin, end] : ranges_) {
    result += android::base::StringPrintf(",%zu,%zu", begin, end);
  }

  return result;
}

// Get the block number for the i-th (starting from 0) block in the RangeSet.
size_t RangeSet::GetBlockNumber(size_t idx) const {
  CHECK_LT(idx, blocks_) << "Out of bound index " << idx << " (total blocks: " << blocks_ << ")";

  for (const auto& [begin, end] : ranges_) {
    if (idx < end - begin) {
      return begin + idx;
    }
    idx -= (end - begin);
  }

  CHECK(false) << "Failed to find block number for index " << idx;
  return 0;  // Unreachable, but to make compiler happy.
}

// RangeSet has half-closed half-open bounds. For example, "3,5" contains blocks 3 and 4. So "3,5"
// and "5,7" are not overlapped.
bool RangeSet::Overlaps(const RangeSet& other) const {
  for (const auto& [begin, end] : ranges_) {
    for (const auto& [other_begin, other_end] : other.ranges_) {
      // [begin, end) vs [other_begin, other_end)
      if (!(other_begin >= end || begin >= other_end)) {
        return true;
      }
    }
  }
  return false;
}

std::optional<RangeSet> RangeSet::GetSubRanges(size_t start_index, size_t num_of_blocks) const {
  size_t end_index = start_index + num_of_blocks;  // The index of final block to read plus one
  if (start_index > end_index || end_index > blocks_) {
    LOG(ERROR) << "Failed to get the sub ranges for start_index " << start_index
               << " num_of_blocks " << num_of_blocks
               << " total number of blocks the range contains is " << blocks_;
    return std::nullopt;
  }

  if (num_of_blocks == 0) {
    LOG(WARNING) << "num_of_blocks is zero when calling GetSubRanges()";
    return RangeSet();
  }

  RangeSet result;
  size_t current_index = 0;
  for (const auto& [range_start, range_end] : ranges_) {
    CHECK_LT(range_start, range_end);
    size_t blocks_in_range = range_end - range_start;
    // Linear search to skip the ranges until we reach start_block.
    if (current_index + blocks_in_range <= start_index) {
      current_index += blocks_in_range;
      continue;
    }

    size_t trimmed_range_start = range_start;
    // We have found the first block range to read, trim the heading blocks.
    if (current_index < start_index) {
      trimmed_range_start += start_index - current_index;
    }
    // Trim the trailing blocks if the last range has more blocks than desired; also return the
    // result.
    if (current_index + blocks_in_range >= end_index) {
      size_t trimmed_range_end = range_end - (current_index + blocks_in_range - end_index);
      if (!result.PushBack({ trimmed_range_start, trimmed_range_end })) {
        return std::nullopt;
      }

      return result;
    }

    if (!result.PushBack({ trimmed_range_start, range_end })) {
      return std::nullopt;
    }
    current_index += blocks_in_range;
  }

  LOG(ERROR) << "Failed to construct byte ranges to read, start_block: " << start_index
             << ", num_of_blocks: " << num_of_blocks << " total number of blocks: " << blocks_;
  return std::nullopt;
}

// Ranges in the the set should be mutually exclusive; and they're sorted by the start block.
SortedRangeSet::SortedRangeSet(std::vector<Range>&& pairs) : RangeSet(std::move(pairs)) {
  std::sort(ranges_.begin(), ranges_.end());
}

void SortedRangeSet::Insert(const Range& to_insert) {
  SortedRangeSet rs({ to_insert });
  Insert(rs);
}

// Insert the input SortedRangeSet; keep the ranges sorted and merge the overlap ranges.
void SortedRangeSet::Insert(const SortedRangeSet& rs) {
  if (rs.size() == 0) {
    return;
  }
  // Merge and sort the two RangeSets.
  std::vector<Range> temp = std::move(ranges_);
  std::copy(rs.begin(), rs.end(), std::back_inserter(temp));
  std::sort(temp.begin(), temp.end());

  Clear();
  // Trim overlaps and insert the result back to ranges_.
  Range to_insert = temp.front();
  for (auto it = temp.cbegin() + 1; it != temp.cend(); it++) {
    if (it->first <= to_insert.second) {
      to_insert.second = std::max(to_insert.second, it->second);
    } else {
      ranges_.push_back(to_insert);
      blocks_ += (to_insert.second - to_insert.first);
      to_insert = *it;
    }
  }
  ranges_.push_back(to_insert);
  blocks_ += (to_insert.second - to_insert.first);
}

// Compute the block range the file occupies, and insert that range.
void SortedRangeSet::Insert(size_t start, size_t len) {
  Range to_insert{ start / kBlockSize, (start + len - 1) / kBlockSize + 1 };
  Insert(to_insert);
}

bool SortedRangeSet::Overlaps(size_t start, size_t len) const {
  RangeSet rs({ { start / kBlockSize, (start + len - 1) / kBlockSize + 1 } });
  return Overlaps(rs);
}

// Given an offset of the file, checks if the corresponding block (by considering the file as
// 0-based continuous block ranges) is covered by the SortedRangeSet. If so, returns the offset
// within this SortedRangeSet.
//
// For example, the 4106-th byte of a file is from block 1, assuming a block size of 4096-byte.
// The mapped offset within a SortedRangeSet("1-9 15-19") is 10.
//
// An offset of 65546 falls into the 16-th block in a file. Block 16 is contained as the 10-th
// item in SortedRangeSet("1-9 15-19"). So its data can be found at offset 40970 (i.e. 4096 * 10
// + 10) in a range represented by this SortedRangeSet.
size_t SortedRangeSet::GetOffsetInRangeSet(size_t old_offset) const {
  size_t old_block_start = old_offset / kBlockSize;
  size_t new_block_start = 0;
  for (const auto& [start, end] : ranges_) {
    // Find the index of old_block_start.
    if (old_block_start >= end) {
      new_block_start += (end - start);
    } else if (old_block_start >= start) {
      new_block_start += (old_block_start - start);
      return (new_block_start * kBlockSize + old_offset % kBlockSize);
    } else {
      CHECK(false) << "block_start " << old_block_start
                   << " is missing between two ranges: " << ToString();
      return 0;
    }
  }
  CHECK(false) << "block_start " << old_block_start
               << " exceeds the limit of current RangeSet: " << ToString();
  return 0;
}
