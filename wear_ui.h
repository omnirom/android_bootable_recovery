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

#include <string>

class WearRecoveryUI : public ScreenRecoveryUI {
  public:
    WearRecoveryUI();

    bool Init(const std::string& locale) override;

    void SetStage(int current, int max) override;

    // printing messages
    void Print(const char* fmt, ...) override;
    void PrintOnScreenOnly(const char* fmt, ...) override __printflike(2, 3);
    void ShowFile(const char* filename) override;
    void ShowFile(FILE* fp) override;

    // menu display
    void StartMenu(const char* const * headers, const char* const * items,
                   int initial_selection) override;
    int SelectMenu(int sel) override;

  protected:
    // progress bar vertical position, it's centered horizontally
    int progress_bar_y;

    // outer of window
    int outer_height, outer_width;

    // Unusable rows when displaying the recovery menu, including the lines
    // for headers (Android Recovery, build id and etc) and the bottom lines
    // that may otherwise go out of the screen.
    int menu_unusable_rows;

    int GetProgressBaseline() override;

    bool InitTextParams() override;

    void update_progress_locked() override;

    void PrintV(const char*, bool, va_list) override;

  private:
    GRSurface* backgroundIcon[5];

    static const int kMaxCols = 96;
    static const int kMaxRows = 96;

    // Number of text rows seen on screen
    int visible_text_rows;

    const char* const* menu_headers_;
    int menu_start, menu_end;

    pthread_t progress_t;

    void draw_background_locked() override;
    void draw_screen_locked() override;
    void draw_progress_locked();

    void PutChar(char);
    void ClearText();
};

#endif  // RECOVERY_WEAR_UI_H
