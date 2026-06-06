#include "monitor.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <cstdio>
#include <cstdlib>

struct MonitorData {
    Display* display = nullptr;
    Atom netActiveWindow = None;
    Atom netWmState = None;
    Atom netWmStateFullscreen = None;
    Atom netWmWindowType = None;
    Atom netWmWindowTypeNormal = None;
};

Monitor::Monitor() {
    auto* data = new MonitorData();
    data->display = XOpenDisplay(nullptr);
    if (!data->display) {
        std::fprintf(stderr, "Monitor: cannot open display\n");
        delete data;
        m_valid = false;
        return;
    }

    data->netActiveWindow = XInternAtom(data->display, "_NET_ACTIVE_WINDOW", False);
    data->netWmState = XInternAtom(data->display, "_NET_WM_STATE", False);
    data->netWmStateFullscreen = XInternAtom(data->display, "_NET_WM_STATE_FULLSCREEN", False);
    data->netWmWindowType = XInternAtom(data->display, "_NET_WM_WINDOW_TYPE", False);
    data->netWmWindowTypeNormal = XInternAtom(data->display, "_NET_WM_WINDOW_TYPE_NORMAL", False);

    m_data = data;
    m_valid = true;
}

Monitor::~Monitor() {
    auto* data = static_cast<MonitorData*>(m_data);
    if (data) {
        if (data->display) XCloseDisplay(data->display);
        delete data;
    }
    m_data = nullptr;
}

bool Monitor::isFullscreenAppActive(const Platform& platform) {
    auto* data = static_cast<MonitorData*>(m_data);
    if (!data || !data->display) return false;

    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char* buf = nullptr;

    int status = XGetWindowProperty(
        data->display, DefaultRootWindow(data->display),
        data->netActiveWindow, 0, 1, False, XA_WINDOW,
        &actualType, &actualFormat, &nitems, &bytesAfter, &buf
    );

    if (status != Success || actualType != XA_WINDOW || nitems == 0) {
        XFree(buf);
        return false;
    }

    Window activeWin = *(Window*)buf;
    XFree(buf);

    if (activeWin == None || activeWin == platform.window)
        return false;

    bool fullscreen = false;
    status = XGetWindowProperty(
        data->display, activeWin,
        data->netWmState, 0, 64, False, XA_ATOM,
        &actualType, &actualFormat, &nitems, &bytesAfter, &buf
    );

    if (status == Success && actualType == XA_ATOM && buf) {
        Atom* atoms = (Atom*)buf;
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == data->netWmStateFullscreen) {
                fullscreen = true;
                break;
            }
        }
    }
    XFree(buf);

    return fullscreen;
}
