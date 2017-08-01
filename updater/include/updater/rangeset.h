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
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

using Range = std::pair<size_t, size_t>;

class RangeSet {
 public:
  RangeSet() : blocks_(0) {}

  explicit RangeSet(std::vector<Range>&& pairs) {
    CHECK_NE(pairs.size(), static_cast<size_t>(0)) << "Invalid number of tokens";

    // Sanity check the input.
    size_t result = 0;
    for (const auto& range : pairs) {
      CHECK_LT(range.first, range.second)
          << "Empty or negative range: " << range.first << ", " << range.second;
      size_t sz = range.second - range.first;
      CHECK_LE(result, SIZE_MAX - sz) << "RangeSet size overflow";
      result += sz;
    }

    ranges_ = pairs;
    blocks_ = result;
  }

  static RangeSet Parse(const std::string& range_text) {
    std::vector<std::string> pieces = android::base::Split(range_text, ",");
    CHECK_GE(pieces.size(), static_cast<size_t>(3)) << "Invalid range text: " << range_text;

    size_t num;
    CHECK(android::base::ParseUint(pieces[0], &num, static_cast<size_t>(INT_MAX)))
        << "Failed to parse the number of tokens: " << range_text;

    CHECK_NE(num, static_cast<size_t>(0)) << "Invalid number of tokens: " << range_text;
    CHECK_EQ(num % 2, static_cast<size_t>(0)) << "Number of tokens must be even: " << range_text;
    CHECK_EQ(num, pieces.size() - 1) << "Mismatching number of tokens: " << range_text;

    std::vector<Range> pairs;
    for (size_t i = 0; i < num; i += 2) {
      size_t first;
      CHECK(android::base::ParseUint(pieces[i + 1], &first, static_cast<size_t>(INT_MAX)));
      size_t second;
      CHECK(android::base::ParseUint(pieces[i + 2], &second, static_cast<size_t>(INT_MAX)));

      pairs.emplace_back(first, second);
    }

    return RangeSet(std::move(pairs));
  }

  std::string ToString() const {
    if (ranges_.empty()) {
      return "";
    }
    std::string result = std::to_string(ranges_.size() * 2);
    for (const auto& r : ranges_) {
      result += android::base::StringPrintf(",%zu,%zu", r.first, r.second);
    }

    return result;
  }

  // Get the block number for the i-th (starting from 0) block in the RangeSet.
  size_t GetBlockNumber(size_t idx) const {
    CHECK_LT(idx, blocks_) << "Out of bound index " << idx << " (total blocks: " << blocks_ << ")";

    for (const auto& range : ranges_) {
      if (idx < range.second - range.first) {
        return range.first + idx;
      }
      idx -= (range.second - range.first);
    }

    CHECK(false) << "Failed to find block number for index " << idx;
    return 0;  // Unreachable, but to make compiler happy.
  }

  // RangeSet has half-closed half-open bounds. For example, "3,5" contains blocks 3 and 4. So "3,5"
  // and "5,7" are not overlapped.
  bool Overlaps(const RangeSet& other) const {
    for (const auto& range : ranges_) {
      size_t start = range.first;
      size_t end = range.second;
      for (const auto& other_range : other.ranges_) {
        size_t other_start = other_range.first;
        size_t other_end = other_range.second;
        // [start, end) vs [other_start, other_end)
        if (!(other_start >= end || start >= other_end)) {
          return true;
        }
      }
    }
    return false;
  }

  // size() gives the number of Range's in this RangeSet.
  size_t size() const {
    return ranges_.size();
  }

  // blocks() gives the number of all blocks in this RangeSet.
  size_t blocks() const {
    return blocks_;
  }

  // We provide const iterators only.
  std::vector<Range>::const_iterator cbegin() const {
    return ranges_.cbegin();
  }

  std::vector<Range>::const_iterator cend() const {
    return ranges_.cend();
  }

  // Need to provide begin()/end() since range-based loop expects begin()/end().
  std::vector<Range>::const_iterator begin() const {
    return ranges_.cbegin();
  }

  std::vector<Range>::const_iterator end() const {
    return ranges_.cend();
  }

  // Reverse const iterators for MoveRange().
  std::vector<Range>::const_reverse_iterator crbegin() const {
    return ranges_.crbegin();
  }

  std::vector<Range>::const_reverse_iterator crend() const {
    return ranges_.crend();
  }

  const Range& operator[](size_t i) const {
    return ranges_[i];
  }

  bool operator==(const RangeSet& other) const {
    // The orders of Range's matter. "4,1,5,8,10" != "4,8,10,1,5".
    return (ranges_ == other.ranges_);
  }

  bool operator!=(const RangeSet& other) const {
    return ranges_ != other.ranges_;
  }

 protected:
  // Actual limit for each value and the total number are both INT_MAX.
  std::vector<Range> ranges_;
  size_t blocks_;
};

static constexpr size_t kBlockSize = 4096;

// The class is a sorted version of a RangeSet; and it's useful in imgdiff to split the input
// files when we're handling large zip files. Specifically, we can treat the input file as a
// continuous RangeSet (i.e. RangeSet("0-99") for a 100 blocks file); and break it down into
// several smaller chunks based on the zip entries.

// For example, [source: 0-99] can be split into
// [split_src1: 10-29]; [split_src2: 40-49, 60-69]; [split_src3: 70-89]
// Here "10-29" simply means block 10th to block 29th with respect to the original input file.
// Also, note that the split sources should be mutual exclusive, but they don't need to cover
// every block in the original source.
class SortedRangeSet : public RangeSet {
 public:
  SortedRangeSet() {}

  // Ranges in the the set should be mutually exclusive; and they're sorted by the start block.
  explicit SortedRangeSet(std::vector<Range>&& pairs) : RangeSet(std::move(pairs)) {
    std::sort(ranges_.begin(), ranges_.end());
  }

  void Insert(const Range& to_insert) {
    SortedRangeSet rs({ to_insert });
    Insert(rs);
  }

  // Insert the input SortedRangeSet; keep the ranges sorted and merge the overlap ranges.
  void Insert(const SortedRangeSet& rs) {
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

  void Clear() {
    blocks_ = 0;
    ranges_.clear();
  }

  using RangeSet::Overlaps;
  bool Overlaps(size_t start, size_t len) const {
    RangeSet rs({ { start / kBlockSize, (start + len - 1) / kBlockSize + 1 } });
    return Overlaps(rs);
  }

  // Compute the block range the file occupies, and insert that range.
  void Insert(size_t start, size_t len) {
    Range to_insert{ start / kBlockSize, (start + len - 1) / kBlockSize + 1 };
    Insert(to_insert);
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
  size_t GetOffsetInRangeSet(size_t old_offset) const {
    size_t old_block_start = old_offset / kBlockSize;
    size_t new_block_start = 0;
    for (const auto& range : ranges_) {
      // Find the index of old_block_start.
      if (old_block_start >= range.second) {
        new_block_start += (range.second - range.first);
      } else if (old_block_start >= range.first) {
        new_block_start += (old_block_start - range.first);
        return (new_block_start * kBlockSize + old_offset % kBlockSize);
      } else {
        CHECK(false) <<"block_start " << old_block_start << " is missing between two ranges: "
                     << this->ToString();
        return 0;
      }
    }
    CHECK(false) <<"block_start " << old_block_start << " exceeds the limit of current RangeSet: "
                 << this->ToString();
    return 0;
  }
};