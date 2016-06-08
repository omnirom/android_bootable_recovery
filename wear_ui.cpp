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

#include <vector>

#include "common.h"
#include "device.h"
#include "wear_ui.h"
#include "cutils/properties.h"
#include "android-base/strings.h"
#include "android-base/stringprintf.h"

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
    progress_bar_height(3),
    progress_bar_width(200),
    progress_bar_y(259),
    outer_height(0),
    outer_width(0),
    menu_unusable_rows(0),
    intro_frames(22),
    loop_frames(60),
    animation_fps(30),
    currentIcon(NONE),
    intro_done(false),
    current_frame(0),
    progressBarType(EMPTY),
    progressScopeStart(0),
    progressScopeSize(0),
    progress(0),
    text_cols(0),
    text_rows(0),
    text_col(0),
    text_row(0),
    text_top(0),
    show_text(false),
    show_text_ever(false),
    show_menu(false),
    menu_items(0),
    menu_sel(0) {

    for (size_t i = 0; i < 5; i++)
        backgroundIcon[i] = NULL;

    self = this;
}

// Draw background frame on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
void WearRecoveryUI::draw_background_locked(Icon icon)
{
    gr_color(0, 0, 0, 255);
    gr_fill(0, 0, gr_fb_width(), gr_fb_height());

    if (icon) {
        GRSurface* surface;
        if (icon == INSTALLING_UPDATE || icon == ERASING) {
            if (!intro_done) {
                surface = introFrames[current_frame];
            } else {
                surface = loopFrames[current_frame];
            }
        }
        else {
            surface = backgroundIcon[icon];
        }

        int width = gr_get_width(surface);
        int height = gr_get_height(surface);

        int x = (gr_fb_width() - width) / 2;
        int y = (gr_fb_height() - height) / 2;

        gr_blit(surface, 0, 0, width, height, x, y);
    }
}

// Draw the progress bar (if any) on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
void WearRecoveryUI::draw_progress_locked()
{
    if (currentIcon == ERROR) return;
    if (progressBarType != DETERMINATE) return;

    int width = progress_bar_width;
    int height = progress_bar_height;
    int dx = (gr_fb_width() - width)/2;
    int dy = progress_bar_y;

    float p = progressScopeStart + progress * progressScopeSize;
    int pos = (int) (p * width);

    gr_color(0x43, 0x43, 0x43, 0xff);
    gr_fill(dx, dy, dx + width, dy + height);

    if (pos > 0) {
        gr_color(0x02, 0xa8, 0xf3, 255);
        if (rtl_locale) {
            // Fill the progress bar from right to left.
            gr_fill(dx + width - pos, dy, dx + width, dy + height);
        } else {
            // Fill the progress bar from left to right.
            gr_fill(dx, dy, dx + pos, dy + height);
        }
    }
}

static const char* HEADERS[] = {
    "Swipe up/down to move.",
    "Swipe left/right to select.",
    "",
    NULL
};

void WearRecoveryUI::draw_screen_locked()
{
    draw_background_locked(currentIcon);
    draw_progress_locked();
    char cur_selection_str[50];

    if (show_text) {
        SetColor(TEXT_FILL);
        gr_fill(0, 0, gr_fb_width(), gr_fb_height());

        int y = outer_height;
        int x = outer_width;
        if (show_menu) {
            char recovery_fingerprint[PROPERTY_VALUE_MAX];
            property_get("ro.bootimage.build.fingerprint", recovery_fingerprint, "");
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
                gr_text(x+4, y, cur_selection_str, 1);
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
                    if (menu[i][0]) gr_text(x+4, y, menu[i], 1);
                    SetColor(MENU);
                } else {
                    if (menu[i][0]) gr_text(x+4, y, menu[i], 0);
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
        int row = (text_top+text_rows-1) % text_rows;
        size_t count = 0;
        for (int ty = gr_fb_height() - char_height_ - outer_height;
             ty > y+2 && count < text_rows;
             ty -= char_height_, ++count) {
            gr_text(x+4, ty, text[row], 0);
            --row;
            if (row < 0) row = text_rows-1;
        }
    }
}

void WearRecoveryUI::update_screen_locked()
{
    draw_screen_locked();
    gr_flip();
}

// Keeps the progress bar updated, even when the process is otherwise busy.
void* WearRecoveryUI::progress_thread(void *cookie) {
    self->progress_loop();
    return NULL;
}

void WearRecoveryUI::progress_loop() {
    double interval = 1.0 / animation_fps;
    for (;;) {
        double start = now();
        pthread_mutex_lock(&updateMutex);
        int redraw = 0;

        if ((currentIcon == INSTALLING_UPDATE || currentIcon == ERASING)
                                                            && !show_text) {
            if (!intro_done) {
                if (current_frame >= intro_frames - 1) {
                    intro_done = true;
                    current_frame = 0;
                } else {
                    current_frame++;
                }
            } else {
                current_frame = (current_frame + 1) % loop_frames;
            }
            redraw = 1;
        }

        // move the progress bar forward on timed intervals, if configured
        int duration = progressScopeDuration;
        if (progressBarType == DETERMINATE && duration > 0) {
            double elapsed = now() - progressScopeTime;
            float p = 1.0 * elapsed / duration;
            if (p > 1.0) p = 1.0;
            if (p > progress) {
                progress = p;
                redraw = 1;
            }
        }

        if (redraw)
            update_screen_locked();

        pthread_mutex_unlock(&updateMutex);
        double end = now();
        // minimum of 20ms delay between frames
        double delay = interval - (end-start);
        if (delay < 0.02) delay = 0.02;
        usleep((long)(delay * 1000000));
    }
}

void WearRecoveryUI::Init()
{
    gr_init();

    gr_font_size(&char_width_, &char_height_);

    text_col = text_row = 0;
    text_rows = (gr_fb_height()) / char_height_;
    visible_text_rows = (gr_fb_height() - (outer_height * 2)) / char_height_;
    if (text_rows > kMaxRows) text_rows = kMaxRows;
    text_top = 1;

    text_cols = (gr_fb_width() - (outer_width * 2)) / char_width_;
    if (text_cols > kMaxCols - 1) text_cols = kMaxCols - 1;

    LoadBitmap("icon_installing", &backgroundIcon[INSTALLING_UPDATE]);
    backgroundIcon[ERASING] = backgroundIcon[INSTALLING_UPDATE];
    LoadBitmap("icon_error", &backgroundIcon[ERROR]);
    backgroundIcon[NO_COMMAND] = backgroundIcon[ERROR];

    introFrames = (GRSurface**)malloc(intro_frames * sizeof(GRSurface*));
    for (int i = 0; i < intro_frames; ++i) {
        char filename[40];
        sprintf(filename, "intro%02d", i);
        LoadBitmap(filename, introFrames + i);
    }

    loopFrames = (GRSurface**)malloc(loop_frames * sizeof(GRSurface*));
    for (int i = 0; i < loop_frames; ++i) {
        char filename[40];
        sprintf(filename, "loop%02d", i);
        LoadBitmap(filename, loopFrames + i);
    }

    pthread_create(&progress_t, NULL, progress_thread, NULL);
    RecoveryUI::Init();
}

void WearRecoveryUI::SetBackground(Icon icon)
{
    pthread_mutex_lock(&updateMutex);
    currentIcon = icon;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void WearRecoveryUI::SetProgressType(ProgressType type)
{
    pthread_mutex_lock(&updateMutex);
    if (progressBarType != type) {
        progressBarType = type;
    }
    progressScopeStart = 0;
    progressScopeSize = 0;
    progress = 0;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void WearRecoveryUI::ShowProgress(float portion, float seconds)
{
    pthread_mutex_lock(&updateMutex);
    progressBarType = DETERMINATE;
    progressScopeStart += progressScopeSize;
    progressScopeSize = portion;
    progressScopeTime = now();
    progressScopeDuration = seconds;
    progress = 0;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void WearRecoveryUI::SetProgress(float fraction)
{
    pthread_mutex_lock(&updateMutex);
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    if (progressBarType == DETERMINATE && fraction > progress) {
        // Skip updates that aren't visibly different.
        int width = progress_bar_width;
        float scale = width * progressScopeSize;
        if ((int) (progress * scale) != (int) (fraction * scale)) {
            progress = fraction;
            update_screen_locked();
        }
    }
    pthread_mutex_unlock(&updateMutex);
}

void WearRecoveryUI::SetStage(int current, int max)
{
}

void WearRecoveryUI::Print(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 256, fmt, ap);
    va_end(ap);

    fputs(buf, stdout);

    // This can get called before ui_init(), so be careful.
    pthread_mutex_lock(&updateMutex);
    if (text_rows > 0 && text_cols > 0) {
        char *ptr;
        for (ptr = buf; *ptr != '\0'; ++ptr) {
            if (*ptr == '\n' || text_col >= text_cols) {
                text[text_row][text_col] = '\0';
                text_col = 0;
                text_row = (text_row + 1) % text_rows;
                if (text_row == text_top) text_top = (text_top + 1) % text_rows;
            }
            if (*ptr != '\n') text[text_row][text_col++] = *ptr;
        }
        text[text_row][text_col] = '\0';
        update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}

void WearRecoveryUI::StartMenu(const char* const * headers, const char* const * items,
                                 int initial_selection) {
    pthread_mutex_lock(&updateMutex);
    if (text_rows > 0 && text_cols > 0) {
        menu_headers_ = headers;
        size_t i = 0;
        // "i < text_rows" is removed from the loop termination condition,
        // which is different from the one in ScreenRecoveryUI::StartMenu().
        // Because WearRecoveryUI supports scrollable menu, it's fine to have
        // more entries than text_rows. The menu may be truncated otherwise.
        // Bug: 23752519
        for (; items[i] != nullptr; i++) {
            strncpy(menu[i], items[i], text_cols - 1);
            menu[i][text_cols - 1] = '\0';
        }
        menu_items = i;
        show_menu = 1;
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
    if (show_menu > 0) {
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

void WearRecoveryUI::EndMenu() {
    int i;
    pthread_mutex_lock(&updateMutex);
    if (show_menu > 0 && text_rows > 0 && text_cols > 0) {
        show_menu = 0;
        update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}

bool WearRecoveryUI::IsTextVisible()
{
    pthread_mutex_lock(&updateMutex);
    int visible = show_text;
    pthread_mutex_unlock(&updateMutex);
    return visible;
}

bool WearRecoveryUI::WasTextEverVisible()
{
    pthread_mutex_lock(&updateMutex);
    int ever_visible = show_text_ever;
    pthread_mutex_unlock(&updateMutex);
    return ever_visible;
}

void WearRecoveryUI::ShowText(bool visible)
{
    pthread_mutex_lock(&updateMutex);
    // Don't show text during ota install or factory reset
    if (currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) {
        pthread_mutex_unlock(&updateMutex);
        return;
    }
    show_text = visible;
    if (show_text) show_text_ever = 1;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void WearRecoveryUI::Redraw()
{
    pthread_mutex_lock(&updateMutex);
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void WearRecoveryUI::ShowFile(FILE* fp) {
    std::vector<long> offsets;
    offsets.push_back(ftell(fp));
    ClearText();

    struct stat sb;
    fstat(fileno(fp), &sb);

    bool show_prompt = false;
    while (true) {
        if (show_prompt) {
            Print("--(%d%% of %d bytes)--",
                  static_cast<int>(100 * (double(ftell(fp)) / double(sb.st_size))),
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
                    offsets.push_back(ftell(fp));
                }
            }
            ClearText();
        }

        int ch = getc(fp);
        if (ch == EOF) {
            text_row = text_top = text_rows - 2;
            show_prompt = true;
        } else {
            PutChar(ch);
            if (text_col == 0 && text_row >= text_rows - 2) {
                text_top = text_row;
                show_prompt = true;
            }
        }
    }
}

void WearRecoveryUI::PutChar(char ch) {
    pthread_mutex_lock(&updateMutex);
    if (ch != '\n') text[text_row][text_col++] = ch;
    if (ch == '\n' || text_col >= text_cols) {
        text_col = 0;
        ++text_row;
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
    text_col = 0;
    text_row = 0;
    text_top = 1;
    for (size_t i = 0; i < text_rows; ++i) {
        memset(text[i], 0, text_cols + 1);
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
    if (text_rows > 0 && text_cols > 0) {
        for (const char* ptr = str.c_str(); *ptr != '\0'; ++ptr) {
            if (*ptr == '\n' || text_col >= text_cols) {
                text[text_row][text_col] = '\0';
                text_col = 0;
                text_row = (text_row + 1) % text_rows;
                if (text_row == text_top) text_top = (text_top + 1) % text_rows;
            }
            if (*ptr != '\n') text[text_row][text_col++] = *ptr;
        }
        text[text_row][text_col] = '\0';
        update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}
