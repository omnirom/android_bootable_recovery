/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
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

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <libubi.h>

extern "C" int ubiVolumeFormat(char *location)
{
    int ret = -1;
    libubi_t libubi;
    struct ubi_vol_info vol_info;
    int fd;
    libubi = libubi_open(1);

    if (libubi == NULL) {
        fprintf(stderr, "can not open libubi");
        goto done;
    }

    ret = ubi_node_type(libubi, location);
    if (ret == 1) {
        fprintf(stderr, "%s is a ubi device node, not a ubi volume node",
                location);
        goto done;
    } else if (ret < 0) {
        fprintf(stderr, "%s is not a UBI volume node", location);
        goto done;
    }

    ret = ubi_get_vol_info(libubi, location, &vol_info);
    if (ret) {
        fprintf(stderr, "can not get information about UBI volume %s", location);
        goto done;
    }

    fd = open(location, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "can not open %s", location);
        goto done;
    }

    ret = ubi_update_start(libubi, fd, 0);
    if (ret) {
        fprintf(stderr, "cannot truncate volume %s", location);
        close(fd);
        goto done;
    }

    close(fd);

  done:
    return ret;
}
