#include "renderer.h"

#include <GL/glew.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>

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
    if (m_glContext && m_platform.hdc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext((HGLRC)m_glContext);
    }
    m_glContext = nullptr;
}

bool Renderer::initGL() {
    HWND hwnd = m_platform.hwnd;
    HDC hdc = m_platform.hdc;

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf) {
        std::fprintf(stderr, "ChoosePixelFormat failed\n");
        return false;
    }
    if (!SetPixelFormat(hdc, pf, &pfd)) {
        std::fprintf(stderr, "SetPixelFormat failed\n");
        return false;
    }

    HGLRC tmpCtx = wglCreateContext(hdc);
    if (!tmpCtx) {
        std::fprintf(stderr, "wglCreateContext failed\n");
        return false;
    }
    wglMakeCurrent(hdc, tmpCtx);

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::fprintf(stderr, "GLEW init failed: %s\n", glewGetErrorString(err));
        wglDeleteContext(tmpCtx);
        return false;
    }

    HGLRC ctx = nullptr;
    if (wglewIsSupported("WGL_ARB_create_context") && wglewIsSupported("WGL_ARB_pixel_format")) {
        int attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
            WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
            WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
            WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
            WGL_COLOR_BITS_ARB, 24,
            WGL_DEPTH_BITS_ARB, 24,
            WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
            0
        };
        int pixelFormat;
        UINT numFormats;
        wglChoosePixelFormatARB(hdc, attribs, nullptr, 1, &pixelFormat, &numFormats);

        if (numFormats > 0) {
            SetPixelFormat(hdc, pixelFormat, &pfd);
        }

        int ctxAttribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        ctx = wglCreateContextAttribsARB(hdc, nullptr, ctxAttribs);
        if (ctx) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(tmpCtx);
            wglMakeCurrent(hdc, ctx);
        }
    }

    if (!ctx) {
        ctx = tmpCtx;
    }

    m_glContext = ctx;

    RECT rect;
    GetClientRect(hwnd, &rect);
    m_winWidth = rect.right;
    m_winHeight = rect.bottom;

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

    if (width != m_texWidth || height != m_texHeight) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        m_texWidth = width;
        m_texHeight = height;
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_RGB, GL_UNSIGNED_BYTE, rgbData);

    glUseProgram(m_program);
    glUniform1i(m_texLoc, 0);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Renderer::swap() {
    SwapBuffers(m_platform.hdc);
}
