#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct Platform {
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HINSTANCE hinst = nullptr;
    int x = 0, y = 0;
    int width = 0, height = 0;
};

#else

struct Platform {
    void* display = nullptr;
    unsigned long window = 0;
    int screen = 0;
    int x = 0, y = 0;
    int width = 0, height = 0;
    void* eglDisplay = nullptr;
    void* eglConfig = nullptr;
};

#endif
