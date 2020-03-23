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

#include "private/commands.h"

#include <stdint.h>
#include <string.h>

#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <openssl/sha.h>

#include "otautil/print_sha1.h"
#include "otautil/rangeset.h"

using namespace std::string_literals;

bool Command::abort_allowed_ = false;

Command::Command(Type type, size_t index, std::string cmdline, HashTreeInfo hash_tree_info)
    : type_(type),
      index_(index),
      cmdline_(std::move(cmdline)),
      hash_tree_info_(std::move(hash_tree_info)) {
  CHECK(type == Type::COMPUTE_HASH_TREE);
}

Command::Type Command::ParseType(const std::string& type_str) {
  if (type_str == "abort") {
    if (!abort_allowed_) {
      LOG(ERROR) << "ABORT disallowed";
      return Type::LAST;
    }
    return Type::ABORT;
  } else if (type_str == "bsdiff") {
    return Type::BSDIFF;
  } else if (type_str == "compute_hash_tree") {
    return Type::COMPUTE_HASH_TREE;
  } else if (type_str == "erase") {
    return Type::ERASE;
  } else if (type_str == "free") {
    return Type::FREE;
  } else if (type_str == "imgdiff") {
    return Type::IMGDIFF;
  } else if (type_str == "move") {
    return Type::MOVE;
  } else if (type_str == "new") {
    return Type::NEW;
  } else if (type_str == "stash") {
    return Type::STASH;
  } else if (type_str == "zero") {
    return Type::ZERO;
  }
  return Type::LAST;
};

bool Command::ParseTargetInfoAndSourceInfo(const std::vector<std::string>& tokens,
                                           const std::string& tgt_hash, TargetInfo* target,
                                           const std::string& src_hash, SourceInfo* source,
                                           std::string* err) {
  // We expect the given args (in 'tokens' vector) in one of the following formats.
  //
  //    <tgt_ranges> <src_block_count> - <[stash_id:location] ...>
  //        (loads data from stashes only)
  //
  //    <tgt_ranges> <src_block_count> <src_ranges>
  //        (loads data from source image only)
  //
  //    <tgt_ranges> <src_block_count> <src_ranges> <src_ranges_location> <[stash_id:location] ...>
  //        (loads data from both of source image and stashes)

  // At least it needs to provide three args: <tgt_ranges>, <src_block_count> and "-"/<src_ranges>.
  if (tokens.size() < 3) {
    *err = "invalid number of args";
    return false;
  }

  size_t pos = 0;
  RangeSet tgt_ranges = RangeSet::Parse(tokens[pos++]);
  if (!tgt_ranges) {
    *err = "invalid target ranges";
    return false;
  }
  *target = TargetInfo(tgt_hash, tgt_ranges);

  // <src_block_count>
  const std::string& token = tokens[pos++];
  size_t src_blocks;
  if (!android::base::ParseUint(token, &src_blocks)) {
    *err = "invalid src_block_count \""s + token + "\"";
    return false;
  }

  RangeSet src_ranges;
  RangeSet src_ranges_location;
  // "-" or <src_ranges> [<src_ranges_location>]
  if (tokens[pos] == "-") {
    // no source ranges, only stashes
    pos++;
  } else {
    src_ranges = RangeSet::Parse(tokens[pos++]);
    if (!src_ranges) {
      *err = "invalid source ranges";
      return false;
    }

    if (pos >= tokens.size()) {
      // No stashes, only source ranges.
      SourceInfo result(src_hash, src_ranges, {}, {});

      // Sanity check the block count.
      if (result.blocks() != src_blocks) {
        *err =
            android::base::StringPrintf("mismatching block count: %zu (%s) vs %zu", result.blocks(),
                                        src_ranges.ToString().c_str(), src_blocks);
        return false;
      }

      *source = result;
      return true;
    }

    src_ranges_location = RangeSet::Parse(tokens[pos++]);
    if (!src_ranges_location) {
      *err = "invalid source ranges location";
      return false;
    }
  }

  // <[stash_id:stash_location]>
  std::vector<StashInfo> stashes;
  while (pos < tokens.size()) {
    // Each word is a an index into the stash table, a colon, and then a RangeSet describing where
    // in the source block that stashed data should go.
    std::vector<std::string> pairs = android::base::Split(tokens[pos++], ":");
    if (pairs.size() != 2) {
      *err = "invalid stash info";
      return false;
    }
    RangeSet stash_location = RangeSet::Parse(pairs[1]);
    if (!stash_location) {
      *err = "invalid stash location";
      return false;
    }
    stashes.emplace_back(pairs[0], stash_location);
  }

  SourceInfo result(src_hash, src_ranges, src_ranges_location, stashes);
  if (src_blocks != result.blocks()) {
    *err = android::base::StringPrintf("mismatching block count: %zu (%s) vs %zu", result.blocks(),
                                       src_ranges.ToString().c_str(), src_blocks);
    return false;
  }

  *source = result;
  return true;
}

Command Command::Parse(const std::string& line, size_t index, std::string* err) {
  std::vector<std::string> tokens = android::base::Split(line, " ");
  size_t pos = 0;
  // tokens.size() will be 1 at least.
  Type op = ParseType(tokens[pos++]);
  if (op == Type::LAST) {
    *err = "invalid type";
    return {};
  }

  PatchInfo patch_info;
  TargetInfo target_info;
  SourceInfo source_info;
  StashInfo stash_info;

  if (op == Type::ZERO || op == Type::NEW || op == Type::ERASE) {
    // zero/new/erase <rangeset>
    if (pos + 1 != tokens.size()) {
      *err = android::base::StringPrintf("invalid number of args: %zu (expected 1)",
                                         tokens.size() - pos);
      return {};
    }
    RangeSet tgt_ranges = RangeSet::Parse(tokens[pos++]);
    if (!tgt_ranges) {
      return {};
    }
    static const std::string kUnknownHash{ "unknown-hash" };
    target_info = TargetInfo(kUnknownHash, tgt_ranges);
  } else if (op == Type::STASH) {
    // stash <stash_id> <src_ranges>
    if (pos + 2 != tokens.size()) {
      *err = android::base::StringPrintf("invalid number of args: %zu (expected 2)",
                                         tokens.size() - pos);
      return {};
    }
    const std::string& id = tokens[pos++];
    RangeSet src_ranges = RangeSet::Parse(tokens[pos++]);
    if (!src_ranges) {
      *err = "invalid token";
      return {};
    }
    stash_info = StashInfo(id, src_ranges);
  } else if (op == Type::FREE) {
    // free <stash_id>
    if (pos + 1 != tokens.size()) {
      *err = android::base::StringPrintf("invalid number of args: %zu (expected 1)",
                                         tokens.size() - pos);
      return {};
    }
    stash_info = StashInfo(tokens[pos++], {});
  } else if (op == Type::MOVE) {
    // <hash>
    if (pos + 1 > tokens.size()) {
      *err = "missing hash";
      return {};
    }
    std::string hash = tokens[pos++];
    if (!ParseTargetInfoAndSourceInfo(
            std::vector<std::string>(tokens.cbegin() + pos, tokens.cend()), hash, &target_info,
            hash, &source_info, err)) {
      return {};
    }
  } else if (op == Type::BSDIFF || op == Type::IMGDIFF) {
    // <offset> <length> <srchash> <dsthash>
    if (pos + 4 > tokens.size()) {
      *err = android::base::StringPrintf("invalid number of args: %zu (expected 4+)",
                                         tokens.size() - pos);
      return {};
    }
    size_t offset;
    size_t length;
    if (!android::base::ParseUint(tokens[pos++], &offset) ||
        !android::base::ParseUint(tokens[pos++], &length)) {
      *err = "invalid patch offset/length";
      return {};
    }
    patch_info = PatchInfo(offset, length);

    std::string src_hash = tokens[pos++];
    std::string dst_hash = tokens[pos++];
    if (!ParseTargetInfoAndSourceInfo(
            std::vector<std::string>(tokens.cbegin() + pos, tokens.cend()), dst_hash, &target_info,
            src_hash, &source_info, err)) {
      return {};
    }
  } else if (op == Type::ABORT) {
    // No-op, other than sanity checking the input args.
    if (pos != tokens.size()) {
      *err = android::base::StringPrintf("invalid number of args: %zu (expected 0)",
                                         tokens.size() - pos);
      return {};
    }
  } else if (op == Type::COMPUTE_HASH_TREE) {
    // <hash_tree_ranges> <source_ranges> <hash_algorithm> <salt_hex> <root_hash>
    if (pos + 5 != tokens.size()) {
      *err = android::base::StringPrintf("invalid number of args: %zu (expected 5)",
                                         tokens.size() - pos);
      return {};
    }

    // Expects the hash_tree data to be contiguous.
    RangeSet hash_tree_ranges = RangeSet::Parse(tokens[pos++]);
    if (!hash_tree_ranges || hash_tree_ranges.size() != 1) {
      *err = "invalid hash tree ranges in: " + line;
      return {};
    }

    RangeSet source_ranges = RangeSet::Parse(tokens[pos++]);
    if (!source_ranges) {
      *err = "invalid source ranges in: " + line;
      return {};
    }

    std::string hash_algorithm = tokens[pos++];
    std::string salt_hex = tokens[pos++];
    std::string root_hash = tokens[pos++];
    if (hash_algorithm.empty() || salt_hex.empty() || root_hash.empty()) {
      *err = "invalid hash tree arguments in " + line;
      return {};
    }

    HashTreeInfo hash_tree_info(std::move(hash_tree_ranges), std::move(source_ranges),
                                std::move(hash_algorithm), std::move(salt_hex),
                                std::move(root_hash));
    return Command(op, index, line, std::move(hash_tree_info));
  } else {
    *err = "invalid op";
    return {};
  }

  return Command(op, index, line, patch_info, target_info, source_info, stash_info);
}

bool SourceInfo::Overlaps(const TargetInfo& target) const {
  return ranges_.Overlaps(target.ranges());
}

// Moves blocks in the 'source' vector to the specified locations (as in 'locs') in the 'dest'
// vector. Note that source and dest may be the same buffer.
static void MoveRange(std::vector<uint8_t>* dest, const RangeSet& locs,
                      const std::vector<uint8_t>& source, size_t block_size) {
  const uint8_t* from = source.data();
  uint8_t* to = dest->data();
  size_t start = locs.blocks();
  // Must do the movement backward.
  for (auto it = locs.crbegin(); it != locs.crend(); it++) {
    size_t blocks = it->second - it->first;
    start -= blocks;
    memmove(to + (it->first * block_size), from + (start * block_size), blocks * block_size);
  }
}

bool SourceInfo::ReadAll(
    std::vector<uint8_t>* buffer, size_t block_size,
    const std::function<int(const RangeSet&, std::vector<uint8_t>*)>& block_reader,
    const std::function<int(const std::string&, std::vector<uint8_t>*)>& stash_reader) const {
  if (buffer->size() < blocks() * block_size) {
    return false;
  }

  // Read in the source ranges.
  if (ranges_) {
    if (block_reader(ranges_, buffer) != 0) {
      return false;
    }
    if (location_) {
      MoveRange(buffer, location_, *buffer, block_size);
    }
  }

  // Read in the stashes.
  for (const StashInfo& stash : stashes_) {
    std::vector<uint8_t> stash_buffer(stash.blocks() * block_size);
    if (stash_reader(stash.id(), &stash_buffer) != 0) {
      return false;
    }
    MoveRange(buffer, stash.ranges(), stash_buffer, block_size);
  }
  return true;
}

void SourceInfo::DumpBuffer(const std::vector<uint8_t>& buffer, size_t block_size) const {
  LOG(INFO) << "Dumping hashes in hex for " << ranges_.blocks() << " source blocks";

  const RangeSet& location = location_ ? location_ : RangeSet({ Range{ 0, ranges_.blocks() } });
  for (size_t i = 0; i < ranges_.blocks(); i++) {
    size_t block_num = ranges_.GetBlockNumber(i);
    size_t buffer_index = location.GetBlockNumber(i);
    CHECK_LE((buffer_index + 1) * block_size, buffer.size());

    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1(buffer.data() + buffer_index * block_size, block_size, digest);
    std::string hexdigest = print_sha1(digest);
    LOG(INFO) << "  block number: " << block_num << ", SHA-1: " << hexdigest;
  }
}

std::ostream& operator<<(std::ostream& os, const Command& command) {
  os << command.index() << ": " << command.cmdline();
  return os;
}

std::ostream& operator<<(std::ostream& os, const TargetInfo& target) {
  os << target.blocks() << " blocks (" << target.hash_ << "): " << target.ranges_.ToString();
  return os;
}

std::ostream& operator<<(std::ostream& os, const StashInfo& stash) {
  os << stash.blocks() << " blocks (" << stash.id_ << "): " << stash.ranges_.ToString();
  return os;
}

std::ostream& operator<<(std::ostream& os, const SourceInfo& source) {
  os << source.blocks_ << " blocks (" << source.hash_ << "): ";
  if (source.ranges_) {
    os << source.ranges_.ToString();
    if (source.location_) {
      os << " (location: " << source.location_.ToString() << ")";
    }
  }
  if (!source.stashes_.empty()) {
    os << " " << source.stashes_.size() << " stash(es)";
  }
  return os;
}

TransferList TransferList::Parse(const std::string& transfer_list_str, std::string* err) {
  TransferList result{};

  std::vector<std::string> lines = android::base::Split(transfer_list_str, "\n");
  if (lines.size() < kTransferListHeaderLines) {
    *err = android::base::StringPrintf("too few lines in the transfer list [%zu]", lines.size());
    return TransferList{};
  }

  // First line in transfer list is the version number.
  if (!android::base::ParseInt(lines[0], &result.version_, 3, 4)) {
    *err = "unexpected transfer list version ["s + lines[0] + "]";
    return TransferList{};
  }

  // Second line in transfer list is the total number of blocks we expect to write.
  if (!android::base::ParseUint(lines[1], &result.total_blocks_)) {
    *err = "unexpected block count ["s + lines[1] + "]";
    return TransferList{};
  }

  // Third line is how many stash entries are needed simultaneously.
  if (!android::base::ParseUint(lines[2], &result.stash_max_entries_)) {
    return TransferList{};
  }

  // Fourth line is the maximum number of blocks that will be stashed simultaneously.
  if (!android::base::ParseUint(lines[3], &result.stash_max_blocks_)) {
    *err = "unexpected maximum stash blocks ["s + lines[3] + "]";
    return TransferList{};
  }

  // Subsequent lines are all individual transfer commands.
  for (size_t i = kTransferListHeaderLines; i < lines.size(); i++) {
    const std::string& line = lines[i];
    if (line.empty()) continue;

    size_t cmdindex = i - kTransferListHeaderLines;
    std::string parsing_error;
    Command command = Command::Parse(line, cmdindex, &parsing_error);
    if (!command) {
      *err = android::base::StringPrintf("Failed to parse command %zu [%s]: %s", cmdindex,
                                         line.c_str(), parsing_error.c_str());
      return TransferList{};
    }
    result.commands_.push_back(command);
  }

  return result;
}
