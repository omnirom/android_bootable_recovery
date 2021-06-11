/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "wear_ui.h"

#include <pthread.h>
#include <stdio.h>  // TODO: Remove after killing the call to sprintf().
#include <string.h>

#include <string>

#include <android-base/properties.h>
#include <android-base/strings.h>
#include <minui/minui.h>

WearRecoveryUI::WearRecoveryUI()
    : kProgressBarBaseline(RECOVERY_UI_PROGRESS_BAR_BASELINE),
      kMenuUnusableRows(RECOVERY_UI_MENU_UNUSABLE_ROWS) {
  // TODO: kMenuUnusableRows should be computed based on the lines in draw_screen_locked().

  // TODO: The following three variables are likely not needed. The first two are detected
  // automatically in ScreenRecoveryUI::LoadAnimation(), based on the actual files seen on device.
  intro_frames = 22;
  loop_frames = 60;

  touch_screen_allowed_ = true;
}

int WearRecoveryUI::GetProgressBaseline() const {
  return kProgressBarBaseline;
}

// Draw background frame on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
// TODO merge drawing routines with screen_ui
void WearRecoveryUI::draw_background_locked() {
  pagesIdentical = false;
  gr_color(0, 0, 0, 255);
  gr_fill(0, 0, gr_fb_width(), gr_fb_height());

  if (currentIcon != NONE) {
    GRSurface* frame = GetCurrentFrame();
    int frame_width = gr_get_width(frame);
    int frame_height = gr_get_height(frame);
    int frame_x = (gr_fb_width() - frame_width) / 2;
    int frame_y = (gr_fb_height() - frame_height) / 2;
    gr_blit(frame, 0, 0, frame_width, frame_height, frame_x, frame_y);
  }
}

static const char* SWIPE_HELP[] = {
  "Swipe up/down to move.",
  "Swipe left/right to select.",
  "",
  NULL
};

// TODO merge drawing routines with screen_ui
void WearRecoveryUI::draw_screen_locked() {
  char cur_selection_str[50];

  draw_background_locked();
  if (!show_text) {
    draw_foreground_locked();
  } else {
    SetColor(TEXT_FILL);
    gr_fill(0, 0, gr_fb_width(), gr_fb_height());

    int y = kMarginHeight;
    int x = kMarginWidth;
    if (show_menu) {
      std::string recovery_fingerprint =
          android::base::GetProperty("ro.bootimage.build.fingerprint", "");
      SetColor(HEADER);
      y += DrawTextLine(x + 4, y, "Android Recovery", true);
      for (auto& chunk : android::base::Split(recovery_fingerprint, ":")) {
        y += DrawTextLine(x + 4, y, chunk.c_str(), false);
      }

      // This is actually the help strings.
      y += DrawTextLines(x + 4, y, SWIPE_HELP);
      SetColor(HEADER);
      y += DrawTextLines(x + 4, y, menu_headers_);

      // Show the current menu item number in relation to total number if
      // items don't fit on the screen.
      if (menu_items > menu_end - menu_start) {
        sprintf(cur_selection_str, "Current item: %d/%d", menu_sel + 1, menu_items);
        gr_text(gr_sys_font(), x + 4, y, cur_selection_str, 1);
        y += char_height_ + 4;
      }

      // Menu begins here
      SetColor(MENU);

      for (int i = menu_start; i < menu_end; ++i) {
        if (i == menu_sel) {
          // draw the highlight bar
          SetColor(MENU_SEL_BG);
          gr_fill(x, y - 2, gr_fb_width() - x, y + char_height_ + 2);
          // white text of selected item
          SetColor(MENU_SEL_FG);
          if (menu_[i][0]) {
            gr_text(gr_sys_font(), x + 4, y, menu_[i].c_str(), 1);
          }
          SetColor(MENU);
        } else if (menu_[i][0]) {
          gr_text(gr_sys_font(), x + 4, y, menu_[i].c_str(), 0);
        }
        y += char_height_ + 4;
      }
      SetColor(MENU);
      y += 4;
      gr_fill(0, y, gr_fb_width(), y + 2);
      y += 4;
    }

    SetColor(LOG);

    // display from the bottom up, until we hit the top of the
    // screen, the bottom of the menu, or we've displayed the
    // entire text buffer.
    int row = text_row_;
    size_t count = 0;
    for (int ty = gr_fb_height() - char_height_ - kMarginHeight; ty > y + 2 && count < text_rows_;
         ty -= char_height_, ++count) {
      gr_text(gr_sys_font(), x + 4, ty, text_[row], 0);
      --row;
      if (row < 0) row = text_rows_ - 1;
    }
  }
}

// TODO merge drawing routines with screen_ui
void WearRecoveryUI::update_progress_locked() {
  draw_screen_locked();
  gr_flip();
}

void WearRecoveryUI::SetStage(int /* current */, int /* max */) {}

void WearRecoveryUI::StartMenu(const char* const* headers, const char* const* items,
                               int initial_selection) {
  pthread_mutex_lock(&updateMutex);
  if (text_rows_ > 0 && text_cols_ > 0) {
    menu_headers_ = headers;
    menu_.clear();
    // "i < text_rows_" is removed from the loop termination condition,
    // which is different from the one in ScreenRecoveryUI::StartMenu().
    // Because WearRecoveryUI supports scrollable menu, it's fine to have
    // more entries than text_rows_. The menu may be truncated otherwise.
    // Bug: 23752519
    for (size_t i = 0; items[i] != nullptr; i++) {
      menu_.emplace_back(std::string(items[i], strnlen(items[i], text_cols_ - 1)));
    }
    menu_items = static_cast<int>(menu_.size());
    show_menu = true;
    menu_sel = initial_selection;
    menu_start = 0;
    menu_end = text_rows_ - 1 - kMenuUnusableRows;
    if (menu_items <= menu_end) menu_end = menu_items;
    update_screen_locked();
  }
  pthread_mutex_unlock(&updateMutex);
}

int WearRecoveryUI::SelectMenu(int sel) {
  int old_sel;
  pthread_mutex_lock(&updateMutex);
  if (show_menu) {
    old_sel = menu_sel;
    menu_sel = sel;
    if (menu_sel < 0) menu_sel = 0;
    if (menu_sel >= menu_items) menu_sel = menu_items - 1;
    if (menu_sel < menu_start) {
      menu_start--;
      menu_end--;
    } else if (menu_sel >= menu_end && menu_sel < menu_items) {
      menu_end++;
      menu_start++;
    }
    sel = menu_sel;
    if (menu_sel != old_sel) update_screen_locked();
  }
  pthread_mutex_unlock(&updateMutex);
  return sel;
}
