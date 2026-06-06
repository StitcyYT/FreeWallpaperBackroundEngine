#include "ipc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>

static std::string sockPath() {
    const char* home = getenv("XDG_RUNTIME_DIR");
    if (!home) home = getenv("TMPDIR");
    if (!home) home = "/tmp";
    return std::string(home) + "/wbe.sock";
}

Ipc::Ipc() : m_path(sockPath()) {}

Ipc::~Ipc() {
    if (m_fd >= 0) {
        close(m_fd);
    }
    unlink(m_path.c_str());
}

bool Ipc::tryConnect() {
    m_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_fd < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(m_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        return true;
    }

    close(m_fd);
    m_fd = -1;
    return false;
}

void Ipc::sendShow() {
    if (m_fd < 0) return;
    const char* msg = "show";
    write(m_fd, msg, strlen(msg));
    char buf[4] = {};
    read(m_fd, buf, sizeof(buf));
    close(m_fd);
    m_fd = -1;
}

void Ipc::sendKill() {
    if (m_fd < 0) return;
    const char* msg = "kill";
    write(m_fd, msg, strlen(msg));
    char buf[4] = {};
    read(m_fd, buf, sizeof(buf));
    close(m_fd);
    m_fd = -1;
}

struct ListenThreadArg {
    intptr_t fd;
    Ipc::ShowCallback cb;
};

static void* listenThread(void* arg) {
    auto* a = static_cast<ListenThreadArg*>(arg);
    int fd = (int)a->fd;

    while (true) {
        struct sockaddr_un clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int client = accept(fd, (struct sockaddr*)&clientAddr, &clientLen);
        if (client < 0) break;

        char buf[16] = {};
        ssize_t n = read(client, buf, sizeof(buf) - 1);
        if (n > 0) {
            if (strcmp(buf, "show") == 0) {
                if (a->cb) a->cb();
                write(client, "ok", 2);
            } else if (strcmp(buf, "kill") == 0) {
                write(client, "ok", 2);
                raise(SIGTERM);
                break;
            }
        }
        close(client);
    }

    delete a;
    return nullptr;
}

bool Ipc::listen(ShowCallback onShow) {
    unlink(m_path.c_str());

    int sock = (int)socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;
    m_fd = sock;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        m_fd = -1;
        return false;
    }

    ::listen(sock, 5);

    auto* arg = new ListenThreadArg{m_fd, std::move(onShow)};
    pthread_t thread;
    pthread_create(&thread, nullptr, listenThread, arg);
    pthread_detach(thread);

    return true;
}
