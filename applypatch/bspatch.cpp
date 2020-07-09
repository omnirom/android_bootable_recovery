/*
 * Copyright (C) 2008 The Android Open Source Project
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

// This file is a nearly line-for-line copy of bspatch.c from the
// bsdiff-4.3 distribution; the primary differences being how the
// input and output data are read and the error handling.  Running
// applypatch with the -l option will display the bsdiff license
// notice.

#include <stdio.h>
#include <sys/types.h>

#include <string>

#include <android-base/logging.h>
#include <bsdiff/bspatch.h>
#include <openssl/sha.h>

#include "applypatch/applypatch.h"
#include "edify/expr.h"
#include "otautil/print_sha1.h"

void ShowBSDiffLicense() {
    puts("The bsdiff library used herein is:\n"
         "\n"
         "Copyright 2003-2005 Colin Percival\n"
         "All rights reserved\n"
         "\n"
         "Redistribution and use in source and binary forms, with or without\n"
         "modification, are permitted providing that the following conditions\n"
         "are met:\n"
         "1. Redistributions of source code must retain the above copyright\n"
         "   notice, this list of conditions and the following disclaimer.\n"
         "2. Redistributions in binary form must reproduce the above copyright\n"
         "   notice, this list of conditions and the following disclaimer in the\n"
         "   documentation and/or other materials provided with the distribution.\n"
         "\n"
         "THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR\n"
         "IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\n"
         "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\n"
         "ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY\n"
         "DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL\n"
         "DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS\n"
         "OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)\n"
         "HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,\n"
         "STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING\n"
         "IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE\n"
         "POSSIBILITY OF SUCH DAMAGE.\n"
         "\n------------------\n\n"
         "This program uses Julian R Seward's \"libbzip2\" library, available\n"
         "from http://www.bzip.org/.\n"
        );
}

int ApplyBSDiffPatch(const unsigned char* old_data, size_t old_size, const Value& patch,
                     size_t patch_offset, SinkFn sink) {
  CHECK_LE(patch_offset, patch.data.size());

  int result = bsdiff::bspatch(old_data, old_size,
                               reinterpret_cast<const uint8_t*>(&patch.data[patch_offset]),
                               patch.data.size() - patch_offset, sink);
  if (result != 0) {
    LOG(ERROR) << "bspatch failed, result: " << result;
    // print SHA1 of the patch in the case of a data error.
    if (result == 2) {
      uint8_t digest[SHA_DIGEST_LENGTH];
      SHA1(reinterpret_cast<const uint8_t*>(patch.data.data() + patch_offset),
           patch.data.size() - patch_offset, digest);
      std::string patch_sha1 = print_sha1(digest);
      LOG(ERROR) << "Patch may be corrupted, offset: " << patch_offset << ", SHA1: " << patch_sha1;
    }
  }
  return result;
}
