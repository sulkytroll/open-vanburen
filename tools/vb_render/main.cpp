// vb_render — native SDL2/GLEW renderer.
//
// Loads a TGA asset (default: a large interface texture) and draws it as a
// textured quad filling the window. Proves the native rendering path: window,
// OpenGL context, texture upload, and the original game art on screen.
//
// Usage: vb_render [path/to/texture.tga]
//   Add -screenshot <path> to grab a frame and exit.

#include "assets/tga.h"
#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <cstdio>
#include <string>
#include <vector>

static void glCheck(const char* what) {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR)
        std::fprintf(stderr, "GL error after %s: 0x%x\n", what, e);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <file.tga> [-screenshot out.bmp]\n", argv[0]); return 2; }
    std::string texPath = argv[1];
    std::string shot;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "-screenshot" && i + 1 < argc) shot = argv[++i];

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("Van Buren (native)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    glewInit();
    std::printf("renderer: %s\n", (const char*)glGetString(GL_RENDERER));

    // Load texture.
    vb::Texture tex; std::string err;
    if (!vb::loadTga(texPath, tex, err)) {
        std::fprintf(stderr, "TGA load failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("texture: %s  %dx%d\n", texPath.c_str(), tex.width, tex.height);

    // Upload.
    GLuint texid;
    glGenTextures(1, &texid);
    glBindTexture(GL_TEXTURE_2D, texid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, tex.rgba.data());
    glCheck("texUpload");

    // Simple shader for a textured quad.
    const char* vs =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "out vec2 vUV;\n"
        "void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }";
    const char* fs =
        "#version 330 core\n"
        "in vec2 vUV;\n out vec4 frag;\n"
        "uniform sampler2D tex;\n"
        "void main(){ frag=texture(tex,vUV); }";
    auto compile = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char b[512]; glGetShaderInfoLog(s, 512, nullptr, b);
                   std::fprintf(stderr, "shader: %s\n", b); }
        return s;
    };
    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);
    glUseProgram(prog);
    glCheck("link");

    // Quad: fullscreen in NDC, UVs flipped so top row of image is on top.
    float verts[] = {
        -1.f,  1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 0.f,
         1.f, -1.f, 1.f, 1.f,
        -1.f, -1.f, 0.f, 1.f,
    };
    GLuint vbo; glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    GLuint vao; glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);

    bool done = false;
    int frames = 0;
    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) done = true;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) done = true;
        }
        glViewport(0, 0, 960, 600);
        glClearColor(0.1, 0.1, 0.12, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_QUADS, 0, 4);
        SDL_GL_SwapWindow(win);
        SDL_Delay(16);
        if (!shot.empty() && ++frames == 1) {
            // Read the framebuffer back and save as a BMP.
            std::vector<uint8_t> px(960u * 600u * 4);
            glReadPixels(0, 0, 960, 600, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
            SDL_Surface* s = SDL_CreateRGBSurfaceFrom(
                px.data(), 960, 600, 32, 960 * 4,
                0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
            if (s) { SDL_SaveBMP(s, shot.c_str()); SDL_FreeSurface(s); }
            std::fprintf(stderr, "screenshot: %s\n", shot.c_str());
            done = true;
        }
    }
    std::printf("exited after %d frames\n", frames);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
