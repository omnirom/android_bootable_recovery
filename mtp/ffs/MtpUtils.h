/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _MTP_UTILS_H
#define _MTP_UTILS_H

#include "private/android_filesystem_config.h"

#include <stdint.h>

constexpr int FILE_GROUP = AID_MEDIA_RW;
constexpr int FILE_PERM = 0664;
constexpr int DIR_PERM = 0775;

bool parseDateTime(const char* dateTime, time_t& outSeconds);
void formatDateTime(time_t seconds, char* buffer, int bufferLength);

int makeFolder(const char *path);
int copyRecursive(const char *fromPath, const char *toPath);
int copyFile(const char *fromPath, const char *toPath);
bool deletePath(const char* path);
int renameTo(const char *oldPath, const char *newPath);

void closeObjFd(int fd, const char *path);
#endif // _MTP_UTILS_H
