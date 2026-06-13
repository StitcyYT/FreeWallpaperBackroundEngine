#include "renderer.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <cstdio>

static const char* vertexSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTex;
out vec2 vTex;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTex = aTex;
}
)";

static const char* fragSrc = R"(
#version 330 core
in vec2 vTex;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    fragColor = texture(uTex, vec2(vTex.x, 1.0 - vTex.y));
}
)";

Renderer::Renderer(const Platform& platform)
    : m_platform(platform) {
    initGL();
}

Renderer::~Renderer() {
    if (m_program) glDeleteProgram(m_program);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_texture) glDeleteTextures(1, &m_texture);

    EGLDisplay dpy = (EGLDisplay)m_eglDisplay;
    EGLContext ctx = (EGLContext)m_eglContext;
    EGLSurface surface = (EGLSurface)m_eglSurface;

    if (dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (ctx != EGL_NO_CONTEXT) eglDestroyContext(dpy, ctx);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(dpy, surface);
        eglTerminate(dpy);
    }
    m_eglDisplay = nullptr;
    m_eglContext = nullptr;
    m_eglSurface = nullptr;
}

bool Renderer::initGL() {
    Display* dpy = (Display*)m_platform.display;
    Window win = (Window)m_platform.window;

    m_eglDisplay = m_platform.eglDisplay;
    EGLDisplay edpy = (EGLDisplay)m_eglDisplay;
    EGLConfig eglConfig = (EGLConfig)m_platform.eglConfig;

    if (edpy == EGL_NO_DISPLAY || !eglConfig) {
        std::fprintf(stderr, "No EGL display or config\n");
        return false;
    }

    m_eglSurface = eglCreateWindowSurface(edpy, eglConfig,
                                           (EGLNativeWindowType)win, nullptr);
    if (m_eglSurface == EGL_NO_SURFACE) {
        std::fprintf(stderr, "Failed to create EGL surface\n");
        return false;
    }

    eglBindAPI(EGL_OPENGL_API);

    EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };

    m_eglContext = eglCreateContext(edpy, eglConfig,
                                     EGL_NO_CONTEXT, contextAttribs);
    if (m_eglContext == EGL_NO_CONTEXT) {
        EGLint fallbackAttribs[] = {
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 0,
            EGL_NONE
        };
        m_eglContext = eglCreateContext(edpy, eglConfig,
                                         EGL_NO_CONTEXT, fallbackAttribs);
    }

    if (m_eglContext == EGL_NO_CONTEXT) {
        std::fprintf(stderr, "Failed to create EGL context\n");
        eglDestroySurface(edpy, (EGLSurface)m_eglSurface);
        m_eglSurface = EGL_NO_SURFACE;
        return false;
    }

    if (!eglMakeCurrent(edpy, (EGLSurface)m_eglSurface,
                         (EGLSurface)m_eglSurface, (EGLContext)m_eglContext)) {
        std::fprintf(stderr, "Failed to make EGL context current\n");
        eglDestroyContext(edpy, (EGLContext)m_eglContext);
        m_eglContext = EGL_NO_CONTEXT;
        eglDestroySurface(edpy, (EGLSurface)m_eglSurface);
        m_eglSurface = EGL_NO_SURFACE;
        return false;
    }

    XWindowAttributes wa;
    XGetWindowAttributes(dpy, win, &wa);
    m_winWidth = wa.width;
    m_winHeight = wa.height;

    glViewport(0, 0, m_winWidth, m_winHeight);

    if (!createShaders()) return false;
    if (!createQuad()) return false;

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    m_valid = true;
    return true;
}

bool Renderer::createShaders() {
    unsigned int vertex = compileShader(vertexSrc, GL_VERTEX_SHADER);
    if (!vertex) return false;

    unsigned int fragment = compileShader(fragSrc, GL_FRAGMENT_SHADER);
    if (!fragment) {
        glDeleteShader(vertex);
        return false;
    }

    m_program = linkProgram(vertex, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    if (!m_program) return false;

    m_texLoc = glGetUniformLocation(m_program, "uTex");
    return true;
}

unsigned int Renderer::compileShader(const char* source, unsigned int type) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error (%s): %s\n",
                     type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

unsigned int Renderer::linkProgram(unsigned int vertex, unsigned int fragment) {
    unsigned int program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool Renderer::createQuad() {
    float vertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
    };
    unsigned int indices[] = { 0, 1, 2, 2, 1, 3 };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    return true;
}

void Renderer::render(const uint8_t* rgbData, int width, int height) {
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, rgbData);

    glUseProgram(m_program);
    glUniform1i(m_texLoc, 0);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Renderer::swap() {
    eglSwapBuffers((EGLDisplay)m_eglDisplay, (EGLSurface)m_eglSurface);
}
