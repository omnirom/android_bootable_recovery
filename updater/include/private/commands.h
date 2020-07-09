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

#include <stdint.h>

#include <functional>
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

  // Reads all the data specified by this SourceInfo object into the given 'buffer', by calling the
  // given readers. Caller needs to specify the block size for the represented blocks. The given
  // buffer needs to be sufficiently large. Otherwise it returns false. 'block_reader' and
  // 'stash_reader' read the specified data into the given buffer (guaranteed to be large enough)
  // respectively. The readers should return 0 on success, or -1 on error.
  bool ReadAll(
      std::vector<uint8_t>* buffer, size_t block_size,
      const std::function<int(const RangeSet&, std::vector<uint8_t>*)>& block_reader,
      const std::function<int(const std::string&, std::vector<uint8_t>*)>& stash_reader) const;

  // Whether this SourceInfo overlaps with the given TargetInfo object.
  bool Overlaps(const TargetInfo& target) const;

  // Dumps the hashes in hex for the given buffer that's loaded from this SourceInfo object
  // (excluding the stashed blocks which are handled separately).
  void DumpBuffer(const std::vector<uint8_t>& buffer, size_t block_size) const;

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

// The arguments to build a hash tree from blocks on the block device.
class HashTreeInfo {
 public:
  HashTreeInfo() = default;

  HashTreeInfo(RangeSet hash_tree_ranges, RangeSet source_ranges, std::string hash_algorithm,
               std::string salt_hex, std::string root_hash)
      : hash_tree_ranges_(std::move(hash_tree_ranges)),
        source_ranges_(std::move(source_ranges)),
        hash_algorithm_(std::move(hash_algorithm)),
        salt_hex_(std::move(salt_hex)),
        root_hash_(std::move(root_hash)) {}

  const RangeSet& hash_tree_ranges() const {
    return hash_tree_ranges_;
  }
  const RangeSet& source_ranges() const {
    return source_ranges_;
  }

  const std::string& hash_algorithm() const {
    return hash_algorithm_;
  }
  const std::string& salt_hex() const {
    return salt_hex_;
  }
  const std::string& root_hash() const {
    return root_hash_;
  }

  bool operator==(const HashTreeInfo& other) const {
    return hash_tree_ranges_ == other.hash_tree_ranges_ && source_ranges_ == other.source_ranges_ &&
           hash_algorithm_ == other.hash_algorithm_ && salt_hex_ == other.salt_hex_ &&
           root_hash_ == other.root_hash_;
  }

 private:
  RangeSet hash_tree_ranges_;
  RangeSet source_ranges_;
  std::string hash_algorithm_;
  std::string salt_hex_;
  std::string root_hash_;
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
//    compute_hash_tree <hash_tree_ranges> <source_ranges> <hash_algorithm> <salt_hex> <root_hash>
//      - Computes the hash_tree bytes and writes the result to the specified range on the
//        block_device.
//
//    abort
//      - Abort the current update. Allowed for testing code only.
//
class Command {
 public:
  enum class Type {
    ABORT,
    BSDIFF,
    COMPUTE_HASH_TREE,
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

  Command(Type type, size_t index, std::string cmdline, HashTreeInfo hash_tree_info);

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

  const HashTreeInfo& hash_tree_info() const {
    return hash_tree_info_;
  }

  size_t block_size() const {
    return block_size_;
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
  // The hash_tree info. Only meaningful for COMPUTE_HASH_TREE.
  HashTreeInfo hash_tree_info_;
  // The unit size of each block to be used in this command.
  size_t block_size_{ 4096 };
};

std::ostream& operator<<(std::ostream& os, const Command& command);

// TransferList represents the info for a transfer list, which is parsed from input text lines
// containing commands to transfer data from one place to another on the target partition.
//
// The creator of the transfer list will guarantee that no block is read (i.e., used as the source
// for a patch or move) after it has been written.
//
// The creator will guarantee that a given stash is loaded (with a stash command) before it's used
// in a move/bsdiff/imgdiff command.
//
// Within one command the source and target ranges may overlap so in general we need to read the
// entire source into memory before writing anything to the target blocks.
//
// All the patch data is concatenated into one patch_data file in the update package. It must be
// stored uncompressed because we memory-map it in directly from the archive. (Since patches are
// already compressed, we lose very little by not compressing their concatenation.)
//
// Commands that read data from the partition (i.e. move/bsdiff/imgdiff/stash) have one or more
// additional hashes before the range parameters, which are used to check if the command has
// already been completed and verify the integrity of the source data.
class TransferList {
 public:
  // Number of header lines.
  static constexpr size_t kTransferListHeaderLines = 4;

  TransferList() = default;

  // Parses the given input string and returns a TransferList object. Sets error message if any.
  static TransferList Parse(const std::string& transfer_list_str, std::string* err);

  int version() const {
    return version_;
  }

  size_t total_blocks() const {
    return total_blocks_;
  }

  size_t stash_max_entries() const {
    return stash_max_entries_;
  }

  size_t stash_max_blocks() const {
    return stash_max_blocks_;
  }

  const std::vector<Command>& commands() const {
    return commands_;
  }

  // Returns whether the TransferList is valid.
  constexpr explicit operator bool() const {
    return version_ != 0;
  }

 private:
  // BBOTA version.
  int version_{ 0 };
  // Total number of blocks to be written in this transfer.
  size_t total_blocks_;
  // Maximum number of stashes that exist at the same time.
  size_t stash_max_entries_;
  // Maximum number of blocks to be stashed.
  size_t stash_max_blocks_;
  // Commands in this transfer.
  std::vector<Command> commands_;
};
