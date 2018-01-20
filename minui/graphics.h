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

#ifndef _GRAPHICS_H_
#define _GRAPHICS_H_

#include "minui/minui.h"

class MinuiBackend {
 public:
  // Initializes the backend and returns a GRSurface* to draw into.
  virtual GRSurface* Init() = 0;

  // Causes the current drawing surface (returned by the most recent call to Flip() or Init()) to
  // be displayed, and returns a new drawing surface.
  virtual GRSurface* Flip() = 0;

  // Blank (or unblank) the screen.
  virtual void Blank(bool) = 0;

  // Device cleanup when drawing is done.
  virtual ~MinuiBackend() {};
};

#endif  // _GRAPHICS_H_
