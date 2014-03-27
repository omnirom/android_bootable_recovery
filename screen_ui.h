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

#ifndef RECOVERY_SCREEN_UI_H
#define RECOVERY_SCREEN_UI_H

#include <pthread.h>

#include "ui.h"
#include "minui/minui.h"

// Implementation of RecoveryUI appropriate for devices with a screen
// (shows an icon + a progress bar, text logging, menu, etc.)
class ScreenRecoveryUI : public RecoveryUI {
  public:
    ScreenRecoveryUI();

    void Init();
    void SetLocale(const char* locale);

    // overall recovery state ("background image")
    void SetBackground(Icon icon);

    // progress indicator
    void SetProgressType(ProgressType type);
    void ShowProgress(float portion, float seconds);
    void SetProgress(float fraction);

    void SetStage(int current, int max);

    // text log
    void ShowText(bool visible);
    bool IsTextVisible();
    bool WasTextEverVisible();

    // printing messages
    void Print(const char* fmt, ...); // __attribute__((format(printf, 1, 2)));
    void ClearLog();
    void DialogShowInfo(const char* text);
    void DialogShowError(const char* text);
    void DialogShowErrorLog(const char* text);
    int  DialogShowing() const { return (dialog_text != NULL); }
    bool DialogDismissable() const { return (dialog_icon == ERROR); }
    void DialogDismiss();

    // menu display
    virtual int MenuItemStart() const { return menu_item_start; }
    virtual int MenuItemHeight() const { return 3*char_height; }
    void StartMenu(const char* const * headers, const char* const * items,
                           int initial_selection);
    int SelectMenu(int sel, bool abs = false);
    void EndMenu();

    void Redraw();

    enum UIElement { HEADER, MENU, TOP, MENU_SEL_BG, MENU_SEL_FG, LOG, TEXT_FILL, ERROR_TEXT };
    virtual void SetColor(UIElement e);

  private:
    Icon currentIcon;
    int installingFrame;
    const char* locale;
    bool rtl_locale;

    pthread_mutex_t updateMutex;
    gr_surface headerIcon;
    gr_surface backgroundIcon[NR_ICONS];
    gr_surface backgroundText[NR_ICONS];
    gr_surface *installation;
    gr_surface progressBarEmpty;
    gr_surface progressBarFill;
    gr_surface stageMarkerEmpty;
    gr_surface stageMarkerFill;

    ProgressType progressBarType;

    float progressScopeStart, progressScopeSize, progress;
    double progressScopeTime, progressScopeDuration;

    // true when both graphics pages are the same (except for the
    // progress bar)
    bool pagesIdentical;

    static const int kMaxCols = 96;
    static const int kMaxRows = 96;

    static const int kMaxMenuCols = 96;
    static const int kMaxMenuRows = 250;

    // Log text overlay, displayed when a magic key is pressed
    char text[kMaxRows][kMaxCols];
    int log_text_cols, log_text_rows;
    int text_cols, text_rows;
    int text_col, text_row, text_top;
    bool show_text;
    bool show_text_ever;   // has show_text ever been true?

    Icon dialog_icon;
    char *dialog_text;
    bool dialog_show_log;

    char menu[kMaxMenuRows][kMaxMenuCols];
    bool show_menu;
    int menu_items, menu_sel;
    int menu_show_start;
    int max_menu_rows;

    int menu_item_start;

    pthread_t progress_t;

    int animation_fps;
    int installing_frames;
  protected:
  private:

    int iconX, iconY;

    int stage, max_stage;

    int log_char_height, log_char_width;
    int char_height, char_width;

    int header_height;
    int header_width;
    int text_first_row;

    void draw_background_locked(Icon icon);
    void draw_progress_locked();
    int  draw_header_icon();
    void draw_menu_item(int textrow, const char *text, int selected);
    void draw_dialog();
    void draw_screen_locked();
    void update_screen_locked();
    void update_progress_locked();
    static void* progress_thread(void* cookie);
    void progress_loop();

    void LoadBitmap(const char* filename, gr_surface* surface);
    void LoadBitmapArray(const char* filename, int* frames, gr_surface** surface);
    void LoadLocalizedBitmap(const char* filename, gr_surface* surface);
};

#endif  // RECOVERY_UI_H
