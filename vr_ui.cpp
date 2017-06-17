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

#include "vr_ui.h"

#include <minui/minui.h>

VrRecoveryUI::VrRecoveryUI() :
  x_offset(400),
  y_offset(400),
  stereo_offset(100) {
}

bool VrRecoveryUI::InitTextParams() {
  if (gr_init() < 0) {
    return false;
  }

  gr_font_size(gr_sys_font(), &char_width_, &char_height_);
  int mid_divide = gr_fb_width() / 2;
  text_rows_ = (gr_fb_height() - 2 * y_offset) / char_height_;
  text_cols_ = (mid_divide - x_offset - stereo_offset) / char_width_;
  log_bottom_offset_ = gr_fb_height() - 2 * y_offset;
  return true;
}

void VrRecoveryUI::DrawHorizontalRule(int* y) {
  SetColor(MENU);
  *y += 4;
  gr_fill(0, *y + y_offset, gr_fb_width(), *y + y_offset + 2);
  *y += 4;
}

void VrRecoveryUI::DrawHighlightBar(int x, int y, int width, int height) const {
  gr_fill(x, y + y_offset, x + width, y + y_offset + height);
}

void VrRecoveryUI::DrawTextLine(int x, int* y, const char* line, bool bold) const {
  int mid_divide = gr_fb_width() / 2;
  gr_text(gr_sys_font(), x + x_offset + stereo_offset, *y + y_offset, line, bold);
  gr_text(gr_sys_font(), x + x_offset - stereo_offset + mid_divide, *y + y_offset, line, bold);
  *y += char_height_ + 4;
}
