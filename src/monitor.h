#pragma once
#include "platform.h"

class Monitor {
public:
    Monitor();
    ~Monitor();

    bool valid() const { return m_valid; }
    bool isFullscreenAppActive(const Platform& platform);

private:
    void* m_data = nullptr;
    bool m_valid = false;
};
