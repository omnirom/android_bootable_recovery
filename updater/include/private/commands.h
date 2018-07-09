/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <ostream>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>  // FRIEND_TEST

#include "otautil/rangeset.h"

// Represents the target info used in a Command. TargetInfo contains the ranges of the blocks and
// the expected hash.
class TargetInfo {
 public:
  TargetInfo() = default;

  TargetInfo(std::string hash, RangeSet ranges)
      : hash_(std::move(hash)), ranges_(std::move(ranges)) {}

  const std::string& hash() const {
    return hash_;
  }

  const RangeSet& ranges() const {
    return ranges_;
  }

  size_t blocks() const {
    return ranges_.blocks();
  }

  bool operator==(const TargetInfo& other) const {
    return hash_ == other.hash_ && ranges_ == other.ranges_;
  }

 private:
  friend std::ostream& operator<<(std::ostream& os, const TargetInfo& source);

  // The hash of the data represented by the object.
  std::string hash_;
  // The block ranges that the data should be written to.
  RangeSet ranges_;
};

std::ostream& operator<<(std::ostream& os, const TargetInfo& source);

// Represents the stash info used in a Command.
class StashInfo {
 public:
  StashInfo() = default;

  StashInfo(std::string id, RangeSet ranges) : id_(std::move(id)), ranges_(std::move(ranges)) {}

  size_t blocks() const {
    return ranges_.blocks();
  }

  const std::string& id() const {
    return id_;
  }

  const RangeSet& ranges() const {
    return ranges_;
  }

  bool operator==(const StashInfo& other) const {
    return id_ == other.id_ && ranges_ == other.ranges_;
  }

 private:
  friend std::ostream& operator<<(std::ostream& os, const StashInfo& stash);

  // The id (i.e. hash) of the stash.
  std::string id_;
  // The matching location of the stash.
  RangeSet ranges_;
};

std::ostream& operator<<(std::ostream& os, const StashInfo& stash);

// Represents the source info in a Command, whose data could come from source image, stashed blocks,
// or both.
class SourceInfo {
 public:
  SourceInfo() = default;

  SourceInfo(std::string hash, RangeSet ranges, RangeSet location, std::vector<StashInfo> stashes)
      : hash_(std::move(hash)),
        ranges_(std::move(ranges)),
        location_(std::move(location)),
        stashes_(std::move(stashes)) {
    blocks_ = ranges_.blocks();
    for (const auto& stash : stashes_) {
      blocks_ += stash.ranges().blocks();
    }
  }

  const std::string& hash() const {
    return hash_;
  }

  size_t blocks() const {
    return blocks_;
  }

  bool operator==(const SourceInfo& other) const {
    return hash_ == other.hash_ && ranges_ == other.ranges_ && location_ == other.location_ &&
           stashes_ == other.stashes_;
  }

 private:
  friend std::ostream& operator<<(std::ostream& os, const SourceInfo& source);

  // The hash of the data represented by the object.
  std::string hash_;
  // The block ranges from the source image to read data from. This could be a subset of all the
  // blocks represented by the object, or empty if all the data should be loaded from stash.
  RangeSet ranges_;
  // The location in the buffer to load ranges_ into. Empty if ranges_ alone covers all the blocks
  // (i.e. nothing needs to be loaded from stash).
  RangeSet location_;
  // The info for the stashed blocks that are part of the source. Empty if there's none.
  std::vector<StashInfo> stashes_;
  // Total number of blocks represented by the object.
  size_t blocks_{ 0 };
};

std::ostream& operator<<(std::ostream& os, const SourceInfo& source);

class PatchInfo {
 public:
  PatchInfo() = default;

  PatchInfo(size_t offset, size_t length) : offset_(offset), length_(length) {}

  size_t offset() const {
    return offset_;
  }

  size_t length() const {
    return length_;
  }

  bool operator==(const PatchInfo& other) const {
    return offset_ == other.offset_ && length_ == other.length_;
  }

 private:
  size_t offset_{ 0 };
  size_t length_{ 0 };
};

// Command class holds the info for an update command that performs block-based OTA (BBOTA). Each
// command consists of one or several args, namely TargetInfo, SourceInfo, StashInfo and PatchInfo.
// The currently used BBOTA version is v4.
//
//    zero <tgt_ranges>
//      - Fill the indicated blocks with zeros.
//      - Meaningful args: TargetInfo
//
//    new <tgt_ranges>
//      - Fill the blocks with data read from the new_data file.
//      - Meaningful args: TargetInfo
//
//    erase <tgt_ranges>
//      - Mark the given blocks as empty.
//      - Meaningful args: TargetInfo
//
//    move <hash> <...>
//      - Read the source blocks, write result to target blocks.
//      - Meaningful args: TargetInfo, SourceInfo
//
//      See the note below for <...>.
//
//    bsdiff <patchstart> <patchlen> <srchash> <dsthash> <...>
//    imgdiff <patchstart> <patchlen> <srchash> <dsthash> <...>
//      - Read the source blocks, apply a patch, and write result to target blocks.
//      - Meaningful args: PatchInfo, TargetInfo, SourceInfo
//
//      It expects <...> in one of the following formats:
//
//        <tgt_ranges> <src_block_count> - <[stash_id:stash_location] ...>
//          (loads data from stashes only)
//
//        <tgt_ranges> <src_block_count> <src_ranges>
//          (loads data from source image only)
//
//        <tgt_ranges> <src_block_count> <src_ranges> <src_ranges_location>
//                                       <[stash_id:stash_location] ...>
//          (loads data from both of source image and stashes)
//
//    stash <stash_id> <src_ranges>
//      - Load the given source blocks and stash the data in the given slot of the stash table.
//      - Meaningful args: StashInfo
//
//    free <stash_id>
//      - Free the given stash data.
//      - Meaningful args: StashInfo
//
//    abort
//      - Abort the current update. Allowed for testing code only.
//
class Command {
 public:
  enum class Type {
    ABORT,
    BSDIFF,
    ERASE,
    FREE,
    IMGDIFF,
    MOVE,
    NEW,
    STASH,
    ZERO,
    LAST,  // Not a valid type.
  };

  Command() = default;

  Command(Type type, size_t index, std::string cmdline, PatchInfo patch, TargetInfo target,
          SourceInfo source, StashInfo stash)
      : type_(type),
        index_(index),
        cmdline_(std::move(cmdline)),
        patch_(std::move(patch)),
        target_(std::move(target)),
        source_(std::move(source)),
        stash_(std::move(stash)) {}

  // Parses the given command 'line' into a Command object and returns it. The 'index' is specified
  // by the caller to index the object. On parsing error, it returns an empty Command object that
  // evaluates to false, and the specific error message will be set in 'err'.
  static Command Parse(const std::string& line, size_t index, std::string* err);

  // Parses the command type from the given string.
  static Type ParseType(const std::string& type_str);

  Type type() const {
    return type_;
  }

  size_t index() const {
    return index_;
  }

  const std::string& cmdline() const {
    return cmdline_;
  }

  const PatchInfo& patch() const {
    return patch_;
  }

  const TargetInfo& target() const {
    return target_;
  }

  const SourceInfo& source() const {
    return source_;
  }

  const StashInfo& stash() const {
    return stash_;
  }

  constexpr explicit operator bool() const {
    return type_ != Type::LAST;
  }

 private:
  friend class ResumableUpdaterTest;
  friend class UpdaterTest;

  FRIEND_TEST(CommandsTest, Parse_ABORT_Allowed);
  FRIEND_TEST(CommandsTest, Parse_InvalidNumberOfArgs);
  FRIEND_TEST(CommandsTest, ParseTargetInfoAndSourceInfo_InvalidInput);
  FRIEND_TEST(CommandsTest, ParseTargetInfoAndSourceInfo_StashesOnly);
  FRIEND_TEST(CommandsTest, ParseTargetInfoAndSourceInfo_SourceBlocksAndStashes);
  FRIEND_TEST(CommandsTest, ParseTargetInfoAndSourceInfo_SourceBlocksOnly);

  // Parses the target and source info from the given 'tokens' vector. Saves the parsed info into
  // 'target' and 'source' objects. Returns the parsing result. Error message will be set in 'err'
  // on parsing error, and the contents in 'target' and 'source' will be undefined.
  static bool ParseTargetInfoAndSourceInfo(const std::vector<std::string>& tokens,
                                           const std::string& tgt_hash, TargetInfo* target,
                                           const std::string& src_hash, SourceInfo* source,
                                           std::string* err);

  // Allows parsing ABORT command, which should be used for testing purpose only.
  static bool abort_allowed_;

  // The type of the command.
  Type type_{ Type::LAST };
  // The index of the Command object, which is specified by the caller.
  size_t index_{ 0 };
  // The input string that the Command object is parsed from.
  std::string cmdline_;
  // The patch info. Only meaningful for BSDIFF and IMGDIFF commands.
  PatchInfo patch_;
  // The target info, where the command should be written to.
  TargetInfo target_;
  // The source info to load the source blocks for the command.
  SourceInfo source_;
  // The stash info. Only meaningful for STASH and FREE commands. Note that although SourceInfo may
  // also load data from stash, such info will be owned and managed by SourceInfo (i.e. in source_).
  StashInfo stash_;
};

std::ostream& operator<<(std::ostream& os, const Command& command);
