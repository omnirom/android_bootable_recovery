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

#include <ostream>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "otautil/rangeset.h"

using namespace std::string_literals;

bool Command::abort_allowed_ = false;

Command::Type Command::ParseType(const std::string& type_str) {
  if (type_str == "abort") {
    if (!abort_allowed_) {
      LOG(ERROR) << "ABORT disallowed";
      return Type::LAST;
    }
    return Type::ABORT;
  } else if (type_str == "bsdiff") {
    return Type::BSDIFF;
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
  } else {
    *err = "invalid op";
    return {};
  }

  return Command(op, index, line, patch_info, target_info, source_info, stash_info);
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
