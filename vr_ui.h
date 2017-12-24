/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef RECOVERY_VR_UI_H
#define RECOVERY_VR_UI_H

#include "screen_ui.h"

class VrRecoveryUI : public ScreenRecoveryUI {
 public:
  VrRecoveryUI();

 protected:
  // Pixel offsets to move drawing functions to visible range.
  // Can vary per device depending on screen size and lens distortion.
  const int kStereoOffset;

  bool InitTextParams() override;

  int DrawTextLine(int x, int y, const char* line, bool bold) const override;
};

#endif  // RECOVERY_VR_UI_H
