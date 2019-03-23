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

#include "recovery_ui/vr_ui.h"

#include <android-base/properties.h>

#include "minui/minui.h"

constexpr int kDefaultStereoOffset = 0;

VrRecoveryUI::VrRecoveryUI()
    : stereo_offset_(
          android::base::GetIntProperty("ro.recovery.ui.stereo_offset", kDefaultStereoOffset)) {}

int VrRecoveryUI::ScreenWidth() const {
  return gr_fb_width() / 2;
}

int VrRecoveryUI::ScreenHeight() const {
  return gr_fb_height();
}

void VrRecoveryUI::DrawSurface(const GRSurface* surface, int sx, int sy, int w, int h, int dx,
                               int dy) const {
  gr_blit(surface, sx, sy, w, h, dx + stereo_offset_, dy);
  gr_blit(surface, sx, sy, w, h, dx - stereo_offset_ + ScreenWidth(), dy);
}

void VrRecoveryUI::DrawTextIcon(int x, int y, const GRSurface* surface) const {
  gr_texticon(x + stereo_offset_, y, surface);
  gr_texticon(x - stereo_offset_ + ScreenWidth(), y, surface);
}

int VrRecoveryUI::DrawTextLine(int x, int y, const std::string& line, bool bold) const {
  gr_text(gr_sys_font(), x + stereo_offset_, y, line.c_str(), bold);
  gr_text(gr_sys_font(), x - stereo_offset_ + ScreenWidth(), y, line.c_str(), bold);
  return char_height_ + 4;
}

int VrRecoveryUI::DrawHorizontalRule(int y) const {
  y += 4;
  gr_fill(margin_width_ + stereo_offset_, y, ScreenWidth() - margin_width_ + stereo_offset_, y + 2);
  gr_fill(ScreenWidth() + margin_width_ - stereo_offset_, y,
          gr_fb_width() - margin_width_ - stereo_offset_, y + 2);
  return y + 4;
}

void VrRecoveryUI::DrawHighlightBar(int /* x */, int y, int /* width */, int height) const {
  gr_fill(margin_width_ + stereo_offset_, y, ScreenWidth() - margin_width_ + stereo_offset_,
          y + height);
  gr_fill(ScreenWidth() + margin_width_ - stereo_offset_, y,
          gr_fb_width() - margin_width_ - stereo_offset_, y + height);
}

void VrRecoveryUI::DrawFill(int x, int y, int w, int h) const {
  gr_fill(x + stereo_offset_, y, w, h);
  gr_fill(x - stereo_offset_ + ScreenWidth(), y, w, h);
}
