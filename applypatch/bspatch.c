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
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <bzlib.h>

#include "mincrypt/sha.h"
#include "applypatch.h"

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

static off_t offtin(u_char *buf)
{
  off_t y;

  y=buf[7]&0x7F;
  y=y*256;y+=buf[6];
  y=y*256;y+=buf[5];
  y=y*256;y+=buf[4];
  y=y*256;y+=buf[3];
  y=y*256;y+=buf[2];
  y=y*256;y+=buf[1];
  y=y*256;y+=buf[0];

  if(buf[7]&0x80) y=-y;

  return y;
}


int ApplyBSDiffPatch(const unsigned char* old_data, ssize_t old_size,
                     const char* patch_filename, ssize_t patch_offset,
                     SinkFn sink, void* token, SHA_CTX* ctx) {

  unsigned char* new_data;
  ssize_t new_size;
  if (ApplyBSDiffPatchMem(old_data, old_size, patch_filename, patch_offset,
                          &new_data, &new_size) != 0) {
    return -1;
  }

  if (sink(new_data, new_size, token) < new_size) {
    fprintf(stderr, "short write of output: %d (%s)\n", errno, strerror(errno));
    return 1;
  }
  if (ctx) {
    SHA_update(ctx, new_data, new_size);
  }
  free(new_data);

  return 0;
}

int ApplyBSDiffPatchMem(const unsigned char* old_data, ssize_t old_size,
                        const char* patch_filename, ssize_t patch_offset,
                        unsigned char** new_data, ssize_t* new_size) {

  FILE* f;
  if ((f = fopen(patch_filename, "rb")) == NULL) {
    fprintf(stderr, "failed to open patch file\n");
    return 1;
  }

  // File format:
  //   0       8       "BSDIFF40"
  //   8       8       X
  //   16      8       Y
  //   24      8       sizeof(newfile)
  //   32      X       bzip2(control block)
  //   32+X    Y       bzip2(diff block)
  //   32+X+Y  ???     bzip2(extra block)
  // with control block a set of triples (x,y,z) meaning "add x bytes
  // from oldfile to x bytes from the diff block; copy y bytes from the
  // extra block; seek forwards in oldfile by z bytes".

  fseek(f, patch_offset, SEEK_SET);

  unsigned char header[32];
  if (fread(header, 1, 32, f) < 32) {
    fprintf(stderr, "failed to read patch file header\n");
    return 1;
  }

  if (memcmp(header, "BSDIFF40", 8) != 0) {
    fprintf(stderr, "corrupt bsdiff patch file header (magic number)\n");
    return 1;
  }

  ssize_t ctrl_len, data_len;
  ctrl_len = offtin(header+8);
  data_len = offtin(header+16);
  *new_size = offtin(header+24);

  if (ctrl_len < 0 || data_len < 0 || *new_size < 0) {
    fprintf(stderr, "corrupt patch file header (data lengths)\n");
    return 1;
  }

  fclose(f);

  int bzerr;

#define OPEN_AT(f, bzf, offset)                                          \
  FILE* f;                                                               \
  BZFILE* bzf;                                                           \
  if ((f = fopen(patch_filename, "rb")) == NULL) {                       \
    fprintf(stderr, "failed to open patch file\n");                      \
    return 1;                                                            \
  }                                                                      \
  if (fseeko(f, offset+patch_offset, SEEK_SET)) {                        \
    fprintf(stderr, "failed to seek in patch file\n");                   \
    return 1;                                                            \
  }                                                                      \
  if ((bzf = BZ2_bzReadOpen(&bzerr, f, 0, 0, NULL, 0)) == NULL) {        \
    fprintf(stderr, "failed to bzReadOpen in patch file (%d)\n", bzerr); \
    return 1;                                                            \
  }

  OPEN_AT(cpf, cpfbz2, 32);
  OPEN_AT(dpf, dpfbz2, 32+ctrl_len);
  OPEN_AT(epf, epfbz2, 32+ctrl_len+data_len);

#undef OPEN_AT

  *new_data = malloc(*new_size);
  if (*new_data == NULL) {
    fprintf(stderr, "failed to allocate %d bytes of memory for output file\n",
            (int)*new_size);
    return 1;
  }

  off_t oldpos = 0, newpos = 0;
  off_t ctrl[3];
  off_t len_read;
  int i;
  unsigned char buf[8];
  while (newpos < *new_size) {
    // Read control data
    for (i = 0; i < 3; ++i) {
      len_read = BZ2_bzRead(&bzerr, cpfbz2, buf, 8);
      if (len_read < 8 || !(bzerr == BZ_OK || bzerr == BZ_STREAM_END)) {
        fprintf(stderr, "corrupt patch (read control)\n");
        return 1;
      }
      ctrl[i] = offtin(buf);
    }

    // Sanity check
    if (newpos + ctrl[0] > *new_size) {
      fprintf(stderr, "corrupt patch (new file overrun)\n");
      return 1;
    }

    // Read diff string
    len_read = BZ2_bzRead(&bzerr, dpfbz2, *new_data + newpos, ctrl[0]);
    if (len_read < ctrl[0] || !(bzerr == BZ_OK || bzerr == BZ_STREAM_END)) {
      fprintf(stderr, "corrupt patch (read diff)\n");
      return 1;
    }

    // Add old data to diff string
    for (i = 0; i < ctrl[0]; ++i) {
      if ((oldpos+i >= 0) && (oldpos+i < old_size)) {
        (*new_data)[newpos+i] += old_data[oldpos+i];
      }
    }

    // Adjust pointers
    newpos += ctrl[0];
    oldpos += ctrl[0];

    // Sanity check
    if (newpos + ctrl[1] > *new_size) {
      fprintf(stderr, "corrupt patch (new file overrun)\n");
      return 1;
    }

    // Read extra string
    len_read = BZ2_bzRead(&bzerr, epfbz2, *new_data + newpos, ctrl[1]);
    if (len_read < ctrl[1] || !(bzerr == BZ_OK || bzerr == BZ_STREAM_END)) {
      fprintf(stderr, "corrupt patch (read extra)\n");
      return 1;
    }

    // Adjust pointers
    newpos += ctrl[1];
    oldpos += ctrl[2];
  }

  BZ2_bzReadClose(&bzerr, cpfbz2);
  BZ2_bzReadClose(&bzerr, dpfbz2);
  BZ2_bzReadClose(&bzerr, epfbz2);
  fclose(cpf);
  fclose(dpf);
  fclose(epf);

  return 0;
}
