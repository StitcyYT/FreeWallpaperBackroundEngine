#include "renderer.h"

#include <GL/glew.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>

static int ctxErrorOccurred = 0;
static int ctxErrorHandler(Display* dpy, XErrorEvent* ev) {
    ctxErrorOccurred = 1;
    return 0;
}

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
    if (m_glContext && m_platform.display) {
        glXMakeCurrent(m_platform.display, 0, nullptr);
        glXDestroyContext(m_platform.display, (GLXContext)m_glContext);
    }
    m_glContext = nullptr;
}

bool Renderer::initGL() {
    Display* dpy = m_platform.display;
    int screen = m_platform.screen;
    Window win = m_platform.window;

    int fbAttribs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        GLX_DOUBLEBUFFER, True,
        None
    };

    int fbCount = 0;
    GLXFBConfig* fbConfigs = glXChooseFBConfig(dpy, screen, fbAttribs, &fbCount);
    if (!fbConfigs || fbCount == 0) {
        std::fprintf(stderr, "Failed to get FB config\n");
        return false;
    }
    GLXFBConfig fbConfig = fbConfigs[0];
    XFree(fbConfigs);

    ctxErrorOccurred = 0;
    XErrorHandler oldHandler = XSetErrorHandler(&ctxErrorHandler);

    using glXCreateContextAttribsARBProc = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    auto glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)
        glXGetProcAddress((const unsigned char*)"glXCreateContextAttribsARB");

    int contextAttribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
    };

    GLXContext ctx = nullptr;
    if (glXCreateContextAttribsARB) {
        ctx = glXCreateContextAttribsARB(dpy, fbConfig, nullptr, True, contextAttribs);
    }
    XSync(dpy, False);

    if (ctxErrorOccurred || !ctx) {
        std::fprintf(stderr, "Falling back to OpenGL 3.0\n");
        ctxErrorOccurred = 0;
        int fallbackAttribs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
            GLX_CONTEXT_MINOR_VERSION_ARB, 0,
            None
        };
        ctx = glXCreateContextAttribsARB(dpy, fbConfig, nullptr, True, fallbackAttribs);
    }

    XSetErrorHandler(oldHandler);

    if (!ctx) {
        std::fprintf(stderr, "Failed to create GL context\n");
        return false;
    }

    m_glContext = ctx;

    if (!glXMakeCurrent(dpy, win, ctx)) {
        std::fprintf(stderr, "Failed to make GL context current\n");
        return false;
    }

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "GLEW init failed\n");
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
    glXSwapBuffers(m_platform.display, m_platform.window);
}
