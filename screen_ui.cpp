/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <minui/minui.h>

#include "common.h"
#include "device.h"
#include "ui.h"

// Return the current time as a double (including fractions of a second).
static double now() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

ScreenRecoveryUI::ScreenRecoveryUI()
    : kMarginWidth(RECOVERY_UI_MARGIN_WIDTH),
      kMarginHeight(RECOVERY_UI_MARGIN_HEIGHT),
      kAnimationFps(RECOVERY_UI_ANIMATION_FPS),
      kDensity(static_cast<float>(android::base::GetIntProperty("ro.sf.lcd_density", 160)) / 160.f),
      currentIcon(NONE),
      progressBarType(EMPTY),
      progressScopeStart(0),
      progressScopeSize(0),
      progress(0),
      pagesIdentical(false),
      text_cols_(0),
      text_rows_(0),
      text_(nullptr),
      text_col_(0),
      text_row_(0),
      show_text(false),
      show_text_ever(false),
      menu_headers_(nullptr),
      show_menu(false),
      menu_items(0),
      menu_sel(0),
      file_viewer_text_(nullptr),
      intro_frames(0),
      loop_frames(0),
      current_frame(0),
      intro_done(false),
      stage(-1),
      max_stage(-1),
      locale_(""),
      rtl_locale_(false),
      updateMutex(PTHREAD_MUTEX_INITIALIZER) {}

GRSurface* ScreenRecoveryUI::GetCurrentFrame() const {
  if (currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) {
    return intro_done ? loopFrames[current_frame] : introFrames[current_frame];
  }
  return error_icon;
}

GRSurface* ScreenRecoveryUI::GetCurrentText() const {
  switch (currentIcon) {
    case ERASING:
      return erasing_text;
    case ERROR:
      return error_text;
    case INSTALLING_UPDATE:
      return installing_text;
    case NO_COMMAND:
      return no_command_text;
    case NONE:
      abort();
  }
}

int ScreenRecoveryUI::PixelsFromDp(int dp) const {
  return dp * kDensity;
}

// Here's the intended layout:

//          | portrait    large        landscape      large
// ---------+-------------------------------------------------
//      gap |
// icon     |                   (200dp)
//      gap |    68dp      68dp             56dp      112dp
// text     |                    (14sp)
//      gap |    32dp      32dp             26dp       52dp
// progress |                     (2dp)
//      gap |

// Note that "baseline" is actually the *top* of each icon (because that's how our drawing routines
// work), so that's the more useful measurement for calling code. We use even top and bottom gaps.

enum Layout { PORTRAIT = 0, PORTRAIT_LARGE = 1, LANDSCAPE = 2, LANDSCAPE_LARGE = 3, LAYOUT_MAX };
enum Dimension { TEXT = 0, ICON = 1, DIMENSION_MAX };
static constexpr int kLayouts[LAYOUT_MAX][DIMENSION_MAX] = {
  { 32,  68, },  // PORTRAIT
  { 32,  68, },  // PORTRAIT_LARGE
  { 26,  56, },  // LANDSCAPE
  { 52, 112, },  // LANDSCAPE_LARGE
};

int ScreenRecoveryUI::GetAnimationBaseline() const {
  return GetTextBaseline() - PixelsFromDp(kLayouts[layout_][ICON]) - gr_get_height(loopFrames[0]);
}

int ScreenRecoveryUI::GetTextBaseline() const {
  return GetProgressBaseline() - PixelsFromDp(kLayouts[layout_][TEXT]) -
         gr_get_height(installing_text);
}

int ScreenRecoveryUI::GetProgressBaseline() const {
  int elements_sum = gr_get_height(loopFrames[0]) + PixelsFromDp(kLayouts[layout_][ICON]) +
                     gr_get_height(installing_text) + PixelsFromDp(kLayouts[layout_][TEXT]) +
                     gr_get_height(progressBarFill);
  int bottom_gap = (ScreenHeight() - elements_sum) / 2;
  return ScreenHeight() - bottom_gap - gr_get_height(progressBarFill);
}

// Clear the screen and draw the currently selected background icon (if any).
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_background_locked() {
  pagesIdentical = false;
  gr_color(0, 0, 0, 255);
  gr_clear();
  if (currentIcon != NONE) {
    if (max_stage != -1) {
      int stage_height = gr_get_height(stageMarkerEmpty);
      int stage_width = gr_get_width(stageMarkerEmpty);
      int x = (ScreenWidth() - max_stage * gr_get_width(stageMarkerEmpty)) / 2;
      int y = ScreenHeight() - stage_height - kMarginHeight;
      for (int i = 0; i < max_stage; ++i) {
        GRSurface* stage_surface = (i < stage) ? stageMarkerFill : stageMarkerEmpty;
        DrawSurface(stage_surface, 0, 0, stage_width, stage_height, x, y);
        x += stage_width;
      }
    }

    GRSurface* text_surface = GetCurrentText();
    int text_x = (ScreenWidth() - gr_get_width(text_surface)) / 2;
    int text_y = GetTextBaseline();
    gr_color(255, 255, 255, 255);
    DrawTextIcon(text_x, text_y, text_surface);
  }
}

// Draws the animation and progress bar (if any) on the screen. Does not flip pages. Should only be
// called with updateMutex locked.
void ScreenRecoveryUI::draw_foreground_locked() {
  if (currentIcon != NONE) {
    GRSurface* frame = GetCurrentFrame();
    int frame_width = gr_get_width(frame);
    int frame_height = gr_get_height(frame);
    int frame_x = (ScreenWidth() - frame_width) / 2;
    int frame_y = GetAnimationBaseline();
    DrawSurface(frame, 0, 0, frame_width, frame_height, frame_x, frame_y);
  }

  if (progressBarType != EMPTY) {
    int width = gr_get_width(progressBarEmpty);
    int height = gr_get_height(progressBarEmpty);

    int progress_x = (ScreenWidth() - width) / 2;
    int progress_y = GetProgressBaseline();

    // Erase behind the progress bar (in case this was a progress-only update)
    gr_color(0, 0, 0, 255);
    DrawFill(progress_x, progress_y, width, height);

    if (progressBarType == DETERMINATE) {
      float p = progressScopeStart + progress * progressScopeSize;
      int pos = static_cast<int>(p * width);

      if (rtl_locale_) {
        // Fill the progress bar from right to left.
        if (pos > 0) {
          DrawSurface(progressBarFill, width - pos, 0, pos, height, progress_x + width - pos,
                      progress_y);
        }
        if (pos < width - 1) {
          DrawSurface(progressBarEmpty, 0, 0, width - pos, height, progress_x, progress_y);
        }
      } else {
        // Fill the progress bar from left to right.
        if (pos > 0) {
          DrawSurface(progressBarFill, 0, 0, pos, height, progress_x, progress_y);
        }
        if (pos < width - 1) {
          DrawSurface(progressBarEmpty, pos, 0, width - pos, height, progress_x + pos, progress_y);
        }
      }
    }
  }
}

void ScreenRecoveryUI::SetColor(UIElement e) const {
  switch (e) {
    case INFO:
      gr_color(249, 194, 0, 255);
      break;
    case HEADER:
      gr_color(247, 0, 6, 255);
      break;
    case MENU:
    case MENU_SEL_BG:
      gr_color(0, 106, 157, 255);
      break;
    case MENU_SEL_BG_ACTIVE:
      gr_color(0, 156, 100, 255);
      break;
    case MENU_SEL_FG:
      gr_color(255, 255, 255, 255);
      break;
    case LOG:
      gr_color(196, 196, 196, 255);
      break;
    case TEXT_FILL:
      gr_color(0, 0, 0, 160);
      break;
    default:
      gr_color(255, 255, 255, 255);
      break;
  }
}

void ScreenRecoveryUI::SelectAndShowBackgroundText(const std::vector<std::string>& locales_entries,
                                                   size_t sel) {
  SetLocale(locales_entries[sel]);
  std::vector<std::string> text_name = { "erasing_text", "error_text", "installing_text",
                                         "installing_security_text", "no_command_text" };
  std::unordered_map<std::string, std::unique_ptr<GRSurface, decltype(&free)>> surfaces;
  for (const auto& name : text_name) {
    GRSurface* text_image = nullptr;
    LoadLocalizedBitmap(name.c_str(), &text_image);
    if (!text_image) {
      Print("Failed to load %s\n", name.c_str());
      return;
    }
    surfaces.emplace(name, std::unique_ptr<GRSurface, decltype(&free)>(text_image, &free));
  }

  pthread_mutex_lock(&updateMutex);
  gr_color(0, 0, 0, 255);
  gr_clear();

  int text_y = kMarginHeight;
  int text_x = kMarginWidth;
  int line_spacing = gr_sys_font()->char_height;  // Put some extra space between images.
  // Write the header and descriptive texts.
  SetColor(INFO);
  std::string header = "Show background text image";
  text_y += DrawTextLine(text_x, text_y, header.c_str(), true);
  std::string locale_selection = android::base::StringPrintf(
      "Current locale: %s, %zu/%zu", locales_entries[sel].c_str(), sel, locales_entries.size());
  const char* instruction[] = { locale_selection.c_str(),
                                "Use volume up/down to switch locales and power to exit.",
                                nullptr };
  text_y += DrawWrappedTextLines(text_x, text_y, instruction);

  // Iterate through the text images and display them in order for the current locale.
  for (const auto& p : surfaces) {
    text_y += line_spacing;
    SetColor(LOG);
    text_y += DrawTextLine(text_x, text_y, p.first.c_str(), false);
    gr_color(255, 255, 255, 255);
    gr_texticon(text_x, text_y, p.second.get());
    text_y += gr_get_height(p.second.get());
  }
  // Update the whole screen.
  gr_flip();
  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::CheckBackgroundTextImages(const std::string& saved_locale) {
  // Load a list of locales embedded in one of the resource files.
  std::vector<std::string> locales_entries = get_locales_in_png("installing_text");
  if (locales_entries.empty()) {
    Print("Failed to load locales from the resource files\n");
    return;
  }
  size_t selected = 0;
  SelectAndShowBackgroundText(locales_entries, selected);

  FlushKeys();
  while (true) {
    int key = WaitKey();
    if (key == KEY_POWER || key == KEY_ENTER) {
      break;
    } else if (key == KEY_UP || key == KEY_VOLUMEUP) {
      selected = (selected == 0) ? locales_entries.size() - 1 : selected - 1;
      SelectAndShowBackgroundText(locales_entries, selected);
    } else if (key == KEY_DOWN || key == KEY_VOLUMEDOWN) {
      selected = (selected == locales_entries.size() - 1) ? 0 : selected + 1;
      SelectAndShowBackgroundText(locales_entries, selected);
    }
  }

  SetLocale(saved_locale);
}

int ScreenRecoveryUI::ScreenWidth() const {
  return gr_fb_width();
}

int ScreenRecoveryUI::ScreenHeight() const {
  return gr_fb_height();
}

void ScreenRecoveryUI::DrawSurface(GRSurface* surface, int sx, int sy, int w, int h, int dx,
                                   int dy) const {
  gr_blit(surface, sx, sy, w, h, dx, dy);
}

int ScreenRecoveryUI::DrawHorizontalRule(int y) const {
  gr_fill(0, y + 4, ScreenWidth(), y + 6);
  return 8;
}

void ScreenRecoveryUI::DrawHighlightBar(int x, int y, int width, int height) const {
  gr_fill(x, y, x + width, y + height);
}

void ScreenRecoveryUI::DrawFill(int x, int y, int w, int h) const {
  gr_fill(x, y, w, h);
}

void ScreenRecoveryUI::DrawTextIcon(int x, int y, GRSurface* surface) const {
  gr_texticon(x, y, surface);
}

int ScreenRecoveryUI::DrawTextLine(int x, int y, const char* line, bool bold) const {
  gr_text(gr_sys_font(), x, y, line, bold);
  return char_height_ + 4;
}

int ScreenRecoveryUI::DrawTextLines(int x, int y, const char* const* lines) const {
  int offset = 0;
  for (size_t i = 0; lines != nullptr && lines[i] != nullptr; ++i) {
    offset += DrawTextLine(x, y + offset, lines[i], false);
  }
  return offset;
}

int ScreenRecoveryUI::DrawWrappedTextLines(int x, int y, const char* const* lines) const {
  int offset = 0;
  for (size_t i = 0; lines != nullptr && lines[i] != nullptr; ++i) {
    // The line will be wrapped if it exceeds text_cols_.
    std::string line(lines[i]);
    size_t next_start = 0;
    while (next_start < line.size()) {
      std::string sub = line.substr(next_start, text_cols_ + 1);
      if (sub.size() <= text_cols_) {
        next_start += sub.size();
      } else {
        // Line too long and must be wrapped to text_cols_ columns.
        size_t last_space = sub.find_last_of(" \t\n");
        if (last_space == std::string::npos) {
          // No space found, just draw as much as we can
          sub.resize(text_cols_);
          next_start += text_cols_;
        } else {
          sub.resize(last_space);
          next_start += last_space + 1;
        }
      }
      offset += DrawTextLine(x, y + offset, sub.c_str(), false);
    }
  }
  return offset;
}

static const char* REGULAR_HELP[] = {
  "Use volume up/down and power.",
  NULL
};

static const char* LONG_PRESS_HELP[] = {
  "Any button cycles highlight.",
  "Long-press activates.",
  NULL
};

// Redraws everything on the screen. Does not flip pages. Should only be called with updateMutex
// locked.
void ScreenRecoveryUI::draw_screen_locked() {
  if (!show_text) {
    draw_background_locked();
    draw_foreground_locked();
    return;
  }

  gr_color(0, 0, 0, 255);
  gr_clear();

  int y = kMarginHeight;
  if (show_menu) {
    static constexpr int kMenuIndent = 4;
    int x = kMarginWidth + kMenuIndent;

    SetColor(INFO);
    y += DrawTextLine(x, y, "Android Recovery", true);
    std::string recovery_fingerprint =
        android::base::GetProperty("ro.bootimage.build.fingerprint", "");
    for (const auto& chunk : android::base::Split(recovery_fingerprint, ":")) {
      y += DrawTextLine(x, y, chunk.c_str(), false);
    }
    y += DrawTextLines(x, y, HasThreeButtons() ? REGULAR_HELP : LONG_PRESS_HELP);

    SetColor(HEADER);
    // Ignore kMenuIndent, which is not taken into account by text_cols_.
    y += DrawWrappedTextLines(kMarginWidth, y, menu_headers_);

    SetColor(MENU);
    y += DrawHorizontalRule(y) + 4;
    for (int i = 0; i < menu_items; ++i) {
      if (i == menu_sel) {
        // Draw the highlight bar.
        SetColor(IsLongPress() ? MENU_SEL_BG_ACTIVE : MENU_SEL_BG);
        DrawHighlightBar(0, y - 2, ScreenWidth(), char_height_ + 4);
        // Bold white text for the selected item.
        SetColor(MENU_SEL_FG);
        y += DrawTextLine(x, y, menu_[i].c_str(), true);
        SetColor(MENU);
      } else {
        y += DrawTextLine(x, y, menu_[i].c_str(), false);
      }
    }
    y += DrawHorizontalRule(y);
  }

  // Display from the bottom up, until we hit the top of the screen, the bottom of the menu, or
  // we've displayed the entire text buffer.
  SetColor(LOG);
  int row = text_row_;
  size_t count = 0;
  for (int ty = ScreenHeight() - kMarginHeight - char_height_; ty >= y && count < text_rows_;
       ty -= char_height_, ++count) {
    DrawTextLine(kMarginWidth, ty, text_[row], false);
    --row;
    if (row < 0) row = text_rows_ - 1;
  }
}

// Redraw everything on the screen and flip the screen (make it visible).
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::update_screen_locked() {
  draw_screen_locked();
  gr_flip();
}

// Updates only the progress bar, if possible, otherwise redraws the screen.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::update_progress_locked() {
  if (show_text || !pagesIdentical) {
    draw_screen_locked();  // Must redraw the whole screen
    pagesIdentical = true;
  } else {
    draw_foreground_locked();  // Draw only the progress bar and overlays
  }
  gr_flip();
}

// Keeps the progress bar updated, even when the process is otherwise busy.
void* ScreenRecoveryUI::ProgressThreadStartRoutine(void* data) {
  reinterpret_cast<ScreenRecoveryUI*>(data)->ProgressThreadLoop();
  return nullptr;
}

void ScreenRecoveryUI::ProgressThreadLoop() {
  double interval = 1.0 / kAnimationFps;
  while (true) {
    double start = now();
    pthread_mutex_lock(&updateMutex);

    bool redraw = false;

    // update the installation animation, if active
    // skip this if we have a text overlay (too expensive to update)
    if ((currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) && !show_text) {
      if (!intro_done) {
        if (current_frame == intro_frames - 1) {
          intro_done = true;
          current_frame = 0;
        } else {
          ++current_frame;
        }
      } else {
        current_frame = (current_frame + 1) % loop_frames;
      }

      redraw = true;
    }

    // move the progress bar forward on timed intervals, if configured
    int duration = progressScopeDuration;
    if (progressBarType == DETERMINATE && duration > 0) {
      double elapsed = now() - progressScopeTime;
      float p = 1.0 * elapsed / duration;
      if (p > 1.0) p = 1.0;
      if (p > progress) {
        progress = p;
        redraw = true;
      }
    }

    if (redraw) update_progress_locked();

    pthread_mutex_unlock(&updateMutex);
    double end = now();
    // minimum of 20ms delay between frames
    double delay = interval - (end - start);
    if (delay < 0.02) delay = 0.02;
    usleep(static_cast<useconds_t>(delay * 1000000));
  }
}

void ScreenRecoveryUI::LoadBitmap(const char* filename, GRSurface** surface) {
  int result = res_create_display_surface(filename, surface);
  if (result < 0) {
    LOG(ERROR) << "couldn't load bitmap " << filename << " (error " << result << ")";
  }
}

void ScreenRecoveryUI::LoadLocalizedBitmap(const char* filename, GRSurface** surface) {
  int result = res_create_localized_alpha_surface(filename, locale_.c_str(), surface);
  if (result < 0) {
    LOG(ERROR) << "couldn't load bitmap " << filename << " (error " << result << ")";
  }
}

static char** Alloc2d(size_t rows, size_t cols) {
  char** result = new char*[rows];
  for (size_t i = 0; i < rows; ++i) {
    result[i] = new char[cols];
    memset(result[i], 0, cols);
  }
  return result;
}

// Choose the right background string to display during update.
void ScreenRecoveryUI::SetSystemUpdateText(bool security_update) {
  if (security_update) {
    LoadLocalizedBitmap("installing_security_text", &installing_text);
  } else {
    LoadLocalizedBitmap("installing_text", &installing_text);
  }
  Redraw();
}

bool ScreenRecoveryUI::InitTextParams() {
  if (gr_init() < 0) {
    return false;
  }

  gr_font_size(gr_sys_font(), &char_width_, &char_height_);
  text_rows_ = (ScreenHeight() - kMarginHeight * 2) / char_height_;
  text_cols_ = (ScreenWidth() - kMarginWidth * 2) / char_width_;
  return true;
}

bool ScreenRecoveryUI::Init(const std::string& locale) {
  RecoveryUI::Init(locale);

  if (!InitTextParams()) {
    return false;
  }

  // Are we portrait or landscape?
  layout_ = (gr_fb_width() > gr_fb_height()) ? LANDSCAPE : PORTRAIT;
  // Are we the large variant of our base layout?
  if (gr_fb_height() > PixelsFromDp(800)) ++layout_;

  text_ = Alloc2d(text_rows_, text_cols_ + 1);
  file_viewer_text_ = Alloc2d(text_rows_, text_cols_ + 1);

  text_col_ = text_row_ = 0;

  // Set up the locale info.
  SetLocale(locale);

  LoadBitmap("icon_error", &error_icon);

  LoadBitmap("progress_empty", &progressBarEmpty);
  LoadBitmap("progress_fill", &progressBarFill);

  LoadBitmap("stage_empty", &stageMarkerEmpty);
  LoadBitmap("stage_fill", &stageMarkerFill);

  // Background text for "installing_update" could be "installing update"
  // or "installing security update". It will be set after UI init according
  // to commands in BCB.
  installing_text = nullptr;
  LoadLocalizedBitmap("erasing_text", &erasing_text);
  LoadLocalizedBitmap("no_command_text", &no_command_text);
  LoadLocalizedBitmap("error_text", &error_text);

  LoadAnimation();

  pthread_create(&progress_thread_, nullptr, ProgressThreadStartRoutine, this);

  return true;
}

void ScreenRecoveryUI::LoadAnimation() {
  std::unique_ptr<DIR, decltype(&closedir)> dir(opendir("/res/images"), closedir);
  dirent* de;
  std::vector<std::string> intro_frame_names;
  std::vector<std::string> loop_frame_names;

  while ((de = readdir(dir.get())) != nullptr) {
    int value, num_chars;
    if (sscanf(de->d_name, "intro%d%n.png", &value, &num_chars) == 1) {
      intro_frame_names.emplace_back(de->d_name, num_chars);
    } else if (sscanf(de->d_name, "loop%d%n.png", &value, &num_chars) == 1) {
      loop_frame_names.emplace_back(de->d_name, num_chars);
    }
  }

  intro_frames = intro_frame_names.size();
  loop_frames = loop_frame_names.size();

  // It's okay to not have an intro.
  if (intro_frames == 0) intro_done = true;
  // But you must have an animation.
  if (loop_frames == 0) abort();

  std::sort(intro_frame_names.begin(), intro_frame_names.end());
  std::sort(loop_frame_names.begin(), loop_frame_names.end());

  introFrames = new GRSurface*[intro_frames];
  for (size_t i = 0; i < intro_frames; i++) {
    LoadBitmap(intro_frame_names.at(i).c_str(), &introFrames[i]);
  }

  loopFrames = new GRSurface*[loop_frames];
  for (size_t i = 0; i < loop_frames; i++) {
    LoadBitmap(loop_frame_names.at(i).c_str(), &loopFrames[i]);
  }
}

void ScreenRecoveryUI::SetBackground(Icon icon) {
  pthread_mutex_lock(&updateMutex);

  currentIcon = icon;
  update_screen_locked();

  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetProgressType(ProgressType type) {
  pthread_mutex_lock(&updateMutex);
  if (progressBarType != type) {
    progressBarType = type;
  }
  progressScopeStart = 0;
  progressScopeSize = 0;
  progress = 0;
  update_progress_locked();
  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::ShowProgress(float portion, float seconds) {
  pthread_mutex_lock(&updateMutex);
  progressBarType = DETERMINATE;
  progressScopeStart += progressScopeSize;
  progressScopeSize = portion;
  progressScopeTime = now();
  progressScopeDuration = seconds;
  progress = 0;
  update_progress_locked();
  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetProgress(float fraction) {
  pthread_mutex_lock(&updateMutex);
  if (fraction < 0.0) fraction = 0.0;
  if (fraction > 1.0) fraction = 1.0;
  if (progressBarType == DETERMINATE && fraction > progress) {
    // Skip updates that aren't visibly different.
    int width = gr_get_width(progressBarEmpty);
    float scale = width * progressScopeSize;
    if ((int)(progress * scale) != (int)(fraction * scale)) {
      progress = fraction;
      update_progress_locked();
    }
  }
  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetStage(int current, int max) {
  pthread_mutex_lock(&updateMutex);
  stage = current;
  max_stage = max;
  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::PrintV(const char* fmt, bool copy_to_stdout, va_list ap) {
  std::string str;
  android::base::StringAppendV(&str, fmt, ap);

  if (copy_to_stdout) {
    fputs(str.c_str(), stdout);
  }

  pthread_mutex_lock(&updateMutex);
  if (text_rows_ > 0 && text_cols_ > 0) {
    for (const char* ptr = str.c_str(); *ptr != '\0'; ++ptr) {
      if (*ptr == '\n' || text_col_ >= text_cols_) {
        text_[text_row_][text_col_] = '\0';
        text_col_ = 0;
        text_row_ = (text_row_ + 1) % text_rows_;
      }
      if (*ptr != '\n') text_[text_row_][text_col_++] = *ptr;
    }
    text_[text_row_][text_col_] = '\0';
    update_screen_locked();
  }
  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::Print(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  PrintV(fmt, true, ap);
  va_end(ap);
}

void ScreenRecoveryUI::PrintOnScreenOnly(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  PrintV(fmt, false, ap);
  va_end(ap);
}

void ScreenRecoveryUI::PutChar(char ch) {
  pthread_mutex_lock(&updateMutex);
  if (ch != '\n') text_[text_row_][text_col_++] = ch;
  if (ch == '\n' || text_col_ >= text_cols_) {
    text_col_ = 0;
    ++text_row_;
  }
  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::ClearText() {
  pthread_mutex_lock(&updateMutex);
  text_col_ = 0;
  text_row_ = 0;
  for (size_t i = 0; i < text_rows_; ++i) {
    memset(text_[i], 0, text_cols_ + 1);
  }
  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::ShowFile(FILE* fp) {
  std::vector<off_t> offsets;
  offsets.push_back(ftello(fp));
  ClearText();

  struct stat sb;
  fstat(fileno(fp), &sb);

  bool show_prompt = false;
  while (true) {
    if (show_prompt) {
      PrintOnScreenOnly("--(%d%% of %d bytes)--",
                        static_cast<int>(100 * (double(ftello(fp)) / double(sb.st_size))),
                        static_cast<int>(sb.st_size));
      Redraw();
      while (show_prompt) {
        show_prompt = false;
        int key = WaitKey();
        if (key == KEY_POWER || key == KEY_ENTER) {
          return;
        } else if (key == KEY_UP || key == KEY_VOLUMEUP) {
          if (offsets.size() <= 1) {
            show_prompt = true;
          } else {
            offsets.pop_back();
            fseek(fp, offsets.back(), SEEK_SET);
          }
        } else {
          if (feof(fp)) {
            return;
          }
          offsets.push_back(ftello(fp));
        }
      }
      ClearText();
    }

    int ch = getc(fp);
    if (ch == EOF) {
      while (text_row_ < text_rows_ - 1) PutChar('\n');
      show_prompt = true;
    } else {
      PutChar(ch);
      if (text_col_ == 0 && text_row_ >= text_rows_ - 1) {
        show_prompt = true;
      }
    }
  }
}

void ScreenRecoveryUI::ShowFile(const char* filename) {
  FILE* fp = fopen_path(filename, "re");
  if (fp == nullptr) {
    Print("  Unable to open %s: %s\n", filename, strerror(errno));
    return;
  }

  char** old_text = text_;
  size_t old_text_col = text_col_;
  size_t old_text_row = text_row_;

  // Swap in the alternate screen and clear it.
  text_ = file_viewer_text_;
  ClearText();

  ShowFile(fp);
  fclose(fp);

  text_ = old_text;
  text_col_ = old_text_col;
  text_row_ = old_text_row;
}

void ScreenRecoveryUI::StartMenu(const char* const* headers, const char* const* items,
                                 int initial_selection) {
  pthread_mutex_lock(&updateMutex);
  if (text_rows_ > 0 && text_cols_ > 0) {
    menu_headers_ = headers;
    menu_.clear();
    for (size_t i = 0; i < text_rows_ && items[i] != nullptr; ++i) {
      menu_.emplace_back(std::string(items[i], strnlen(items[i], text_cols_ - 1)));
    }
    menu_items = static_cast<int>(menu_.size());
    show_menu = true;
    menu_sel = initial_selection;
    update_screen_locked();
  }
  pthread_mutex_unlock(&updateMutex);
}

int ScreenRecoveryUI::SelectMenu(int sel) {
  pthread_mutex_lock(&updateMutex);
  if (show_menu) {
    int old_sel = menu_sel;
    menu_sel = sel;

    // Wrap at top and bottom.
    if (menu_sel < 0) menu_sel = menu_items - 1;
    if (menu_sel >= menu_items) menu_sel = 0;

    sel = menu_sel;
    if (menu_sel != old_sel) update_screen_locked();
  }
  pthread_mutex_unlock(&updateMutex);
  return sel;
}

void ScreenRecoveryUI::EndMenu() {
  pthread_mutex_lock(&updateMutex);
  if (show_menu && text_rows_ > 0 && text_cols_ > 0) {
    show_menu = false;
    update_screen_locked();
  }
  pthread_mutex_unlock(&updateMutex);
}

bool ScreenRecoveryUI::IsTextVisible() {
  pthread_mutex_lock(&updateMutex);
  int visible = show_text;
  pthread_mutex_unlock(&updateMutex);
  return visible;
}

bool ScreenRecoveryUI::WasTextEverVisible() {
  pthread_mutex_lock(&updateMutex);
  int ever_visible = show_text_ever;
  pthread_mutex_unlock(&updateMutex);
  return ever_visible;
}

void ScreenRecoveryUI::ShowText(bool visible) {
  pthread_mutex_lock(&updateMutex);
  show_text = visible;
  if (show_text) show_text_ever = true;
  update_screen_locked();
  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::Redraw() {
  pthread_mutex_lock(&updateMutex);
  update_screen_locked();
  pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::KeyLongPress(int) {
  // Redraw so that if we're in the menu, the highlight
  // will change color to indicate a successful long press.
  Redraw();
}

void ScreenRecoveryUI::SetLocale(const std::string& new_locale) {
  locale_ = new_locale;
  rtl_locale_ = false;

  if (!new_locale.empty()) {
    size_t underscore = new_locale.find('_');
    // lang has the language prefix prior to '_', or full string if '_' doesn't exist.
    std::string lang = new_locale.substr(0, underscore);

    // A bit cheesy: keep an explicit list of supported RTL languages.
    if (lang == "ar" ||  // Arabic
        lang == "fa" ||  // Persian (Farsi)
        lang == "he" ||  // Hebrew (new language code)
        lang == "iw" ||  // Hebrew (old language code)
        lang == "ur") {  // Urdu
      rtl_locale_ = true;
    }
  }
}
