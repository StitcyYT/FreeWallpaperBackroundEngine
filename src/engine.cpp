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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static bool createPlatform(Platform& plat) {
    HINSTANCE hinst = GetModuleHandle(nullptr);

    const wchar_t clsName[] = L"WbeWallpaper";
    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = clsName;
    RegisterClassW(&wc);

    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_LAYERED,
        clsName, L"Desktop Wallpaper",
        WS_POPUP | WS_VISIBLE,
        0, 0, width, height,
        nullptr, nullptr, hinst, nullptr
    );
    if (!hwnd) {
        std::fprintf(stderr, "Failed to create window\n");
        return false;
    }

    plat.hwnd = hwnd;
    plat.hdc = GetDC(hwnd);
    plat.hinst = hinst;
    plat.width = width;
    plat.height = height;
    return true;
}

static void destroyPlatform(Platform& plat) {
    if (plat.hdc) ReleaseDC(plat.hwnd, plat.hdc);
    if (plat.hwnd) DestroyWindow(plat.hwnd);
}

#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>

static bool detectXwayland(Display* dpy) {
    const char* vendor = ServerVendor(dpy);
    if (vendor && std::strstr(vendor, "XWayland")) {
        std::fprintf(stderr, "Warning: Running under XWayland (Wayland).\n"
                    "Desktop wallpaper may not work correctly.\n");
        return true;
    }
    return false;
}

static bool createPlatform(Platform& plat) {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        std::fprintf(stderr, "Cannot open X display\n");
        return false;
    }

    detectXwayland(display);

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    int width = DisplayWidth(display, screen);
    int height = DisplayHeight(display, screen);

    XSetWindowAttributes attrs;
    attrs.background_pixmap = None;
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    attrs.colormap = CopyFromParent;
    attrs.event_mask = ExposureMask | StructureNotifyMask;

    Window window = XCreateWindow(
        display, root,
        0, 0, width, height, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWBackPixmap | CWBackPixel | CWBorderPixel | CWColormap | CWEventMask,
        &attrs
    );

    if (!window) {
        std::fprintf(stderr, "Cannot create X window\n");
        XCloseDisplay(display);
        return false;
    }

    XStoreName(display, window, "Desktop Wallpaper");

    plat.display = display;
    plat.window = window;
    plat.screen = screen;
    plat.width = width;
    plat.height = height;
    return true;
}

static void destroyPlatform(Platform& plat) {
    if (plat.display) {
        XDestroyWindow(plat.display, plat.window);
        XCloseDisplay(plat.display);
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
    Platform platform = {};
    if (!createPlatform(platform)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = "Failed to create platform window";
        m_active = false;
        return;
    }

    if (!wallpaper::init(platform)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = "Wallpaper init failed";
        destroyPlatform(platform);
        m_active = false;
        return;
    }

    {
        Renderer renderer(platform);
        if (!renderer.valid()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_error = "Renderer initialization failed";
            destroyPlatform(platform);
            m_active = false;
            return;
        }

        Monitor monitor;
        std::unique_ptr<Decoder> decoder;
        std::string currentPath;
        m_active = true;

        updateStatus("Ready");
        std::printf("Wallpaper engine running in background\n");

        while (!m_quit.load()) {
#ifdef _WIN32
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
#else
            processXEvents(platform.display);
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
                bool gameActive = monitor.isFullscreenAppActive(platform);
                decoder->setTargetFps(gameActive ? 1.0 : 0.0);
                decoder->setSpeed(m_speed.load());

                auto frame = decoder->read();
                if (frame) {
                    renderer.render(frame->data.data(), frame->width, frame->height);
                    renderer.swap();
                }

                std::this_thread::sleep_for(
                    gameActive ? std::chrono::milliseconds(100) : std::chrono::microseconds(1000));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        decoder.reset();
    }

    destroyPlatform(platform);
    m_active = false;
    std::printf("Wallpaper engine stopped\n");
}
