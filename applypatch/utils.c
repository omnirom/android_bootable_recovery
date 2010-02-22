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

#include "utils.h"

/** Write a 4-byte value to f in little-endian order. */
void Write4(int value, FILE* f) {
  fputc(value & 0xff, f);
  fputc((value >> 8) & 0xff, f);
  fputc((value >> 16) & 0xff, f);
  fputc((value >> 24) & 0xff, f);
}

/** Write an 8-byte value to f in little-endian order. */
void Write8(long long value, FILE* f) {
  fputc(value & 0xff, f);
  fputc((value >> 8) & 0xff, f);
  fputc((value >> 16) & 0xff, f);
  fputc((value >> 24) & 0xff, f);
  fputc((value >> 32) & 0xff, f);
  fputc((value >> 40) & 0xff, f);
  fputc((value >> 48) & 0xff, f);
  fputc((value >> 56) & 0xff, f);
}

int Read2(void* pv) {
    unsigned char* p = pv;
    return (int)(((unsigned int)p[1] << 8) |
                 (unsigned int)p[0]);
}

int Read4(void* pv) {
    unsigned char* p = pv;
    return (int)(((unsigned int)p[3] << 24) |
                 ((unsigned int)p[2] << 16) |
                 ((unsigned int)p[1] << 8) |
                 (unsigned int)p[0]);
}

long long Read8(void* pv) {
    unsigned char* p = pv;
    return (long long)(((unsigned long long)p[7] << 56) |
                       ((unsigned long long)p[6] << 48) |
                       ((unsigned long long)p[5] << 40) |
                       ((unsigned long long)p[4] << 32) |
                       ((unsigned long long)p[3] << 24) |
                       ((unsigned long long)p[2] << 16) |
                       ((unsigned long long)p[1] << 8) |
                       (unsigned long long)p[0]);
}
