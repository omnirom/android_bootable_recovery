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

#ifndef RECOVERY_WEAR_UI_H
#define RECOVERY_WEAR_UI_H

#include "screen_ui.h"

class WearRecoveryUI : public ScreenRecoveryUI {
  public:
    WearRecoveryUI();

    void Init();
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
    void Print(const char* fmt, ...);
    void PrintOnScreenOnly(const char* fmt, ...) __printflike(2, 3);
    void ShowFile(const char* filename);
    void ShowFile(FILE* fp);

    // menu display
    void StartMenu(const char* const * headers, const char* const * items,
                           int initial_selection);
    int SelectMenu(int sel);
    void EndMenu();

    void Redraw();

  protected:
    int progress_bar_height, progress_bar_width;

    // progress bar vertical position, it's centered horizontally
    int progress_bar_y;

    // outer of window
    int outer_height, outer_width;

    // Unusable rows when displaying the recovery menu, including the lines
    // for headers (Android Recovery, build id and etc) and the bottom lines
    // that may otherwise go out of the screen.
    int menu_unusable_rows;

    // number of intro frames (default: 22) and loop frames (default: 60)
    int intro_frames;
    int loop_frames;

    // Number of frames per sec (default: 30) for both of intro and loop.
    int animation_fps;

  private:
    Icon currentIcon;

    bool intro_done;

    int current_frame;

    GRSurface* backgroundIcon[5];
    GRSurface* *introFrames;
    GRSurface* *loopFrames;

    ProgressType progressBarType;

    float progressScopeStart, progressScopeSize, progress;
    double progressScopeTime, progressScopeDuration;

    static const int kMaxCols = 96;
    static const int kMaxRows = 96;

    // Log text overlay, displayed when a magic key is pressed
    char text[kMaxRows][kMaxCols];
    size_t text_cols, text_rows;
    // Number of text rows seen on screen
    int visible_text_rows;
    size_t text_col, text_row, text_top;
    bool show_text;
    bool show_text_ever;   // has show_text ever been true?

    char menu[kMaxRows][kMaxCols];
    bool show_menu;
    const char* const* menu_headers_;
    int menu_items, menu_sel;
    int menu_start, menu_end;

    pthread_t progress_t;

  private:
    void draw_background_locked(Icon icon);
    void draw_progress_locked();
    void draw_screen_locked();
    void update_screen_locked();
    static void* progress_thread(void* cookie);
    void progress_loop();
    void PutChar(char);
    void ClearText();
    void PrintV(const char*, bool, va_list);
};

#endif  // RECOVERY_WEAR_UI_H
