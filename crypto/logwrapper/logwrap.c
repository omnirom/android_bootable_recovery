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

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <pthread.h>

#include <logwrap/logwrap.h>
#include "private/android_filesystem_config.h"
#include "cutils/log.h"
#include <cutils/klog.h>

#define ARRAY_SIZE(x)   (sizeof(x) / sizeof(*(x)))
#define MIN(a,b) (((a)<(b))?(a):(b))

static pthread_mutex_t fd_mutex = PTHREAD_MUTEX_INITIALIZER;

#define ERROR(fmt, args...)                                                   \
do {                                                                          \
    fprintf(stderr, fmt, ## args);                                            \
    ALOG(LOG_ERROR, "logwrapper", fmt, ## args);                              \
} while(0)

#define FATAL_CHILD(fmt, args...)                                             \
do {                                                                          \
    ERROR(fmt, ## args);                                                      \
    _exit(-1);                                                                \
} while(0)

#define MAX_KLOG_TAG 16

/* This is a simple buffer that holds up to the first beginning_buf->buf_size
 * bytes of output from a command.
 */
#define BEGINNING_BUF_SIZE 0x1000
struct beginning_buf {
    char *buf;
    size_t alloc_len;
    /* buf_size is the usable space, which is one less than the allocated size */
    size_t buf_size;
    size_t used_len;
};

/* This is a circular buf that holds up to the last ending_buf->buf_size bytes
 * of output from a command after the first beginning_buf->buf_size bytes
 * (which are held in beginning_buf above).
 */
#define ENDING_BUF_SIZE 0x1000
struct ending_buf {
    char *buf;
    ssize_t alloc_len;
    /* buf_size is the usable space, which is one less than the allocated size */
    ssize_t buf_size;
    ssize_t used_len;
    /* read and write offsets into the circular buffer */
    int read;
    int write;
};

 /* A structure to hold all the abbreviated buf data */
struct abbr_buf {
    struct beginning_buf b_buf;
    struct ending_buf e_buf;
    int beginning_buf_full;
};

/* Collect all the various bits of info needed for logging in one place. */
struct log_info {
    int log_target;
    char klog_fmt[MAX_KLOG_TAG * 2];
    char *btag;
    bool abbreviated;
    FILE *fp;
    struct abbr_buf a_buf;
};

/* Forware declaration */
static void add_line_to_abbr_buf(struct abbr_buf *a_buf, char *linebuf, int linelen);

/* Return 0 on success, and 1 when full */
static int add_line_to_linear_buf(struct beginning_buf *b_buf,
                                   char *line, ssize_t line_len)
{
    size_t new_len;
    char *new_buf;
    int full = 0;

    if ((line_len + b_buf->used_len) > b_buf->buf_size) {
        full = 1;
    } else {
        /* Add to the end of the buf */
        memcpy(b_buf->buf + b_buf->used_len, line, line_len);
        b_buf->used_len += line_len;
    }

    return full;
}

static void add_line_to_circular_buf(struct ending_buf *e_buf,
                                     char *line, ssize_t line_len)
{
    ssize_t free_len;
    ssize_t needed_space;
    char *new_buf;
    int cnt;

    if (e_buf->buf == NULL) {
        return;
    }

   if (line_len > e_buf->buf_size) {
       return;
   }

    free_len = e_buf->buf_size - e_buf->used_len;

    if (line_len > free_len) {
        /* remove oldest entries at read, and move read to make
         * room for the new string */
        needed_space = line_len - free_len;
        e_buf->read = (e_buf->read + needed_space) % e_buf->buf_size;
        e_buf->used_len -= needed_space;
    }

    /* Copy the line into the circular buffer, dealing with possible
     * wraparound.
     */
    cnt = MIN(line_len, e_buf->buf_size - e_buf->write);
    memcpy(e_buf->buf + e_buf->write, line, cnt);
    if (cnt < line_len) {
        memcpy(e_buf->buf, line + cnt, line_len - cnt);
    }
    e_buf->used_len += line_len;
    e_buf->write = (e_buf->write + line_len) % e_buf->buf_size;
}

/* Log directly to the specified log */
static void do_log_line(struct log_info *log_info, char *line) {
    if (log_info->log_target & LOG_KLOG) {
        klog_write(6, log_info->klog_fmt, line);
    }
    if (log_info->log_target & LOG_ALOG) {
        ALOG(LOG_INFO, log_info->btag, "%s", line);
    }
    if (log_info->log_target & LOG_FILE) {
        fprintf(log_info->fp, "%s\n", line);
    }
}

/* Log to either the abbreviated buf, or directly to the specified log
 * via do_log_line() above.
 */
static void log_line(struct log_info *log_info, char *line, int len) {
    if (log_info->abbreviated) {
        add_line_to_abbr_buf(&log_info->a_buf, line, len);
    } else {
        do_log_line(log_info, line);
    }
}

/*
 * The kernel will take a maximum of 1024 bytes in any single write to
 * the kernel logging device file, so find and print each line one at
 * a time.  The allocated size for buf should be at least 1 byte larger
 * than buf_size (the usable size of the buffer) to make sure there is
 * room to temporarily stuff a null byte to terminate a line for logging.
 */
static void print_buf_lines(struct log_info *log_info, char *buf, int buf_size)
{
    char *line_start;
    char c;
    int line_len;
    int i;

    line_start = buf;
    for (i = 0; i < buf_size; i++) {
        if (*(buf + i) == '\n') {
            /* Found a line ending, print the line and compute new line_start */
            /* Save the next char and replace with \0 */
            c = *(buf + i + 1);
            *(buf + i + 1) = '\0';
            do_log_line(log_info, line_start);
            /* Restore the saved char */
            *(buf + i + 1) = c;
            line_start = buf + i + 1;
        } else if (*(buf + i) == '\0') {
            /* The end of the buffer, print the last bit */
            do_log_line(log_info, line_start);
            break;
        }
    }
    /* If the buffer was completely full, and didn't end with a newline, just
     * ignore the partial last line.
     */
}

static void init_abbr_buf(struct abbr_buf *a_buf) {
    char *new_buf;

    memset(a_buf, 0, sizeof(struct abbr_buf));
    new_buf = malloc(BEGINNING_BUF_SIZE);
    if (new_buf) {
        a_buf->b_buf.buf = new_buf;
        a_buf->b_buf.alloc_len = BEGINNING_BUF_SIZE;
        a_buf->b_buf.buf_size = BEGINNING_BUF_SIZE - 1;
    }
    new_buf = malloc(ENDING_BUF_SIZE);
    if (new_buf) {
        a_buf->e_buf.buf = new_buf;
        a_buf->e_buf.alloc_len = ENDING_BUF_SIZE;
        a_buf->e_buf.buf_size = ENDING_BUF_SIZE - 1;
    }
}

static void free_abbr_buf(struct abbr_buf *a_buf) {
    free(a_buf->b_buf.buf);
    free(a_buf->e_buf.buf);
}

static void add_line_to_abbr_buf(struct abbr_buf *a_buf, char *linebuf, int linelen) {
    if (!a_buf->beginning_buf_full) {
        a_buf->beginning_buf_full =
            add_line_to_linear_buf(&a_buf->b_buf, linebuf, linelen);
    }
    if (a_buf->beginning_buf_full) {
        add_line_to_circular_buf(&a_buf->e_buf, linebuf, linelen);
    }
}

static void print_abbr_buf(struct log_info *log_info) {
    struct abbr_buf *a_buf = &log_info->a_buf;

    /* Add the abbreviated output to the kernel log */
    if (a_buf->b_buf.alloc_len) {
        print_buf_lines(log_info, a_buf->b_buf.buf, a_buf->b_buf.used_len);
    }

    /* Print an ellipsis to indicate that the buffer has wrapped or
     * is full, and some data was not logged.
     */
    if (a_buf->e_buf.used_len == a_buf->e_buf.buf_size) {
        do_log_line(log_info, "...\n");
    }

    if (a_buf->e_buf.used_len == 0) {
        return;
    }

    /* Simplest way to print the circular buffer is allocate a second buf
     * of the same size, and memcpy it so it's a simple linear buffer,
     * and then cal print_buf_lines on it */
    if (a_buf->e_buf.read < a_buf->e_buf.write) {
        /* no wrap around, just print it */
        print_buf_lines(log_info, a_buf->e_buf.buf + a_buf->e_buf.read,
                        a_buf->e_buf.used_len);
    } else {
        /* The circular buffer will always have at least 1 byte unused,
         * so by allocating alloc_len here we will have at least
         * 1 byte of space available as required by print_buf_lines().
         */
        char * nbuf = malloc(a_buf->e_buf.alloc_len);
        if (!nbuf) {
            return;
        }
        int first_chunk_len = a_buf->e_buf.buf_size - a_buf->e_buf.read;
        memcpy(nbuf, a_buf->e_buf.buf + a_buf->e_buf.read, first_chunk_len);
        /* copy second chunk */
        memcpy(nbuf + first_chunk_len, a_buf->e_buf.buf, a_buf->e_buf.write);
        print_buf_lines(log_info, nbuf, first_chunk_len + a_buf->e_buf.write);
        free(nbuf);
    }
}

static int parent(const char *tag, int parent_read, pid_t pid,
        int *chld_sts, int log_target, bool abbreviated, char *file_path) {
    int status = 0;
    char buffer[4096];
    struct pollfd poll_fds[] = {
        [0] = {
            .fd = parent_read,
            .events = POLLIN,
        },
    };
    int rc = 0;
    int fd;

    struct log_info log_info;

    int a = 0;  // start index of unprocessed data
    int b = 0;  // end index of unprocessed data
    int sz;
    bool found_child = false;
    char tmpbuf[256];

    log_info.btag = basename(tag);
    if (!log_info.btag) {
        log_info.btag = (char*) tag;
    }

    if (abbreviated && (log_target == LOG_NONE)) {
        abbreviated = 0;
    }
    if (abbreviated) {
        init_abbr_buf(&log_info.a_buf);
    }

    if (log_target & LOG_KLOG) {
        snprintf(log_info.klog_fmt, sizeof(log_info.klog_fmt),
                 "<6>%.*s: %%s", MAX_KLOG_TAG, log_info.btag);
    }

    if ((log_target & LOG_FILE) && !file_path) {
        /* No file_path specified, clear the LOG_FILE bit */
        log_target &= ~LOG_FILE;
    }

    if (log_target & LOG_FILE) {
        fd = open(file_path, O_WRONLY | O_CREAT, 0664);
        if (fd < 0) {
            ERROR("Cannot log to file %s\n", file_path);
            log_target &= ~LOG_FILE;
        } else {
            lseek(fd, 0, SEEK_END);
            log_info.fp = fdopen(fd, "a");
        }
    }

    log_info.log_target = log_target;
    log_info.abbreviated = abbreviated;

    while (!found_child) {
        if (TEMP_FAILURE_RETRY(poll(poll_fds, ARRAY_SIZE(poll_fds), -1)) < 0) {
            ERROR("poll failed\n");
            rc = -1;
            goto err_poll;
        }

        if (poll_fds[0].revents & POLLIN) {
            sz = read(parent_read, &buffer[b], sizeof(buffer) - 1 - b);

            sz += b;
            // Log one line at a time
            for (b = 0; b < sz; b++) {
                if (buffer[b] == '\r') {
                    if (abbreviated) {
                        /* The abbreviated logging code uses newline as
                         * the line separator.  Lucikly, the pty layer
                         * helpfully cooks the output of the command
                         * being run and inserts a CR before NL.  So
                         * I just change it to NL here when doing
                         * abbreviated logging.
                         */
                        buffer[b] = '\n';
                    } else {
                        buffer[b] = '\0';
                    }
                } else if (buffer[b] == '\n') {
                    buffer[b] = '\0';
                    log_line(&log_info, &buffer[a], b - a);
                    a = b + 1;
                }
            }

            if (a == 0 && b == sizeof(buffer) - 1) {
                // buffer is full, flush
                buffer[b] = '\0';
                log_line(&log_info, &buffer[a], b - a);
                b = 0;
            } else if (a != b) {
                // Keep left-overs
                b -= a;
                memmove(buffer, &buffer[a], b);
                a = 0;
            } else {
                a = 0;
                b = 0;
            }
        }

        if (poll_fds[0].revents & POLLHUP) {
            int ret;

            ret = waitpid(pid, &status, WNOHANG);
            if (ret < 0) {
                rc = errno;
                ALOG(LOG_ERROR, "logwrap", "waitpid failed with %s\n", strerror(errno));
                goto err_waitpid;
            }
            if (ret > 0) {
                found_child = true;
            }
        }
    }

    if (chld_sts != NULL) {
        *chld_sts = status;
    } else {
      if (WIFEXITED(status))
        rc = WEXITSTATUS(status);
      else
        rc = -ECHILD;
    }

    // Flush remaining data
    if (a != b) {
      buffer[b] = '\0';
      log_line(&log_info, &buffer[a], b - a);
    }

    /* All the output has been processed, time to dump the abbreviated output */
    if (abbreviated) {
        print_abbr_buf(&log_info);
    }

    if (WIFEXITED(status)) {
      if (WEXITSTATUS(status)) {
        snprintf(tmpbuf, sizeof(tmpbuf),
                 "%s terminated by exit(%d)\n", log_info.btag, WEXITSTATUS(status));
        do_log_line(&log_info, tmpbuf);
      }
    } else {
      if (WIFSIGNALED(status)) {
        snprintf(tmpbuf, sizeof(tmpbuf),
                       "%s terminated by signal %d\n", log_info.btag, WTERMSIG(status));
        do_log_line(&log_info, tmpbuf);
      } else if (WIFSTOPPED(status)) {
        snprintf(tmpbuf, sizeof(tmpbuf),
                       "%s stopped by signal %d\n", log_info.btag, WSTOPSIG(status));
        do_log_line(&log_info, tmpbuf);
      }
    }

err_waitpid:
err_poll:
    if (log_target & LOG_FILE) {
        fclose(log_info.fp); /* Also closes underlying fd */
    }
    if (abbreviated) {
        free_abbr_buf(&log_info.a_buf);
    }
    return rc;
}

static void child(int argc, char* argv[]) {
    // create null terminated argv_child array
    char* argv_child[argc + 1];
    memcpy(argv_child, argv, argc * sizeof(char *));
    argv_child[argc] = NULL;

    if (execvp(argv_child[0], argv_child)) {
        FATAL_CHILD("executing %s failed: %s\n", argv_child[0],
                strerror(errno));
    }
}

int android_fork_execvp_ext(int argc, char* argv[], int *status, bool ignore_int_quit,
        int log_target, bool abbreviated, char *file_path) {
    pid_t pid;
    int parent_ptty;
    int child_ptty;
    char *child_devname = NULL;
    struct sigaction intact;
    struct sigaction quitact;
    sigset_t blockset;
    sigset_t oldset;
    int rc = 0;

    rc = pthread_mutex_lock(&fd_mutex);
    if (rc) {
        ERROR("failed to lock signal_fd mutex\n");
        goto err_lock;
    }

    /* Use ptty instead of socketpair so that STDOUT is not buffered */
    parent_ptty = open("/dev/ptmx", O_RDWR);
    if (parent_ptty < 0) {
        ERROR("Cannot create parent ptty\n");
        rc = -1;
        goto err_open;
    }

    if (grantpt(parent_ptty) || unlockpt(parent_ptty) ||
            ((child_devname = (char*)ptsname(parent_ptty)) == 0)) {
        ERROR("Problem with /dev/ptmx\n");
        rc = -1;
        goto err_ptty;
    }

    child_ptty = open(child_devname, O_RDWR);
    if (child_ptty < 0) {
        ERROR("Cannot open child_ptty\n");
        rc = -1;
        goto err_child_ptty;
    }

    sigemptyset(&blockset);
    sigaddset(&blockset, SIGINT);
    sigaddset(&blockset, SIGQUIT);
    pthread_sigmask(SIG_BLOCK, &blockset, &oldset);

    pid = fork();
    if (pid < 0) {
        close(child_ptty);
        ERROR("Failed to fork\n");
        rc = -1;
        goto err_fork;
    } else if (pid == 0) {
        pthread_mutex_unlock(&fd_mutex);
        pthread_sigmask(SIG_SETMASK, &oldset, NULL);
        close(parent_ptty);

        // redirect stdout and stderr
        dup2(child_ptty, 1);
        dup2(child_ptty, 2);
        close(child_ptty);

        child(argc, argv);
    } else {
        close(child_ptty);
        if (ignore_int_quit) {
            struct sigaction ignact;

            memset(&ignact, 0, sizeof(ignact));
            ignact.sa_handler = SIG_IGN;
            sigaction(SIGINT, &ignact, &intact);
            sigaction(SIGQUIT, &ignact, &quitact);
        }

        rc = parent(argv[0], parent_ptty, pid, status, log_target,
                    abbreviated, file_path);
    }

    if (ignore_int_quit) {
        sigaction(SIGINT, &intact, NULL);
        sigaction(SIGQUIT, &quitact, NULL);
    }
err_fork:
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
err_child_ptty:
err_ptty:
    close(parent_ptty);
err_open:
    pthread_mutex_unlock(&fd_mutex);
err_lock:
    return rc;
}
