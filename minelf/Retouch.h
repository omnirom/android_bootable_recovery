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

#ifndef _MINELF_RETOUCH
#define _MINELF_RETOUCH

#include <stdbool.h>
#include <sys/types.h>

typedef struct {
  char tag[8];        /* "RETOUCH ", not zero-terminated */
  uint32_t blob_size; /* in bytes, located right before this struct */
} retouch_info_t __attribute__((packed));

#define RETOUCH_DONT_MASK           0
#define RETOUCH_DO_MASK             1

#define RETOUCH_DATA_ERROR          0 // This is bad. Should not happen.
#define RETOUCH_DATA_MATCHED        1 // Up to an uniform random offset.
#define RETOUCH_DATA_MISMATCHED     2 // Partially randomized, or total mess.
#define RETOUCH_DATA_NOTAPPLICABLE  3 // Not retouched. Only when inferring.

// Mask retouching in-memory. Used before apply_patch[_check].
// Also used to determine status of retouching after a crash.
//
// If desired_offset is not NULL, then apply retouching instead,
// and return that in retouch_offset.
int retouch_mask_data(uint8_t *binary_object,
                      int32_t binary_size,
                      int32_t *desired_offset,
                      int32_t *retouch_offset);
#endif
