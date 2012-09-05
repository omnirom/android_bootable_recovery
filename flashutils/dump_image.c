/*
 * Copyright (C) 2008 The Android Open Source Project
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
#include <sys/ioctl.h>

#include "cutils/log.h"
#include "flashutils.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#if 0

#define LOG_TAG "dump_image"

#define BLOCK_SIZE    2048
#define SPARE_SIZE    (BLOCK_SIZE >> 5)

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
    return 1;
}

/* Read a flash partition and write it to an image file. */

int dump_image(char* partition_name, char* filename, dump_image_callback callback) {
    MtdReadContext *in;
    const MtdPartition *partition;
    char buf[BLOCK_SIZE + SPARE_SIZE];
    size_t partition_size;
    size_t read_size;
    size_t total;
    int fd;
    int wrote;
    int len;
    
    if (mtd_scan_partitions() <= 0)
        return die("error scanning partitions");

    partition = mtd_find_partition_by_name(partition_name);
    if (partition == NULL)
        return die("can't find %s partition", partition_name);

    if (mtd_partition_info(partition, &partition_size, NULL, NULL)) {
        return die("can't get info of partition %s", partition_name);
    }

    if (!strcmp(filename, "-")) {
        fd = fileno(stdout);
    } 
    else {
        fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
    }

    if (fd < 0)
        return die("error opening %s", filename);

    in = mtd_read_partition(partition);
    if (in == NULL) {
        close(fd);
        unlink(filename);
        return die("error opening %s: %s\n", partition_name, strerror(errno));
    }

    total = 0;
    while ((len = mtd_read_data(in, buf, BLOCK_SIZE)) > 0) {
        wrote = write(fd, buf, len);
        if (wrote != len) {
            close(fd);
            unlink(filename);
            return die("error writing %s", filename);
        }
        total += BLOCK_SIZE;
        if (callback != NULL)
            callback(total, partition_size);
    }

    mtd_read_close(in);

    if (close(fd)) {
        unlink(filename);
        return die("error closing %s", filename);
    }
    return 0;
}

int main(int argc, char **argv)
{
    ssize_t (*read_func) (MtdReadContext *, char *, size_t);
    MtdReadContext *in;
    const MtdPartition *partition;
    char buf[BLOCK_SIZE + SPARE_SIZE];
    size_t partition_size;
    size_t read_size;
    size_t total;
    int fd;
    int wrote;
    int len;

    if (argc != 3) {
        fprintf(stderr, "usage: %s partition file.img\n", argv[0]);
        return 2;
    }

    return dump_image(argv[1], argv[2], NULL);
}

#endif

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s partition file.img\n", argv[0]);
        return 2;
    }

    return backup_raw_partition(NULL, argv[1], argv[2]);
}
