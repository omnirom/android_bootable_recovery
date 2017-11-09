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

using Range = std::pair<size_t, size_t>;

class RangeSet {
 public:
  RangeSet() : blocks_(0) {}

  explicit RangeSet(std::vector<Range>&& pairs);

  // Parses the given string into a RangeSet. Returns the parsed RangeSet, or an empty RangeSet on
  // errors.
  static RangeSet Parse(const std::string& range_text);

  // Appends the given Range to the current RangeSet.
  bool PushBack(Range range);

  // Clears all the ranges from the RangeSet.
  void Clear();

  std::string ToString() const;

  // Gets the block number for the i-th (starting from 0) block in the RangeSet.
  size_t GetBlockNumber(size_t idx) const;

  // Returns whether the current RangeSet overlaps with other. RangeSet has half-closed half-open
  // bounds. For example, "3,5" contains blocks 3 and 4. So "3,5" and "5,7" are not overlapped.
  bool Overlaps(const RangeSet& other) const;

  // Returns a vector of RangeSets that contain the same set of blocks represented by the current
  // RangeSet. The RangeSets in the vector contain similar number of blocks, with a maximum delta
  // of 1-block between any two of them. For example, 14 blocks would be split into 4 + 4 + 3 + 3,
  // as opposed to 4 + 4 + 4 + 2. If the total number of blocks (T) is less than groups, it
  // returns a vector of T 1-block RangeSets. Otherwise the number of the returned RangeSets must
  // equal to groups. The current RangeSet remains intact after the split.
  std::vector<RangeSet> Split(size_t groups) const;

  // Returns the number of Range's in this RangeSet.
  size_t size() const {
    return ranges_.size();
  }

  // Returns the total number of blocks in this RangeSet.
  size_t blocks() const {
    return blocks_;
  }

  std::vector<Range>::const_iterator cbegin() const {
    return ranges_.cbegin();
  }

  std::vector<Range>::const_iterator cend() const {
    return ranges_.cend();
  }

  std::vector<Range>::iterator begin() {
    return ranges_.begin();
  }

  std::vector<Range>::iterator end() {
    return ranges_.end();
  }

  std::vector<Range>::const_iterator begin() const {
    return ranges_.begin();
  }

  std::vector<Range>::const_iterator end() const {
    return ranges_.end();
  }

  // Reverse const iterators for MoveRange().
  std::vector<Range>::const_reverse_iterator crbegin() const {
    return ranges_.crbegin();
  }

  std::vector<Range>::const_reverse_iterator crend() const {
    return ranges_.crend();
  }

  // Returns whether the RangeSet is valid (i.e. non-empty).
  explicit operator bool() const {
    return !ranges_.empty();
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
  // The block size when working with offset and file length.
  static constexpr size_t kBlockSize = 4096;

  SortedRangeSet() {}

  // Ranges in the the set should be mutually exclusive; and they're sorted by the start block.
  explicit SortedRangeSet(std::vector<Range>&& pairs);

  void Insert(const Range& to_insert);

  // Insert the input SortedRangeSet; keep the ranges sorted and merge the overlap ranges.
  void Insert(const SortedRangeSet& rs);

  // Compute the block range the file occupies, and insert that range.
  void Insert(size_t start, size_t len);

  using RangeSet::Overlaps;

  bool Overlaps(size_t start, size_t len) const;

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
  size_t GetOffsetInRangeSet(size_t old_offset) const;
};
