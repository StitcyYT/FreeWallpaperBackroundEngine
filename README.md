# WBE — Desktop Wallpaper Engine

Animated video wallpaper engine for Linux (X11) and Windows. Plays any video
format (via FFmpeg) as your actual desktop background. Drops to ~1 FPS when a
fullscreen game is detected so your GPU isn't wasted.

## Build

**Dependencies**: FFmpeg (libavcodec, libavformat, libavutil, libswscale),
GTK3, OpenGL, GLEW (Linux), X11+Xlib (Linux).

```bash
cmake -B build
cmake --build build -j$(nproc)
```

The binary is written to `build/wbe-lnx` (Linux) or `build/wbe-win.exe` (Windows).

### Windows (cross-compile or native via vcpkg)

```pwsh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

## Install

```bash
# one-command rebuild + install to ~/.local/
./install.sh
```

Installs the binary, `.desktop` file, SVG icon, and sample videos.

## Usage

```bash
desktop-wallpaper              # launch (single-instance; re-launch shows the window)
desktop-wallpaper /path/to/vid # launch with a video
desktop-wallpaper --kill       # stop the background engine
```

- Click **Select Video** to pick a file
- **Play/Pause** toggles playback
- **Speed** slider: 0.25× – 3.0×
- **Kill** stops the engine and quits
- Close the window → engine stays in background; run `desktop-wallpaper` to
  bring the control panel back, or `desktop-wallpaper --kill` to stop.

## Project structure

```
CMakeLists.txt       # build system
src/
  main.cpp           # GTK3 control panel, IPC entry
  engine.h/cpp       # background thread: decoder + renderer + monitor
  decoder.h/cpp      # FFmpeg video decoder (threaded, lock-free frame swap)
  renderer.h/cpp     # OpenGL 3.3 renderer via GLX (Linux)
  renderer_win.cpp   # OpenGL 3.3 renderer via WGL (Windows)
  wallpaper.h/cpp    # X11 EWMH desktop background (Linux)
  wallpaper_win.cpp  # Win32 Progman/WorkerW wallpaper (Windows)
  monitor.h/cpp      # X11 fullscreen game detection (Linux)
  monitor_win.cpp    # GetForegroundWindow game detection (Windows)
  ipc.h               # single-instance IPC interface
  ipc_linux.cpp      # Unix domain socket IPC (Linux)
  ipc_win.cpp        # Named pipe IPC (Windows)
  platform.h          # platform abstraction struct
  frame.h            # shared Frame (RGB + dims + PTS)
example/             # sample videos
build/               # build output (wbe-lnx / wbe-win.exe)
install.sh           # build + install helper
```

## How it works

- **Linux**: Creates an X11 window tagged with `_NET_WM_WINDOW_TYPE_DESKTOP`,
  placed below all other windows via `_NET_WM_STATE_BELOW` and `XLowerWindow`.
  OpenGL 3.3 core-profile context created via GLX with `glXCreateContextAttribsARB`.
  Game detection: polls `_NET_ACTIVE_WINDOW` for `_NET_WM_STATE_FULLSCREEN` on
  a separate X11 display connection.

- **Windows**: Creates a borderless child window of the Progman `WorkerW` (the
  window behind desktop icons). OpenGL context via WGL with
  `wglCreateContextAttribsARB`. Game detection:
  `GetForegroundWindow` + `GetWindowRect` size comparison.

- **Both**: A single-instance IPC mechanism prevents duplicate processes.
  Re-launching shows the control window; `--kill` stops the engine.
