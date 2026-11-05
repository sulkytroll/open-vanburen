#pragma once
// 2D drawing for the in-game interface: textured / solid quads in screen pixels and TrueType text (FreeType, glyph atlas per font+size).
#include "common/ui.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <map>
#include <string>
#include <vector>

struct Rgba { float r = 1, g = 1, b = 1, a = 1; };
inline Rgba argb(uint32_t c) { return {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, ((c >> 24) & 255) / 255.f}; }

struct UiDraw {
    GLuint prog = 0, vao = 0, vbo = 0; int W = 0, H = 0;
    void init() {
        prog = program("#version 330 core\nlayout(location=0) in vec2 p; layout(location=1) in vec2 uv; uniform vec2 screen; out vec2 v; void main(){ v = uv; vec2 n = p / screen * 2.0 - 1.0; gl_Position = vec4(n.x, -n.y, 0.0, 1.0); }",
                       "#version 330 core\nin vec2 v; uniform sampler2D tex; uniform int mode; uniform vec4 tint; out vec4 o; void main(){\n"
                       " if (mode == 0) o = tint; else if (mode == 1) o = texture(tex, v) * tint; else o = vec4(tint.rgb, tint.a * texture(tex, v).r);\n if (o.a < 0.004) discard; }");
        glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0); glEnableVertexAttribArray(0); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8); glEnableVertexAttribArray(1);
    }
    void begin(int w, int h) {
        W = w; H = h; glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(prog); glUniform2f(glGetUniformLocation(prog, "screen"), (float)w, (float)h); glUniform1i(glGetUniformLocation(prog, "tex"), 0);
        glActiveTexture(GL_TEXTURE0); glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    }
    void end() { glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); }
    void draw(const std::vector<float>& v, GLuint tex, int mode, Rgba c) {
        if (v.empty()) return;
        glBindTexture(GL_TEXTURE_2D, tex); glUniform1i(glGetUniformLocation(prog, "mode"), mode); glUniform4f(glGetUniformLocation(prog, "tint"), c.r, c.g, c.b, c.a);
        glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 4));
    }
    static void pushQuad(std::vector<float>& v, float x, float y, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
        float q[24] = {x, y, u0, v0, x + w, y, u1, v0, x + w, y + h, u1, v1, x, y, u0, v0, x + w, y + h, u1, v1, x, y + h, u0, v1}; v.insert(v.end(), q, q + 24);
    }
    void solid(float x, float y, float w, float h, Rgba c) { std::vector<float> v; pushQuad(v, x, y, w, h); draw(v, 0, 0, c); }
    void image(GLuint tex, float x, float y, float w, float h, Rgba tint = {}, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) { std::vector<float> v; pushQuad(v, x, y, w, h, u0, v0, u1, v1); draw(v, tex, 1, tint); }
};

// One TrueType face at one pixel size, ASCII 32..126 baked into an R8 atlas.
struct TtFont {
    struct G { float u0, v0, u1, v1; int w, h, left, top; float adv; };
    GLuint tex = 0; std::map<int, G> g; float ascent = 0, lineH = 0, px = 0;
    bool load(const std::string& path, float pixelSize) {
        static FT_Library lib = nullptr; if (!lib && FT_Init_FreeType(&lib)) return false;
        FT_Face face; if (FT_New_Face(lib, path.c_str(), 0, &face)) return false;
        FT_Set_Pixel_Sizes(face, 0, (FT_UInt)std::max(6.f, pixelSize)); px = pixelSize;
        const int AW = 512, AH = 512; std::vector<uint8_t> atlas((size_t)AW * AH, 0); int cx = 1, cy = 1, rowH = 0;
        ascent = (float)(face->size->metrics.ascender >> 6); lineH = (float)(face->size->metrics.height >> 6);
        for (int c = 32; c < 127; c++) {
            if (FT_Load_Char(face, (FT_ULong)c, FT_LOAD_RENDER)) continue;
            FT_GlyphSlot s = face->glyph; int w = (int)s->bitmap.width, h = (int)s->bitmap.rows;
            if (cx + w + 1 > AW) { cx = 1; cy += rowH + 1; rowH = 0; }
            if (cy + h + 1 > AH) break;
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) atlas[(size_t)(cy + y) * AW + cx + x] = s->bitmap.buffer[y * s->bitmap.pitch + x];
            g[c] = {(float)cx / AW, (float)cy / AH, (float)(cx + w) / AW, (float)(cy + h) / AH, w, h, s->bitmap_left, s->bitmap_top, (float)(s->advance.x >> 6)};
            cx += w + 1; rowH = std::max(rowH, h);
        }
        FT_Done_Face(face);
        glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, AW, AH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        return !g.empty();
    }
    float width(const std::string& s) const { float w = 0; for (unsigned char c : s) { auto it = g.find(c); if (it != g.end()) w += it->second.adv; } return w; }
    // (x, y) = top-left of the line box
    void draw(UiDraw& ui, float x, float y, const std::string& s, Rgba c) const {
        std::vector<float> v; float cx = x;
        for (unsigned char ch : s) {
            auto it = g.find(ch); if (it == g.end()) continue; const G& q = it->second;
            if (q.w) UiDraw::pushQuad(v, std::round(cx + q.left), std::round(y + ascent - q.top), (float)q.w, (float)q.h, q.u0, q.v0, q.u1, q.v1);
            cx += q.adv;
        }
        ui.draw(v, tex, 2, c);
    }
};
