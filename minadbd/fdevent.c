/* http://frotznet.googlecode.com/svn/trunk/utils/fdevent.c
**
** Copyright 2006, Brian Swetland <swetland@frotz.net>
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#include <sys/ioctl.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <fcntl.h>

#include <stdarg.h>
#include <stddef.h>

#include "fdevent.h"
#include "transport.h"
#include "sysdeps.h"


/* !!! Do not enable DEBUG for the adb that will run as the server:
** both stdout and stderr are used to communicate between the client
** and server. Any extra output will cause failures.
*/
#define DEBUG 0   /* non-0 will break adb server */

// This socket is used when a subproc shell service exists.
// It wakes up the fdevent_loop() and cause the correct handling
// of the shell's pseudo-tty master. I.e. force close it.
int SHELL_EXIT_NOTIFY_FD = -1;

static void fatal(const char *fn, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:", fn);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    abort();
}

#define FATAL(x...) fatal(__FUNCTION__, x)

#if DEBUG
#define D(...) \
    do { \
        adb_mutex_lock(&D_lock);               \
        int save_errno = errno;                \
        fprintf(stderr, "%s::%s():", __FILE__, __FUNCTION__);  \
        errno = save_errno;                    \
        fprintf(stderr, __VA_ARGS__);          \
        adb_mutex_unlock(&D_lock);             \
        errno = save_errno;                    \
    } while(0)
static void dump_fde(fdevent *fde, const char *info)
{
    adb_mutex_lock(&D_lock);
    fprintf(stderr,"FDE #%03d %c%c%c %s\n", fde->fd,
            fde->state & FDE_READ ? 'R' : ' ',
            fde->state & FDE_WRITE ? 'W' : ' ',
            fde->state & FDE_ERROR ? 'E' : ' ',
            info);
    adb_mutex_unlock(&D_lock);
}
#else
#define D(...) ((void)0)
#define dump_fde(fde, info) do { } while(0)
#endif

#define FDE_EVENTMASK  0x00ff
#define FDE_STATEMASK  0xff00

#define FDE_ACTIVE     0x0100
#define FDE_PENDING    0x0200
#define FDE_CREATED    0x0400

static void fdevent_plist_enqueue(fdevent *node);
static void fdevent_plist_remove(fdevent *node);
static fdevent *fdevent_plist_dequeue(void);
static void fdevent_subproc_event_func(int fd, unsigned events, void *userdata);

static fdevent list_pending = {
    .next = &list_pending,
    .prev = &list_pending,
};

static fdevent **fd_table = 0;
static int fd_table_max = 0;

#ifdef CRAPTASTIC
//HAVE_EPOLL

#include <sys/epoll.h>

static int epoll_fd = -1;

static void fdevent_init()
{
        /* XXX: what's a good size for the passed in hint? */
    epoll_fd = epoll_create(256);

    if(epoll_fd < 0) {
        perror("epoll_create() failed");
        exit(1);
    }

        /* mark for close-on-exec */
    fcntl(epoll_fd, F_SETFD, FD_CLOEXEC);
}

static void fdevent_connect(fdevent *fde)
{
    struct epoll_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.events = 0;
    ev.data.ptr = fde;

#if 0
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fde->fd, &ev)) {
        perror("epoll_ctl() failed\n");
        exit(1);
    }
#endif
}

static void fdevent_disconnect(fdevent *fde)
{
    struct epoll_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.events = 0;
    ev.data.ptr = fde;

        /* technically we only need to delete if we
        ** were actively monitoring events, but let's
        ** be aggressive and do it anyway, just in case
        ** something's out of sync
        */
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fde->fd, &ev);
}

static void fdevent_update(fdevent *fde, unsigned events)
{
    struct epoll_event ev;
    int active;

    active = (fde->state & FDE_EVENTMASK) != 0;

    memset(&ev, 0, sizeof(ev));
    ev.events = 0;
    ev.data.ptr = fde;

    if(events & FDE_READ) ev.events |= EPOLLIN;
    if(events & FDE_WRITE) ev.events |= EPOLLOUT;
    if(events & FDE_ERROR) ev.events |= (EPOLLERR | EPOLLHUP);

    fde->state = (fde->state & FDE_STATEMASK) | events;

    if(active) {
            /* we're already active. if we're changing to *no*
            ** events being monitored, we need to delete, otherwise
            ** we need to just modify
            */
        if(ev.events) {
            if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fde->fd, &ev)) {
                perror("epoll_ctl() failed\n");
                exit(1);
            }
        } else {
            if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fde->fd, &ev)) {
                perror("epoll_ctl() failed\n");
                exit(1);
            }
        }
    } else {
            /* we're not active.  if we're watching events, we need
            ** to add, otherwise we can just do nothing
            */
        if(ev.events) {
            if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fde->fd, &ev)) {
                perror("epoll_ctl() failed\n");
                exit(1);
            }
        }
    }
}

static void fdevent_process()
{
    struct epoll_event events[256];
    fdevent *fde;
    int i, n;

    n = epoll_wait(epoll_fd, events, 256, -1);

    if(n < 0) {
        if(errno == EINTR) return;
        perror("epoll_wait");
        exit(1);
    }

    for(i = 0; i < n; i++) {
        struct epoll_event *ev = events + i;
        fde = ev->data.ptr;

        if(ev->events & EPOLLIN) {
            fde->events |= FDE_READ;
        }
        if(ev->events & EPOLLOUT) {
            fde->events |= FDE_WRITE;
        }
        if(ev->events & (EPOLLERR | EPOLLHUP)) {
            fde->events |= FDE_ERROR;
        }
        if(fde->events) {
            if(fde->state & FDE_PENDING) continue;
            fde->state |= FDE_PENDING;
            fdevent_plist_enqueue(fde);
        }
    }
}

#else /* USE_SELECT */

#ifdef HAVE_WINSOCK
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

static fd_set read_fds;
static fd_set write_fds;
static fd_set error_fds;

static int select_n = 0;

static void fdevent_init(void)
{
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&error_fds);
}

static void fdevent_connect(fdevent *fde)
{
    if(fde->fd >= select_n) {
        select_n = fde->fd + 1;
    }
}

static void fdevent_disconnect(fdevent *fde)
{
    int i, n;

    FD_CLR(fde->fd, &read_fds);
    FD_CLR(fde->fd, &write_fds);
    FD_CLR(fde->fd, &error_fds);

    for(n = 0, i = 0; i < select_n; i++) {
        if(fd_table[i] != 0) n = i;
    }
    select_n = n + 1;
}

static void fdevent_update(fdevent *fde, unsigned events)
{
    if(events & FDE_READ) {
        FD_SET(fde->fd, &read_fds);
    } else {
        FD_CLR(fde->fd, &read_fds);
    }
    if(events & FDE_WRITE) {
        FD_SET(fde->fd, &write_fds);
    } else {
        FD_CLR(fde->fd, &write_fds);
    }
    if(events & FDE_ERROR) {
        FD_SET(fde->fd, &error_fds);
    } else {
        FD_CLR(fde->fd, &error_fds);
    }

    fde->state = (fde->state & FDE_STATEMASK) | events;
}

/* Looks at fd_table[] for bad FDs and sets bit in fds.
** Returns the number of bad FDs.
*/
static int fdevent_fd_check(fd_set *fds)
{
    int i, n = 0;
    fdevent *fde;

    for(i = 0; i < select_n; i++) {
        fde = fd_table[i];
        if(fde == 0) continue;
        if(fcntl(i, F_GETFL, NULL) < 0) {
            FD_SET(i, fds);
            n++;
            // fde->state |= FDE_DONT_CLOSE;

        }
    }
    return n;
}

#if !DEBUG
static inline void dump_all_fds(const char *extra_msg) {}
#else
static void dump_all_fds(const char *extra_msg)
{
int i;
    fdevent *fde;
    // per fd: 4 digits (but really: log10(FD_SETSIZE)), 1 staus, 1 blank
    char msg_buff[FD_SETSIZE*6 + 1], *pb=msg_buff;
    size_t max_chars = FD_SETSIZE * 6 + 1;
    int printed_out;
#define SAFE_SPRINTF(...)                                                    \
    do {                                                                     \
        printed_out = snprintf(pb, max_chars, __VA_ARGS__);                  \
        if (printed_out <= 0) {                                              \
            D("... snprintf failed.\n");                                     \
            return;                                                          \
        }                                                                    \
        if (max_chars < (unsigned int)printed_out) {                         \
            D("... snprintf out of space.\n");                               \
            return;                                                          \
        }                                                                    \
        pb += printed_out;                                                   \
        max_chars -= printed_out;                                            \
    } while(0)

    for(i = 0; i < select_n; i++) {
        fde = fd_table[i];
        SAFE_SPRINTF("%d", i);
        if(fde == 0) {
            SAFE_SPRINTF("? ");
            continue;
        }
        if(fcntl(i, F_GETFL, NULL) < 0) {
            SAFE_SPRINTF("b");
        }
        SAFE_SPRINTF(" ");
    }
    D("%s fd_table[]->fd = {%s}\n", extra_msg, msg_buff);
}
#endif

static void fdevent_process()
{
    int i, n;
    fdevent *fde;
    unsigned events;
    fd_set rfd, wfd, efd;

    memcpy(&rfd, &read_fds, sizeof(fd_set));
    memcpy(&wfd, &write_fds, sizeof(fd_set));
    memcpy(&efd, &error_fds, sizeof(fd_set));

    dump_all_fds("pre select()");

    n = select(select_n, &rfd, &wfd, &efd, NULL);
    int saved_errno = errno;
    D("select() returned n=%d, errno=%d\n", n, n<0?saved_errno:0);

    dump_all_fds("post select()");

    if(n < 0) {
        switch(saved_errno) {
        case EINTR: return;
        case EBADF:
            // Can't trust the FD sets after an error.
            FD_ZERO(&wfd);
            FD_ZERO(&efd);
            FD_ZERO(&rfd);
            break;
        default:
            D("Unexpected select() error=%d\n", saved_errno);
            return;
        }
    }
    if(n <= 0) {
        // We fake a read, as the rest of the code assumes
        // that errors will be detected at that point.
        n = fdevent_fd_check(&rfd);
    }

    for(i = 0; (i < select_n) && (n > 0); i++) {
        events = 0;
        if(FD_ISSET(i, &rfd)) { events |= FDE_READ; n--; }
        if(FD_ISSET(i, &wfd)) { events |= FDE_WRITE; n--; }
        if(FD_ISSET(i, &efd)) { events |= FDE_ERROR; n--; }

        if(events) {
            fde = fd_table[i];
            if(fde == 0)
              FATAL("missing fde for fd %d\n", i);

            fde->events |= events;

            D("got events fde->fd=%d events=%04x, state=%04x\n",
                fde->fd, fde->events, fde->state);
            if(fde->state & FDE_PENDING) continue;
            fde->state |= FDE_PENDING;
            fdevent_plist_enqueue(fde);
        }
    }
}

#endif

static void fdevent_register(fdevent *fde)
{
    if(fde->fd < 0) {
        FATAL("bogus negative fd (%d)\n", fde->fd);
    }

    if(fde->fd >= fd_table_max) {
        int oldmax = fd_table_max;
        if(fde->fd > 32000) {
            FATAL("bogus huuuuge fd (%d)\n", fde->fd);
        }
        if(fd_table_max == 0) {
            fdevent_init();
            fd_table_max = 256;
        }
        while(fd_table_max <= fde->fd) {
            fd_table_max *= 2;
        }
        fd_table = realloc(fd_table, sizeof(fdevent*) * fd_table_max);
        if(fd_table == 0) {
            FATAL("could not expand fd_table to %d entries\n", fd_table_max);
        }
        memset(fd_table + oldmax, 0, sizeof(int) * (fd_table_max - oldmax));
    }

    fd_table[fde->fd] = fde;
}

static void fdevent_unregister(fdevent *fde)
{
    if((fde->fd < 0) || (fde->fd >= fd_table_max)) {
        FATAL("fd out of range (%d)\n", fde->fd);
    }

    if(fd_table[fde->fd] != fde) {
        FATAL("fd_table out of sync [%d]\n", fde->fd);
    }

    fd_table[fde->fd] = 0;

    if(!(fde->state & FDE_DONT_CLOSE)) {
        dump_fde(fde, "close");
        adb_close(fde->fd);
    }
}

static void fdevent_plist_enqueue(fdevent *node)
{
    fdevent *list = &list_pending;

    node->next = list;
    node->prev = list->prev;
    node->prev->next = node;
    list->prev = node;
}

static void fdevent_plist_remove(fdevent *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = 0;
    node->prev = 0;
}

static fdevent *fdevent_plist_dequeue(void)
{
    fdevent *list = &list_pending;
    fdevent *node = list->next;

    if(node == list) return 0;

    list->next = node->next;
    list->next->prev = list;
    node->next = 0;
    node->prev = 0;

    return node;
}

static void fdevent_call_fdfunc(fdevent* fde)
{
    unsigned events = fde->events;
    fde->events = 0;
    if(!(fde->state & FDE_PENDING)) return;
    fde->state &= (~FDE_PENDING);
    dump_fde(fde, "callback");
    fde->func(fde->fd, events, fde->arg);
}

static void fdevent_subproc_event_func(int fd, unsigned ev, void *userdata)
{

    D("subproc handling on fd=%d ev=%04x\n", fd, ev);

    // Hook oneself back into the fde's suitable for select() on read.
    if((fd < 0) || (fd >= fd_table_max)) {
        FATAL("fd %d out of range for fd_table \n", fd);
    }
    fdevent *fde = fd_table[fd];
    fdevent_add(fde, FDE_READ);

    if(ev & FDE_READ){
      int subproc_fd;

      if(readx(fd, &subproc_fd, sizeof(subproc_fd))) {
          FATAL("Failed to read the subproc's fd from fd=%d\n", fd);
      }
      if((subproc_fd < 0) || (subproc_fd >= fd_table_max)) {
          D("subproc_fd %d out of range 0, fd_table_max=%d\n",
            subproc_fd, fd_table_max);
          return;
      }
      fdevent *subproc_fde = fd_table[subproc_fd];
      if(!subproc_fde) {
          D("subproc_fd %d cleared from fd_table\n", subproc_fd);
          return;
      }
      if(subproc_fde->fd != subproc_fd) {
          // Already reallocated?
          D("subproc_fd %d != fd_table[].fd %d\n", subproc_fd, subproc_fde->fd);
          return;
      }

      subproc_fde->force_eof = 1;

      int rcount = 0;
      ioctl(subproc_fd, FIONREAD, &rcount);
      D("subproc with fd=%d  has rcount=%d err=%d\n",
        subproc_fd, rcount, errno);

      if(rcount) {
        // If there is data left, it will show up in the select().
        // This works because there is no other thread reading that
        // data when in this fd_func().
        return;
      }

      D("subproc_fde.state=%04x\n", subproc_fde->state);
      subproc_fde->events |= FDE_READ;
      if(subproc_fde->state & FDE_PENDING) {
        return;
      }
      subproc_fde->state |= FDE_PENDING;
      fdevent_call_fdfunc(subproc_fde);
    }
}

fdevent *fdevent_create(int fd, fd_func func, void *arg)
{
    fdevent *fde = (fdevent*) malloc(sizeof(fdevent));
    if(fde == 0) return 0;
    fdevent_install(fde, fd, func, arg);
    fde->state |= FDE_CREATED;
    return fde;
}

void fdevent_destroy(fdevent *fde)
{
    if(fde == 0) return;
    if(!(fde->state & FDE_CREATED)) {
        FATAL("fde %p not created by fdevent_create()\n", fde);
    }
    fdevent_remove(fde);
}

void fdevent_install(fdevent *fde, int fd, fd_func func, void *arg)
{
    memset(fde, 0, sizeof(fdevent));
    fde->state = FDE_ACTIVE;
    fde->fd = fd;
    fde->force_eof = 0;
    fde->func = func;
    fde->arg = arg;

#ifndef HAVE_WINSOCK
    fcntl(fd, F_SETFL, O_NONBLOCK);
#endif
    fdevent_register(fde);
    dump_fde(fde, "connect");
    fdevent_connect(fde);
    fde->state |= FDE_ACTIVE;
}

void fdevent_remove(fdevent *fde)
{
    if(fde->state & FDE_PENDING) {
        fdevent_plist_remove(fde);
    }

    if(fde->state & FDE_ACTIVE) {
        fdevent_disconnect(fde);
        dump_fde(fde, "disconnect");
        fdevent_unregister(fde);
    }

    fde->state = 0;
    fde->events = 0;
}


void fdevent_set(fdevent *fde, unsigned events)
{
    events &= FDE_EVENTMASK;

    if((fde->state & FDE_EVENTMASK) == events) return;

    if(fde->state & FDE_ACTIVE) {
        fdevent_update(fde, events);
        dump_fde(fde, "update");
    }

    fde->state = (fde->state & FDE_STATEMASK) | events;

    if(fde->state & FDE_PENDING) {
            /* if we're pending, make sure
            ** we don't signal an event that
            ** is no longer wanted.
            */
        fde->events &= (~events);
        if(fde->events == 0) {
            fdevent_plist_remove(fde);
            fde->state &= (~FDE_PENDING);
        }
    }
}

void fdevent_add(fdevent *fde, unsigned events)
{
    fdevent_set(
        fde, (fde->state & FDE_EVENTMASK) | (events & FDE_EVENTMASK));
}

void fdevent_del(fdevent *fde, unsigned events)
{
    fdevent_set(
        fde, (fde->state & FDE_EVENTMASK) & (~(events & FDE_EVENTMASK)));
}

void fdevent_subproc_setup()
{
    int s[2];

    if(adb_socketpair(s)) {
        FATAL("cannot create shell-exit socket-pair\n");
    }
    SHELL_EXIT_NOTIFY_FD = s[0];
    fdevent *fde;
    fde = fdevent_create(s[1], fdevent_subproc_event_func, NULL);
    if(!fde)
      FATAL("cannot create fdevent for shell-exit handler\n");
    fdevent_add(fde, FDE_READ);
}

void fdevent_loop()
{
    fdevent *fde;
    fdevent_subproc_setup();

    for(;;) {
        D("--- ---- waiting for events\n");

        fdevent_process();

        while((fde = fdevent_plist_dequeue())) {
            fdevent_call_fdfunc(fde);
        }
    }
}
