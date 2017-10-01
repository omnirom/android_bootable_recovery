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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <thread>

#include "adb.h"
#include "fdevent.h"
#include "fuse_adb_provider.h"
#include "sysdeps.h"

typedef struct stinfo stinfo;

struct stinfo {
    void (*func)(int fd, void *cookie);
    int fd;
    void *cookie;
};

void service_bootstrap_func(void* x) {
    stinfo* sti = reinterpret_cast<stinfo*>(x);
    sti->func(sti->fd, sti->cookie);
    free(sti);
}

#if PLATFORM_SDK_VERSION < 26
static void sideload_host_service(int sfd, void* data) {
    char* args = reinterpret_cast<char*>(data);
#else
static void sideload_host_service(int sfd, const std::string& args) {
#endif
    int file_size;
    int block_size;
#if PLATFORM_SDK_VERSION < 26
    if (sscanf(args, "%d:%d", &file_size, &block_size) != 2) {
        printf("bad sideload-host arguments: %s\n", args);
#else
    if (sscanf(args.c_str(), "%d:%d", &file_size, &block_size) != 2) {
        printf("bad sideload-host arguments: %s\n", args.c_str());
#endif
        exit(1);
    }
#if PLATFORM_SDK_VERSION < 26
    free(args);
#endif

    printf("sideload-host file size %d block size %d\n", file_size, block_size);

    int result = run_adb_fuse(sfd, file_size, block_size);

    printf("sideload_host finished\n");
    sleep(1);
    exit(result == 0 ? 0 : 1);
}

#if PLATFORM_SDK_VERSION < 26
static int create_service_thread(void (*func)(int, void *), void *cookie) {
    int s[2];
    if (adb_socketpair(s)) {
        printf("cannot create service socket pair\n");
        return -1;
    }

    stinfo* sti = static_cast<stinfo*>(malloc(sizeof(stinfo)));
    if(sti == 0) fatal("cannot allocate stinfo");
    sti->func = func;
    sti->cookie = cookie;
    sti->fd = s[1];

#if PLATFORM_SDK_VERSION == 23
    adb_thread_t t;
    if (adb_thread_create( &t, (adb_thread_func_t)service_bootstrap_func, sti)){
#else
    if (!adb_thread_create(service_bootstrap_func, sti)) {
#endif
        free(sti);
        adb_close(s[0]);
        adb_close(s[1]);
        printf("cannot create service thread\n");
        return -1;
    }

    //VLOG(SERVICES) << "service thread started, " << s[0] << ":" << s[1];
    return s[0];
}
#else
static int create_service_thread(void (*func)(int, const std::string&), const std::string& args) {
    int s[2];
    if (adb_socketpair(s)) {
        printf("cannot create service socket pair\n");
        return -1;
    }

    std::thread([s, func, args]() { func(s[1], args); }).detach();

    //VLOG(SERVICES) << "service thread started, " << s[0] << ":" << s[1];
    return s[0];
}
#endif

int service_to_fd(const char* name, const atransport* transport) {
    int ret = -1;

    if (!strncmp(name, "sideload:", 9)) {
        // this exit status causes recovery to print a special error
        // message saying to use a newer adb (that supports
        // sideload-host).
        exit(3);
    } else if (!strncmp(name, "sideload-host:", 14)) {
#if PLATFORM_SDK_VERSION < 26
        char* arg = strdup(name + 14);
#else
        std::string arg(name + 14);
#endif
        ret = create_service_thread(sideload_host_service, arg);
    }
    if (ret >= 0) {
        close_on_exec(ret);
    }
    return ret;
}

#if PLATFORM_SDK_VERSION == 23
int service_to_fd(const char* name) {
    atransport transport;
    return service_to_fd(name, &transport);
}
#endif
