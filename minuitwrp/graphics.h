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

#include "minui.h"

// TODO: lose the function pointers.
struct minui_backend {
    // Initializes the backend and returns a GRSurface* to draw into.
    GRSurface* (*init)(minui_backend*);

    // Causes the current drawing surface (returned by the most recent
    // call to flip() or init()) to be displayed, and returns a new
    // drawing surface.
    GRSurface* (*flip)(minui_backend*);

    // Blank (or unblank) the screen.
    void (*blank)(minui_backend*, bool);

    // Device cleanup when drawing is done.
    void (*exit)(minui_backend*);
};

minui_backend* open_fbdev();
minui_backend* open_adf();
minui_backend* open_drm();
minui_backend* open_overlay();

#endif
