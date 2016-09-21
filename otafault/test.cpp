/*
 * Copyright (C) 2015 The Android Open Source Project
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
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "ota_io.h"

int main(int /* argc */, char** /* argv */) {
    int fd = open("testdata/test.file", O_RDWR);
    char buf[8];
    const char* out = "321";
    int readv = ota_read(fd, buf, 4);
    printf("Read returned %d\n", readv);
    int writev = ota_write(fd, out, 4);
    printf("Write returned %d\n", writev);
    close(fd);
    return 0;
}
