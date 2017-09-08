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

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/stringprintf.h>
#include <minui/minui.h>

#include "common.h"
#include "device.h"

// There's only (at most) one of these objects, and global callbacks
// (for pthread_create, and the input event system) need to find it,
// so use a global variable.
static WearRecoveryUI* self = NULL;

// Return the current time as a double (including fractions of a second).
static double now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

WearRecoveryUI::WearRecoveryUI() :
    progress_bar_y(259),
    outer_height(0),
    outer_width(0),
    menu_unusable_rows(0) {
    intro_frames = 22;
    loop_frames = 60;
    animation_fps = 30;

    for (size_t i = 0; i < 5; i++)
        backgroundIcon[i] = NULL;

    self = this;
}

int WearRecoveryUI::GetProgressBaseline() {
    return progress_bar_y;
}

// Draw background frame on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
// TODO merge drawing routines with screen_ui
void WearRecoveryUI::draw_background_locked()
{
    pagesIdentical = false;
    gr_color(0, 0, 0, 255);
    gr_fill(0, 0, gr_fb_width(), gr_fb_height());

    if (currentIcon != NONE) {
        GRSurface* surface;
        if (currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) {
            if (!intro_done) {
                surface = introFrames[current_frame];
            } else {
                surface = loopFrames[current_frame];
            }
        }
        else {
            surface = backgroundIcon[currentIcon];
        }

        int width = gr_get_width(surface);
        int height = gr_get_height(surface);

        int x = (gr_fb_width() - width) / 2;
        int y = (gr_fb_height() - height) / 2;

        gr_blit(surface, 0, 0, width, height, x, y);
    }
}

static const char* HEADERS[] = {
    "Swipe up/down to move.",
    "Swipe left/right to select.",
    "",
    NULL
};

// TODO merge drawing routines with screen_ui
void WearRecoveryUI::draw_screen_locked()
{
    char cur_selection_str[50];

    draw_background_locked();
    if (!show_text) {
        draw_foreground_locked();
    } else {
        SetColor(TEXT_FILL);
        gr_fill(0, 0, gr_fb_width(), gr_fb_height());

        int y = outer_height;
        int x = outer_width;
        if (show_menu) {
            std::string recovery_fingerprint =
                    android::base::GetProperty("ro.bootimage.build.fingerprint", "");
            SetColor(HEADER);
            DrawTextLine(x + 4, &y, "Android Recovery", true);
            for (auto& chunk: android::base::Split(recovery_fingerprint, ":")) {
                DrawTextLine(x +4, &y, chunk.c_str(), false);
            }

            // This is actually the help strings.
            DrawTextLines(x + 4, &y, HEADERS);
            SetColor(HEADER);
            DrawTextLines(x + 4, &y, menu_headers_);

            // Show the current menu item number in relation to total number if
            // items don't fit on the screen.
            if (menu_items > menu_end - menu_start) {
                sprintf(cur_selection_str, "Current item: %d/%d", menu_sel + 1, menu_items);
                gr_text(gr_sys_font(), x+4, y, cur_selection_str, 1);
                y += char_height_+4;
            }

            // Menu begins here
            SetColor(MENU);

            for (int i = menu_start; i < menu_end; ++i) {

                if (i == menu_sel) {
                    // draw the highlight bar
                    SetColor(MENU_SEL_BG);
                    gr_fill(x, y-2, gr_fb_width()-x, y+char_height_+2);
                    // white text of selected item
                    SetColor(MENU_SEL_FG);
                    if (menu_[i][0]) {
                        gr_text(gr_sys_font(), x + 4, y, menu_[i], 1);
                    }
                    SetColor(MENU);
                } else if (menu_[i][0]) {
                    gr_text(gr_sys_font(), x + 4, y, menu_[i], 0);
                }
                y += char_height_+4;
            }
            SetColor(MENU);
            y += 4;
            gr_fill(0, y, gr_fb_width(), y+2);
            y += 4;
        }

        SetColor(LOG);

        // display from the bottom up, until we hit the top of the
        // screen, the bottom of the menu, or we've displayed the
        // entire text buffer.
        int ty;
        int row = (text_top_ + text_rows_ - 1) % text_rows_;
        size_t count = 0;
        for (int ty = gr_fb_height() - char_height_ - outer_height;
             ty > y + 2 && count < text_rows_;
             ty -= char_height_, ++count) {
            gr_text(gr_sys_font(), x+4, ty, text_[row], 0);
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

bool WearRecoveryUI::InitTextParams() {
    if (!ScreenRecoveryUI::InitTextParams()) {
        return false;
    }

    text_cols_ = (gr_fb_width() - (outer_width * 2)) / char_width_;

    if (text_rows_ > kMaxRows) text_rows_ = kMaxRows;
    if (text_cols_ > kMaxCols) text_cols_ = kMaxCols;

    visible_text_rows = (gr_fb_height() - (outer_height * 2)) / char_height_;
    return true;
}

bool WearRecoveryUI::Init(const std::string& locale) {
  if (!ScreenRecoveryUI::Init(locale)) {
    return false;
  }

  LoadBitmap("icon_error", &backgroundIcon[ERROR]);
  backgroundIcon[NO_COMMAND] = backgroundIcon[ERROR];

  // This leaves backgroundIcon[INSTALLING_UPDATE] and backgroundIcon[ERASING]
  // as NULL which is fine since draw_background_locked() doesn't use them.

  return true;
}

void WearRecoveryUI::SetStage(int current, int max) {}

void WearRecoveryUI::Print(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, 256, fmt, ap);
  va_end(ap);

  fputs(buf, stdout);

  // This can get called before ui_init(), so be careful.
  pthread_mutex_lock(&updateMutex);
  if (text_rows_ > 0 && text_cols_ > 0) {
    char* ptr;
    for (ptr = buf; *ptr != '\0'; ++ptr) {
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

void WearRecoveryUI::StartMenu(const char* const * headers, const char* const * items,
                               int initial_selection) {
    pthread_mutex_lock(&updateMutex);
    if (text_rows_ > 0 && text_cols_ > 0) {
        menu_headers_ = headers;
        size_t i = 0;
        // "i < text_rows_" is removed from the loop termination condition,
        // which is different from the one in ScreenRecoveryUI::StartMenu().
        // Because WearRecoveryUI supports scrollable menu, it's fine to have
        // more entries than text_rows_. The menu may be truncated otherwise.
        // Bug: 23752519
        for (; items[i] != nullptr; i++) {
            strncpy(menu_[i], items[i], text_cols_ - 1);
            menu_[i][text_cols_ - 1] = '\0';
        }
        menu_items = i;
        show_menu = true;
        menu_sel = initial_selection;
        menu_start = 0;
        menu_end = visible_text_rows - 1 - menu_unusable_rows;
        if (menu_items <= menu_end)
          menu_end = menu_items;
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
        if (menu_sel >= menu_items) menu_sel = menu_items-1;
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

void WearRecoveryUI::ShowFile(FILE* fp) {
    std::vector<off_t> offsets;
    offsets.push_back(ftello(fp));
    ClearText();

    struct stat sb;
    fstat(fileno(fp), &sb);

    bool show_prompt = false;
    while (true) {
        if (show_prompt) {
            Print("--(%d%% of %d bytes)--",
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
            text_row_ = text_top_ = text_rows_ - 2;
            show_prompt = true;
        } else {
            PutChar(ch);
            if (text_col_ == 0 && text_row_ >= text_rows_ - 2) {
                text_top_ = text_row_;
                show_prompt = true;
            }
        }
    }
}

void WearRecoveryUI::PutChar(char ch) {
    pthread_mutex_lock(&updateMutex);
    if (ch != '\n') text_[text_row_][text_col_++] = ch;
    if (ch == '\n' || text_col_ >= text_cols_) {
        text_col_ = 0;
        ++text_row_;
    }
    pthread_mutex_unlock(&updateMutex);
}

void WearRecoveryUI::ShowFile(const char* filename) {
    FILE* fp = fopen_path(filename, "re");
    if (fp == nullptr) {
        Print("  Unable to open %s: %s\n", filename, strerror(errno));
        return;
    }
    ShowFile(fp);
    fclose(fp);
}

void WearRecoveryUI::ClearText() {
    pthread_mutex_lock(&updateMutex);
    text_col_ = 0;
    text_row_ = 0;
    text_top_ = 1;
    for (size_t i = 0; i < text_rows_; ++i) {
        memset(text_[i], 0, text_cols_ + 1);
    }
    pthread_mutex_unlock(&updateMutex);
}

void WearRecoveryUI::PrintOnScreenOnly(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    PrintV(fmt, false, ap);
    va_end(ap);
}

void WearRecoveryUI::PrintV(const char* fmt, bool copy_to_stdout, va_list ap) {
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
