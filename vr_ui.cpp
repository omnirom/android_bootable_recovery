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

VrRecoveryUI::VrRecoveryUI() : kStereoOffset(RECOVERY_UI_VR_STEREO_OFFSET) {}

bool VrRecoveryUI::InitTextParams() {
  if (!ScreenRecoveryUI::InitTextParams()) return false;
  int mid_divide = gr_fb_width() / 2;
  text_cols_ = (mid_divide - kMarginWidth - kStereoOffset) / char_width_;
  return true;
}

int VrRecoveryUI::DrawTextLine(int x, int y, const char* line, bool bold) const {
  int mid_divide = gr_fb_width() / 2;
  gr_text(gr_sys_font(), x + kStereoOffset, y, line, bold);
  gr_text(gr_sys_font(), x - kStereoOffset + mid_divide, y, line, bold);
  return char_height_ + 4;
}
