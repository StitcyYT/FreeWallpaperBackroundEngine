#include "ipc.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <signal.h>

static const wchar_t* pipePath = L"\\\\.\\pipe\\wbe";

Ipc::Ipc() : m_path("") {}

Ipc::~Ipc() {
    if (m_fd >= 0) {
        CloseHandle((HANDLE)(intptr_t)m_fd);
    }
    m_fd = -1;
}

bool Ipc::tryConnect() {
    HANDLE h = CreateFileW(
        pipePath, GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr
    );
    if (h != INVALID_HANDLE_VALUE) {
        m_fd = (int)(intptr_t)h;
        return true;
    }
    return false;
}

void Ipc::sendShow() {
    if (m_fd < 0) return;
    HANDLE h = (HANDLE)(intptr_t)m_fd;
    const char* msg = "show";
    DWORD written;
    WriteFile(h, msg, (DWORD)strlen(msg), &written, nullptr);
    char buf[4] = {};
    DWORD read;
    ReadFile(h, buf, sizeof(buf), &read, nullptr);
    CloseHandle(h);
    m_fd = -1;
}

void Ipc::sendKill() {
    if (m_fd < 0) return;
    HANDLE h = (HANDLE)(intptr_t)m_fd;
    const char* msg = "kill";
    DWORD written;
    WriteFile(h, msg, (DWORD)strlen(msg), &written, nullptr);
    char buf[4] = {};
    DWORD read;
    ReadFile(h, buf, sizeof(buf), &read, nullptr);
    CloseHandle(h);
    m_fd = -1;
}

struct ListenThreadArgWin {
    HANDLE pipe;
    Ipc::ShowCallback cb;
};

static DWORD WINAPI listenThreadWin(LPVOID arg) {
    auto* a = static_cast<ListenThreadArgWin*>(arg);

    while (true) {
        BOOL connected = ConnectNamedPipe(a->pipe, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            if (GetLastError() == ERROR_NO_DATA)
                continue;
            break;
        }

        char buf[16] = {};
        DWORD read;
        if (ReadFile(a->pipe, buf, sizeof(buf) - 1, &read, nullptr) && read > 0) {
            buf[read] = '\0';
            if (strcmp(buf, "show") == 0) {
                if (a->cb) a->cb();
                WriteFile(a->pipe, "ok", 2, &read, nullptr);
            } else if (strcmp(buf, "kill") == 0) {
                WriteFile(a->pipe, "ok", 2, &read, nullptr);
                raise(SIGTERM);
                break;
            }
        }
        DisconnectNamedPipe(a->pipe);
        CloseHandle(a->pipe);
    }

    delete a;
    return 0;
}

bool Ipc::listen(ShowCallback onShow) {
    HANDLE h = CreateNamedPipeW(
        pipePath,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        256, 256, 0, nullptr
    );
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    m_fd = (int)(intptr_t)h;

    auto* arg = new ListenThreadArgWin{h, std::move(onShow)};
    HANDLE thread = CreateThread(nullptr, 0, listenThreadWin, arg, 0, nullptr);
    if (thread) CloseHandle(thread);

    return true;
}
