/*
 * Copyright (C) 2008 The Android Open Source Project
 * Portions Copyright (C) 2010 Magnus Eriksson <packetlss@gmail.com>
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>

#include "cutils/log.h"
#include "flashutils.h"

#if 0

#ifdef LOG_TAG
#undef LOG_TAG
#endif


#define LOG_TAG "erase_image"

static int die(const char *msg, ...) {
    int err = errno;
    va_list args;
    va_start(args, msg);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), msg, args);
    va_end(args);

    if (err != 0) {
        strlcat(buf, ": ", sizeof(buf));
        strlcat(buf, strerror(err), sizeof(buf));
    }

    fprintf(stderr, "%s\n", buf);
    LOGE("%s\n", buf);
    return 3;
}


int erase_image(char* partition_name) {
    MtdWriteContext *out;
    size_t erased;
    size_t total_size;
    size_t erase_size;

    if (mtd_scan_partitions() <= 0) die("error scanning partitions");
    const MtdPartition *partition = mtd_find_partition_by_name(partition_name);
    if (partition == NULL) return die("can't find %s partition", partition_name);

    out = mtd_write_partition(partition);
    if (out == NULL) return die("could not estabilish write context for %s", partition_name);

    // do the actual erase, -1 = full partition erase
    erased = mtd_erase_blocks(out, -1);

    // erased = bytes erased, if zero, something borked
    if (!erased) return die("error erasing %s", partition_name);

    return 0;
}


/* Erase a mtd partition */

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <partition>\n", argv[0]);
        return 2;
    }
    
    return erase_image(argv[1]);
}

#endif


int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s partition\n", argv[0]);
        return 2;
    }

    return erase_raw_partition(NULL, argv[1]);
}
