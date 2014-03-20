/*
 * Copyright (c) 2013 The CyanogenMod Project
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

#include "voldclient.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>

#include <cutils/sockets.h>
#include <private/android_filesystem_config.h>

#include "ResponseCode.h"

#include "common.h"

// locking
static pthread_mutex_t mutex      = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  completion = PTHREAD_COND_INITIALIZER;

// command result status, read with mutex held
static int cmd_result = 0;

// commands currently in flight
static int cmd_inflight = 0;

// socket fd
static int sock = -1;


static int vold_connect() {

    int retries = 5;
    int ret = -1;
    if (sock > 0) {
        return 1;
    }

    // socket connection to vold
    while (retries > 0) {
        if ((sock = socket_local_client("vold",
                                         ANDROID_SOCKET_NAMESPACE_RESERVED,
                                         SOCK_STREAM)) < 0) {
            LOGE("Error connecting to Vold! (%s)\n", strerror(errno));
        } else {
            LOGI("Connected to Vold..\n");
            ret = 1;
            break;
        }
        sleep(1);
        retries--;
    }
    return ret;
}

static int split(char *str, char **splitstr) {

    char *p;
    int i = 0;

    p = strtok(str, " ");

    while(p != NULL) {
        splitstr[i] = (char *)malloc(strlen(p) + 1);
        if (splitstr[i])
            strcpy(splitstr[i], p);
        i++;
        p = strtok (NULL, " ");
    }

    return i;
}

extern int vold_dispatch(int code, char** tokens, int len);

static int handle_response(char* response) {

    int code = 0, len = 0, i = 0;
    char *tokens[32] = { NULL };

    len = split(response, tokens);
    code = atoi(tokens[0]);

    if (len) {
        vold_dispatch(code, tokens, len);

        for (i = 0; i < len; i++)
            free(tokens[i]);
    }

    return code;
}

static int monitor_started = 0;

// wait for events and signal waiters when appropriate
static int monitor() {

    char *buffer = (char *)malloc(4096);
    int code = 0;

    while(1) {
        fd_set read_fds;
        struct timeval to;
        int rc = 0;

        to.tv_sec = 10;
        to.tv_usec = 0;

        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);

        if (!monitor_started) {
            pthread_mutex_lock(&mutex);
            monitor_started = 1;
            pthread_cond_signal(&completion);
            pthread_mutex_unlock(&mutex);
        }

        if ((rc = select(sock +1, &read_fds, NULL, NULL, &to)) < 0) {
            LOGE("Error in select (%s)\n", strerror(errno));
            goto out;

        } else if (!rc) {
            continue;

        } else if (FD_ISSET(sock, &read_fds)) {
            memset(buffer, 0, 4096);
            if ((rc = read(sock, buffer, 4096)) <= 0) {
                if (rc == 0)
                    LOGE("Lost connection to Vold - did it crash?\n");
                else
                    LOGE("Error reading data (%s)\n", strerror(errno));
                if (rc == 0)
                    return ECONNRESET;
                goto out;
            }

            int offset = 0;
            int i = 0;

            // dispatch each line of the response
            for (i = 0; i < rc; i++) {
                if (buffer[i] == '\0') {

                    LOGI("%s\n", buffer + offset);
                    code = handle_response(strdup(buffer + offset));

                    if (code >= 200 && code < 600) {
                        pthread_mutex_lock(&mutex);
                        cmd_result = code;
                        cmd_inflight--;
                        pthread_cond_signal(&completion);
                        pthread_mutex_unlock(&mutex);
                    }
                    offset = i + 1;
                }
            }
        }
    }
out:
    free(buffer);
    pthread_mutex_unlock(&mutex);
    return code;
}

static void *event_thread_func(void* v) {

    // if monitor() returns, it means we lost the connection to vold
    while (1) {

        if (vold_connect()) {
            monitor();

            if (sock)
                close(sock);
        }
        sleep(3);
    }
    return NULL;
}

extern void vold_set_callbacks(struct vold_callbacks* callbacks);
extern void vold_set_automount(int automount);

// start the client thread
void vold_client_start(struct vold_callbacks* callbacks, int automount) {

    if (sock > 0) {
        return;
    }

    pthread_mutex_lock(&mutex);

    vold_set_callbacks(callbacks);

    pthread_t vold_event_thread;
    pthread_create(&vold_event_thread, NULL, &event_thread_func, NULL);
    pthread_cond_wait(&completion, &mutex);
    pthread_mutex_unlock(&mutex);

    vold_update_volumes();

    if (automount) {
        vold_mount_all();
    }
    vold_set_automount(automount);
}

// send a command to vold. waits for completion and returns result
// code if wait is 1, otherwise returns zero immediately.
int vold_command(int len, const char** command, int wait) {

    char final_cmd[255] = "0 "; /* 0 is a (now required) sequence number */
    int i;
    size_t sz;
    int ret = 0;

    if (!vold_connect()) {
        return -1;
    }

    for (i = 0; i < len; i++) {
        char *cmp;

        if (!strchr(command[i], ' '))
            asprintf(&cmp, "%s%s", command[i], (i == (len -1)) ? "" : " ");
        else
            asprintf(&cmp, "\"%s\"%s", command[i], (i == (len -1)) ? "" : " ");

        sz = strlcat(final_cmd, cmp, sizeof(final_cmd));

        if (sz >= sizeof(final_cmd)) {
            LOGE("command syntax error  sz=%d size=%d", sz, sizeof(final_cmd));
            free(cmp);
            return -1;
        }
        free(cmp);
    }

    // only one writer at a time
    pthread_mutex_lock(&mutex);
    if (write(sock, final_cmd, strlen(final_cmd) + 1) < 0) {
        LOGE("Unable to send command to vold!\n");
        ret = -1;
    }
    cmd_inflight++;

    if (wait) {
        while (cmd_inflight) {
            // wait for completion
            pthread_cond_wait(&completion, &mutex);
            ret = cmd_result;
        }
    }
    pthread_mutex_unlock(&mutex);

    return ret == ResponseCode::CommandOkay ? 0 : -1;
}
