/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef __FUSE_SIDELOAD_H
#define __FUSE_SIDELOAD_H

#include <sys/cdefs.h>

__BEGIN_DECLS

// define the filenames created by the sideload FUSE filesystem
#define FUSE_SIDELOAD_HOST_MOUNTPOINT "/sideload"
#define FUSE_SIDELOAD_HOST_FILENAME "package.zip"
#define FUSE_SIDELOAD_HOST_PATHNAME (FUSE_SIDELOAD_HOST_MOUNTPOINT "/" FUSE_SIDELOAD_HOST_FILENAME)
#define FUSE_SIDELOAD_HOST_EXIT_FLAG "exit"
#define FUSE_SIDELOAD_HOST_EXIT_PATHNAME (FUSE_SIDELOAD_HOST_MOUNTPOINT "/" FUSE_SIDELOAD_HOST_EXIT_FLAG)

struct provider_vtab {
    // read a block
    int (*read_block)(void* cookie, uint32_t block, uint8_t* buffer, uint32_t fetch_size);

    // close down
    void (*close)(void* cookie);
};

int run_fuse_sideload(struct provider_vtab* vtab, void* cookie,
                      uint64_t file_size, uint32_t block_size);

__END_DECLS

#endif
