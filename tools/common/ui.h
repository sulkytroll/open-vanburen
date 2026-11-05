#pragma once
// Shared GL helpers for the Van Buren viewers: shader compile/link and a bitmap-font 2D text renderer.
#include "font_data.h"
#include <GL/glew.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char b[1024]; glGetShaderInfoLog(s, 1024, nullptr, b); std::fprintf(stderr, "shader error: %s\n", b); std::exit(1); }
    return s;
}
static GLuint program(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs)); glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p); return p;
}

static const char* kTextVS = R"(#version 330 core
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV; uniform vec2 screen; out vec2 vUV;
void main(){ vUV=aUV; gl_Position=vec4(aPos.x/screen.x*2.0-1.0, 1.0-aPos.y/screen.y*2.0, 0.0, 1.0); })";
static const char* kTextFS = R"(#version 330 core
in vec2 vUV; out vec4 frag; uniform sampler2D font; uniform vec4 color; uniform int solid;
void main(){ float a = solid!=0 ? 1.0 : texture(font,vUV).r; frag = vec4(color.rgb, color.a*a); })";

struct Text {
    GLuint prog = 0, tex = 0, vao = 0, vbo = 0; int W = 0, H = 0;
    void init() {
        prog = program(kTextVS, kTextFS);
        glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kFontW, kFontH, 0, GL_RED, GL_UNSIGNED_BYTE, kFontData);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);  glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);  glEnableVertexAttribArray(1);
    }
    void begin(int w, int h) {
        W = w; H = h;
        glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(prog); glUniform2f(glGetUniformLocation(prog, "screen"), (float)w, (float)h);
        glUniform1i(glGetUniformLocation(prog, "font"), 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex); glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    }
    void end() { glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); }
    void color(float r, float g, float b, float a) { glUniform4f(glGetUniformLocation(prog, "color"), r, g, b, a); }
    void rect(float x, float y, float w, float h, float r, float g, float b, float a) {
        float v[24] = {x, y, 0, 0, x + w, y, 0, 0, x + w, y + h, 0, 0, x, y, 0, 0, x + w, y + h, 0, 0, x, y + h, 0, 0};
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
        glUniform1i(glGetUniformLocation(prog, "solid"), 1); color(r, g, b, a);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    float width(const std::string& s, float scale) const { return (float)s.size() * kFontCW * scale; }
    void draw(float x, float y, float scale, float r, float g, float b, const std::string& s, float a = 1.f) {
        std::vector<float> v; v.reserve(s.size() * 24);
        float cx = x;
        for (unsigned char c : s) {
            if (c >= 32 && c <= 126) {
                int i = c - 32, gx = (i % kFontCols) * kFontAW, gy = (i / kFontCols) * kFontAH;
                float u0 = (float)gx / kFontW, v0 = (float)gy / kFontH, u1 = (float)(gx + kFontAW) / kFontW, v1 = (float)(gy + kFontAH) / kFontH;
                float w = kFontAW * scale, h = kFontAH * scale, x0 = cx - scale, y0 = y - scale;   // 1px atlas border
                float q[24] = {x0, y0, u0, v0, x0 + w, y0, u1, v0, x0 + w, y0 + h, u1, v1, x0, y0, u0, v0, x0 + w, y0 + h, u1, v1, x0, y0 + h, u0, v1};
                v.insert(v.end(), q, q + 24);
            }
            cx += kFontCW * scale;
        }
        if (v.empty()) return;
        glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STREAM_DRAW);
        glUniform1i(glGetUniformLocation(prog, "solid"), 0); color(r, g, b, a);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 4));
    }
};

