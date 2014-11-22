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

#include "common.h"
#include "device.h"
#include "minui/minui.h"
#include "screen_ui.h"
#include "ui.h"

// There's only (at most) one of these objects, and global callbacks
// (for pthread_create, and the input event system) need to find it,
// so use a global variable.
static ScreenRecoveryUI* self = NULL;

// Return the current time as a double (including fractions of a second).
static double now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

ScreenRecoveryUI::ScreenRecoveryUI() :
    currentIcon(NONE),
    installingFrame(0),
    locale(NULL),
    rtl_locale(false),
    progressBarType(EMPTY),
    progressScopeStart(0),
    progressScopeSize(0),
    progress(0),
    pagesIdentical(false),
    log_text_cols(0),
    log_text_rows(0),
    text_cols(0),
    text_rows(0),
    text_col(0),
    text_row(0),
    text_top(0),
    show_text(false),
    show_text_ever(false),
    dialog_icon(NONE),
    dialog_text(NULL),
    dialog_show_log(false),
    show_menu(false),
    menu_items(0),
    menu_sel(0),
    menu_show_start(0),
    max_menu_rows(0),
    animation_fps(20),
    installing_frames(-1),
    stage(-1),
    max_stage(-1) {

    headerIcon = NULL;
    for (int i = 0; i < NR_ICONS; i++)
        backgroundIcon[i] = NULL;

    memset(text, 0, sizeof(text));

    pthread_mutex_init(&updateMutex, NULL);
    self = this;
}

// Clear the screen and draw the currently selected background icon (if any).
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_background_locked(Icon icon)
{
    pagesIdentical = false;
    SetColor(TEXT_FILL);
    gr_clear();

    if (icon) {
        gr_surface surface = backgroundIcon[icon];
        if (icon == INSTALLING_UPDATE || icon == ERASING) {
            surface = installation[installingFrame];
        }
        gr_surface text_surface = backgroundText[icon];

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
void ScreenRecoveryUI::draw_progress_locked()
{
    if (currentIcon == ERROR) return;

    gr_color(0, 0, 0, 255);
    gr_clear();

    if (currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) {
        gr_surface icon = installation[installingFrame];
        gr_blit(icon, 0, 0, gr_get_width(icon), gr_get_height(icon), iconX, iconY);
    }

    if (progressBarType != EMPTY) {
        int iconHeight = gr_get_height(backgroundIcon[INSTALLING_UPDATE]);
        int width = gr_get_width(progressBarEmpty);
        int height = gr_get_height(progressBarEmpty);

        int dx = (gr_fb_width() - width)/2;
        int dy = (3*gr_fb_height() + iconHeight - 2*height)/4;

        // Erase behind the progress bar (in case this was a progress-only update)
        SetColor(TEXT_FILL);
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
        case HEADER:
            gr_color(111,111,111,255);
            break;
        case TOP:
            gr_color(208, 208, 208, 255);
            break;
        case MENU:
        case MENU_SEL_FG:
            gr_color(0, 177, 229, 255);
            break;
        case MENU_SEL_BG:
            gr_color(106, 103, 102, 255);
            break;
        case LOG:
            gr_color(76, 76, 76, 255);
            break;
        case TEXT_FILL:
            gr_color(0, 0, 0, 255);
            break;
        case ERROR_TEXT:
            gr_color(255, 0, 0, 255);
            break;
        default:
            gr_color(255, 255, 255, 255);
            break;
    }
}

int ScreenRecoveryUI::draw_header_icon()
{
    gr_surface surface = headerIcon;
    int iw = header_width;
    int ih = header_height;
    int ix = (gr_fb_width() - iw) / 2;
    int iy = 0;
    gr_blit(surface, 0, 0, iw, ih, ix, iy);
    return ih;
}

void ScreenRecoveryUI::draw_menu_item(int textrow, const char *text, int selected)
{
    if (selected) {
        SetColor(MENU_SEL_BG);
        gr_fill(0, (textrow)*char_height,
                gr_fb_width(), (textrow+3)*char_height-1);
        SetColor(MENU_SEL_FG);
        gr_text(4, (textrow+1)*char_height-1, text, 0);
        SetColor(MENU);
    }
    else {
        SetColor(MENU);
        gr_text(4, (textrow+1)*char_height-1, text, 0);
    }
}

void ScreenRecoveryUI::draw_dialog()
{
    int x, y, w, h;

    if (dialog_show_log) {
        draw_background_locked(NONE);
    }
    else {
        draw_background_locked(dialog_icon);
    }
    draw_header_icon();

    int iconHeight = gr_get_height(backgroundIcon[dialog_icon]);

    x = (gr_fb_width()/2 - (char_width*strlen(dialog_text))/2);
    if (dialog_show_log) {
        y = gr_get_height(headerIcon) + char_height;
    }
    else {
        y = (gr_fb_height()/2 + iconHeight/2);
    }

    SetColor(ERROR_TEXT);
    gr_text(x, y, dialog_text, 0);
    y += char_height+2;

    if (dialog_show_log) {
        int cx, cy;
        gr_set_font("log");
        gr_font_size(&cx, &cy);

        int row;
        for (row = 0; row < log_text_rows; ++row) {
            gr_text(2, y, text[row], 0);
            y += cy+2;
        }
        gr_set_font("menu");
    }

    if (dialog_icon == ERROR) {
        /*
         * This could be improved...
         *
         * Draw rect around text "Okay".
         * Text is centered horizontally.
         * Bottom of text is 4 lines from bottom of screen.
         * Rect width 4px
         * Rect padding 8px
         */
        w = char_width*4;
        h = char_height;
        x = gr_fb_width()/2 - w/2;
        y = gr_fb_height() - h - 4*char_height;
        SetColor(HEADER);
        gr_fill(x-(4+8), y-(4+8), x+w+(4+8), y+h+(4+8));
        SetColor(MENU_SEL_BG);
        gr_fill(x-8, y-8, x+w+8, y+h+8);
        SetColor(MENU_SEL_FG);
        gr_text(x, y, "Okay", 0);
    }
}

// Redraw everything on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_screen_locked()
{
    if (!show_text) {
        draw_background_locked(currentIcon);
        draw_progress_locked();
    } else {

        if (DialogShowing()) {
            draw_dialog();
            return;
        }
        gr_color(0, 0, 0, 255);
        gr_clear();

        if (currentIcon == INSTALLING_UPDATE || currentIcon == VIEWING_LOG) {
            int y = header_height + 4;

            draw_background_locked(currentIcon);

            SetColor(LOG);
            int cx, cy;
            gr_set_font("log");
            gr_font_size(&cx, &cy);
            // display from the bottom up, until we hit the top of the
            // screen or we've displayed the entire text buffer.
            int ty;
            int row = (text_first_row+log_text_rows-1) % log_text_rows;
            for (int ty = gr_fb_height() - cy, count = 0;
                 ty > y+2 && count < log_text_rows;
                 ty -= (cy+2), ++count) {
                gr_text(4, ty, text[row], 0);
                --row;
                if (row < 0) row = log_text_rows-1;
            }
            gr_set_font("menu");
            return;
        }

        if (show_menu) {
            draw_header_icon();
            int nr_items = menu_items - menu_show_start;
            if (nr_items > max_menu_rows)
                nr_items = max_menu_rows;
            for (int i = 0; i < nr_items; ++i) {
                draw_menu_item(text_first_row + 3*i, menu[menu_show_start+i],
                        ((menu_show_start+i) == menu_sel));
            }
        }
    }
}

// Redraw everything on the screen and flip the screen (make it visible).
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::update_screen_locked()
{
    draw_screen_locked();
    LOGV("%s: flip %p\n", __func__, __builtin_return_address(0));
    gr_flip();
}

// Updates only the progress bar, if possible, otherwise redraws the screen.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::update_progress_locked()
{
    draw_progress_locked();
    LOGV("%s: flip %p\n", __func__, __builtin_return_address(0));
    gr_flip();
}

// Keeps the progress bar updated, even when the process is otherwise busy.
void* ScreenRecoveryUI::progress_thread(void *cookie) {
    self->progress_loop();
    return NULL;
}

void ScreenRecoveryUI::progress_loop() {
    double interval = 1.0 / animation_fps;
    for (;;) {
        double start = now();
        pthread_mutex_lock(&updateMutex);

        int redraw = 0;

        // update the installation animation, if active
        // skip this if we have a text overlay (too expensive to update)
        if ((currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) &&
            installing_frames > 0) {
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

        if (progressBarType == EMPTY)
            break;

        double end = now();
        // minimum of 20ms delay between frames
        double delay = interval - (end-start);
        if (delay < 0.02) delay = 0.02;
        usleep((long)(delay * 1000000));

    }
}

void ScreenRecoveryUI::LoadBitmap(const char* filename, gr_surface* surface) {
    int result = res_create_display_surface(filename, surface);
    if (result < 0) {
        LOGE("missing bitmap %s\n(Code %d)\n", filename, result);
    }
}

void ScreenRecoveryUI::LoadBitmapArray(const char* filename, int* frames, gr_surface** surface) {
    int result = res_create_multi_display_surface(filename, frames, surface);
    if (result < 0) {
        LOGE("missing bitmap %s\n(Code %d)\n", filename, result);
    }
}

void ScreenRecoveryUI::LoadLocalizedBitmap(const char* filename, gr_surface* surface) {
    int result = res_create_localized_alpha_surface(filename, locale, surface);
    if (result < 0) {
        LOGE("missing bitmap %s\n(Code %d)\n", filename, result);
    }
}

void ScreenRecoveryUI::Init()
{
    gr_init();

    gr_set_font("log");
    gr_font_size(&log_char_width, &log_char_height);
    gr_set_font("menu");
    gr_font_size(&char_width, &char_height);

    log_text_rows = gr_fb_height() / log_char_height;
    log_text_cols = gr_fb_width() / log_char_width;

    text_col = text_row = 0;
    text_rows = gr_fb_height() / char_height;
    if (max_menu_rows > kMaxMenuRows)
        max_menu_rows = kMaxMenuRows;
    if (text_rows > kMaxRows) text_rows = kMaxRows;
    text_top = 1;

    text_cols = gr_fb_width() / char_width;
    if (text_cols > kMaxCols - 1) text_cols = kMaxCols - 1;

    LoadBitmap("icon_header", &headerIcon);
    header_height = gr_get_height(headerIcon);
    header_width = gr_get_width(headerIcon);

    text_first_row = (header_height / char_height) + 1;
    menu_item_start = text_first_row * char_height;
    max_menu_rows = (text_rows - text_first_row) / 3;

    backgroundIcon[NONE] = NULL;
    LoadBitmapArray("icon_installing", &installing_frames, &installation);
    backgroundIcon[INSTALLING_UPDATE] = installing_frames ? installation[0] : NULL;
    backgroundIcon[ERASING] = backgroundIcon[INSTALLING_UPDATE];
    LoadBitmap("icon_info", &backgroundIcon[INFO]);
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
        new_locale = NULL;
    }
}

void ScreenRecoveryUI::SetBackground(Icon icon)
{
    pthread_mutex_lock(&updateMutex);

    currentIcon = icon;
    update_screen_locked();

    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetProgressType(ProgressType type)
{
    pthread_mutex_lock(&updateMutex);
    if (progressBarType != type) {
        progressBarType = type;
        if (progressBarType != EMPTY) {
            pthread_create(&progress_t, NULL, progress_thread, NULL);
        }
    }
    progressScopeStart = 0;
    progressScopeSize = 0;
    progress = 0;
    update_progress_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::ShowProgress(float portion, float seconds)
{
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

void ScreenRecoveryUI::SetProgress(float fraction)
{
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

void ScreenRecoveryUI::Print(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 256, fmt, ap);
    va_end(ap);

    fputs(buf, stdout);

    // This can get called before ui_init(), so be careful.
    pthread_mutex_lock(&updateMutex);
    if (log_text_rows > 0 && log_text_cols > 0) {
        char *ptr;
        for (ptr = buf; *ptr != '\0'; ++ptr) {
            if (*ptr == '\n' || text_col >= log_text_cols) {
                text[text_row][text_col] = '\0';
                text_col = 0;
                text_row = (text_row + 1) % log_text_rows;
                if (text_row == text_top) text_top = (text_top + 1) % log_text_rows;
            }
            if (*ptr != '\n') text[text_row][text_col++] = *ptr;
        }
        text[text_row][text_col] = '\0';
        update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::ClearLog()
{
    memset(text, 0, sizeof(text));
    text_col = text_row = 0;
}

void ScreenRecoveryUI::DialogShowInfo(const char* text)
{
    pthread_mutex_lock(&updateMutex);
    free(dialog_text);
    dialog_text = strdup(text);
    dialog_show_log = false;
    dialog_icon = INFO;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::DialogShowError(const char* text)
{
    pthread_mutex_lock(&updateMutex);
    free(dialog_text);
    dialog_text = strdup(text);
    dialog_show_log = false;
    dialog_icon = ERROR;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::DialogShowErrorLog(const char* text)
{
    pthread_mutex_lock(&updateMutex);
    free(dialog_text);
    dialog_text = strdup(text);
    dialog_show_log = true;
    dialog_icon = ERROR;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::DialogDismiss()
{
    pthread_mutex_lock(&updateMutex);
    free(dialog_text);
    dialog_text = NULL;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::StartMenu(const char* const * headers, const char* const * items,
                                 int initial_selection) {
    int i = 0;
    pthread_mutex_lock(&updateMutex);
    if (text_rows > 0 && text_cols > 0) {
        for (; i < kMaxMenuRows; ++i) {
            if (items[i] == NULL) break;
            strncpy(menu[i], items[i], text_cols-1);
            menu[i][text_cols-1] = '\0';
        }
        menu_items = i;
        show_menu = 1;
        menu_sel = initial_selection;
        if (menu_show_start <= menu_sel - max_menu_rows ||
                menu_show_start > menu_sel) {
            menu_show_start = menu_sel;
        }
        update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}

int ScreenRecoveryUI::SelectMenu(int sel, bool abs) {
    int old_sel;
    pthread_mutex_lock(&updateMutex);
    if (abs) {
        sel += menu_show_start;
    }
    if (show_menu > 0) {
        old_sel = menu_sel;
        menu_sel = sel;
        if (menu_sel < 0) menu_sel = menu_items + menu_sel;
        if (menu_sel >= menu_items) menu_sel = menu_sel - menu_items;
        if (menu_sel < menu_show_start && menu_show_start > 0) {
            menu_show_start = menu_sel;
        }
        if (menu_sel - menu_show_start >= max_menu_rows) {
            menu_show_start = menu_sel - max_menu_rows + 1;
        }
        sel = menu_sel;
        if (menu_sel != old_sel) update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
    return sel;
}

void ScreenRecoveryUI::EndMenu() {
    int i;
    pthread_mutex_lock(&updateMutex);
    if (show_menu > 0 && text_rows > 0 && text_cols > 0) {
        show_menu = 0;
    }
    pthread_mutex_unlock(&updateMutex);
}

bool ScreenRecoveryUI::IsTextVisible()
{
    pthread_mutex_lock(&updateMutex);
    int visible = show_text;
    pthread_mutex_unlock(&updateMutex);
    return visible;
}

bool ScreenRecoveryUI::WasTextEverVisible()
{
    pthread_mutex_lock(&updateMutex);
    int ever_visible = show_text_ever;
    pthread_mutex_unlock(&updateMutex);
    return ever_visible;
}

void ScreenRecoveryUI::ShowText(bool visible)
{
    pthread_mutex_lock(&updateMutex);
    show_text = visible;
    if (show_text) show_text_ever = 1;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::Redraw()
{
    pthread_mutex_lock(&updateMutex);
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}
