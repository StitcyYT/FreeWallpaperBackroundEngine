#include "engine.h"
#include "decoder.h"
#include "renderer.h"
#include "wallpaper.h"
#include "monitor.h"
#include "platform.h"

#include <cstdio>
#include <chrono>
#include <thread>
#include <cstring>
#include <vector>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct MonitorRect {
    int x, y, width, height;
};

static std::vector<MonitorRect> getMonitors() {
    std::vector<MonitorRect> monitors;

    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT rect, LPARAM data) -> BOOL {
            auto* vec = reinterpret_cast<std::vector<MonitorRect>*>(data);
            vec->push_back({rect->left, rect->top,
                            rect->right - rect->left, rect->bottom - rect->top});
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors));

    if (monitors.empty()) {
        int w = GetSystemMetrics(SM_CXSCREEN);
        int h = GetSystemMetrics(SM_CYSCREEN);
        monitors.push_back({0, 0, w, h});
    }
    return monitors;
}

static bool createPlatform(Platform& plat, const MonitorRect& mon) {
    HINSTANCE hinst = GetModuleHandle(nullptr);

    const wchar_t clsName[] = L"WbeWallpaper";
    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = clsName;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_LAYERED,
        clsName, L"Desktop Wallpaper",
        WS_POPUP | WS_VISIBLE,
        mon.x, mon.y, mon.width, mon.height,
        nullptr, nullptr, hinst, nullptr
    );
    if (!hwnd) {
        std::fprintf(stderr, "Failed to create window\n");
        return false;
    }

    plat.hwnd = hwnd;
    plat.hdc = GetDC(hwnd);
    plat.hinst = hinst;
    plat.x = mon.x;
    plat.y = mon.y;
    plat.width = mon.width;
    plat.height = mon.height;
    return true;
}

static void destroyPlatform(Platform& plat) {
    if (plat.hdc) ReleaseDC(plat.hwnd, plat.hdc);
    if (plat.hwnd) DestroyWindow(plat.hwnd);
}

#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <EGL/egl.h>

struct MonitorRect {
    int x, y, width, height;
};

struct DisplayCtx {
    Display* display = nullptr;
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLConfig eglConfig = nullptr;
    int eglMajor = 0, eglMinor = 0;
    XVisualInfo* visualInfo = nullptr;
    int screen = 0;
    void* colormap = nullptr;
};

static bool initDisplay(DisplayCtx& ctx) {
    ctx.display = XOpenDisplay(nullptr);
    if (!ctx.display) {
        std::fprintf(stderr, "Cannot open X display\n");
        return false;
    }

    ctx.screen = DefaultScreen(ctx.display);

    ctx.eglDisplay = eglGetDisplay((EGLNativeDisplayType)ctx.display);
    if (ctx.eglDisplay == EGL_NO_DISPLAY) {
        std::fprintf(stderr, "Failed to get EGL display\n");
        XCloseDisplay(ctx.display);
        return false;
    }

    if (!eglInitialize(ctx.eglDisplay, &ctx.eglMajor, &ctx.eglMinor)) {
        std::fprintf(stderr, "Failed to initialize EGL\n");
        XCloseDisplay(ctx.display);
        return false;
    }

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };

    EGLint configCount;
    if (!eglChooseConfig(ctx.eglDisplay, configAttribs, &ctx.eglConfig, 1, &configCount)) {
        std::fprintf(stderr, "Failed to choose EGL config\n");
        eglTerminate(ctx.eglDisplay);
        XCloseDisplay(ctx.display);
        return false;
    }

    EGLint visualId;
    eglGetConfigAttrib(ctx.eglDisplay, ctx.eglConfig, EGL_NATIVE_VISUAL_ID, &visualId);

    XVisualInfo visTemplate;
    visTemplate.visualid = visualId;
    int viCount;
    ctx.visualInfo = XGetVisualInfo(ctx.display, VisualIDMask, &visTemplate, &viCount);

    if (!ctx.visualInfo) {
        std::fprintf(stderr, "Failed to get X visual for EGL config\n");
        eglTerminate(ctx.eglDisplay);
        XCloseDisplay(ctx.display);
        return false;
    }

    return true;
}

static void destroyDisplayCtx(DisplayCtx& ctx) {
    if (ctx.visualInfo) XFree(ctx.visualInfo);
    if (ctx.display) XCloseDisplay(ctx.display);
    ctx.visualInfo = nullptr;
    ctx.display = nullptr;
    ctx.eglDisplay = EGL_NO_DISPLAY;
}

static std::vector<MonitorRect> getMonitors(Display* dpy) {
    std::vector<MonitorRect> monitors;

    int nscreens;
    XRRMonitorInfo* info = XRRGetMonitors(dpy, DefaultRootWindow(dpy), True, &nscreens);
    if (info && nscreens > 0) {
        for (int i = 0; i < nscreens; i++) {
            monitors.push_back({info[i].x, info[i].y,
                                info[i].width, info[i].height});
        }
        XFree(info);
    }

    if (monitors.empty()) {
        int w = DisplayWidth(dpy, DefaultScreen(dpy));
        int h = DisplayHeight(dpy, DefaultScreen(dpy));
        monitors.push_back({0, 0, w, h});
    }
    return monitors;
}

static bool createPlatform(Platform& plat, const DisplayCtx& ctx, const MonitorRect& mon) {
    Display* dpy = ctx.display;
    Window root = RootWindow(dpy, ctx.screen);
    XVisualInfo* vi = ctx.visualInfo;

    XSetWindowAttributes attrs;
    attrs.background_pixmap = None;
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    attrs.colormap = XCreateColormap(dpy, root, vi->visual, AllocNone);
    attrs.event_mask = ExposureMask | StructureNotifyMask;

    Window window = XCreateWindow(
        dpy, root,
        mon.x, mon.y, mon.width, mon.height, 0,
        vi->depth, InputOutput, vi->visual,
        CWBackPixmap | CWBackPixel | CWBorderPixel | CWColormap | CWEventMask,
        &attrs
    );

    if (!window) {
        std::fprintf(stderr, "Cannot create X window\n");
        return false;
    }

    XStoreName(dpy, window, "Desktop Wallpaper");

    plat.display = dpy;
    plat.window = window;
    plat.screen = ctx.screen;
    plat.x = mon.x;
    plat.y = mon.y;
    plat.width = mon.width;
    plat.height = mon.height;
    plat.eglDisplay = ctx.eglDisplay;
    plat.eglConfig = ctx.eglConfig;
    return true;
}

static void destroyPlatform(Platform& plat) {
    if (plat.display && plat.window) {
        XDestroyWindow((Display*)plat.display, plat.window);
    }
}

static bool processXEvents(Display* display) {
    while (XPending(display)) {
        XEvent ev;
        XNextEvent(display, &ev);
    }
    return true;
}
#endif

Engine::Engine() {
    m_thread = std::thread(&Engine::threadFunc, this);
}

Engine::~Engine() {
    quit();
    if (m_thread.joinable()) m_thread.join();
}

void Engine::play(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_path = path;
        m_pathChanged = true;
        m_error.clear();
    }
    m_playing = true;
    m_stopRequested = false;
}

void Engine::stop() {
    m_playing = false;
    m_stopRequested = true;
}

void Engine::resume() {
    m_stopRequested = false;
    m_playing = true;
}

void Engine::quit() {
    m_quit = true;
    m_playing = false;
}

void Engine::setSpeed(double speed) {
    m_speed.store(speed);
}

std::string Engine::error() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_error;
}

std::string Engine::status() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

std::string Engine::videoInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_videoInfo;
}

void Engine::updateStatus(const std::string& s) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = s;
}

void Engine::threadFunc() {
#ifdef _WIN32
    auto monitors = getMonitors();
#else
    DisplayCtx dctx;
    if (!initDisplay(dctx)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = "Failed to initialize display";
        m_active = false;
        return;
    }

    auto monitors = getMonitors(dctx.display);
#endif

    std::vector<Platform> platforms;
    std::vector<std::unique_ptr<Renderer>> renderers;

    for (const auto& mon : monitors) {
        Platform plat = {};

#ifdef _WIN32
        if (!createPlatform(plat, mon)) continue;
#else
        if (!createPlatform(plat, dctx, mon)) continue;
#endif

    if (!wallpaper::init(plat)) {
        destroyPlatform(plat);
        continue;
    }
    XSync((Display*)dctx.display, False);

        auto ren = std::make_unique<Renderer>(plat);
        if (!ren->valid()) {
            destroyPlatform(plat);
            continue;
        }

        platforms.push_back(plat);
        renderers.push_back(std::move(ren));
    }

    if (platforms.empty()) {
#ifdef _WIN32
#else
        destroyDisplayCtx(dctx);
#endif
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = "No valid monitor created";
        m_active = false;
        return;
    }

    {
        Monitor monitor;
        std::unique_ptr<Decoder> decoder;
        std::string currentPath;
        m_active = true;

        updateStatus("Ready");
        std::printf("Wallpaper engine running across %zu monitors\n", platforms.size());

        while (!m_quit.load()) {
#ifdef _WIN32
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
#else
            processXEvents((Display*)platforms[0].display);
#endif

            bool doPlay = m_playing.load();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_pathChanged && !m_path.empty() && m_path != currentPath) {
                    decoder.reset();
                    decoder = std::make_unique<Decoder>(m_path);
                    if (decoder->valid()) {
                        currentPath = m_path;
                        m_pathChanged = false;
                        m_error.clear();
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "%dx%d @ %.1ffps",
                                      decoder->width(), decoder->height(), decoder->fps());
                        m_videoInfo = buf;
                        decoder->start();
                        decoder->setSpeed(m_speed.load());
                        m_status = "Playing " + m_videoInfo;
                        std::printf("Now playing: %s (%s)\n", m_path.c_str(), m_videoInfo.c_str());
                    } else {
                        m_error = "Failed to load: " + m_path;
                        m_status = "Error: " + m_error;
                        decoder.reset();
                        m_pathChanged = false;
                    }
                }
            }

            if (doPlay && decoder && decoder->valid()) {
                bool gameActive = monitor.isFullscreenAppActive(platforms[0]);
                decoder->setTargetFps(gameActive ? 1.0 : 0.0);
                decoder->setSpeed(m_speed.load());

                auto frame = decoder->read();
                if (frame) {
                    for (auto& ren : renderers) {
                        ren->render(frame->data.data(), frame->width, frame->height);
                        ren->swap();
                    }
                }

                std::this_thread::sleep_for(
                    gameActive ? std::chrono::milliseconds(100) : std::chrono::microseconds(1000));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        decoder.reset();
    }

    for (size_t i = 0; i < platforms.size(); i++) {
        renderers[i].reset();
        destroyPlatform(platforms[i]);
    }

#ifndef _WIN32
    destroyDisplayCtx(dctx);
#endif

    m_active = false;
    std::printf("Wallpaper engine stopped\n");
}
