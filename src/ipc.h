#pragma once
#include <cstdint>
#include <string>
#include <functional>

class Ipc {
public:
    using ShowCallback = std::function<void()>;

    Ipc();
    ~Ipc();

    bool tryConnect();
    void sendShow();
    void sendKill();
    bool listen(ShowCallback onShow);

private:
    intptr_t m_fd = -1;
    std::string m_path;
};
