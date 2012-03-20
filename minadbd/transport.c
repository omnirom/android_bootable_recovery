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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "sysdeps.h"

#define   TRACE_TAG  TRACE_TRANSPORT
#include "adb.h"

static void transport_unref(atransport *t);

static atransport transport_list = {
    .next = &transport_list,
    .prev = &transport_list,
};

ADB_MUTEX_DEFINE( transport_lock );

#if ADB_TRACE
#define MAX_DUMP_HEX_LEN 16
static void  dump_hex( const unsigned char*  ptr, size_t  len )
{
    int  nn, len2 = len;
    // Build a string instead of logging each character.
    // MAX chars in 2 digit hex, one space, MAX chars, one '\0'.
    char buffer[MAX_DUMP_HEX_LEN *2 + 1 + MAX_DUMP_HEX_LEN + 1 ], *pb = buffer;

    if (len2 > MAX_DUMP_HEX_LEN) len2 = MAX_DUMP_HEX_LEN;

    for (nn = 0; nn < len2; nn++) {
        sprintf(pb, "%02x", ptr[nn]);
        pb += 2;
    }
    sprintf(pb++, " ");

    for (nn = 0; nn < len2; nn++) {
        int  c = ptr[nn];
        if (c < 32 || c > 127)
            c = '.';
        *pb++ =  c;
    }
    *pb++ = '\0';
    DR("%s\n", buffer);
}
#endif

void
kick_transport(atransport*  t)
{
    if (t && !t->kicked)
    {
        int  kicked;

        adb_mutex_lock(&transport_lock);
        kicked = t->kicked;
        if (!kicked)
            t->kicked = 1;
        adb_mutex_unlock(&transport_lock);

        if (!kicked)
            t->kick(t);
    }
}

void
run_transport_disconnects(atransport*  t)
{
    adisconnect*  dis = t->disconnects.next;

    D("%s: run_transport_disconnects\n", t->serial);
    while (dis != &t->disconnects) {
        adisconnect*  next = dis->next;
        dis->func( dis->opaque, t );
        dis = next;
    }
}

#if ADB_TRACE
static void
dump_packet(const char* name, const char* func, apacket* p)
{
    unsigned  command = p->msg.command;
    int       len     = p->msg.data_length;
    char      cmd[9];
    char      arg0[12], arg1[12];
    int       n;

    for (n = 0; n < 4; n++) {
        int  b = (command >> (n*8)) & 255;
        if (b < 32 || b >= 127)
            break;
        cmd[n] = (char)b;
    }
    if (n == 4) {
        cmd[4] = 0;
    } else {
        /* There is some non-ASCII name in the command, so dump
            * the hexadecimal value instead */
        snprintf(cmd, sizeof cmd, "%08x", command);
    }

    if (p->msg.arg0 < 256U)
        snprintf(arg0, sizeof arg0, "%d", p->msg.arg0);
    else
        snprintf(arg0, sizeof arg0, "0x%x", p->msg.arg0);

    if (p->msg.arg1 < 256U)
        snprintf(arg1, sizeof arg1, "%d", p->msg.arg1);
    else
        snprintf(arg1, sizeof arg1, "0x%x", p->msg.arg1);

    D("%s: %s: [%s] arg0=%s arg1=%s (len=%d) ",
        name, func, cmd, arg0, arg1, len);
    dump_hex(p->data, len);
}
#endif /* ADB_TRACE */

static int
read_packet(int  fd, const char* name, apacket** ppacket)
{
    char *p = (char*)ppacket;  /* really read a packet address */
    int   r;
    int   len = sizeof(*ppacket);
    char  buff[8];
    if (!name) {
        snprintf(buff, sizeof buff, "fd=%d", fd);
        name = buff;
    }
    while(len > 0) {
        r = adb_read(fd, p, len);
        if(r > 0) {
            len -= r;
            p   += r;
        } else {
            D("%s: read_packet (fd=%d), error ret=%d errno=%d: %s\n", name, fd, r, errno, strerror(errno));
            if((r < 0) && (errno == EINTR)) continue;
            return -1;
        }
    }

#if ADB_TRACE
    if (ADB_TRACING) {
        dump_packet(name, "from remote", *ppacket);
    }
#endif
    return 0;
}

static int
write_packet(int  fd, const char* name, apacket** ppacket)
{
    char *p = (char*) ppacket;  /* we really write the packet address */
    int r, len = sizeof(ppacket);
    char buff[8];
    if (!name) {
        snprintf(buff, sizeof buff, "fd=%d", fd);
        name = buff;
    }

#if ADB_TRACE
    if (ADB_TRACING) {
        dump_packet(name, "to remote", *ppacket);
    }
#endif
    len = sizeof(ppacket);
    while(len > 0) {
        r = adb_write(fd, p, len);
        if(r > 0) {
            len -= r;
            p += r;
        } else {
            D("%s: write_packet (fd=%d) error ret=%d errno=%d: %s\n", name, fd, r, errno, strerror(errno));
            if((r < 0) && (errno == EINTR)) continue;
            return -1;
        }
    }
    return 0;
}

static void transport_socket_events(int fd, unsigned events, void *_t)
{
    atransport *t = _t;
    D("transport_socket_events(fd=%d, events=%04x,...)\n", fd, events);
    if(events & FDE_READ){
        apacket *p = 0;
        if(read_packet(fd, t->serial, &p)){
            D("%s: failed to read packet from transport socket on fd %d\n", t->serial, fd);
        } else {
            handle_packet(p, (atransport *) _t);
        }
    }
}

void send_packet(apacket *p, atransport *t)
{
    unsigned char *x;
    unsigned sum;
    unsigned count;

    p->msg.magic = p->msg.command ^ 0xffffffff;

    count = p->msg.data_length;
    x = (unsigned char *) p->data;
    sum = 0;
    while(count-- > 0){
        sum += *x++;
    }
    p->msg.data_check = sum;

    print_packet("send", p);

    if (t == NULL) {
        D("Transport is null \n");
        // Zap errno because print_packet() and other stuff have errno effect.
        errno = 0;
        fatal_errno("Transport is null");
    }

    if(write_packet(t->transport_socket, t->serial, &p)){
        fatal_errno("cannot enqueue packet on transport socket");
    }
}

/* The transport is opened by transport_register_func before
** the input and output threads are started.
**
** The output thread issues a SYNC(1, token) message to let
** the input thread know to start things up.  In the event
** of transport IO failure, the output thread will post a
** SYNC(0,0) message to ensure shutdown.
**
** The transport will not actually be closed until both
** threads exit, but the input thread will kick the transport
** on its way out to disconnect the underlying device.
*/

static void *output_thread(void *_t)
{
    atransport *t = _t;
    apacket *p;

    D("%s: starting transport output thread on fd %d, SYNC online (%d)\n",
       t->serial, t->fd, t->sync_token + 1);
    p = get_apacket();
    p->msg.command = A_SYNC;
    p->msg.arg0 = 1;
    p->msg.arg1 = ++(t->sync_token);
    p->msg.magic = A_SYNC ^ 0xffffffff;
    if(write_packet(t->fd, t->serial, &p)) {
        put_apacket(p);
        D("%s: failed to write SYNC packet\n", t->serial);
        goto oops;
    }

    D("%s: data pump started\n", t->serial);
    for(;;) {
        p = get_apacket();

        if(t->read_from_remote(p, t) == 0){
            D("%s: received remote packet, sending to transport\n",
              t->serial);
            if(write_packet(t->fd, t->serial, &p)){
                put_apacket(p);
                D("%s: failed to write apacket to transport\n", t->serial);
                goto oops;
            }
        } else {
            D("%s: remote read failed for transport\n", t->serial);
            put_apacket(p);
            break;
        }
    }

    D("%s: SYNC offline for transport\n", t->serial);
    p = get_apacket();
    p->msg.command = A_SYNC;
    p->msg.arg0 = 0;
    p->msg.arg1 = 0;
    p->msg.magic = A_SYNC ^ 0xffffffff;
    if(write_packet(t->fd, t->serial, &p)) {
        put_apacket(p);
        D("%s: failed to write SYNC apacket to transport", t->serial);
    }

oops:
    D("%s: transport output thread is exiting\n", t->serial);
    kick_transport(t);
    transport_unref(t);
    return 0;
}

static void *input_thread(void *_t)
{
    atransport *t = _t;
    apacket *p;
    int active = 0;

    D("%s: starting transport input thread, reading from fd %d\n",
       t->serial, t->fd);

    for(;;){
        if(read_packet(t->fd, t->serial, &p)) {
            D("%s: failed to read apacket from transport on fd %d\n",
               t->serial, t->fd );
            break;
        }
        if(p->msg.command == A_SYNC){
            if(p->msg.arg0 == 0) {
                D("%s: transport SYNC offline\n", t->serial);
                put_apacket(p);
                break;
            } else {
                if(p->msg.arg1 == t->sync_token) {
                    D("%s: transport SYNC online\n", t->serial);
                    active = 1;
                } else {
                    D("%s: transport ignoring SYNC %d != %d\n",
                      t->serial, p->msg.arg1, t->sync_token);
                }
            }
        } else {
            if(active) {
                D("%s: transport got packet, sending to remote\n", t->serial);
                t->write_to_remote(p, t);
            } else {
                D("%s: transport ignoring packet while offline\n", t->serial);
            }
        }

        put_apacket(p);
    }

    // this is necessary to avoid a race condition that occured when a transport closes
    // while a client socket is still active.
    close_all_sockets(t);

    D("%s: transport input thread is exiting, fd %d\n", t->serial, t->fd);
    kick_transport(t);
    transport_unref(t);
    return 0;
}


static int transport_registration_send = -1;
static int transport_registration_recv = -1;
static fdevent transport_registration_fde;

void  update_transports(void)
{
    // nothing to do on the device side
}

typedef struct tmsg tmsg;
struct tmsg
{
    atransport *transport;
    int         action;
};

static int
transport_read_action(int  fd, struct tmsg*  m)
{
    char *p   = (char*)m;
    int   len = sizeof(*m);
    int   r;

    while(len > 0) {
        r = adb_read(fd, p, len);
        if(r > 0) {
            len -= r;
            p   += r;
        } else {
            if((r < 0) && (errno == EINTR)) continue;
            D("transport_read_action: on fd %d, error %d: %s\n",
              fd, errno, strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int
transport_write_action(int  fd, struct tmsg*  m)
{
    char *p   = (char*)m;
    int   len = sizeof(*m);
    int   r;

    while(len > 0) {
        r = adb_write(fd, p, len);
        if(r > 0) {
            len -= r;
            p   += r;
        } else {
            if((r < 0) && (errno == EINTR)) continue;
            D("transport_write_action: on fd %d, error %d: %s\n",
              fd, errno, strerror(errno));
            return -1;
        }
    }
    return 0;
}

static void transport_registration_func(int _fd, unsigned ev, void *data)
{
    tmsg m;
    adb_thread_t output_thread_ptr;
    adb_thread_t input_thread_ptr;
    int s[2];
    atransport *t;

    if(!(ev & FDE_READ)) {
        return;
    }

    if(transport_read_action(_fd, &m)) {
        fatal_errno("cannot read transport registration socket");
    }

    t = m.transport;

    if(m.action == 0){
        D("transport: %s removing and free'ing %d\n", t->serial, t->transport_socket);

            /* IMPORTANT: the remove closes one half of the
            ** socket pair.  The close closes the other half.
            */
        fdevent_remove(&(t->transport_fde));
        adb_close(t->fd);

        adb_mutex_lock(&transport_lock);
        t->next->prev = t->prev;
        t->prev->next = t->next;
        adb_mutex_unlock(&transport_lock);

        run_transport_disconnects(t);

        if (t->product)
            free(t->product);
        if (t->serial)
            free(t->serial);

        memset(t,0xee,sizeof(atransport));
        free(t);

        update_transports();
        return;
    }

    /* don't create transport threads for inaccessible devices */
    if (t->connection_state != CS_NOPERM) {
        /* initial references are the two threads */
        t->ref_count = 2;

        if(adb_socketpair(s)) {
            fatal_errno("cannot open transport socketpair");
        }

        D("transport: %s (%d,%d) starting\n", t->serial, s[0], s[1]);

        t->transport_socket = s[0];
        t->fd = s[1];

        fdevent_install(&(t->transport_fde),
                        t->transport_socket,
                        transport_socket_events,
                        t);

        fdevent_set(&(t->transport_fde), FDE_READ);

        if(adb_thread_create(&input_thread_ptr, input_thread, t)){
            fatal_errno("cannot create input thread");
        }

        if(adb_thread_create(&output_thread_ptr, output_thread, t)){
            fatal_errno("cannot create output thread");
        }
    }

        /* put us on the master device list */
    adb_mutex_lock(&transport_lock);
    t->next = &transport_list;
    t->prev = transport_list.prev;
    t->next->prev = t;
    t->prev->next = t;
    adb_mutex_unlock(&transport_lock);

    t->disconnects.next = t->disconnects.prev = &t->disconnects;

    update_transports();
}

void init_transport_registration(void)
{
    int s[2];

    if(adb_socketpair(s)){
        fatal_errno("cannot open transport registration socketpair");
    }

    transport_registration_send = s[0];
    transport_registration_recv = s[1];

    fdevent_install(&transport_registration_fde,
                    transport_registration_recv,
                    transport_registration_func,
                    0);

    fdevent_set(&transport_registration_fde, FDE_READ);
}

/* the fdevent select pump is single threaded */
static void register_transport(atransport *transport)
{
    tmsg m;
    m.transport = transport;
    m.action = 1;
    D("transport: %s registered\n", transport->serial);
    if(transport_write_action(transport_registration_send, &m)) {
        fatal_errno("cannot write transport registration socket\n");
    }
}

static void remove_transport(atransport *transport)
{
    tmsg m;
    m.transport = transport;
    m.action = 0;
    D("transport: %s removed\n", transport->serial);
    if(transport_write_action(transport_registration_send, &m)) {
        fatal_errno("cannot write transport registration socket\n");
    }
}


static void transport_unref_locked(atransport *t)
{
    t->ref_count--;
    if (t->ref_count == 0) {
        D("transport: %s unref (kicking and closing)\n", t->serial);
        if (!t->kicked) {
            t->kicked = 1;
            t->kick(t);
        }
        t->close(t);
        remove_transport(t);
    } else {
        D("transport: %s unref (count=%d)\n", t->serial, t->ref_count);
    }
}

static void transport_unref(atransport *t)
{
    if (t) {
        adb_mutex_lock(&transport_lock);
        transport_unref_locked(t);
        adb_mutex_unlock(&transport_lock);
    }
}

void add_transport_disconnect(atransport*  t, adisconnect*  dis)
{
    adb_mutex_lock(&transport_lock);
    dis->next       = &t->disconnects;
    dis->prev       = dis->next->prev;
    dis->prev->next = dis;
    dis->next->prev = dis;
    adb_mutex_unlock(&transport_lock);
}

void remove_transport_disconnect(atransport*  t, adisconnect*  dis)
{
    dis->prev->next = dis->next;
    dis->next->prev = dis->prev;
    dis->next = dis->prev = dis;
}


atransport *acquire_one_transport(int state, transport_type ttype, const char* serial, char** error_out)
{
    atransport *t;
    atransport *result = NULL;
    int ambiguous = 0;

retry:
    if (error_out)
        *error_out = "device not found";

    adb_mutex_lock(&transport_lock);
    for (t = transport_list.next; t != &transport_list; t = t->next) {
        if (t->connection_state == CS_NOPERM) {
        if (error_out)
            *error_out = "insufficient permissions for device";
            continue;
        }

        /* check for matching serial number */
        if (serial) {
            if (t->serial && !strcmp(serial, t->serial)) {
                result = t;
                break;
            }
        } else {
            if (ttype == kTransportUsb && t->type == kTransportUsb) {
                if (result) {
                    if (error_out)
                        *error_out = "more than one device";
                    ambiguous = 1;
                    result = NULL;
                    break;
                }
                result = t;
            } else if (ttype == kTransportLocal && t->type == kTransportLocal) {
                if (result) {
                    if (error_out)
                        *error_out = "more than one emulator";
                    ambiguous = 1;
                    result = NULL;
                    break;
                }
                result = t;
            } else if (ttype == kTransportAny) {
                if (result) {
                    if (error_out)
                        *error_out = "more than one device and emulator";
                    ambiguous = 1;
                    result = NULL;
                    break;
                }
                result = t;
            }
        }
    }
    adb_mutex_unlock(&transport_lock);

    if (result) {
         /* offline devices are ignored -- they are either being born or dying */
        if (result && result->connection_state == CS_OFFLINE) {
            if (error_out)
                *error_out = "device offline";
            result = NULL;
        }
         /* check for required connection state */
        if (result && state != CS_ANY && result->connection_state != state) {
            if (error_out)
                *error_out = "invalid device state";
            result = NULL;
        }
    }

    if (result) {
        /* found one that we can take */
        if (error_out)
            *error_out = NULL;
    } else if (state != CS_ANY && (serial || !ambiguous)) {
        adb_sleep_ms(1000);
        goto retry;
    }

    return result;
}

void register_socket_transport(int s, const char *serial, int port, int local)
{
    atransport *t = calloc(1, sizeof(atransport));
    char buff[32];

    if (!serial) {
        snprintf(buff, sizeof buff, "T-%p", t);
        serial = buff;
    }
    D("transport: %s init'ing for socket %d, on port %d\n", serial, s, port);
    if ( init_socket_transport(t, s, port, local) < 0 ) {
        adb_close(s);
        free(t);
        return;
    }
    if(serial) {
        t->serial = strdup(serial);
    }
    register_transport(t);
}

void register_usb_transport(usb_handle *usb, const char *serial, unsigned writeable)
{
    atransport *t = calloc(1, sizeof(atransport));
    D("transport: %p init'ing for usb_handle %p (sn='%s')\n", t, usb,
      serial ? serial : "");
    init_usb_transport(t, usb, (writeable ? CS_OFFLINE : CS_NOPERM));
    if(serial) {
        t->serial = strdup(serial);
    }
    register_transport(t);
}

/* this should only be used for transports with connection_state == CS_NOPERM */
void unregister_usb_transport(usb_handle *usb)
{
    atransport *t;
    adb_mutex_lock(&transport_lock);
    for(t = transport_list.next; t != &transport_list; t = t->next) {
        if (t->usb == usb && t->connection_state == CS_NOPERM) {
            t->next->prev = t->prev;
            t->prev->next = t->next;
            break;
        }
     }
    adb_mutex_unlock(&transport_lock);
}

#undef TRACE_TAG
#define TRACE_TAG  TRACE_RWX

int readx(int fd, void *ptr, size_t len)
{
    char *p = ptr;
    int r;
#if ADB_TRACE
    int  len0 = len;
#endif
    D("readx: fd=%d wanted=%d\n", fd, (int)len);
    while(len > 0) {
        r = adb_read(fd, p, len);
        if(r > 0) {
            len -= r;
            p += r;
        } else {
            if (r < 0) {
                D("readx: fd=%d error %d: %s\n", fd, errno, strerror(errno));
                if (errno == EINTR)
                    continue;
            } else {
                D("readx: fd=%d disconnected\n", fd);
            }
            return -1;
        }
    }

#if ADB_TRACE
    D("readx: fd=%d wanted=%d got=%d\n", fd, len0, len0 - len);
    dump_hex( ptr, len0 );
#endif
    return 0;
}

int writex(int fd, const void *ptr, size_t len)
{
    char *p = (char*) ptr;
    int r;

#if ADB_TRACE
    D("writex: fd=%d len=%d: ", fd, (int)len);
    dump_hex( ptr, len );
#endif
    while(len > 0) {
        r = adb_write(fd, p, len);
        if(r > 0) {
            len -= r;
            p += r;
        } else {
            if (r < 0) {
                D("writex: fd=%d error %d: %s\n", fd, errno, strerror(errno));
                if (errno == EINTR)
                    continue;
            } else {
                D("writex: fd=%d disconnected\n", fd);
            }
            return -1;
        }
    }
    return 0;
}

int check_header(apacket *p)
{
    if(p->msg.magic != (p->msg.command ^ 0xffffffff)) {
        D("check_header(): invalid magic\n");
        return -1;
    }

    if(p->msg.data_length > MAX_PAYLOAD) {
        D("check_header(): %d > MAX_PAYLOAD\n", p->msg.data_length);
        return -1;
    }

    return 0;
}

int check_data(apacket *p)
{
    unsigned count, sum;
    unsigned char *x;

    count = p->msg.data_length;
    x = p->data;
    sum = 0;
    while(count-- > 0) {
        sum += *x++;
    }

    if(sum != p->msg.data_check) {
        return -1;
    } else {
        return 0;
    }
}
