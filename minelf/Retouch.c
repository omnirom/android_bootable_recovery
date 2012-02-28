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

#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include "Retouch.h"
#include "applypatch/applypatch.h"

typedef struct {
    int32_t mmap_addr;
    char tag[4]; /* 'P', 'R', 'E', ' ' */
} prelink_info_t __attribute__((packed));

#define false 0
#define true 1

static int32_t offs_prev;
static uint32_t cont_prev;

static void init_compression_state(void) {
    offs_prev = 0;
    cont_prev = 0;
}

// For details on the encoding used for relocation lists, please
// refer to build/tools/retouch/retouch-prepare.c. The intent is to
// save space by removing most of the inherent redundancy.

static void decode_bytes(uint8_t *encoded_bytes, int encoded_size,
                         int32_t *dst_offset, uint32_t *dst_contents) {
    if (encoded_size == 2) {
        *dst_offset = offs_prev + (((encoded_bytes[0]&0x60)>>5)+1)*4;

        // if the original was negative, we need to 1-pad before applying delta
        int32_t tmp = (((encoded_bytes[0] & 0x0000001f) << 8) |
                       encoded_bytes[1]);
        if (tmp & 0x1000) tmp = 0xffffe000 | tmp;
        *dst_contents = cont_prev + tmp;
    } else if (encoded_size == 3) {
        *dst_offset = offs_prev + (((encoded_bytes[0]&0x30)>>4)+1)*4;

        // if the original was negative, we need to 1-pad before applying delta
        int32_t tmp = (((encoded_bytes[0] & 0x0000000f) << 16) |
                       (encoded_bytes[1] << 8) |
                       encoded_bytes[2]);
        if (tmp & 0x80000) tmp = 0xfff00000 | tmp;
        *dst_contents = cont_prev + tmp;
    } else {
        *dst_offset =
          (encoded_bytes[0]<<24) |
          (encoded_bytes[1]<<16) |
          (encoded_bytes[2]<<8) |
          encoded_bytes[3];
        if (*dst_offset == 0x3fffffff) *dst_offset = -1;
        *dst_contents =
          (encoded_bytes[4]<<24) |
          (encoded_bytes[5]<<16) |
          (encoded_bytes[6]<<8) |
          encoded_bytes[7];
    }
}

static uint8_t *decode_in_memory(uint8_t *encoded_bytes,
                                 int32_t *offset, uint32_t *contents) {
    int input_size, charIx;
    uint8_t input[8];

    input[0] = *(encoded_bytes++);
    if (input[0] & 0x80)
        input_size = 2;
    else if (input[0] & 0x40)
        input_size = 3;
    else
        input_size = 8;

    // we already read one byte..
    charIx = 1;
    while (charIx < input_size) {
        input[charIx++] = *(encoded_bytes++);
    }

    // depends on the decoder state!
    decode_bytes(input, input_size, offset, contents);

    offs_prev = *offset;
    cont_prev = *contents;

    return encoded_bytes;
}

int retouch_mask_data(uint8_t *binary_object,
                      int32_t binary_size,
                      int32_t *desired_offset,
                      int32_t *retouch_offset) {
    retouch_info_t *r_info;
    prelink_info_t *p_info;

    int32_t target_offset = 0;
    if (desired_offset) target_offset = *desired_offset;

    int32_t p_offs = binary_size-sizeof(prelink_info_t); // prelink_info_t
    int32_t r_offs = p_offs-sizeof(retouch_info_t); // retouch_info_t
    int32_t b_offs; // retouch data blob

    // If not retouched, we say it was a match. This might get invoked on
    // non-retouched binaries, so that's why we need to do this.
    if (retouch_offset != NULL) *retouch_offset = target_offset;
    if (r_offs < 0) return (desired_offset == NULL) ?
                      RETOUCH_DATA_NOTAPPLICABLE : RETOUCH_DATA_MATCHED;
    p_info = (prelink_info_t *)(binary_object+p_offs);
    r_info = (retouch_info_t *)(binary_object+r_offs);
    if (strncmp(p_info->tag, "PRE ", 4) ||
        strncmp(r_info->tag, "RETOUCH ", 8))
        return (desired_offset == NULL) ?
          RETOUCH_DATA_NOTAPPLICABLE : RETOUCH_DATA_MATCHED;

    b_offs = r_offs-r_info->blob_size;
    if (b_offs < 0) {
        printf("negative binary offset: %d = %d - %d\n",
               b_offs, r_offs, r_info->blob_size);
        return RETOUCH_DATA_ERROR;
    }
    uint8_t *b_ptr = binary_object+b_offs;

    // Retouched: let's go through the work then.
    int32_t offset_candidate = target_offset;
    bool offset_set = false, offset_mismatch = false;
    init_compression_state();
    while (b_ptr < (uint8_t *)r_info) {
        int32_t retouch_entry_offset;
        uint32_t *retouch_entry;
        uint32_t retouch_original_value;

        b_ptr = decode_in_memory(b_ptr,
                                 &retouch_entry_offset,
                                 &retouch_original_value);
        if (retouch_entry_offset < (-1) ||
            retouch_entry_offset >= b_offs) {
            printf("bad retouch_entry_offset: %d", retouch_entry_offset);
            return RETOUCH_DATA_ERROR;
        }

        // "-1" means this is the value in prelink_info_t, which also gets
        // randomized.
        if (retouch_entry_offset == -1)
            retouch_entry = (uint32_t *)&(p_info->mmap_addr);
        else
            retouch_entry = (uint32_t *)(binary_object+retouch_entry_offset);

        if (desired_offset)
            *retouch_entry = retouch_original_value + target_offset;

        // Infer the randomization shift, compare to previously inferred.
        int32_t offset_of_this_entry = (int32_t)(*retouch_entry-
                                                 retouch_original_value);
        if (!offset_set) {
            offset_candidate = offset_of_this_entry;
            offset_set = true;
        } else {
            if (offset_candidate != offset_of_this_entry) {
                offset_mismatch = true;
                printf("offset is mismatched: %d, this entry is %d,"
                       " original 0x%x @ 0x%x",
                       offset_candidate, offset_of_this_entry,
                       retouch_original_value, retouch_entry_offset);
            }
        }
    }
    if (b_ptr > (uint8_t *)r_info) {
        printf("b_ptr went too far: %p, while r_info is %p",
               b_ptr, r_info);
        return RETOUCH_DATA_ERROR;
    }

    if (offset_mismatch) return RETOUCH_DATA_MISMATCHED;
    if (retouch_offset != NULL) *retouch_offset = offset_candidate;
    return RETOUCH_DATA_MATCHED;
}
