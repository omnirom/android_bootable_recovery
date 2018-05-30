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

#include <string.h>

#include <string>
#include <vector>

#include <android-base/properties.h>
#include <android-base/strings.h>
#include <minui/minui.h>

WearRecoveryUI::WearRecoveryUI()
    : ScreenRecoveryUI(true),
      kProgressBarBaseline(RECOVERY_UI_PROGRESS_BAR_BASELINE),
      kMenuUnusableRows(RECOVERY_UI_MENU_UNUSABLE_ROWS) {
  // TODO: kMenuUnusableRows should be computed based on the lines in draw_screen_locked().

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

void WearRecoveryUI::draw_screen_locked() {
  draw_background_locked();
  if (!show_text) {
    draw_foreground_locked();
  } else {
    SetColor(TEXT_FILL);
    gr_fill(0, 0, gr_fb_width(), gr_fb_height());

    // clang-format off
    static std::vector<std::string> SWIPE_HELP = {
      "Swipe up/down to move.",
      "Swipe left/right to select.",
      "",
    };
    // clang-format on
    draw_menu_and_text_buffer_locked(SWIPE_HELP);
  }
}

// TODO merge drawing routines with screen_ui
void WearRecoveryUI::update_progress_locked() {
  draw_screen_locked();
  gr_flip();
}

void WearRecoveryUI::SetStage(int /* current */, int /* max */) {}

void WearRecoveryUI::StartMenu(const std::vector<std::string>& headers,
                               const std::vector<std::string>& items, size_t initial_selection) {
  std::lock_guard<std::mutex> lg(updateMutex);
  if (text_rows_ > 0 && text_cols_ > 0) {
    menu_ = std::make_unique<Menu>(scrollable_menu_, text_rows_ - kMenuUnusableRows - 1,
                                   text_cols_ - 1, headers, items, initial_selection);
    update_screen_locked();
  }
}
