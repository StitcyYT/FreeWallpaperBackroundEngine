#include "wallpaper.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cstdio>

#define DISPLAY(d) ((Display*)(d))
#define WINDOW(w) ((Window)(w))

static void sendStateMessage(Display* display, Window window, Atom state, int action) {
    Atom netWmState = XInternAtom(display, "_NET_WM_STATE", False);
    XEvent ev = {};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = window;
    ev.xclient.message_type = netWmState;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = action;
    ev.xclient.data.l[1] = state;
    ev.xclient.data.l[2] = 0;
    ev.xclient.data.l[3] = 1;

    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

bool wallpaper::init(const Platform& platform) {
    Display* display = DISPLAY(platform.display);
    Window window = WINDOW(platform.window);

    Atom netWmWindowType = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom netWmWindowTypeDesktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    Atom netWmStateBelow = XInternAtom(display, "_NET_WM_STATE_BELOW", False);
    Atom netWmStateSticky = XInternAtom(display, "_NET_WM_STATE_STICKY", False);
    Atom netWmStateSkipTaskbar = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom netWmStateSkipPager = XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False);

    XChangeProperty(display, window, netWmWindowType, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)&netWmWindowTypeDesktop, 1);

    Atom states[] = {
        netWmStateBelow,
        netWmStateSticky,
        netWmStateSkipTaskbar,
        netWmStateSkipPager
    };
    XChangeProperty(display, window, XInternAtom(display, "_NET_WM_STATE", False),
                    XA_ATOM, 32, PropModeReplace, (unsigned char*)states, 4);

    XMapWindow(display, window);

    sendStateMessage(display, window, netWmStateBelow, 1);
    sendStateMessage(display, window, netWmStateSticky, 1);
    XLowerWindow(display, window);

    XFlush(display);

    std::printf("Desktop wallpaper window created\n");
    return true;
}

void wallpaper::setSize(const Platform& platform, int width, int height) {
    XResizeWindow(DISPLAY(platform.display), WINDOW(platform.window), width, height);
    XFlush(DISPLAY(platform.display));
}
