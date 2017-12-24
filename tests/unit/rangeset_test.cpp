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

#include <vector>

#include <gtest/gtest.h>

#include "updater/rangeset.h"

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
  ASSERT_EXIT(RangeSet::Parse("2,1,10 "), ::testing::KilledBySignal(SIGABRT), "");
}

TEST(RangeSetTest, Parse_InvalidCases) {
  // Insufficient number of tokens.
  ASSERT_EXIT(RangeSet::Parse(""), ::testing::KilledBySignal(SIGABRT), "");
  ASSERT_EXIT(RangeSet::Parse("2,1"), ::testing::KilledBySignal(SIGABRT), "");

  // The first token (i.e. the number of following tokens) is invalid.
  ASSERT_EXIT(RangeSet::Parse("a,1,1"), ::testing::KilledBySignal(SIGABRT), "");
  ASSERT_EXIT(RangeSet::Parse("3,1,1"), ::testing::KilledBySignal(SIGABRT), "");
  ASSERT_EXIT(RangeSet::Parse("-3,1,1"), ::testing::KilledBySignal(SIGABRT), "");
  ASSERT_EXIT(RangeSet::Parse("2,1,2,3"), ::testing::KilledBySignal(SIGABRT), "");

  // Invalid tokens.
  ASSERT_EXIT(RangeSet::Parse("2,1,10a"), ::testing::KilledBySignal(SIGABRT), "");
  ASSERT_EXIT(RangeSet::Parse("2,,10"), ::testing::KilledBySignal(SIGABRT), "");

  // Empty or negative range.
  ASSERT_EXIT(RangeSet::Parse("2,2,2"), ::testing::KilledBySignal(SIGABRT), "");
  ASSERT_EXIT(RangeSet::Parse("2,2,1"), ::testing::KilledBySignal(SIGABRT), "");
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

TEST(RangeSetTest, GetBlockNumber) {
  RangeSet rs = RangeSet::Parse("2,1,10");
  ASSERT_EQ(static_cast<size_t>(1), rs.GetBlockNumber(0));
  ASSERT_EQ(static_cast<size_t>(6), rs.GetBlockNumber(5));
  ASSERT_EQ(static_cast<size_t>(9), rs.GetBlockNumber(8));

  // Out of bound.
  ASSERT_EXIT(rs.GetBlockNumber(9), ::testing::KilledBySignal(SIGABRT), "");
}

TEST(RangeSetTest, equality) {
  ASSERT_EQ(RangeSet::Parse("2,1,6"), RangeSet::Parse("2,1,6"));

  ASSERT_NE(RangeSet::Parse("2,1,6"), RangeSet::Parse("2,1,7"));
  ASSERT_NE(RangeSet::Parse("2,1,6"), RangeSet::Parse("2,2,7"));

  // The orders of Range's matter. "4,1,5,8,10" != "4,8,10,1,5".
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
