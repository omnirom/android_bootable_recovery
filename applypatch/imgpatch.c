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
#include <sys/cdefs.h>
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
int ApplyImagePatch(const unsigned char* old_data, ssize_t old_size __unused,
                    const Value* patch,
                    SinkFn sink, void* token, SHA_CTX* ctx,
                    const Value* bonus_data) {
    ssize_t pos = 12;
    char* header = patch->data;
    if (patch->size < 12) {
        printf("patch too short to contain header\n");
        return -1;
    }

    // IMGDIFF2 uses CHUNK_NORMAL, CHUNK_DEFLATE, and CHUNK_RAW.
    // (IMGDIFF1, which is no longer supported, used CHUNK_NORMAL and
    // CHUNK_GZIP.)
    if (memcmp(header, "IMGDIFF2", 8) != 0) {
        printf("corrupt patch file header (magic number)\n");
        return -1;
    }

    int num_chunks = Read4(header+8);

    int i;
    for (i = 0; i < num_chunks; ++i) {
        // each chunk's header record starts with 4 bytes.
        if (pos + 4 > patch->size) {
            printf("failed to read chunk %d record\n", i);
            return -1;
        }
        int type = Read4(patch->data + pos);
        pos += 4;

        if (type == CHUNK_NORMAL) {
            char* normal_header = patch->data + pos;
            pos += 24;
            if (pos > patch->size) {
                printf("failed to read chunk %d normal header data\n", i);
                return -1;
            }

            size_t src_start = Read8(normal_header);
            size_t src_len = Read8(normal_header+8);
            size_t patch_offset = Read8(normal_header+16);

            ApplyBSDiffPatch(old_data + src_start, src_len,
                             patch, patch_offset, sink, token, ctx);
        } else if (type == CHUNK_RAW) {
            char* raw_header = patch->data + pos;
            pos += 4;
            if (pos > patch->size) {
                printf("failed to read chunk %d raw header data\n", i);
                return -1;
            }

            ssize_t data_len = Read4(raw_header);

            if (pos + data_len > patch->size) {
                printf("failed to read chunk %d raw data\n", i);
                return -1;
            }
            SHA_update(ctx, patch->data + pos, data_len);
            if (sink((unsigned char*)patch->data + pos,
                     data_len, token) != data_len) {
                printf("failed to write chunk %d raw data\n", i);
                return -1;
            }
            pos += data_len;
        } else if (type == CHUNK_DEFLATE) {
            // deflate chunks have an additional 60 bytes in their chunk header.
            char* deflate_header = patch->data + pos;
            pos += 60;
            if (pos > patch->size) {
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

            // Decompress the source data; the chunk header tells us exactly
            // how big we expect it to be when decompressed.

            // Note: expanded_len will include the bonus data size if
            // the patch was constructed with bonus data.  The
            // deflation will come up 'bonus_size' bytes short; these
            // must be appended from the bonus_data value.
            size_t bonus_size = (i == 1 && bonus_data != NULL) ? bonus_data->size : 0;

            unsigned char* expanded_source = malloc(expanded_len);
            if (expanded_source == NULL) {
                printf("failed to allocate %zu bytes for expanded_source\n",
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
            // We should have filled the output buffer exactly, except
            // for the bonus_size.
            if (strm.avail_out != bonus_size) {
                printf("source inflation short by %zu bytes\n", strm.avail_out-bonus_size);
                return -1;
            }
            inflateEnd(&strm);

            if (bonus_size) {
                memcpy(expanded_source + (expanded_len - bonus_size),
                       bonus_data->data, bonus_size);
            }

            // Next, apply the bsdiff patch (in memory) to the uncompressed
            // data.
            unsigned char* uncompressed_target_data;
            ssize_t uncompressed_target_size;
            if (ApplyBSDiffPatchMem(expanded_source, expanded_len,
                                    patch, patch_offset,
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
                ssize_t have = temp_size - strm.avail_out;

                if (sink(temp_data, have, token) != have) {
                    printf("failed to write %ld compressed bytes to output\n",
                           (long)have);
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
