#ifndef MESSAGESOCKET_H
#define MESSAGESOCKET_H

class MessageSocket
{
private:
    // Unimplemented
    MessageSocket(const MessageSocket&);
    MessageSocket& operator=(const MessageSocket&);

public:
    MessageSocket() : _sock(-1) {}
    ~MessageSocket() { Close(); }
    int fd() const { return _sock; }

    bool ServerInit();
    MessageSocket* Accept();
    ssize_t Read(void* buf, size_t len);

    bool ClientInit();
    bool Show(const char* message);
    bool Dismiss();

    void Close();

private:
    explicit MessageSocket(int fd) : _sock(fd) {}

    bool send_command(const char* command);

    int _sock;
};

extern int dialog_server_init();
extern int dialog_client_init();
extern int dialog_accept(int fd);
extern int dialog_show(int fd);
extern int dialog_dismiss(int fd);

#endif
