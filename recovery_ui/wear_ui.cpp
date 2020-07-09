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

#include "recovery_ui/wear_ui.h"

#include <string.h>

#include <string>
#include <vector>

#include <android-base/properties.h>
#include <android-base/strings.h>
#include <minui/minui.h>

constexpr int kDefaultProgressBarBaseline = 259;
constexpr int kDefaultMenuUnusableRows = 9;

WearRecoveryUI::WearRecoveryUI()
    : ScreenRecoveryUI(true),
      progress_bar_baseline_(android::base::GetIntProperty("ro.recovery.ui.progress_bar_baseline",
                                                           kDefaultProgressBarBaseline)),
      menu_unusable_rows_(android::base::GetIntProperty("ro.recovery.ui.menu_unusable_rows",
                                                        kDefaultMenuUnusableRows)) {
  // TODO: menu_unusable_rows_ should be computed based on the lines in draw_screen_locked().

  touch_screen_allowed_ = true;
}

int WearRecoveryUI::GetProgressBaseline() const {
  return progress_bar_baseline_;
}

// Draw background frame on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
// TODO merge drawing routines with screen_ui
void WearRecoveryUI::draw_background_locked() {
  pagesIdentical = false;
  gr_color(0, 0, 0, 255);
  gr_fill(0, 0, gr_fb_width(), gr_fb_height());

  if (current_icon_ != NONE) {
    const auto& frame = GetCurrentFrame();
    int frame_width = gr_get_width(frame);
    int frame_height = gr_get_height(frame);
    int frame_x = (gr_fb_width() - frame_width) / 2;
    int frame_y = (gr_fb_height() - frame_height) / 2;
    gr_blit(frame, 0, 0, frame_width, frame_height, frame_x, frame_y);

    // Draw recovery text on screen above progress bar.
    const auto& text = GetCurrentText();
    int text_x = (ScreenWidth() - gr_get_width(text)) / 2;
    int text_y = GetProgressBaseline() - gr_get_height(text) - 10;
    gr_color(255, 255, 255, 255);
    gr_texticon(text_x, text_y, text);
  }
}

void WearRecoveryUI::draw_screen_locked() {
  draw_background_locked();
  if (!show_text) {
    draw_foreground_locked();
  } else {
    SetColor(UIElement::TEXT_FILL);
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

std::unique_ptr<Menu> WearRecoveryUI::CreateMenu(const std::vector<std::string>& text_headers,
                                                 const std::vector<std::string>& text_items,
                                                 size_t initial_selection) const {
  if (text_rows_ > 0 && text_cols_ > 0) {
    return std::make_unique<TextMenu>(scrollable_menu_, text_rows_ - menu_unusable_rows_ - 1,
                                      text_cols_ - 1, text_headers, text_items, initial_selection,
                                      char_height_, *this);
  }

  return nullptr;
}
