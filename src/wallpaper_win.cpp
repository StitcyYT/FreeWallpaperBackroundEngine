#include "wallpaper.h"
#include <cstdio>

static HWND findWorkerW() {
    HWND progman = FindWindow(L"Progman", nullptr);
    if (!progman) {
        std::fprintf(stderr, "Progman window not found\n");
        return nullptr;
    }

    // Send 0x052c to Progman to create a WorkerW behind desktop icons
    SendMessage(progman, 0x052c, 0, 0);

    HWND workerw = nullptr;
    while (true) {
        workerw = FindWindowEx(nullptr, workerw, L"WorkerW", nullptr);
        if (!workerw) break;

        HWND child = FindWindowEx(workerw, nullptr, L"SHELLDLL_DefView", nullptr);
        if (child) {
            // This WorkerW has the DefView — continue to find the one without it
            // (the WorkerW without DefView is behind the desktop)
            continue;
        }
        break;
    }

    return workerw;
}

bool wallpaper::init(const Platform& platform) {
    HWND hwnd = platform.hwnd;

    HWND workerw = findWorkerW();
    if (!workerw) {
        std::fprintf(stderr, "Could not find WorkerW window\n");
        return false;
    }

    SetParent(hwnd, workerw);

    // Remove window decorations and taskbar entry
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME);
    style |= WS_CHILD;
    SetWindowLong(hwnd, GWL_STYLE, style);

    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_NOACTIVATE;
    exStyle &= ~(WS_EX_APPWINDOW | WS_EX_TOOLWINDOW);
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, platform.width, platform.height,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);

    ShowWindow(hwnd, SW_SHOW);

    std::printf("Desktop wallpaper window created\n");
    return true;
}

void wallpaper::setSize(const Platform& platform, int width, int height) {
    SetWindowPos(platform.hwnd, HWND_BOTTOM, 0, 0, width, height,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
