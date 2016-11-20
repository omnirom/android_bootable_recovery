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
    void SetSystemUpdateText(bool security_update);

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
    void PrintOnScreenOnly(const char* fmt, ...) __printflike(2, 3);
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

  protected:
    Icon currentIcon;

    const char* locale;
    bool intro_done;
    int current_frame;

    // The scale factor from dp to pixels. 1.0 for mdpi, 4.0 for xxxhdpi.
    float density_;
    // The layout to use.
    int layout_;

    GRSurface* error_icon;

    GRSurface* erasing_text;
    GRSurface* error_text;
    GRSurface* installing_text;
    GRSurface* no_command_text;

    GRSurface** introFrames;
    GRSurface** loopFrames;

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

    // Number of intro frames and loop frames in the animation.
    int intro_frames;
    int loop_frames;

    // Number of frames per sec (default: 30) for both parts of the animation.
    int animation_fps;

    int stage, max_stage;

    int char_width_;
    int char_height_;
    pthread_mutex_t updateMutex;
    bool rtl_locale;

    void draw_background_locked();
    void draw_foreground_locked();
    void draw_screen_locked();
    void update_screen_locked();
    void update_progress_locked();

    GRSurface* GetCurrentFrame();
    GRSurface* GetCurrentText();

    static void* ProgressThreadStartRoutine(void* data);
    void ProgressThreadLoop();

    void ShowFile(FILE*);
    void PrintV(const char*, bool, va_list);
    void PutChar(char);
    void ClearText();

    void LoadAnimation();
    void LoadBitmap(const char* filename, GRSurface** surface);
    void LoadLocalizedBitmap(const char* filename, GRSurface** surface);

    int PixelsFromDp(int dp);
    int GetAnimationBaseline();
    int GetProgressBaseline();
    int GetTextBaseline();

    void DrawHorizontalRule(int* y);
    void DrawTextLine(int x, int* y, const char* line, bool bold);
    void DrawTextLines(int x, int* y, const char* const* lines);
};

#endif  // RECOVERY_UI_H
