/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "applypatch_modes.h"

// This program (applypatch) applies binary patches to files in a way that
// is safe (the original file is not touched until we have the desired
// replacement for it) and idempotent (it's okay to run this program
// multiple times).
//
// See the comments to applypatch_modes() function.

int main(int argc, char** argv) {
    return applypatch_modes(argc, const_cast<const char**>(argv));
}
