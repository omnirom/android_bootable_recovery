#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "messagesocket.h"

static const char * const SOCKET_PATH = "/tmp/.dialog_sock";

bool MessageSocket::ServerInit()
{
    int fd, rc;
    unlink(SOCKET_PATH);
    fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    struct sockaddr_un sa;
    socklen_t salen;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, SOCKET_PATH);
    rc = ::bind(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc != 0) {
        ::close(fd);
        return false;
    }
    rc = ::listen(fd, 5);
    if (rc != 0) {
        ::close(fd);
        return false;
    }
    _sock = fd;
    return true;
}

MessageSocket* MessageSocket::Accept()
{
    int clientfd;
    struct sockaddr_un sa;
    socklen_t salen;
    memset(&sa, 0, sizeof(sa));
    salen = sizeof(sa);
    clientfd = ::accept(_sock, (struct sockaddr*)&sa, &salen);
    if (clientfd < 0) {
        return NULL;
    }
    return new MessageSocket(clientfd);
}

ssize_t MessageSocket::Read(void* buf, size_t len)
{
    //XXX: use fdopen/getline
    ssize_t nread = ::read(_sock, buf, len);
    if (nread > 0) {
        char* p = (char*)memchr(buf, '\n', nread);
        if (p) {
            *p = '\0';
        }
    }
    return nread;
}

bool MessageSocket::ClientInit()
{
    int fd, rc;
    fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    struct sockaddr_un sa;
    socklen_t salen;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, SOCKET_PATH);
    rc = ::connect(fd, (struct sockaddr*)&sa, sizeof(sa));
    if (rc != 0) {
        ::close(fd);
        return false;
    }
    _sock = fd;
    return true;
}

bool MessageSocket::Show(const char* message)
{
    char buf[256];
    sprintf(buf, "show %s", message);
    return send_command(buf);
}

bool MessageSocket::Dismiss()
{
    return send_command("dismiss");
}

void MessageSocket::Close()
{
    if (_sock != -1) {
        ::close(_sock);
        _sock = -1;
    }
}

bool MessageSocket::send_command(const char* command)
{
    char buf[256];
    int len;
    ssize_t written;
    len = sprintf(buf, "dialog %s\n", command);
    written = ::write(_sock, buf, len);
    return (written == len);
}
