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

#include <vector>

#include "base/strings.h"
#include "cutils/properties.h"
#include "common.h"
#include "device.h"
#include "minui/minui.h"
#include "screen_ui.h"
#include "ui.h"

static int char_width;
static int char_height;

// Return the current time as a double (including fractions of a second).
static double now() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

ScreenRecoveryUI::ScreenRecoveryUI() :
    currentIcon(NONE),
    installingFrame(0),
    locale(nullptr),
    rtl_locale(false),
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
    animation_fps(20),
    installing_frames(-1),
    stage(-1),
    max_stage(-1) {

    for (int i = 0; i < 5; i++) {
        backgroundIcon[i] = nullptr;
    }
    pthread_mutex_init(&updateMutex, nullptr);
}

// Clear the screen and draw the currently selected background icon (if any).
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_background_locked(Icon icon) {
    pagesIdentical = false;
    gr_color(0, 0, 0, 255);
    gr_clear();

    if (icon) {
        GRSurface* surface = backgroundIcon[icon];
        if (icon == INSTALLING_UPDATE || icon == ERASING) {
            surface = installation[installingFrame];
        }
        GRSurface* text_surface = backgroundText[icon];

        int iconWidth = gr_get_width(surface);
        int iconHeight = gr_get_height(surface);
        int textWidth = gr_get_width(text_surface);
        int textHeight = gr_get_height(text_surface);
        int stageHeight = gr_get_height(stageMarkerEmpty);

        int sh = (max_stage >= 0) ? stageHeight : 0;

        iconX = (gr_fb_width() - iconWidth) / 2;
        iconY = (gr_fb_height() - (iconHeight+textHeight+40+sh)) / 2;

        int textX = (gr_fb_width() - textWidth) / 2;
        int textY = ((gr_fb_height() - (iconHeight+textHeight+40+sh)) / 2) + iconHeight + 40;

        gr_blit(surface, 0, 0, iconWidth, iconHeight, iconX, iconY);
        if (stageHeight > 0) {
            int sw = gr_get_width(stageMarkerEmpty);
            int x = (gr_fb_width() - max_stage * gr_get_width(stageMarkerEmpty)) / 2;
            int y = iconY + iconHeight + 20;
            for (int i = 0; i < max_stage; ++i) {
                gr_blit((i < stage) ? stageMarkerFill : stageMarkerEmpty,
                        0, 0, sw, stageHeight, x, y);
                x += sw;
            }
        }

        gr_color(255, 255, 255, 255);
        gr_texticon(textX, textY, text_surface);
    }
}

// Draw the progress bar (if any) on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_progress_locked() {
    if (currentIcon == ERROR) return;

    if (currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) {
        GRSurface* icon = installation[installingFrame];
        gr_blit(icon, 0, 0, gr_get_width(icon), gr_get_height(icon), iconX, iconY);
    }

    if (progressBarType != EMPTY) {
        int iconHeight = gr_get_height(backgroundIcon[INSTALLING_UPDATE]);
        int width = gr_get_width(progressBarEmpty);
        int height = gr_get_height(progressBarEmpty);

        int dx = (gr_fb_width() - width)/2;
        int dy = (3*gr_fb_height() + iconHeight - 2*height)/4;

        // Erase behind the progress bar (in case this was a progress-only update)
        gr_color(0, 0, 0, 255);
        gr_fill(dx, dy, width, height);

        if (progressBarType == DETERMINATE) {
            float p = progressScopeStart + progress * progressScopeSize;
            int pos = (int) (p * width);

            if (rtl_locale) {
                // Fill the progress bar from right to left.
                if (pos > 0) {
                    gr_blit(progressBarFill, width-pos, 0, pos, height, dx+width-pos, dy);
                }
                if (pos < width-1) {
                    gr_blit(progressBarEmpty, 0, 0, width-pos, height, dx, dy);
                }
            } else {
                // Fill the progress bar from left to right.
                if (pos > 0) {
                    gr_blit(progressBarFill, 0, 0, pos, height, dx, dy);
                }
                if (pos < width-1) {
                    gr_blit(progressBarEmpty, pos, 0, width-pos, height, dx+pos, dy);
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

void ScreenRecoveryUI::DrawTextLine(int* y, const char* line, bool bold) {
    gr_text(4, *y, line, bold);
    *y += char_height + 4;
}

void ScreenRecoveryUI::DrawTextLines(int* y, const char* const* lines) {
    for (size_t i = 0; lines != nullptr && lines[i] != nullptr; ++i) {
        DrawTextLine(y, lines[i], false);
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
        draw_background_locked(currentIcon);
        draw_progress_locked();
    } else {
        gr_color(0, 0, 0, 255);
        gr_clear();

        int y = 0;
        if (show_menu) {
            char recovery_fingerprint[PROPERTY_VALUE_MAX];
            property_get("ro.bootimage.build.fingerprint", recovery_fingerprint, "");

            SetColor(INFO);
            DrawTextLine(&y, "Android Recovery", true);
            for (auto& chunk : android::base::Split(recovery_fingerprint, ":")) {
                DrawTextLine(&y, chunk.c_str(), false);
            }
            DrawTextLines(&y, HasThreeButtons() ? REGULAR_HELP : LONG_PRESS_HELP);

            SetColor(HEADER);
            DrawTextLines(&y, menu_headers_);

            SetColor(MENU);
            DrawHorizontalRule(&y);
            y += 4;
            for (int i = 0; i < menu_items; ++i) {
                if (i == menu_sel) {
                    // Draw the highlight bar.
                    SetColor(IsLongPress() ? MENU_SEL_BG_ACTIVE : MENU_SEL_BG);
                    gr_fill(0, y - 2, gr_fb_width(), y + char_height + 2);
                    // Bold white text for the selected item.
                    SetColor(MENU_SEL_FG);
                    gr_text(4, y, menu_[i], true);
                    SetColor(MENU);
                } else {
                    gr_text(4, y, menu_[i], false);
                }
                y += char_height + 4;
            }
            DrawHorizontalRule(&y);
        }

        // display from the bottom up, until we hit the top of the
        // screen, the bottom of the menu, or we've displayed the
        // entire text buffer.
        SetColor(LOG);
        int row = (text_top_ + text_rows_ - 1) % text_rows_;
        size_t count = 0;
        for (int ty = gr_fb_height() - char_height;
             ty >= y && count < text_rows_;
             ty -= char_height, ++count) {
            gr_text(0, ty, text_[row], false);
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
        draw_progress_locked();  // Draw only the progress bar and overlays
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

        int redraw = 0;

        // update the installation animation, if active
        // skip this if we have a text overlay (too expensive to update)
        if ((currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) &&
            installing_frames > 0 && !show_text) {
            installingFrame = (installingFrame + 1) % installing_frames;
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

        if (redraw) update_progress_locked();

        pthread_mutex_unlock(&updateMutex);
        double end = now();
        // minimum of 20ms delay between frames
        double delay = interval - (end-start);
        if (delay < 0.02) delay = 0.02;
        usleep((long)(delay * 1000000));
    }
}

void ScreenRecoveryUI::LoadBitmap(const char* filename, GRSurface** surface) {
    int result = res_create_display_surface(filename, surface);
    if (result < 0) {
        LOGE("missing bitmap %s\n(Code %d)\n", filename, result);
    }
}

void ScreenRecoveryUI::LoadBitmapArray(const char* filename, int* frames, GRSurface*** surface) {
    int result = res_create_multi_display_surface(filename, frames, surface);
    if (result < 0) {
        LOGE("missing bitmap %s\n(Code %d)\n", filename, result);
    }
}

void ScreenRecoveryUI::LoadLocalizedBitmap(const char* filename, GRSurface** surface) {
    int result = res_create_localized_alpha_surface(filename, locale, surface);
    if (result < 0) {
        LOGE("missing bitmap %s\n(Code %d)\n", filename, result);
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

void ScreenRecoveryUI::Init() {
    gr_init();

    gr_font_size(&char_width, &char_height);
    text_rows_ = gr_fb_height() / char_height;
    text_cols_ = gr_fb_width() / char_width;

    text_ = Alloc2d(text_rows_, text_cols_ + 1);
    file_viewer_text_ = Alloc2d(text_rows_, text_cols_ + 1);
    menu_ = Alloc2d(text_rows_, text_cols_ + 1);

    text_col_ = text_row_ = 0;
    text_top_ = 1;

    backgroundIcon[NONE] = nullptr;
    LoadBitmapArray("icon_installing", &installing_frames, &installation);
    backgroundIcon[INSTALLING_UPDATE] = installing_frames ? installation[0] : nullptr;
    backgroundIcon[ERASING] = backgroundIcon[INSTALLING_UPDATE];
    LoadBitmap("icon_error", &backgroundIcon[ERROR]);
    backgroundIcon[NO_COMMAND] = backgroundIcon[ERROR];

    LoadBitmap("progress_empty", &progressBarEmpty);
    LoadBitmap("progress_fill", &progressBarFill);
    LoadBitmap("stage_empty", &stageMarkerEmpty);
    LoadBitmap("stage_fill", &stageMarkerFill);

    LoadLocalizedBitmap("installing_text", &backgroundText[INSTALLING_UPDATE]);
    LoadLocalizedBitmap("erasing_text", &backgroundText[ERASING]);
    LoadLocalizedBitmap("no_command_text", &backgroundText[NO_COMMAND]);
    LoadLocalizedBitmap("error_text", &backgroundText[ERROR]);

    pthread_create(&progress_thread_, nullptr, ProgressThreadStartRoutine, this);

    RecoveryUI::Init();
}

void ScreenRecoveryUI::SetLocale(const char* new_locale) {
    if (new_locale) {
        this->locale = new_locale;
        char* lang = strdup(locale);
        for (char* p = lang; *p; ++p) {
            if (*p == '_') {
                *p = '\0';
                break;
            }
        }

        // A bit cheesy: keep an explicit list of supported languages
        // that are RTL.
        if (strcmp(lang, "ar") == 0 ||   // Arabic
            strcmp(lang, "fa") == 0 ||   // Persian (Farsi)
            strcmp(lang, "he") == 0 ||   // Hebrew (new language code)
            strcmp(lang, "iw") == 0 ||   // Hebrew (old language code)
            strcmp(lang, "ur") == 0) {   // Urdu
            rtl_locale = true;
        }
        free(lang);
    } else {
        new_locale = nullptr;
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

void ScreenRecoveryUI::Print(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 256, fmt, ap);
    va_end(ap);

    fputs(buf, stdout);

    pthread_mutex_lock(&updateMutex);
    if (text_rows_ > 0 && text_cols_ > 0) {
        for (const char* ptr = buf; *ptr != '\0'; ++ptr) {
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
