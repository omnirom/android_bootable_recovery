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

// See imgdiff.c in this directory for a description of the patch file
// format.

#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "zlib.h"
#include "mincrypt/sha.h"
#include "applypatch.h"
#include "imgdiff.h"
#include "utils.h"

/*
 * Apply the patch given in 'patch_filename' to the source data given
 * by (old_data, old_size).  Write the patched output to the 'output'
 * file, and update the SHA context with the output data as well.
 * Return 0 on success.
 */
int ApplyImagePatch(const unsigned char* old_data, ssize_t old_size,
                    const char* patch_filename,
                    SinkFn sink, void* token, SHA_CTX* ctx) {
  FILE* f;
  if ((f = fopen(patch_filename, "rb")) == NULL) {
    printf("failed to open patch file\n");
    return -1;
  }

  unsigned char header[12];
  if (fread(header, 1, 12, f) != 12) {
    printf("failed to read patch file header\n");
    return -1;
  }

  // IMGDIFF1 uses CHUNK_NORMAL and CHUNK_GZIP.
  // IMGDIFF2 uses CHUNK_NORMAL, CHUNK_DEFLATE, and CHUNK_RAW.
  if (memcmp(header, "IMGDIFF", 7) != 0 ||
      (header[7] != '1' && header[7] != '2')) {
    printf("corrupt patch file header (magic number)\n");
    return -1;
  }

  int num_chunks = Read4(header+8);

  int i;
  for (i = 0; i < num_chunks; ++i) {
    // each chunk's header record starts with 4 bytes.
    unsigned char chunk[4];
    if (fread(chunk, 1, 4, f) != 4) {
      printf("failed to read chunk %d record\n", i);
      return -1;
    }

    int type = Read4(chunk);

    if (type == CHUNK_NORMAL) {
      unsigned char normal_header[24];
      if (fread(normal_header, 1, 24, f) != 24) {
        printf("failed to read chunk %d normal header data\n", i);
        return -1;
      }

      size_t src_start = Read8(normal_header);
      size_t src_len = Read8(normal_header+8);
      size_t patch_offset = Read8(normal_header+16);

      printf("CHUNK %d:  normal   patch offset %d\n", i, patch_offset);

      ApplyBSDiffPatch(old_data + src_start, src_len,
                       patch_filename, patch_offset,
                       sink, token, ctx);
    } else if (type == CHUNK_GZIP) {
      // This branch is basically a duplicate of the CHUNK_DEFLATE
      // branch, with a bit of extra processing for the gzip header
      // and footer.  I've avoided factoring the common code out since
      // this branch will just be deleted when we drop support for
      // IMGDIFF1.

      // gzip chunks have an additional 64 + gzip_header_len + 8 bytes
      // in their chunk header.
      unsigned char* gzip = malloc(64);
      if (fread(gzip, 1, 64, f) != 64) {
        printf("failed to read chunk %d initial gzip header data\n",
                i);
        return -1;
      }
      size_t gzip_header_len = Read4(gzip+60);
      gzip = realloc(gzip, 64 + gzip_header_len + 8);
      if (fread(gzip+64, 1, gzip_header_len+8, f) != gzip_header_len+8) {
        printf("failed to read chunk %d remaining gzip header data\n",
                i);
        return -1;
      }

      size_t src_start = Read8(gzip);
      size_t src_len = Read8(gzip+8);
      size_t patch_offset = Read8(gzip+16);

      size_t expanded_len = Read8(gzip+24);
      size_t target_len = Read8(gzip+32);
      int gz_level = Read4(gzip+40);
      int gz_method = Read4(gzip+44);
      int gz_windowBits = Read4(gzip+48);
      int gz_memLevel = Read4(gzip+52);
      int gz_strategy = Read4(gzip+56);

      printf("CHUNK %d:  gzip     patch offset %d\n", i, patch_offset);

      // Decompress the source data; the chunk header tells us exactly
      // how big we expect it to be when decompressed.

      unsigned char* expanded_source = malloc(expanded_len);
      if (expanded_source == NULL) {
        printf("failed to allocate %d bytes for expanded_source\n",
                expanded_len);
        return -1;
      }

      z_stream strm;
      strm.zalloc = Z_NULL;
      strm.zfree = Z_NULL;
      strm.opaque = Z_NULL;
      strm.avail_in = src_len - (gzip_header_len + 8);
      strm.next_in = (unsigned char*)(old_data + src_start + gzip_header_len);
      strm.avail_out = expanded_len;
      strm.next_out = expanded_source;

      int ret;
      ret = inflateInit2(&strm, -15);
      if (ret != Z_OK) {
        printf("failed to init source inflation: %d\n", ret);
        return -1;
      }

      // Because we've provided enough room to accommodate the output
      // data, we expect one call to inflate() to suffice.
      ret = inflate(&strm, Z_SYNC_FLUSH);
      if (ret != Z_STREAM_END) {
        printf("source inflation returned %d\n", ret);
        return -1;
      }
      // We should have filled the output buffer exactly.
      if (strm.avail_out != 0) {
        printf("source inflation short by %d bytes\n", strm.avail_out);
        return -1;
      }
      inflateEnd(&strm);

      // Next, apply the bsdiff patch (in memory) to the uncompressed
      // data.
      unsigned char* uncompressed_target_data;
      ssize_t uncompressed_target_size;
      if (ApplyBSDiffPatchMem(expanded_source, expanded_len,
                              patch_filename, patch_offset,
                              &uncompressed_target_data,
                              &uncompressed_target_size) != 0) {
        return -1;
      }

      // Now compress the target data and append it to the output.

      // start with the gzip header.
      sink(gzip+64, gzip_header_len, token);
      SHA_update(ctx, gzip+64, gzip_header_len);

      // we're done with the expanded_source data buffer, so we'll
      // reuse that memory to receive the output of deflate.
      unsigned char* temp_data = expanded_source;
      ssize_t temp_size = expanded_len;
      if (temp_size < 32768) {
        // ... unless the buffer is too small, in which case we'll
        // allocate a fresh one.
        free(temp_data);
        temp_data = malloc(32768);
        temp_size = 32768;
      }

      // now the deflate stream
      strm.zalloc = Z_NULL;
      strm.zfree = Z_NULL;
      strm.opaque = Z_NULL;
      strm.avail_in = uncompressed_target_size;
      strm.next_in = uncompressed_target_data;
      ret = deflateInit2(&strm, gz_level, gz_method, gz_windowBits,
                         gz_memLevel, gz_strategy);
      do {
        strm.avail_out = temp_size;
        strm.next_out = temp_data;
        ret = deflate(&strm, Z_FINISH);
        size_t have = temp_size - strm.avail_out;

        if (sink(temp_data, have, token) != have) {
          printf("failed to write %d compressed bytes to output\n",
                  have);
          return -1;
        }
        SHA_update(ctx, temp_data, have);
      } while (ret != Z_STREAM_END);
      deflateEnd(&strm);

      // lastly, the gzip footer.
      sink(gzip+64+gzip_header_len, 8, token);
      SHA_update(ctx, gzip+64+gzip_header_len, 8);

      free(temp_data);
      free(uncompressed_target_data);
      free(gzip);
    } else if (type == CHUNK_RAW) {
      unsigned char raw_header[4];
      if (fread(raw_header, 1, 4, f) != 4) {
        printf("failed to read chunk %d raw header data\n", i);
        return -1;
      }

      size_t data_len = Read4(raw_header);

      printf("CHUNK %d:  raw      data %d\n", i, data_len);

      unsigned char* temp = malloc(data_len);
      if (fread(temp, 1, data_len, f) != data_len) {
          printf("failed to read chunk %d raw data\n", i);
          return -1;
      }
      SHA_update(ctx, temp, data_len);
      if (sink(temp, data_len, token) != data_len) {
          printf("failed to write chunk %d raw data\n", i);
          return -1;
      }
    } else if (type == CHUNK_DEFLATE) {
      // deflate chunks have an additional 60 bytes in their chunk header.
      unsigned char deflate_header[60];
      if (fread(deflate_header, 1, 60, f) != 60) {
        printf("failed to read chunk %d deflate header data\n", i);
        return -1;
      }

      size_t src_start = Read8(deflate_header);
      size_t src_len = Read8(deflate_header+8);
      size_t patch_offset = Read8(deflate_header+16);
      size_t expanded_len = Read8(deflate_header+24);
      size_t target_len = Read8(deflate_header+32);
      int level = Read4(deflate_header+40);
      int method = Read4(deflate_header+44);
      int windowBits = Read4(deflate_header+48);
      int memLevel = Read4(deflate_header+52);
      int strategy = Read4(deflate_header+56);

      printf("CHUNK %d:  deflate  patch offset %d\n", i, patch_offset);

      // Decompress the source data; the chunk header tells us exactly
      // how big we expect it to be when decompressed.

      unsigned char* expanded_source = malloc(expanded_len);
      if (expanded_source == NULL) {
        printf("failed to allocate %d bytes for expanded_source\n",
                expanded_len);
        return -1;
      }

      z_stream strm;
      strm.zalloc = Z_NULL;
      strm.zfree = Z_NULL;
      strm.opaque = Z_NULL;
      strm.avail_in = src_len;
      strm.next_in = (unsigned char*)(old_data + src_start);
      strm.avail_out = expanded_len;
      strm.next_out = expanded_source;

      int ret;
      ret = inflateInit2(&strm, -15);
      if (ret != Z_OK) {
        printf("failed to init source inflation: %d\n", ret);
        return -1;
      }

      // Because we've provided enough room to accommodate the output
      // data, we expect one call to inflate() to suffice.
      ret = inflate(&strm, Z_SYNC_FLUSH);
      if (ret != Z_STREAM_END) {
        printf("source inflation returned %d\n", ret);
        return -1;
      }
      // We should have filled the output buffer exactly.
      if (strm.avail_out != 0) {
        printf("source inflation short by %d bytes\n", strm.avail_out);
        return -1;
      }
      inflateEnd(&strm);

      // Next, apply the bsdiff patch (in memory) to the uncompressed
      // data.
      unsigned char* uncompressed_target_data;
      ssize_t uncompressed_target_size;
      if (ApplyBSDiffPatchMem(expanded_source, expanded_len,
                              patch_filename, patch_offset,
                              &uncompressed_target_data,
                              &uncompressed_target_size) != 0) {
        return -1;
      }

      // Now compress the target data and append it to the output.

      // we're done with the expanded_source data buffer, so we'll
      // reuse that memory to receive the output of deflate.
      unsigned char* temp_data = expanded_source;
      ssize_t temp_size = expanded_len;
      if (temp_size < 32768) {
        // ... unless the buffer is too small, in which case we'll
        // allocate a fresh one.
        free(temp_data);
        temp_data = malloc(32768);
        temp_size = 32768;
      }

      // now the deflate stream
      strm.zalloc = Z_NULL;
      strm.zfree = Z_NULL;
      strm.opaque = Z_NULL;
      strm.avail_in = uncompressed_target_size;
      strm.next_in = uncompressed_target_data;
      ret = deflateInit2(&strm, level, method, windowBits, memLevel, strategy);
      do {
        strm.avail_out = temp_size;
        strm.next_out = temp_data;
        ret = deflate(&strm, Z_FINISH);
        size_t have = temp_size - strm.avail_out;

        if (sink(temp_data, have, token) != have) {
          printf("failed to write %d compressed bytes to output\n",
                  have);
          return -1;
        }
        SHA_update(ctx, temp_data, have);
      } while (ret != Z_STREAM_END);
      deflateEnd(&strm);

      free(temp_data);
      free(uncompressed_target_data);
    } else {
      printf("patch chunk %d is unknown type %d\n", i, type);
      return -1;
    }
  }

  return 0;
}
