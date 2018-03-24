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

#include "screen_ui.h"

#include <string>

#include <gtest/gtest.h>

constexpr const char* HEADER[] = { "header", nullptr };
constexpr const char* ITEMS[] = { "items1", "items2", "items3", "items4", "1234567890", nullptr };

TEST(ScreenUITest, StartPhoneMenuSmoke) {
  Menu menu(false, 10, 20);
  ASSERT_FALSE(menu.scrollable());

  menu.Start(HEADER, ITEMS, 0);
  ASSERT_EQ(HEADER[0], menu.text_headers()[0]);
  ASSERT_EQ(5u, menu.ItemsCount());

  std::string message;
  ASSERT_FALSE(menu.ItemsOverflow(&message));
  for (size_t i = 0; i < menu.ItemsCount(); i++) {
    ASSERT_EQ(ITEMS[i], menu.TextItem(i));
  }

  ASSERT_EQ(0, menu.selection());
}

TEST(ScreenUITest, StartWearMenuSmoke) {
  Menu menu(true, 10, 8);
  ASSERT_TRUE(menu.scrollable());

  menu.Start(HEADER, ITEMS, 1);
  ASSERT_EQ(HEADER[0], menu.text_headers()[0]);
  ASSERT_EQ(5u, menu.ItemsCount());

  std::string message;
  ASSERT_FALSE(menu.ItemsOverflow(&message));
  for (size_t i = 0; i < menu.ItemsCount() - 1; i++) {
    ASSERT_EQ(ITEMS[i], menu.TextItem(i));
  }
  // Test of the last item is truncated
  ASSERT_EQ("12345678", menu.TextItem(4));
  ASSERT_EQ(1, menu.selection());
}

TEST(ScreenUITest, StartPhoneMenuItemsOverflow) {
  Menu menu(false, 1, 20);
  ASSERT_FALSE(menu.scrollable());

  menu.Start(HEADER, ITEMS, 0);
  ASSERT_EQ(1u, menu.ItemsCount());

  std::string message;
  ASSERT_FALSE(menu.ItemsOverflow(&message));
  for (size_t i = 0; i < menu.ItemsCount(); i++) {
    ASSERT_EQ(ITEMS[i], menu.TextItem(i));
  }

  ASSERT_EQ(0u, menu.MenuStart());
  ASSERT_EQ(1u, menu.MenuEnd());
}

TEST(ScreenUITest, StartWearMenuItemsOverflow) {
  Menu menu(true, 1, 20);
  ASSERT_TRUE(menu.scrollable());

  menu.Start(HEADER, ITEMS, 0);
  ASSERT_EQ(5u, menu.ItemsCount());

  std::string message;
  ASSERT_TRUE(menu.ItemsOverflow(&message));
  ASSERT_EQ("Current item: 1/5", message);

  for (size_t i = 0; i < menu.ItemsCount(); i++) {
    ASSERT_EQ(ITEMS[i], menu.TextItem(i));
  }

  ASSERT_EQ(0u, menu.MenuStart());
  ASSERT_EQ(1u, menu.MenuEnd());
}

TEST(ScreenUITest, PhoneMenuSelectSmoke) {
  Menu menu(false, 10, 20);

  int sel = 0;
  menu.Start(HEADER, ITEMS, sel);
  // Mimic down button 10 times (2 * items size)
  for (int i = 0; i < 10; i++) {
    sel = menu.Select(++sel);
    ASSERT_EQ(sel, menu.selection());

    // Wraps the selection for unscrollable menu when it reaches the boundary.
    int expected = (i + 1) % 5;
    ASSERT_EQ(expected, menu.selection());

    ASSERT_EQ(0u, menu.MenuStart());
    ASSERT_EQ(5u, menu.MenuEnd());
  }

  // Mimic up button 10 times
  for (int i = 0; i < 10; i++) {
    sel = menu.Select(--sel);
    ASSERT_EQ(sel, menu.selection());

    int expected = (9 - i) % 5;
    ASSERT_EQ(expected, menu.selection());

    ASSERT_EQ(0u, menu.MenuStart());
    ASSERT_EQ(5u, menu.MenuEnd());
  }
}

TEST(ScreenUITest, WearMenuSelectSmoke) {
  Menu menu(true, 10, 20);

  int sel = 0;
  menu.Start(HEADER, ITEMS, sel);
  // Mimic pressing down button 10 times (2 * items size)
  for (int i = 0; i < 10; i++) {
    sel = menu.Select(++sel);
    ASSERT_EQ(sel, menu.selection());

    // Stops the selection at the boundary if the menu is scrollable.
    int expected = std::min(i + 1, 4);
    ASSERT_EQ(expected, menu.selection());

    ASSERT_EQ(0u, menu.MenuStart());
    ASSERT_EQ(5u, menu.MenuEnd());
  }

  // Mimic pressing up button 10 times
  for (int i = 0; i < 10; i++) {
    sel = menu.Select(--sel);
    ASSERT_EQ(sel, menu.selection());

    int expected = std::max(3 - i, 0);
    ASSERT_EQ(expected, menu.selection());

    ASSERT_EQ(0u, menu.MenuStart());
    ASSERT_EQ(5u, menu.MenuEnd());
  }
}

TEST(ScreenUITest, WearMenuSelectItemsOverflow) {
  Menu menu(true, 3, 20);

  int sel = 1;
  menu.Start(HEADER, ITEMS, sel);
  ASSERT_EQ(5u, menu.ItemsCount());

  // Scroll the menu to the end, and check the start & end of menu.
  for (int i = 0; i < 3; i++) {
    sel = menu.Select(++sel);
    ASSERT_EQ(i + 2, sel);
    ASSERT_EQ(static_cast<size_t>(i), menu.MenuStart());
    ASSERT_EQ(static_cast<size_t>(i + 3), menu.MenuEnd());
  }

  // Press down button one more time won't change the MenuStart() and MenuEnd().
  sel = menu.Select(++sel);
  ASSERT_EQ(4, sel);
  ASSERT_EQ(2u, menu.MenuStart());
  ASSERT_EQ(5u, menu.MenuEnd());

  // Scroll the menu to the top.
  // The expected menu sel, start & ends are:
  // sel 3, start 2, end 5
  // sel 2, start 2, end 5
  // sel 1, start 1, end 4
  // sel 0, start 0, end 3
  for (int i = 0; i < 4; i++) {
    sel = menu.Select(--sel);
    ASSERT_EQ(3 - i, sel);
    ASSERT_EQ(static_cast<size_t>(std::min(3 - i, 2)), menu.MenuStart());
    ASSERT_EQ(static_cast<size_t>(std::min(6 - i, 5)), menu.MenuEnd());
  }

  // Press up button one more time won't change the MenuStart() and MenuEnd().
  sel = menu.Select(--sel);
  ASSERT_EQ(0, sel);
  ASSERT_EQ(0u, menu.MenuStart());
  ASSERT_EQ(3u, menu.MenuEnd());
}
