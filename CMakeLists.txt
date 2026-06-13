cmake_minimum_required(VERSION 3.16)
project(desktop-wallpaper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig QUIET)

set(PLATFORM_SRCS)
set(PLATFORM_LIBS)
set(PLATFORM_INCS)

if(WIN32)
    list(APPEND PLATFORM_SRCS
        src/ipc_win.cpp
        src/renderer_win.cpp
        src/wallpaper_win.cpp
        src/monitor_win.cpp
    )

    find_package(FFmpeg QUIET COMPONENTS avcodec avformat avutil swscale)
    if(NOT FFmpeg_FOUND)
        find_package(PkgConfig)
        pkg_check_modules(AVCODEC REQUIRED libavcodec)
        pkg_check_modules(AVFMT REQUIRED libavformat)
        pkg_check_modules(AVUTIL REQUIRED libavutil)
        pkg_check_modules(SWSW REQUIRED libswscale)
    endif()

    find_package(GTK3 REQUIRED)
    find_package(OpenGL REQUIRED)

    list(APPEND PLATFORM_LIBS
        ${OPENGL_LIBRARIES}
        opengl32
        gdi32
        user32
    )
else()
    pkg_check_modules(AVCODEC REQUIRED libavcodec)
    pkg_check_modules(AVFMT REQUIRED libavformat)
    pkg_check_modules(AVUTIL REQUIRED libavutil)
    pkg_check_modules(SWSW REQUIRED libswscale)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(X11 REQUIRED x11)
    pkg_check_modules(XRANDR REQUIRED xrandr)
    pkg_check_modules(EGL REQUIRED egl)

    find_package(OpenGL REQUIRED)

    list(APPEND PLATFORM_SRCS
        src/ipc_linux.cpp
        src/renderer.cpp
        src/wallpaper.cpp
        src/monitor.cpp
    )
    list(APPEND PLATFORM_LIBS
        ${OPENGL_LIBRARIES}
        ${X11_LIBRARIES}
        ${XRANDR_LIBRARIES}
        ${EGL_LIBRARIES}
        pthread
    )
    list(APPEND PLATFORM_INCS
        ${X11_INCLUDE_DIRS}
        ${XRANDR_INCLUDE_DIRS}
        ${EGL_INCLUDE_DIRS}
    )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

add_executable(desktop-wallpaper
    src/main.cpp
    src/decoder.cpp
    src/decoder.h
    src/engine.cpp
    src/engine.h
    src/frame.h
    src/ipc.h
    src/platform.h
    src/renderer.h
    src/wallpaper.h
    src/monitor.h
    ${PLATFORM_SRCS}
)

target_include_directories(desktop-wallpaper PRIVATE
    src
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFMT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSW_INCLUDE_DIRS}
    ${GTK3_INCLUDE_DIRS}
    ${PLATFORM_INCS}
)

target_link_libraries(desktop-wallpaper PRIVATE
    ${AVCODEC_LIBRARIES}
    ${AVFMT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSW_LIBRARIES}
    ${GTK3_LIBRARIES}
    ${PLATFORM_LIBS}
)

if(WIN32)
    set_target_properties(desktop-wallpaper PROPERTIES OUTPUT_NAME "wbe-win")
else()
    set_target_properties(desktop-wallpaper PROPERTIES OUTPUT_NAME "wbe-lnx")
endif()

target_compile_options(desktop-wallpaper PRIVATE
    ${AVCODEC_CFLAGS_OTHER}
    ${AVFMT_CFLAGS_OTHER}
    ${AVUTIL_CFLAGS_OTHER}
    ${SWSW_CFLAGS_OTHER}
    ${GTK3_CFLAGS_OTHER}
)
