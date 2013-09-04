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

    // text log
    void ShowText(bool visible);
    bool IsTextVisible();
    bool WasTextEverVisible();

    // printing messages
    void Print(const char* fmt, ...); // __attribute__((format(printf, 1, 2)));

    // menu display
    void StartMenu(const char* const * headers, const char* const * items,
                           int initial_selection);
    int SelectMenu(int sel);
    void EndMenu();

  private:
    Icon currentIcon;
    int installingFrame;
    bool rtl_locale;

    pthread_mutex_t updateMutex;
    gr_surface backgroundIcon[5];
    gr_surface backgroundText[5];
    gr_surface *installationOverlay;
    gr_surface *progressBarIndeterminate;
    gr_surface progressBarEmpty;
    gr_surface progressBarFill;

    ProgressType progressBarType;

    float progressScopeStart, progressScopeSize, progress;
    double progressScopeTime, progressScopeDuration;

    // true when both graphics pages are the same (except for the
    // progress bar)
    bool pagesIdentical;

    static const int kMaxCols = 96;
    static const int kMaxRows = 96;

    // Log text overlay, displayed when a magic key is pressed
    char text[kMaxRows][kMaxCols];
    int text_cols, text_rows;
    int text_col, text_row, text_top;
    bool show_text;
    bool show_text_ever;   // has show_text ever been true?

    char menu[kMaxRows][kMaxCols];
    bool show_menu;
    int menu_top, menu_items, menu_sel;

    pthread_t progress_t;

    int animation_fps;
    int indeterminate_frames;
    int installing_frames;
    int install_overlay_offset_x, install_overlay_offset_y;
    int overlay_offset_x, overlay_offset_y;

    void draw_install_overlay_locked(int frame);
    void draw_background_locked(Icon icon);
    void draw_progress_locked();
    void draw_screen_locked();
    void update_screen_locked();
    void update_progress_locked();
    static void* progress_thread(void* cookie);
    void progress_loop();

    void LoadBitmap(const char* filename, gr_surface* surface);
    void LoadLocalizedBitmap(const char* filename, gr_surface* surface);
};

#endif  // RECOVERY_UI_H
