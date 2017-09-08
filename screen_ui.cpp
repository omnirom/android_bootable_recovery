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

#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/stringprintf.h>

#include "common.h"
#include "device.h"
#include "minui/minui.h"
#include "screen_ui.h"
#include "ui.h"

#define TEXT_INDENT     4

// Return the current time as a double (including fractions of a second).
static double now() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

ScreenRecoveryUI::ScreenRecoveryUI()
    : currentIcon(NONE),
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
      text_top_(0),
      show_text(false),
      show_text_ever(false),
      menu_(nullptr),
      show_menu(false),
      menu_items(0),
      menu_sel(0),
      file_viewer_text_(nullptr),
      intro_frames(0),
      loop_frames(0),
      current_frame(0),
      intro_done(false),
      animation_fps(30),  // TODO: there's currently no way to infer this.
      stage(-1),
      max_stage(-1),
      updateMutex(PTHREAD_MUTEX_INITIALIZER) {}

GRSurface* ScreenRecoveryUI::GetCurrentFrame() {
    if (currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) {
        return intro_done ? loopFrames[current_frame] : introFrames[current_frame];
    }
    return error_icon;
}

GRSurface* ScreenRecoveryUI::GetCurrentText() {
    switch (currentIcon) {
        case ERASING: return erasing_text;
        case ERROR: return error_text;
        case INSTALLING_UPDATE: return installing_text;
        case NO_COMMAND: return no_command_text;
        case NONE: abort();
    }
}

int ScreenRecoveryUI::PixelsFromDp(int dp) const {
    return dp * density_;
}

// Here's the intended layout:

//          | portrait    large        landscape      large
// ---------+-------------------------------------------------
//      gap |   220dp     366dp            142dp      284dp
// icon     |                   (200dp)
//      gap |    68dp      68dp             56dp      112dp
// text     |                    (14sp)
//      gap |    32dp      32dp             26dp       52dp
// progress |                     (2dp)
//      gap |   194dp     340dp            131dp      262dp

// Note that "baseline" is actually the *top* of each icon (because that's how our drawing
// routines work), so that's the more useful measurement for calling code.

enum Layout { PORTRAIT = 0, PORTRAIT_LARGE = 1, LANDSCAPE = 2, LANDSCAPE_LARGE = 3, LAYOUT_MAX };
enum Dimension { PROGRESS = 0, TEXT = 1, ICON = 2, DIMENSION_MAX };
static constexpr int kLayouts[LAYOUT_MAX][DIMENSION_MAX] = {
    { 194,  32,  68, }, // PORTRAIT
    { 340,  32,  68, }, // PORTRAIT_LARGE
    { 131,  26,  56, }, // LANDSCAPE
    { 262,  52, 112, }, // LANDSCAPE_LARGE
};

int ScreenRecoveryUI::GetAnimationBaseline() {
    return GetTextBaseline() - PixelsFromDp(kLayouts[layout_][ICON]) -
            gr_get_height(loopFrames[0]);
}

int ScreenRecoveryUI::GetTextBaseline() {
    return GetProgressBaseline() - PixelsFromDp(kLayouts[layout_][TEXT]) -
            gr_get_height(installing_text);
}

int ScreenRecoveryUI::GetProgressBaseline() {
    return gr_fb_height() - PixelsFromDp(kLayouts[layout_][PROGRESS]) -
            gr_get_height(progressBarFill);
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
            int x = (gr_fb_width() - max_stage * gr_get_width(stageMarkerEmpty)) / 2;
            int y = gr_fb_height() - stage_height;
            for (int i = 0; i < max_stage; ++i) {
                GRSurface* stage_surface = (i < stage) ? stageMarkerFill : stageMarkerEmpty;
                gr_blit(stage_surface, 0, 0, stage_width, stage_height, x, y);
                x += stage_width;
            }
        }

        GRSurface* text_surface = GetCurrentText();
        int text_x = (gr_fb_width() - gr_get_width(text_surface)) / 2;
        int text_y = GetTextBaseline();
        gr_color(255, 255, 255, 255);
        gr_texticon(text_x, text_y, text_surface);
    }
}

// Draws the animation and progress bar (if any) on the screen.
// Does not flip pages.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_foreground_locked() {
  if (currentIcon != NONE) {
    GRSurface* frame = GetCurrentFrame();
    int frame_width = gr_get_width(frame);
    int frame_height = gr_get_height(frame);
    int frame_x = (gr_fb_width() - frame_width) / 2;
    int frame_y = GetAnimationBaseline();
    gr_blit(frame, 0, 0, frame_width, frame_height, frame_x, frame_y);
  }

  if (progressBarType != EMPTY) {
    int width = gr_get_width(progressBarEmpty);
    int height = gr_get_height(progressBarEmpty);

    int progress_x = (gr_fb_width() - width) / 2;
    int progress_y = GetProgressBaseline();

    // Erase behind the progress bar (in case this was a progress-only update)
    gr_color(0, 0, 0, 255);
    gr_fill(progress_x, progress_y, width, height);

    if (progressBarType == DETERMINATE) {
      float p = progressScopeStart + progress * progressScopeSize;
      int pos = static_cast<int>(p * width);

      if (rtl_locale_) {
        // Fill the progress bar from right to left.
        if (pos > 0) {
          gr_blit(progressBarFill, width - pos, 0, pos, height, progress_x + width - pos,
                  progress_y);
        }
        if (pos < width - 1) {
          gr_blit(progressBarEmpty, 0, 0, width - pos, height, progress_x, progress_y);
        }
      } else {
        // Fill the progress bar from left to right.
        if (pos > 0) {
          gr_blit(progressBarFill, 0, 0, pos, height, progress_x, progress_y);
        }
        if (pos < width - 1) {
          gr_blit(progressBarEmpty, pos, 0, width - pos, height, progress_x + pos, progress_y);
        }
      }
    }
  }
}

void ScreenRecoveryUI::SetColor(UIElement e) {
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

void ScreenRecoveryUI::DrawHorizontalRule(int* y) {
    SetColor(MENU);
    *y += 4;
    gr_fill(0, *y, gr_fb_width(), *y + 2);
    *y += 4;
}

void ScreenRecoveryUI::DrawTextLine(int x, int* y, const char* line, bool bold) const {
    gr_text(gr_sys_font(), x, *y, line, bold);
    *y += char_height_ + 4;
}

void ScreenRecoveryUI::DrawTextLines(int x, int* y, const char* const* lines) const {
    for (size_t i = 0; lines != nullptr && lines[i] != nullptr; ++i) {
        DrawTextLine(x, y, lines[i], false);
    }
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

// Redraw everything on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_screen_locked() {
    if (!show_text) {
        draw_background_locked();
        draw_foreground_locked();
    } else {
        gr_color(0, 0, 0, 255);
        gr_clear();

        int y = 0;
        if (show_menu) {
            std::string recovery_fingerprint =
                    android::base::GetProperty("ro.bootimage.build.fingerprint", "");

            SetColor(INFO);
            DrawTextLine(TEXT_INDENT, &y, "Android Recovery", true);
            for (auto& chunk : android::base::Split(recovery_fingerprint, ":")) {
                DrawTextLine(TEXT_INDENT, &y, chunk.c_str(), false);
            }
            DrawTextLines(TEXT_INDENT, &y, HasThreeButtons() ? REGULAR_HELP : LONG_PRESS_HELP);

            SetColor(HEADER);
            DrawTextLines(TEXT_INDENT, &y, menu_headers_);

            SetColor(MENU);
            DrawHorizontalRule(&y);
            y += 4;
            for (int i = 0; i < menu_items; ++i) {
                if (i == menu_sel) {
                    // Draw the highlight bar.
                    SetColor(IsLongPress() ? MENU_SEL_BG_ACTIVE : MENU_SEL_BG);
                    gr_fill(0, y - 2, gr_fb_width(), y + char_height_ + 2);
                    // Bold white text for the selected item.
                    SetColor(MENU_SEL_FG);
                    gr_text(gr_sys_font(), 4, y, menu_[i], true);
                    SetColor(MENU);
                } else {
                    gr_text(gr_sys_font(), 4, y, menu_[i], false);
                }
                y += char_height_ + 4;
            }
            DrawHorizontalRule(&y);
        }

        // display from the bottom up, until we hit the top of the
        // screen, the bottom of the menu, or we've displayed the
        // entire text buffer.
        SetColor(LOG);
        int row = (text_top_ + text_rows_ - 1) % text_rows_;
        size_t count = 0;
        for (int ty = gr_fb_height() - char_height_;
             ty >= y && count < text_rows_;
             ty -= char_height_, ++count) {
            gr_text(gr_sys_font(), 0, ty, text_[row], false);
            --row;
            if (row < 0) row = text_rows_ - 1;
        }
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
        draw_screen_locked();    // Must redraw the whole screen
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
    double interval = 1.0 / animation_fps;
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
        double delay = interval - (end-start);
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
    text_rows_ = gr_fb_height() / char_height_;
    text_cols_ = gr_fb_width() / char_width_;
    return true;
}

bool ScreenRecoveryUI::Init(const std::string& locale) {
  RecoveryUI::Init(locale);
  if (!InitTextParams()) {
    return false;
  }

  density_ = static_cast<float>(android::base::GetIntProperty("ro.sf.lcd_density", 160)) / 160.f;

  // Are we portrait or landscape?
  layout_ = (gr_fb_width() > gr_fb_height()) ? LANDSCAPE : PORTRAIT;
  // Are we the large variant of our base layout?
  if (gr_fb_height() > PixelsFromDp(800)) ++layout_;

  text_ = Alloc2d(text_rows_, text_cols_ + 1);
  file_viewer_text_ = Alloc2d(text_rows_, text_cols_ + 1);
  menu_ = Alloc2d(text_rows_, text_cols_ + 1);

  text_col_ = text_row_ = 0;
  text_top_ = 1;

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
        if ((int) (progress * scale) != (int) (fraction * scale)) {
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
                if (text_row_ == text_top_) text_top_ = (text_top_ + 1) % text_rows_;
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

        if (text_row_ == text_top_) text_top_ = (text_top_ + 1) % text_rows_;
    }
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::ClearText() {
    pthread_mutex_lock(&updateMutex);
    text_col_ = 0;
    text_row_ = 0;
    text_top_ = 1;
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
    size_t old_text_top = text_top_;

    // Swap in the alternate screen and clear it.
    text_ = file_viewer_text_;
    ClearText();

    ShowFile(fp);
    fclose(fp);

    text_ = old_text;
    text_col_ = old_text_col;
    text_row_ = old_text_row;
    text_top_ = old_text_top;
}

void ScreenRecoveryUI::StartMenu(const char* const * headers, const char* const * items,
                                 int initial_selection) {
    pthread_mutex_lock(&updateMutex);
    if (text_rows_ > 0 && text_cols_ > 0) {
        menu_headers_ = headers;
        size_t i = 0;
        for (; i < text_rows_ && items[i] != nullptr; ++i) {
            strncpy(menu_[i], items[i], text_cols_ - 1);
            menu_[i][text_cols_ - 1] = '\0';
        }
        menu_items = i;
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
