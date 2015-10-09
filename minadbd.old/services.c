/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "sysdeps.h"
#include "fdevent.h"
#include "fuse_adb_provider.h"

#define  TRACE_TAG  TRACE_SERVICES
#include "adb.h"

typedef struct stinfo stinfo;

struct stinfo {
    void (*func)(int fd, void *cookie);
    int fd;
    void *cookie;
};


void *service_bootstrap_func(void *x)
{
    stinfo *sti = x;
    sti->func(sti->fd, sti->cookie);
    free(sti);
    return 0;
}

static void sideload_host_service(int sfd, void* cookie)
{
    char* saveptr;
    const char* s = strtok_r(cookie, ":", &saveptr);
    uint64_t file_size = strtoull(s, NULL, 10);
    s = strtok_r(NULL, ":", &saveptr);
    uint32_t block_size = strtoul(s, NULL, 10);

    printf("sideload-host file size %llu block size %lu\n", file_size, block_size);

    int result = run_adb_fuse(sfd, file_size, block_size);

    printf("sideload_host finished\n");
    sleep(1);
    exit(result == 0 ? 0 : 1);
}

#if 0
static void echo_service(int fd, void *cookie)
{
    char buf[4096];
    int r;
    char *p;
    int c;

    for(;;) {
        r = read(fd, buf, 4096);
        if(r == 0) goto done;
        if(r < 0) {
            if(errno == EINTR) continue;
            else goto done;
        }

        c = r;
        p = buf;
        while(c > 0) {
            r = write(fd, p, c);
            if(r > 0) {
                c -= r;
                p += r;
                continue;
            }
            if((r < 0) && (errno == EINTR)) continue;
            goto done;
        }
    }
done:
    close(fd);
}
#endif

static int create_service_thread(void (*func)(int, void *), void *cookie)
{
    stinfo *sti;
    adb_thread_t t;
    int s[2];

    if(adb_socketpair(s)) {
        printf("cannot create service socket pair\n");
        return -1;
    }

    sti = malloc(sizeof(stinfo));
    if(sti == 0) fatal("cannot allocate stinfo");
    sti->func = func;
    sti->cookie = cookie;
    sti->fd = s[1];

    if(adb_thread_create( &t, service_bootstrap_func, sti)){
        free(sti);
        adb_close(s[0]);
        adb_close(s[1]);
        printf("cannot create service thread\n");
        return -1;
    }

    D("service thread started, %d:%d\n",s[0], s[1]);
    return s[0];
}

int service_to_fd(const char *name)
{
    int ret = -1;

    if (!strncmp(name, "sideload:", 9)) {
        // this exit status causes recovery to print a special error
        // message saying to use a newer adb (that supports
        // sideload-host).
        exit(3);
    } else if (!strncmp(name, "sideload-host:", 14)) {
        ret = create_service_thread(sideload_host_service, (void*)(name + 14));
#if 0
    } else if(!strncmp(name, "echo:", 5)){
        ret = create_service_thread(echo_service, 0);
#endif
    }
    if (ret >= 0) {
        close_on_exec(ret);
    }
    return ret;
}
