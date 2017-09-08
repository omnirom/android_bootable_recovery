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

  void SetStage(int current, int max) override;

  // menu display
  void StartMenu(const char* const* headers, const char* const* items,
                 int initial_selection) override;
  int SelectMenu(int sel) override;

 protected:
  // progress bar vertical position, it's centered horizontally
  const int kProgressBarBaseline;

  // Unusable rows when displaying the recovery menu, including the lines for headers (Android
  // Recovery, build id and etc) and the bottom lines that may otherwise go out of the screen.
  const int kMenuUnusableRows;

  int GetProgressBaseline() const override;

  void update_progress_locked() override;

 private:
  void draw_background_locked() override;
  void draw_screen_locked() override;

  int menu_start, menu_end;
};

#endif  // RECOVERY_WEAR_UI_H
