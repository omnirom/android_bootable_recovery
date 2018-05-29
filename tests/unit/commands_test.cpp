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

#include <string>

#include <gtest/gtest.h>

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
}

TEST(CommandsTest, ParseType_InvalidCommand) {
  ASSERT_EQ(Command::Type::LAST, Command::ParseType("foo"));
  ASSERT_EQ(Command::Type::LAST, Command::ParseType("bar"));
}
