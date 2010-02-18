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

#include <stdio.h>

extern int applypatch(int argc, char** argv);

// This program applies binary patches to files in a way that is safe
// (the original file is not touched until we have the desired
// replacement for it) and idempotent (it's okay to run this program
// multiple times).
//
// - if the sha1 hash of <tgt-file> is <tgt-sha1>, does nothing and exits
//   successfully.
//
// - otherwise, if the sha1 hash of <src-file> is <src-sha1>, applies the
//   bsdiff <patch> to <src-file> to produce a new file (the type of patch
//   is automatically detected from the file header).  If that new
//   file has sha1 hash <tgt-sha1>, moves it to replace <tgt-file>, and
//   exits successfully.  Note that if <src-file> and <tgt-file> are
//   not the same, <src-file> is NOT deleted on success.  <tgt-file>
//   may be the string "-" to mean "the same as src-file".
//
// - otherwise, or if any error is encountered, exits with non-zero
//   status.
//
// <src-file> (or <file> in check mode) may refer to an MTD partition
// to read the source data.  See the comments for the
// LoadMTDContents() function above for the format of such a filename.

int main(int argc, char** argv) {
  int result = applypatch(argc, argv);
  if (result == 2) {
    printf(
            "usage: %s <src-file> <tgt-file> <tgt-sha1> <tgt-size> "
            "[<src-sha1>:<patch> ...]\n"
            "   or  %s -c <file> [<sha1> ...]\n"
            "   or  %s -s <bytes>\n"
            "   or  %s -l\n"
            "\n"
            "Filenames may be of the form\n"
            "  MTD:<partition>:<len_1>:<sha1_1>:<len_2>:<sha1_2>:...\n"
            "to specify reading from or writing to an MTD partition.\n\n",
            argv[0], argv[0], argv[0], argv[0]);
  }
  return result;
}
