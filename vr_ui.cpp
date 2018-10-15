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

int VrRecoveryUI::ScreenWidth() const {
  return gr_fb_width() / 2;
}

int VrRecoveryUI::ScreenHeight() const {
  return gr_fb_height();
}

void VrRecoveryUI::DrawSurface(GRSurface* surface, int sx, int sy, int w, int h, int dx,
                               int dy) const {
  gr_blit(surface, sx, sy, w, h, dx + kStereoOffset, dy);
  gr_blit(surface, sx, sy, w, h, dx - kStereoOffset + ScreenWidth(), dy);
}

void VrRecoveryUI::DrawTextIcon(int x, int y, GRSurface* surface) const {
  gr_texticon(x + kStereoOffset, y, surface);
  gr_texticon(x - kStereoOffset + ScreenWidth(), y, surface);
}

int VrRecoveryUI::DrawTextLine(int x, int y, const char* line, bool bold) const {
  gr_text(gr_sys_font(), x + kStereoOffset, y, line, bold);
  gr_text(gr_sys_font(), x - kStereoOffset + ScreenWidth(), y, line, bold);
  return char_height_ + 4;
}

int VrRecoveryUI::DrawHorizontalRule(int y) const {
  y += 4;
  gr_fill(kMarginWidth + kStereoOffset, y, ScreenWidth() - kMarginWidth + kStereoOffset, y + 2);
  gr_fill(ScreenWidth() + kMarginWidth - kStereoOffset, y,
          gr_fb_width() - kMarginWidth - kStereoOffset, y + 2);
  return y + 4;
}

void VrRecoveryUI::DrawHighlightBar(int /* x */, int y, int /* width */, int height) const {
  gr_fill(kMarginWidth + kStereoOffset, y, ScreenWidth() - kMarginWidth + kStereoOffset, y + height);
  gr_fill(ScreenWidth() + kMarginWidth - kStereoOffset, y,
          gr_fb_width() - kMarginWidth - kStereoOffset, y + height);
}

void VrRecoveryUI::DrawFill(int x, int y, int w, int h) const {
  gr_fill(x + kStereoOffset, y, w, h);
  gr_fill(x - kStereoOffset + ScreenWidth(), y, w, h);
}
