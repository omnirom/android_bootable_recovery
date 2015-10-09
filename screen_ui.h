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
#include <stdio.h>

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
    void Print(const char* fmt, ...) __printflike(2, 3);
    void ShowFile(const char* filename);

    // menu display
    void StartMenu(const char* const * headers, const char* const * items,
                   int initial_selection);
    int SelectMenu(int sel);
    void EndMenu();

    void KeyLongPress(int);

    void Redraw();

    enum UIElement {
        HEADER, MENU, MENU_SEL_BG, MENU_SEL_BG_ACTIVE, MENU_SEL_FG, LOG, TEXT_FILL, INFO
    };
    void SetColor(UIElement e);

  private:
    Icon currentIcon;
    int installingFrame;
    const char* locale;
    bool rtl_locale;

    pthread_mutex_t updateMutex;
    GRSurface* backgroundIcon[5];
    GRSurface* backgroundText[5];
    GRSurface** installation;
    GRSurface* progressBarEmpty;
    GRSurface* progressBarFill;
    GRSurface* stageMarkerEmpty;
    GRSurface* stageMarkerFill;

    ProgressType progressBarType;

    float progressScopeStart, progressScopeSize, progress;
    double progressScopeTime, progressScopeDuration;

    // true when both graphics pages are the same (except for the progress bar).
    bool pagesIdentical;

    size_t text_cols_, text_rows_;

    // Log text overlay, displayed when a magic key is pressed.
    char** text_;
    size_t text_col_, text_row_, text_top_;

    bool show_text;
    bool show_text_ever;   // has show_text ever been true?

    char** menu_;
    const char* const* menu_headers_;
    bool show_menu;
    int menu_items, menu_sel;

    // An alternate text screen, swapped with 'text_' when we're viewing a log file.
    char** file_viewer_text_;

    pthread_t progress_thread_;

    int animation_fps;
    int installing_frames;

    int iconX, iconY;

    int stage, max_stage;

    void draw_background_locked(Icon icon);
    void draw_progress_locked();
    void draw_screen_locked();
    void update_screen_locked();
    void update_progress_locked();

    static void* ProgressThreadStartRoutine(void* data);
    void ProgressThreadLoop();

    void ShowFile(FILE*);
    void PutChar(char);
    void ClearText();

    void DrawHorizontalRule(int* y);
    void DrawTextLine(int* y, const char* line, bool bold);
    void DrawTextLines(int* y, const char* const* lines);

    void LoadBitmap(const char* filename, GRSurface** surface);
    void LoadBitmapArray(const char* filename, int* frames, GRSurface*** surface);
    void LoadLocalizedBitmap(const char* filename, GRSurface** surface);
};

#endif  // RECOVERY_UI_H
