#include "monitor.h"
#include <cstdio>

Monitor::Monitor() {
    m_valid = true;
}

Monitor::~Monitor() {
}

bool Monitor::isFullscreenAppActive(const Platform& platform) {
    if (!m_valid) return false;

    HWND ourHwnd = platform.hwnd;
    HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == ourHwnd)
        return false;

    // Check if the foreground window is fullscreen (covers the entire monitor)
    RECT fgRect, desktopRect;
    if (!GetWindowRect(foreground, &fgRect))
        return false;

    HWND desktop = GetDesktopWindow();
    if (!GetWindowRect(desktop, &desktopRect))
        return false;

    int fgW = fgRect.right - fgRect.left;
    int fgH = fgRect.bottom - fgRect.top;
    int deskW = desktopRect.right - desktopRect.left;
    int deskH = desktopRect.bottom - desktopRect.top;

    // Consider it fullscreen if the window covers at least 95% of the screen
    return (fgW >= deskW * 0.95 && fgH >= deskH * 0.95);
}
