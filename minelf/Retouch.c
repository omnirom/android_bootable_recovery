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

// On success, _override is set to the offset that was actually applied.
// This implies that once we randomize to an offset we stick with it.
// This in turn is necessary in order to guarantee recovery after crash.
bool retouch_one_library(const char *binary_name,
                         const char *binary_sha1,
                         int32_t retouch_offset,
                         int32_t *retouch_offset_override) {
    bool success = true;
    int result;

    FileContents file;
    file.data = NULL;

    char binary_name_atomic[strlen(binary_name)+10];
    strcpy(binary_name_atomic, binary_name);
    strcat(binary_name_atomic, ".atomic");

    // We need a path that exists for calling statfs() later.
    //
    // Assume that binary_name (eg "/system/app/Foo.apk") is located
    // on the same filesystem as its top-level directory ("/system").
    char target_fs[strlen(binary_name)+1];
    char* slash = strchr(binary_name+1, '/');
    if (slash != NULL) {
        int count = slash - binary_name;
        strncpy(target_fs, binary_name, count);
        target_fs[count] = '\0';
    } else {
        strcpy(target_fs, binary_name);
    }

    result = LoadFileContents(binary_name, &file, RETOUCH_DONT_MASK);

    if (result == 0) {
        // Figure out the *apparent* offset to which this file has been
        // retouched. If it looks good, we will skip processing (we might
        // have crashed and during this recovery pass we don't want to
        // overwrite a valuable saved file in /cache---which would happen
        // if we blindly retouch everything again). NOTE: This implies
        // that we might have to override the supplied retouch offset. We
        // can do the override only once though: everything should match
        // afterward.

        int32_t inferred_offset;
        int retouch_probe_result = retouch_mask_data(file.data,
                                                     file.size,
                                                     NULL,
                                                     &inferred_offset);

        if (retouch_probe_result == RETOUCH_DATA_MATCHED) {
            if ((retouch_offset == inferred_offset) ||
                ((retouch_offset != 0 && inferred_offset != 0) &&
                 (retouch_offset_override != NULL))) {
                // This file is OK already and we are allowed to override.
                // Let's just return the offset override value. It is critical
                // to skip regardless of override: a broken file might need
                // recovery down the list and we should not mess up the saved
                // copy by doing unnecessary retouching.
                //
                // NOTE: If retouching was already started with a different
                // value, we will not be allowed to override. This happens
                // if on the retouch list there is a patched binary (which is
                // masked in apply_patch()) before there is a non-patched
                // binary.
                if (retouch_offset_override != NULL)
                    *retouch_offset_override = inferred_offset;
                success = true;
                goto out;
            } else {
                // Retouch to zero (mask the retouching), to make sure that
                // the SHA-1 check will pass below.
                int32_t zero = 0;
                retouch_mask_data(file.data, file.size, &zero, NULL);
                SHA(file.data, file.size, file.sha1);
            }
        }

        if (retouch_probe_result == RETOUCH_DATA_NOTAPPLICABLE) {
            // In the case of not retouchable, fake it. We do not want
            // to do the normal processing and overwrite the backup file:
            // we might be recovering!
            //
            // We return a zero override, which tells the caller that we
            // simply skipped the file.
            if (retouch_offset_override != NULL)
                *retouch_offset_override = 0;
            success = true;
            goto out;
        }

        // If we get here, either there was a mismatch in the offset, or
        // the file has not been processed yet. Continue with normal
        // processing.
    }

    if (result != 0 || FindMatchingPatch(file.sha1, &binary_sha1, 1) < 0) {
        free(file.data);
        printf("Attempting to recover source from '%s' ...\n",
               CACHE_TEMP_SOURCE);
        result = LoadFileContents(CACHE_TEMP_SOURCE, &file, RETOUCH_DO_MASK);
        if (result != 0 || FindMatchingPatch(file.sha1, &binary_sha1, 1) < 0) {
            printf(" failed.\n");
            success = false;
            goto out;
        }
        printf(" succeeded.\n");
    }

    // Retouch in-memory before worrying about backing up the original.
    //
    // Recovery steps will be oblivious to the actual retouch offset used,
    // so might as well write out the already-retouched copy. Then, in the
    // usual case, we will just swap the file locally, with no more writes
    // needed. In the no-free-space case, we will then write the same to the
    // original location.

    result = retouch_mask_data(file.data, file.size, &retouch_offset, NULL);
    if (result != RETOUCH_DATA_MATCHED) {
        success = false;
        goto out;
    }
    if (retouch_offset_override != NULL)
        *retouch_offset_override = retouch_offset;

    // How much free space do we need?
    bool enough_space = false;
    size_t free_space = FreeSpaceForFile(target_fs);
    // 50% margin when estimating the space needed.
    enough_space = (free_space > (file.size * 3 / 2));

    // The experts say we have to allow for a retry of the
    // whole process to avoid filesystem weirdness.
    int retry = 1;
    bool made_copy = false;
    do {
        // First figure out where to store a copy of the original.
        // Ideally leave the original itself intact until the
        // atomic swap. If no room on the same partition, fall back
        // to the cache partition and remove the original.

        if (!enough_space) {
            printf("Target is %ldB; free space is %ldB: not enough.\n",
                   (long)file.size, (long)free_space);

            retry = 0;
            if (MakeFreeSpaceOnCache(file.size) < 0) {
                printf("Not enough free space on '/cache'.\n");
                success = false;
                goto out;
            }
            if (SaveFileContents(CACHE_TEMP_SOURCE, file) < 0) {
                printf("Failed to back up source file.\n");
                success = false;
                goto out;
            }
            made_copy = true;
            unlink(binary_name);

            size_t free_space = FreeSpaceForFile(target_fs);
            printf("(now %ld bytes free for target)\n", (long)free_space);
        }

        result = SaveFileContents(binary_name_atomic, file);
        if (result != 0) {
            // Maybe the filesystem was optimistic: retry.
            enough_space = false;
            unlink(binary_name_atomic);
            printf("Saving the retouched contents failed; retrying.\n");
            continue;
        }

        // Succeeded; no need to retry.
        break;
    } while (retry-- > 0);

    // Give the .atomic file the same owner, group, and mode of the
    // original source file.
    if (chmod(binary_name_atomic, file.st.st_mode) != 0) {
        printf("chmod of \"%s\" failed: %s\n",
               binary_name_atomic, strerror(errno));
        success = false;
        goto out;
    }
    if (chown(binary_name_atomic, file.st.st_uid, file.st.st_gid) != 0) {
        printf("chown of \"%s\" failed: %s\n",
               binary_name_atomic,
               strerror(errno));
        success = false;
        goto out;
    }

    // Finally, rename the .atomic file to replace the target file.
    if (rename(binary_name_atomic, binary_name) != 0) {
        printf("rename of .atomic to \"%s\" failed: %s\n",
               binary_name, strerror(errno));
        success = false;
        goto out;
    }

    // If this run created a copy, and we're here, we can delete it.
    if (made_copy) unlink(CACHE_TEMP_SOURCE);

  out:
    // clean up
    free(file.data);
    unlink(binary_name_atomic);

    return success;
}
