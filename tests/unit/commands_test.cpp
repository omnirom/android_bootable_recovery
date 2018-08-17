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

#include <algorithm>
#include <string>

#include <android-base/strings.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>

#include "otautil/print_sha1.h"
#include "otautil/rangeset.h"
#include "private/commands.h"

TEST(CommandsTest, ParseType) {
  ASSERT_EQ(Command::Type::ZERO, Command::ParseType("zero"));
  ASSERT_EQ(Command::Type::NEW, Command::ParseType("new"));
  ASSERT_EQ(Command::Type::ERASE, Command::ParseType("erase"));
  ASSERT_EQ(Command::Type::MOVE, Command::ParseType("move"));
  ASSERT_EQ(Command::Type::BSDIFF, Command::ParseType("bsdiff"));
  ASSERT_EQ(Command::Type::IMGDIFF, Command::ParseType("imgdiff"));
  ASSERT_EQ(Command::Type::STASH, Command::ParseType("stash"));
  ASSERT_EQ(Command::Type::FREE, Command::ParseType("free"));
  ASSERT_EQ(Command::Type::COMPUTE_HASH_TREE, Command::ParseType("compute_hash_tree"));
}

TEST(CommandsTest, ParseType_InvalidCommand) {
  ASSERT_EQ(Command::Type::LAST, Command::ParseType("foo"));
  ASSERT_EQ(Command::Type::LAST, Command::ParseType("bar"));
}

TEST(CommandsTest, ParseTargetInfoAndSourceInfo_SourceBlocksOnly) {
  const std::vector<std::string> tokens{
    "4,569884,569904,591946,592043",
    "117",
    "4,566779,566799,591946,592043",
  };
  TargetInfo target;
  SourceInfo source;
  std::string err;
  ASSERT_TRUE(Command::ParseTargetInfoAndSourceInfo(
      tokens, "1d74d1a60332fd38cf9405f1bae67917888da6cb", &target,
      "1d74d1a60332fd38cf9405f1bae67917888da6cb", &source, &err));
  ASSERT_EQ(TargetInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                       RangeSet({ { 569884, 569904 }, { 591946, 592043 } })),
            target);
  ASSERT_EQ(SourceInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                       RangeSet({ { 566779, 566799 }, { 591946, 592043 } }), {}, {}),
            source);
  ASSERT_EQ(117, source.blocks());
}

TEST(CommandsTest, ParseTargetInfoAndSourceInfo_StashesOnly) {
  const std::vector<std::string> tokens{
    "2,350729,350731",
    "2",
    "-",
    "6ebcf8cf1f6be0bc49e7d4a864214251925d1d15:2,0,2",
  };
  TargetInfo target;
  SourceInfo source;
  std::string err;
  ASSERT_TRUE(Command::ParseTargetInfoAndSourceInfo(
      tokens, "6ebcf8cf1f6be0bc49e7d4a864214251925d1d15", &target,
      "1c25ba04d3278d6b65a1b9f17abac78425ec8b8d", &source, &err));
  ASSERT_EQ(
      TargetInfo("6ebcf8cf1f6be0bc49e7d4a864214251925d1d15", RangeSet({ { 350729, 350731 } })),
      target);
  ASSERT_EQ(
      SourceInfo("1c25ba04d3278d6b65a1b9f17abac78425ec8b8d", {}, {},
                 {
                     StashInfo("6ebcf8cf1f6be0bc49e7d4a864214251925d1d15", RangeSet({ { 0, 2 } })),
                 }),
      source);
  ASSERT_EQ(2, source.blocks());
}

TEST(CommandsTest, ParseTargetInfoAndSourceInfo_SourceBlocksAndStashes) {
  const std::vector<std::string> tokens{
    "4,611641,611643,636981,637075",
    "96",
    "4,636981,637075,770665,770666",
    "4,0,94,95,96",
    "9eedf00d11061549e32503cadf054ec6fbfa7a23:2,94,95",
  };
  TargetInfo target;
  SourceInfo source;
  std::string err;
  ASSERT_TRUE(Command::ParseTargetInfoAndSourceInfo(
      tokens, "4734d1b241eb3d0f993714aaf7d665fae43772b6", &target,
      "a6cbdf3f416960f02189d3a814ec7e9e95c44a0d", &source, &err));
  ASSERT_EQ(TargetInfo("4734d1b241eb3d0f993714aaf7d665fae43772b6",
                       RangeSet({ { 611641, 611643 }, { 636981, 637075 } })),
            target);
  ASSERT_EQ(SourceInfo(
                "a6cbdf3f416960f02189d3a814ec7e9e95c44a0d",
                RangeSet({ { 636981, 637075 }, { 770665, 770666 } }),  // source ranges
                RangeSet({ { 0, 94 }, { 95, 96 } }),                   // source location
                {
                    StashInfo("9eedf00d11061549e32503cadf054ec6fbfa7a23", RangeSet({ { 94, 95 } })),
                }),
            source);
  ASSERT_EQ(96, source.blocks());
}

TEST(CommandsTest, ParseTargetInfoAndSourceInfo_InvalidInput) {
  const std::vector<std::string> tokens{
    "4,611641,611643,636981,637075",
    "96",
    "4,636981,637075,770665,770666",
    "4,0,94,95,96",
    "9eedf00d11061549e32503cadf054ec6fbfa7a23:2,94,95",
  };
  TargetInfo target;
  SourceInfo source;
  std::string err;

  // Mismatching block count.
  {
    std::vector<std::string> tokens_copy(tokens);
    tokens_copy[1] = "97";
    ASSERT_FALSE(Command::ParseTargetInfoAndSourceInfo(
        tokens_copy, "1d74d1a60332fd38cf9405f1bae67917888da6cb", &target,
        "1d74d1a60332fd38cf9405f1bae67917888da6cb", &source, &err));
  }

  // Excess stashes (causing block count mismatch).
  {
    std::vector<std::string> tokens_copy(tokens);
    tokens_copy.push_back("e145a2f83a33334714ac65e34969c1f115e54a6f:2,0,22");
    ASSERT_FALSE(Command::ParseTargetInfoAndSourceInfo(
        tokens_copy, "1d74d1a60332fd38cf9405f1bae67917888da6cb", &target,
        "1d74d1a60332fd38cf9405f1bae67917888da6cb", &source, &err));
  }

  // Invalid args.
  for (size_t i = 0; i < tokens.size(); i++) {
    TargetInfo target;
    SourceInfo source;
    std::string err;
    ASSERT_FALSE(Command::ParseTargetInfoAndSourceInfo(
        std::vector<std::string>(tokens.cbegin() + i + 1, tokens.cend()),
        "1d74d1a60332fd38cf9405f1bae67917888da6cb", &target,
        "1d74d1a60332fd38cf9405f1bae67917888da6cb", &source, &err));
  }
}

TEST(CommandsTest, Parse_EmptyInput) {
  std::string err;
  ASSERT_FALSE(Command::Parse("", 0, &err));
  ASSERT_EQ("invalid type", err);
}

TEST(CommandsTest, Parse_ABORT_Allowed) {
  Command::abort_allowed_ = true;

  const std::string input{ "abort" };
  std::string err;
  Command command = Command::Parse(input, 0, &err);
  ASSERT_TRUE(command);

  ASSERT_EQ(TargetInfo(), command.target());
  ASSERT_EQ(SourceInfo(), command.source());
  ASSERT_EQ(StashInfo(), command.stash());
  ASSERT_EQ(PatchInfo(), command.patch());
}

TEST(CommandsTest, Parse_ABORT_NotAllowed) {
  const std::string input{ "abort" };
  std::string err;
  Command command = Command::Parse(input, 0, &err);
  ASSERT_FALSE(command);
}

TEST(CommandsTest, Parse_BSDIFF) {
  const std::string input{
    "bsdiff 0 148 "
    "f201a4e04bd3860da6ad47b957ef424d58a58f8c 9d5d223b4bc5c45dbd25a799c4f1a98466731599 "
    "4,565704,565752,566779,566799 "
    "68 4,64525,64545,565704,565752"
  };
  std::string err;
  Command command = Command::Parse(input, 1, &err);
  ASSERT_TRUE(command);

  ASSERT_EQ(Command::Type::BSDIFF, command.type());
  ASSERT_EQ(1, command.index());
  ASSERT_EQ(input, command.cmdline());

  ASSERT_EQ(TargetInfo("9d5d223b4bc5c45dbd25a799c4f1a98466731599",
                       RangeSet({ { 565704, 565752 }, { 566779, 566799 } })),
            command.target());
  ASSERT_EQ(SourceInfo("f201a4e04bd3860da6ad47b957ef424d58a58f8c",
                       RangeSet({ { 64525, 64545 }, { 565704, 565752 } }), RangeSet(), {}),
            command.source());
  ASSERT_EQ(StashInfo(), command.stash());
  ASSERT_EQ(PatchInfo(0, 148), command.patch());
}

TEST(CommandsTest, Parse_ERASE) {
  const std::string input{ "erase 2,5,10" };
  std::string err;
  Command command = Command::Parse(input, 2, &err);
  ASSERT_TRUE(command);

  ASSERT_EQ(Command::Type::ERASE, command.type());
  ASSERT_EQ(2, command.index());
  ASSERT_EQ(input, command.cmdline());

  ASSERT_EQ(TargetInfo("unknown-hash", RangeSet({ { 5, 10 } })), command.target());
  ASSERT_EQ(SourceInfo(), command.source());
  ASSERT_EQ(StashInfo(), command.stash());
  ASSERT_EQ(PatchInfo(), command.patch());
}

TEST(CommandsTest, Parse_FREE) {
  const std::string input{ "free hash1" };
  std::string err;
  Command command = Command::Parse(input, 3, &err);
  ASSERT_TRUE(command);

  ASSERT_EQ(Command::Type::FREE, command.type());
  ASSERT_EQ(3, command.index());
  ASSERT_EQ(input, command.cmdline());

  ASSERT_EQ(TargetInfo(), command.target());
  ASSERT_EQ(SourceInfo(), command.source());
  ASSERT_EQ(StashInfo("hash1", RangeSet()), command.stash());
  ASSERT_EQ(PatchInfo(), command.patch());
}

TEST(CommandsTest, Parse_IMGDIFF) {
  const std::string input{
    "imgdiff 29629269 185 "
    "a6b1c49aed1b57a2aab1ec3e1505b945540cd8db 51978f65035f584a8ef7afa941dacb6d5e862164 "
    "2,90851,90852 "
    "1 2,90851,90852"
  };
  std::string err;
  Command command = Command::Parse(input, 4, &err);
  ASSERT_TRUE(command);

  ASSERT_EQ(Command::Type::IMGDIFF, command.type());
  ASSERT_EQ(4, command.index());
  ASSERT_EQ(input, command.cmdline());

  ASSERT_EQ(TargetInfo("51978f65035f584a8ef7afa941dacb6d5e862164", RangeSet({ { 90851, 90852 } })),
            command.target());
  ASSERT_EQ(SourceInfo("a6b1c49aed1b57a2aab1ec3e1505b945540cd8db", RangeSet({ { 90851, 90852 } }),
                       RangeSet(), {}),
            command.source());
  ASSERT_EQ(StashInfo(), command.stash());
  ASSERT_EQ(PatchInfo(29629269, 185), command.patch());
}

TEST(CommandsTest, Parse_MOVE) {
  const std::string input{
    "move 1d74d1a60332fd38cf9405f1bae67917888da6cb "
    "4,569884,569904,591946,592043 117 4,566779,566799,591946,592043"
  };
  std::string err;
  Command command = Command::Parse(input, 5, &err);
  ASSERT_TRUE(command);

  ASSERT_EQ(Command::Type::MOVE, command.type());
  ASSERT_EQ(5, command.index());
  ASSERT_EQ(input, command.cmdline());

  ASSERT_EQ(TargetInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                       RangeSet({ { 569884, 569904 }, { 591946, 592043 } })),
            command.target());
  ASSERT_EQ(SourceInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                       RangeSet({ { 566779, 566799 }, { 591946, 592043 } }), RangeSet(), {}),
            command.source());
  ASSERT_EQ(StashInfo(), command.stash());
  ASSERT_EQ(PatchInfo(), command.patch());
}

TEST(CommandsTest, Parse_NEW) {
  const std::string input{ "new 4,3,5,10,12" };
  std::string err;
  Command command = Command::Parse(input, 6, &err);
  ASSERT_TRUE(command);

  ASSERT_EQ(Command::Type::NEW, command.type());
  ASSERT_EQ(6, command.index());
  ASSERT_EQ(input, command.cmdline());

  ASSERT_EQ(TargetInfo("unknown-hash", RangeSet({ { 3, 5 }, { 10, 12 } })), command.target());
  ASSERT_EQ(SourceInfo(), command.source());
  ASSERT_EQ(StashInfo(), command.stash());
  ASSERT_EQ(PatchInfo(), command.patch());
}

TEST(CommandsTest, Parse_STASH) {
  const std::string input{ "stash hash1 2,5,10" };
  std::string err;
  Command command = Command::Parse(input, 7, &err);
  ASSERT_TRUE(command);

  ASSERT_EQ(Command::Type::STASH, command.type());
  ASSERT_EQ(7, command.index());
  ASSERT_EQ(input, command.cmdline());

  ASSERT_EQ(TargetInfo(), command.target());
  ASSERT_EQ(SourceInfo(), command.source());
  ASSERT_EQ(StashInfo("hash1", RangeSet({ { 5, 10 } })), command.stash());
  ASSERT_EQ(PatchInfo(), command.patch());
}

TEST(CommandsTest, Parse_ZERO) {
  const std::string input{ "zero 2,1,5" };
  std::string err;
  Command command = Command::Parse(input, 8, &err);
  ASSERT_TRUE(command);

  ASSERT_EQ(Command::Type::ZERO, command.type());
  ASSERT_EQ(8, command.index());
  ASSERT_EQ(input, command.cmdline());

  ASSERT_EQ(TargetInfo("unknown-hash", RangeSet({ { 1, 5 } })), command.target());
  ASSERT_EQ(SourceInfo(), command.source());
  ASSERT_EQ(StashInfo(), command.stash());
  ASSERT_EQ(PatchInfo(), command.patch());
}

TEST(CommandsTest, Parse_COMPUTE_HASH_TREE) {
  const std::string input{ "compute_hash_tree 2,0,1 2,3,4 sha1 unknown-salt unknown-root-hash" };
  std::string err;
  Command command = Command::Parse(input, 9, &err);
  ASSERT_TRUE(command);

  ASSERT_EQ(Command::Type::COMPUTE_HASH_TREE, command.type());
  ASSERT_EQ(9, command.index());
  ASSERT_EQ(input, command.cmdline());

  HashTreeInfo expected_info(RangeSet({ { 0, 1 } }), RangeSet({ { 3, 4 } }), "sha1", "unknown-salt",
                             "unknown-root-hash");
  ASSERT_EQ(expected_info, command.hash_tree_info());
  ASSERT_EQ(TargetInfo(), command.target());
  ASSERT_EQ(SourceInfo(), command.source());
  ASSERT_EQ(StashInfo(), command.stash());
  ASSERT_EQ(PatchInfo(), command.patch());
}

TEST(CommandsTest, Parse_InvalidNumberOfArgs) {
  Command::abort_allowed_ = true;

  // Note that the case of having excess args in BSDIFF, IMGDIFF and MOVE is covered by
  // ParseTargetInfoAndSourceInfo_InvalidInput.
  std::vector<std::string> inputs{
    "abort foo",
    "bsdiff",
    "compute_hash_tree, 2,0,1 2,0,1 unknown-algorithm unknown-salt",
    "erase",
    "erase 4,3,5,10,12 hash1",
    "free",
    "free id1 id2",
    "imgdiff",
    "move",
    "new",
    "new 4,3,5,10,12 hash1",
    "stash",
    "stash id1",
    "stash id1 4,3,5,10,12 id2",
    "zero",
    "zero 4,3,5,10,12 hash2",
  };
  for (const auto& input : inputs) {
    std::string err;
    ASSERT_FALSE(Command::Parse(input, 0, &err));
  }
}

TEST(SourceInfoTest, Overlaps) {
  ASSERT_TRUE(SourceInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                         RangeSet({ { 7, 9 }, { 16, 20 } }), {}, {})
                  .Overlaps(TargetInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                                       RangeSet({ { 7, 9 }, { 16, 20 } }))));

  ASSERT_TRUE(SourceInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                         RangeSet({ { 7, 9 }, { 16, 20 } }), {}, {})
                  .Overlaps(TargetInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                                       RangeSet({ { 4, 7 }, { 16, 23 } }))));

  ASSERT_FALSE(SourceInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                          RangeSet({ { 7, 9 }, { 16, 20 } }), {}, {})
                   .Overlaps(TargetInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                                        RangeSet({ { 9, 16 } }))));
}

TEST(SourceInfoTest, Overlaps_EmptySourceOrTarget) {
  ASSERT_FALSE(SourceInfo().Overlaps(TargetInfo()));

  ASSERT_FALSE(SourceInfo().Overlaps(
      TargetInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb", RangeSet({ { 7, 9 }, { 16, 20 } }))));

  ASSERT_FALSE(SourceInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                          RangeSet({ { 7, 9 }, { 16, 20 } }), {}, {})
                   .Overlaps(TargetInfo()));
}

TEST(SourceInfoTest, Overlaps_WithStashes) {
  ASSERT_FALSE(SourceInfo("a6cbdf3f416960f02189d3a814ec7e9e95c44a0d",
                          RangeSet({ { 81, 175 }, { 265, 266 } }),  // source ranges
                          RangeSet({ { 0, 94 }, { 95, 96 } }),      // source location
                          { StashInfo("9eedf00d11061549e32503cadf054ec6fbfa7a23",
                                      RangeSet({ { 94, 95 } })) })
                   .Overlaps(TargetInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                                        RangeSet({ { 175, 265 } }))));

  ASSERT_TRUE(SourceInfo("a6cbdf3f416960f02189d3a814ec7e9e95c44a0d",
                         RangeSet({ { 81, 175 }, { 265, 266 } }),  // source ranges
                         RangeSet({ { 0, 94 }, { 95, 96 } }),      // source location
                         { StashInfo("9eedf00d11061549e32503cadf054ec6fbfa7a23",
                                     RangeSet({ { 94, 95 } })) })
                  .Overlaps(TargetInfo("1d74d1a60332fd38cf9405f1bae67917888da6cb",
                                       RangeSet({ { 265, 266 } }))));
}

// The block size should be specified by the caller of ReadAll (i.e. from Command instance during
// normal run).
constexpr size_t kBlockSize = 4096;

TEST(SourceInfoTest, ReadAll) {
  // "2727756cfee3fbfe24bf5650123fd7743d7b3465" is the SHA-1 hex digest of 8192 * 'a'.
  const SourceInfo source("2727756cfee3fbfe24bf5650123fd7743d7b3465", RangeSet({ { 0, 2 } }), {},
                          {});
  auto block_reader = [](const RangeSet& src, std::vector<uint8_t>* block_buffer) -> int {
    std::fill_n(block_buffer->begin(), src.blocks() * kBlockSize, 'a');
    return 0;
  };
  auto stash_reader = [](const std::string&, std::vector<uint8_t>*) -> int { return 0; };
  std::vector<uint8_t> buffer(source.blocks() * kBlockSize);
  ASSERT_TRUE(source.ReadAll(&buffer, kBlockSize, block_reader, stash_reader));
  ASSERT_EQ(source.blocks() * kBlockSize, buffer.size());

  uint8_t digest[SHA_DIGEST_LENGTH];
  SHA1(buffer.data(), buffer.size(), digest);
  ASSERT_EQ(source.hash(), print_sha1(digest));
}

TEST(SourceInfoTest, ReadAll_WithStashes) {
  const SourceInfo source(
      // SHA-1 hex digest of 8192 * 'a' + 4096 * 'b'.
      "ee3ebea26130769c10ad13604712100346d48660", RangeSet({ { 0, 2 } }), RangeSet({ { 0, 2 } }),
      { StashInfo("1e41f7a59e80c6eb4dc043caae80d273f130bed8", RangeSet({ { 2, 3 } })) });
  auto block_reader = [](const RangeSet& src, std::vector<uint8_t>* block_buffer) -> int {
    std::fill_n(block_buffer->begin(), src.blocks() * kBlockSize, 'a');
    return 0;
  };
  auto stash_reader = [](const std::string&, std::vector<uint8_t>* stash_buffer) -> int {
    std::fill_n(stash_buffer->begin(), kBlockSize, 'b');
    return 0;
  };
  std::vector<uint8_t> buffer(source.blocks() * kBlockSize);
  ASSERT_TRUE(source.ReadAll(&buffer, kBlockSize, block_reader, stash_reader));
  ASSERT_EQ(source.blocks() * kBlockSize, buffer.size());

  uint8_t digest[SHA_DIGEST_LENGTH];
  SHA1(buffer.data(), buffer.size(), digest);
  ASSERT_EQ(source.hash(), print_sha1(digest));
}

TEST(SourceInfoTest, ReadAll_BufferTooSmall) {
  const SourceInfo source("2727756cfee3fbfe24bf5650123fd7743d7b3465", RangeSet({ { 0, 2 } }), {},
                          {});
  auto block_reader = [](const RangeSet&, std::vector<uint8_t>*) -> int { return 0; };
  auto stash_reader = [](const std::string&, std::vector<uint8_t>*) -> int { return 0; };
  std::vector<uint8_t> buffer(source.blocks() * kBlockSize - 1);
  ASSERT_FALSE(source.ReadAll(&buffer, kBlockSize, block_reader, stash_reader));
}

TEST(SourceInfoTest, ReadAll_FailingReader) {
  const SourceInfo source(
      "ee3ebea26130769c10ad13604712100346d48660", RangeSet({ { 0, 2 } }), RangeSet({ { 0, 2 } }),
      { StashInfo("1e41f7a59e80c6eb4dc043caae80d273f130bed8", RangeSet({ { 2, 3 } })) });
  std::vector<uint8_t> buffer(source.blocks() * kBlockSize);
  auto failing_block_reader = [](const RangeSet&, std::vector<uint8_t>*) -> int { return -1; };
  auto stash_reader = [](const std::string&, std::vector<uint8_t>*) -> int { return 0; };
  ASSERT_FALSE(source.ReadAll(&buffer, kBlockSize, failing_block_reader, stash_reader));

  auto block_reader = [](const RangeSet&, std::vector<uint8_t>*) -> int { return 0; };
  auto failing_stash_reader = [](const std::string&, std::vector<uint8_t>*) -> int { return -1; };
  ASSERT_FALSE(source.ReadAll(&buffer, kBlockSize, block_reader, failing_stash_reader));
}

TEST(TransferListTest, Parse) {
  std::vector<std::string> input_lines{
    "4",  // version
    "2",  // total blocks
    "1",  // max stashed entries
    "1",  // max stashed blocks
    "stash 1d74d1a60332fd38cf9405f1bae67917888da6cb 2,0,1",
    "move 1d74d1a60332fd38cf9405f1bae67917888da6cb 2,0,1 1 2,0,1",
  };

  std::string err;
  TransferList transfer_list = TransferList::Parse(android::base::Join(input_lines, '\n'), &err);
  ASSERT_TRUE(static_cast<bool>(transfer_list));
  ASSERT_EQ(4, transfer_list.version());
  ASSERT_EQ(2, transfer_list.total_blocks());
  ASSERT_EQ(1, transfer_list.stash_max_entries());
  ASSERT_EQ(1, transfer_list.stash_max_blocks());
  ASSERT_EQ(2U, transfer_list.commands().size());
  ASSERT_EQ(Command::Type::STASH, transfer_list.commands()[0].type());
  ASSERT_EQ(Command::Type::MOVE, transfer_list.commands()[1].type());
}

TEST(TransferListTest, Parse_InvalidCommand) {
  std::vector<std::string> input_lines{
    "4",  // version
    "2",  // total blocks
    "1",  // max stashed entries
    "1",  // max stashed blocks
    "stash 1d74d1a60332fd38cf9405f1bae67917888da6cb 2,0,1",
    "move 1d74d1a60332fd38cf9405f1bae67917888da6cb 2,0,1 1",
  };

  std::string err;
  TransferList transfer_list = TransferList::Parse(android::base::Join(input_lines, '\n'), &err);
  ASSERT_FALSE(static_cast<bool>(transfer_list));
}

TEST(TransferListTest, Parse_ZeroTotalBlocks) {
  std::vector<std::string> input_lines{
    "4",  // version
    "0",  // total blocks
    "0",  // max stashed entries
    "0",  // max stashed blocks
  };

  std::string err;
  TransferList transfer_list = TransferList::Parse(android::base::Join(input_lines, '\n'), &err);
  ASSERT_TRUE(static_cast<bool>(transfer_list));
  ASSERT_EQ(4, transfer_list.version());
  ASSERT_EQ(0, transfer_list.total_blocks());
  ASSERT_EQ(0, transfer_list.stash_max_entries());
  ASSERT_EQ(0, transfer_list.stash_max_blocks());
  ASSERT_TRUE(transfer_list.commands().empty());
}
