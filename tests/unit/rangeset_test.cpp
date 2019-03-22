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

#include <signal.h>
#include <sys/types.h>

#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "otautil/rangeset.h"

TEST(RangeSetTest, ctor) {
  RangeSet rs(std::vector<Range>{ Range{ 8, 10 }, Range{ 1, 5 } });
  ASSERT_TRUE(rs);

  RangeSet rs2(std::vector<Range>{});
  ASSERT_FALSE(rs2);

  RangeSet rs3(std::vector<Range>{ Range{ 8, 10 }, Range{ 5, 1 } });
  ASSERT_FALSE(rs3);
}

TEST(RangeSetTest, Parse_smoke) {
  RangeSet rs = RangeSet::Parse("2,1,10");
  ASSERT_EQ(static_cast<size_t>(1), rs.size());
  ASSERT_EQ((Range{ 1, 10 }), rs[0]);
  ASSERT_EQ(static_cast<size_t>(9), rs.blocks());

  RangeSet rs2 = RangeSet::Parse("4,15,20,1,10");
  ASSERT_EQ(static_cast<size_t>(2), rs2.size());
  ASSERT_EQ((Range{ 15, 20 }), rs2[0]);
  ASSERT_EQ((Range{ 1, 10 }), rs2[1]);
  ASSERT_EQ(static_cast<size_t>(14), rs2.blocks());

  // Leading zeros are fine. But android::base::ParseUint() doesn't like trailing zeros like "10 ".
  ASSERT_EQ(rs, RangeSet::Parse(" 2, 1,   10"));
  ASSERT_FALSE(RangeSet::Parse("2,1,10 "));
}

TEST(RangeSetTest, Parse_InvalidCases) {
  // Insufficient number of tokens.
  ASSERT_FALSE(RangeSet::Parse(""));
  ASSERT_FALSE(RangeSet::Parse("2,1"));

  // The first token (i.e. the number of following tokens) is invalid.
  ASSERT_FALSE(RangeSet::Parse("a,1,1"));
  ASSERT_FALSE(RangeSet::Parse("3,1,1"));
  ASSERT_FALSE(RangeSet::Parse("-3,1,1"));
  ASSERT_FALSE(RangeSet::Parse("2,1,2,3"));

  // Invalid tokens.
  ASSERT_FALSE(RangeSet::Parse("2,1,10a"));
  ASSERT_FALSE(RangeSet::Parse("2,,10"));

  // Empty or negative range.
  ASSERT_FALSE(RangeSet::Parse("2,2,2"));
  ASSERT_FALSE(RangeSet::Parse("2,2,1"));
}

TEST(RangeSetTest, Clear) {
  RangeSet rs = RangeSet::Parse("2,1,6");
  ASSERT_TRUE(rs);
  rs.Clear();
  ASSERT_FALSE(rs);

  // No-op to clear an empty RangeSet.
  rs.Clear();
  ASSERT_FALSE(rs);
}

TEST(RangeSetTest, PushBack) {
  RangeSet rs;
  ASSERT_FALSE(rs);

  ASSERT_TRUE(rs.PushBack({ 3, 5 }));
  ASSERT_EQ(RangeSet::Parse("2,3,5"), rs);

  ASSERT_TRUE(rs.PushBack({ 5, 15 }));
  ASSERT_EQ(RangeSet::Parse("4,3,5,5,15"), rs);
  ASSERT_EQ(static_cast<size_t>(2), rs.size());
  ASSERT_EQ(static_cast<size_t>(12), rs.blocks());
}

TEST(RangeSetTest, PushBack_InvalidInput) {
  RangeSet rs;
  ASSERT_FALSE(rs);
  ASSERT_FALSE(rs.PushBack({ 5, 3 }));
  ASSERT_FALSE(rs);
  ASSERT_FALSE(rs.PushBack({ 15, 15 }));
  ASSERT_FALSE(rs);

  ASSERT_TRUE(rs.PushBack({ 5, 15 }));
  ASSERT_FALSE(rs.PushBack({ 5, std::numeric_limits<size_t>::max() - 2 }));
  ASSERT_EQ(RangeSet::Parse("2,5,15"), rs);
}

TEST(RangeSetTest, Overlaps) {
  RangeSet r1 = RangeSet::Parse("2,1,6");
  RangeSet r2 = RangeSet::Parse("2,5,10");
  ASSERT_TRUE(r1.Overlaps(r2));
  ASSERT_TRUE(r2.Overlaps(r1));

  r2 = RangeSet::Parse("2,6,10");
  ASSERT_FALSE(r1.Overlaps(r2));
  ASSERT_FALSE(r2.Overlaps(r1));

  ASSERT_FALSE(RangeSet::Parse("2,3,5").Overlaps(RangeSet::Parse("2,5,7")));
  ASSERT_FALSE(RangeSet::Parse("2,5,7").Overlaps(RangeSet::Parse("2,3,5")));
}

TEST(RangeSetTest, Split) {
  RangeSet rs1 = RangeSet::Parse("2,1,2");
  ASSERT_TRUE(rs1);
  ASSERT_EQ((std::vector<RangeSet>{ RangeSet::Parse("2,1,2") }), rs1.Split(1));

  RangeSet rs2 = RangeSet::Parse("2,5,10");
  ASSERT_TRUE(rs2);
  ASSERT_EQ((std::vector<RangeSet>{ RangeSet::Parse("2,5,8"), RangeSet::Parse("2,8,10") }),
            rs2.Split(2));

  RangeSet rs3 = RangeSet::Parse("4,0,1,5,10");
  ASSERT_TRUE(rs3);
  ASSERT_EQ((std::vector<RangeSet>{ RangeSet::Parse("4,0,1,5,7"), RangeSet::Parse("2,7,10") }),
            rs3.Split(2));

  RangeSet rs4 = RangeSet::Parse("6,1,3,3,4,4,5");
  ASSERT_TRUE(rs4);
  ASSERT_EQ((std::vector<RangeSet>{ RangeSet::Parse("2,1,3"), RangeSet::Parse("2,3,4"),
                                    RangeSet::Parse("2,4,5") }),
            rs4.Split(3));

  RangeSet rs5 = RangeSet::Parse("2,0,10");
  ASSERT_TRUE(rs5);
  ASSERT_EQ((std::vector<RangeSet>{ RangeSet::Parse("2,0,3"), RangeSet::Parse("2,3,6"),
                                    RangeSet::Parse("2,6,8"), RangeSet::Parse("2,8,10") }),
            rs5.Split(4));

  RangeSet rs6 = RangeSet::Parse(
      "20,0,268,269,271,286,447,8350,32770,33022,98306,98558,163842,164094,196609,204800,229378,"
      "229630,294914,295166,457564");
  ASSERT_TRUE(rs6);
  size_t rs6_blocks = rs6.blocks();
  auto splits = rs6.Split(4);
  ASSERT_EQ(
      (std::vector<RangeSet>{
          RangeSet::Parse("12,0,268,269,271,286,447,8350,32770,33022,98306,98558,118472"),
          RangeSet::Parse("8,118472,163842,164094,196609,204800,229378,229630,237216"),
          RangeSet::Parse("4,237216,294914,295166,347516"), RangeSet::Parse("2,347516,457564") }),
      splits);
  size_t sum = 0;
  for (const auto& element : splits) {
    sum += element.blocks();
  }
  ASSERT_EQ(rs6_blocks, sum);
}

TEST(RangeSetTest, Split_EdgeCases) {
  // Empty RangeSet.
  RangeSet rs1;
  ASSERT_FALSE(rs1);
  ASSERT_EQ((std::vector<RangeSet>{}), rs1.Split(2));
  ASSERT_FALSE(rs1);

  // Zero group.
  RangeSet rs2 = RangeSet::Parse("2,1,5");
  ASSERT_TRUE(rs2);
  ASSERT_EQ((std::vector<RangeSet>{}), rs2.Split(0));

  // The number of blocks equals to the number of groups.
  RangeSet rs3 = RangeSet::Parse("2,1,5");
  ASSERT_TRUE(rs3);
  ASSERT_EQ((std::vector<RangeSet>{ RangeSet::Parse("2,1,2"), RangeSet::Parse("2,2,3"),
                                    RangeSet::Parse("2,3,4"), RangeSet::Parse("2,4,5") }),
            rs3.Split(4));

  // Less blocks than the number of groups.
  RangeSet rs4 = RangeSet::Parse("2,1,5");
  ASSERT_TRUE(rs4);
  ASSERT_EQ((std::vector<RangeSet>{ RangeSet::Parse("2,1,2"), RangeSet::Parse("2,2,3"),
                                    RangeSet::Parse("2,3,4"), RangeSet::Parse("2,4,5") }),
            rs4.Split(8));

  // Less blocks than the number of groups.
  RangeSet rs5 = RangeSet::Parse("2,0,3");
  ASSERT_TRUE(rs5);
  ASSERT_EQ((std::vector<RangeSet>{ RangeSet::Parse("2,0,1"), RangeSet::Parse("2,1,2"),
                                    RangeSet::Parse("2,2,3") }),
            rs5.Split(4));
}

TEST(RangeSetTest, GetBlockNumber) {
  RangeSet rs = RangeSet::Parse("2,1,10");
  ASSERT_EQ(static_cast<size_t>(1), rs.GetBlockNumber(0));
  ASSERT_EQ(static_cast<size_t>(6), rs.GetBlockNumber(5));
  ASSERT_EQ(static_cast<size_t>(9), rs.GetBlockNumber(8));

  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  // Out of bound.
  ASSERT_EXIT(rs.GetBlockNumber(9), ::testing::KilledBySignal(SIGABRT), "");
}

TEST(RangeSetTest, equality) {
  ASSERT_EQ(RangeSet::Parse("2,1,6"), RangeSet::Parse("2,1,6"));

  ASSERT_NE(RangeSet::Parse("2,1,6"), RangeSet::Parse("2,1,7"));
  ASSERT_NE(RangeSet::Parse("2,1,6"), RangeSet::Parse("2,2,7"));

  // The orders of Range's matter, e.g. "4,1,5,8,10" != "4,8,10,1,5".
  ASSERT_NE(RangeSet::Parse("4,1,5,8,10"), RangeSet::Parse("4,8,10,1,5"));
}

TEST(RangeSetTest, iterators) {
  RangeSet rs = RangeSet::Parse("4,1,5,8,10");
  std::vector<Range> ranges;
  for (const auto& range : rs) {
    ranges.push_back(range);
  }
  ASSERT_EQ((std::vector<Range>{ Range{ 1, 5 }, Range{ 8, 10 } }), ranges);

  ranges.clear();

  // Reverse iterators.
  for (auto it = rs.crbegin(); it != rs.crend(); it++) {
    ranges.push_back(*it);
  }
  ASSERT_EQ((std::vector<Range>{ Range{ 8, 10 }, Range{ 1, 5 } }), ranges);
}

TEST(RangeSetTest, ToString) {
  ASSERT_EQ("", RangeSet::Parse("").ToString());
  ASSERT_EQ("2,1,6", RangeSet::Parse("2,1,6").ToString());
  ASSERT_EQ("4,1,5,8,10", RangeSet::Parse("4,1,5,8,10").ToString());
  ASSERT_EQ("6,1,3,4,6,15,22", RangeSet::Parse("6,1,3,4,6,15,22").ToString());
}

TEST(RangeSetTest, GetSubRanges_invalid) {
  RangeSet range0({ { 1, 11 }, { 20, 30 } });
  ASSERT_FALSE(range0.GetSubRanges(0, 21));  // too many blocks
  ASSERT_FALSE(range0.GetSubRanges(21, 1));  // start block OOB
}

TEST(RangeSetTest, GetSubRanges_empty) {
  RangeSet range0({ { 1, 11 }, { 20, 30 } });
  ASSERT_EQ(RangeSet{}, range0.GetSubRanges(1, 0));  // empty num_of_blocks
}

TEST(RangeSetTest, GetSubRanges_smoke) {
  RangeSet range0({ { 10, 11 } });
  ASSERT_EQ(RangeSet({ { 10, 11 } }), range0.GetSubRanges(0, 1));

  RangeSet range1({ { 10, 11 }, { 20, 21 }, { 30, 31 } });
  ASSERT_EQ(range1, range1.GetSubRanges(0, 3));
  ASSERT_EQ(RangeSet({ { 20, 21 } }), range1.GetSubRanges(1, 1));

  RangeSet range2({ { 1, 11 }, { 20, 25 }, { 30, 35 } });
  ASSERT_EQ(RangeSet({ { 10, 11 }, { 20, 25 }, { 30, 31 } }), range2.GetSubRanges(9, 7));
}

TEST(SortedRangeSetTest, Insert) {
  SortedRangeSet rs({ { 2, 3 }, { 4, 6 }, { 8, 14 } });
  rs.Insert({ 1, 2 });
  ASSERT_EQ(SortedRangeSet({ { 1, 3 }, { 4, 6 }, { 8, 14 } }), rs);
  ASSERT_EQ(static_cast<size_t>(10), rs.blocks());
  rs.Insert({ 3, 5 });
  ASSERT_EQ(SortedRangeSet({ { 1, 6 }, { 8, 14 } }), rs);
  ASSERT_EQ(static_cast<size_t>(11), rs.blocks());

  SortedRangeSet r1({ { 20, 22 }, { 15, 18 } });
  rs.Insert(r1);
  ASSERT_EQ(SortedRangeSet({ { 1, 6 }, { 8, 14 }, { 15, 18 }, { 20, 22 } }), rs);
  ASSERT_EQ(static_cast<size_t>(16), rs.blocks());

  SortedRangeSet r2({ { 2, 7 }, { 15, 21 }, { 20, 25 } });
  rs.Insert(r2);
  ASSERT_EQ(SortedRangeSet({ { 1, 7 }, { 8, 14 }, { 15, 25 } }), rs);
  ASSERT_EQ(static_cast<size_t>(22), rs.blocks());
}

TEST(SortedRangeSetTest, file_range) {
  SortedRangeSet rs;
  rs.Insert(4096, 4096);
  ASSERT_EQ(SortedRangeSet({ { 1, 2 } }), rs);
  // insert block 2-9
  rs.Insert(4096 * 3 - 1, 4096 * 7);
  ASSERT_EQ(SortedRangeSet({ { 1, 10 } }), rs);
  // insert block 15-19
  rs.Insert(4096 * 15 + 1, 4096 * 4);
  ASSERT_EQ(SortedRangeSet({ { 1, 10 }, { 15, 20 } }), rs);

  // rs overlaps block 2-2
  ASSERT_TRUE(rs.Overlaps(4096 * 2 - 1, 10));
  ASSERT_FALSE(rs.Overlaps(4096 * 10, 4096 * 5));

  ASSERT_EQ(static_cast<size_t>(10), rs.GetOffsetInRangeSet(4106));
  ASSERT_EQ(static_cast<size_t>(40970), rs.GetOffsetInRangeSet(4096 * 16 + 10));

  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  // block#10 not in range.
  ASSERT_EXIT(rs.GetOffsetInRangeSet(40970), ::testing::KilledBySignal(SIGABRT), "");
}
