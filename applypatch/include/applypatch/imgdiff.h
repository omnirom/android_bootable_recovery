/*
 * Copyright (C) 2009 The Android Open Source Project
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

#ifndef _APPLYPATCH_IMGDIFF_H
#define _APPLYPATCH_IMGDIFF_H

#include <stddef.h>

// Image patch chunk types
#define CHUNK_NORMAL 0
#define CHUNK_GZIP 1     // version 1 only
#define CHUNK_DEFLATE 2  // version 2 only
#define CHUNK_RAW 3      // version 2 only

// The gzip header size is actually variable, but we currently don't
// support gzipped data with any of the optional fields, so for now it
// will always be ten bytes.  See RFC 1952 for the definition of the
// gzip format.
static constexpr size_t GZIP_HEADER_LEN = 10;

// The gzip footer size really is fixed.
static constexpr size_t GZIP_FOOTER_LEN = 8;

int imgdiff(int argc, const char** argv);

#endif  // _APPLYPATCH_IMGDIFF_H
