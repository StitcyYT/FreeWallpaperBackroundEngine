#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct Platform {
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HINSTANCE hinst = nullptr;
    int width = 0, height = 0;
};

#else
#include <X11/Xlib.h>

struct Platform {
    Display* display = nullptr;
    Window window = 0;
    int screen = 0;
    int width = 0, height = 0;
};

#endif
