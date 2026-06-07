#pragma once
#include <cstdint>
#include "platform.h"

class Renderer {
public:
    explicit Renderer(const Platform& platform);
    ~Renderer();

    bool valid() const { return m_valid; }
    void render(const uint8_t* rgbData, int width, int height);
    void swap();

private:
    bool initGL();
    bool createShaders();
    bool createQuad();
    unsigned int compileShader(const char* source, unsigned int type);
    unsigned int linkProgram(unsigned int vertex, unsigned int fragment);

    const Platform& m_platform;
    void* m_glContext = nullptr;  // GLXContext on Linux, HGLRC on Windows
    bool m_valid = false;

    unsigned int m_program = 0;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    unsigned int m_texture = 0;
    int m_texLoc = -1;

    int m_winWidth = 0, m_winHeight = 0;
    int m_texWidth = 0, m_texHeight = 0;
};
