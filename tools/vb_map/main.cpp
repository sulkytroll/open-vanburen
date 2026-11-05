// vb_map — Van Buren level viewer with a map selector.
//
//   vb_map                 open the map selector
//   vb_map <mapname>       open a map directly (Tab returns to the selector)
//
// Reads the tech demo folder directly: run it next to F3.exe or give -game DIR (or set VB_GAME_DIR); -assets DIR reads an extracted copy.
// Options: -game DIR  -assets DIR  -shot out.bmp  -cam x y z yawdeg pitchdeg  -markers  -col  -nolm  -size W H  -frames N  -list
//
// Selector: Up/Down/PgUp/PgDn/Home/End or mouse wheel/click to choose, type to filter, Enter to open, Esc to quit.
// Viewer:   WASD move in the ground plane (not along the view pitch), Space/Ctrl up/down, Shift fast, hold right mouse (or arrow keys) to look, wheel = speed,
//           Tab selector, [ / ] previous/next map, M markers, C collision overlay, L lightmaps, T textures,
//           F wireframe, H HUD, F1 help, Esc selector.

#include "common/level.h"
#include "common/gamedir.h"

struct Camera { V3 eye{0, 0, 0}; float yaw = 0, pitch = 0, speed = 20; };
static Camera defaultCamera(const Level& L) {
    V3 center = (L.cmin + L.cmax) * 0.5f;
    float ext = std::max(L.cmax.x - L.cmin.x, L.cmax.z - L.cmin.z);
    Camera c; c.eye = {center.x, L.cmax.y + ext * 0.55f, L.cmax.z + ext * 0.55f};
    V3 d = norm(center - c.eye);
    c.yaw = std::atan2(d.x, d.z); c.pitch = std::asin(d.y); c.speed = std::max(10.f, ext * 0.25f);
    return c;
}

// ---------------------------------------------------------------- map list ----
static std::vector<std::string> listMaps(const std::string& assets) {
    std::vector<std::string> out;
    for (const char* sub : {"Maps", "Engine"}) {
        for (const std::string& fn : vb::vfs::list(assets + "/" + sub)) if (fs::path(fn).extension() == ".tre") out.push_back(fs::path(fn).stem().string());
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        std::string x = a, y = b; for (auto& c : x) c = (char)std::tolower((unsigned char)c); for (auto& c : y) c = (char)std::tolower((unsigned char)c); return x < y; });
    return out;
}

enum class Mode { Selector, Viewer };

int main(int argc, char** argv) {
    std::string initial, assets, gameArg, shot;
    bool markers = false, showEnts = true, showNoTex = std::getenv("VB_NOTEX") == nullptr, animate = true, colOverlay = false, useLm = true, useTex = true, wire = false, haveCam = false, listOnly = false, hud = true, help = false;
    float camArg[5] = {0, 0, 0, 0, 0}; int W = 1920, H = 1080, frames = 3;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-assets" && i + 1 < argc) assets = argv[++i];
        else if (a == "-game" && i + 1 < argc) gameArg = argv[++i];
        else if (a == "-shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "-markers") markers = true;
        else if (a == "-col") colOverlay = true;
        else if (a == "-nolm") useLm = false;
        else if (a == "-list") listOnly = true;
        else if (a == "-size" && i + 2 < argc) { W = std::atoi(argv[++i]); H = std::atoi(argv[++i]); }
        else if (a == "-frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (a == "-cam" && i + 5 < argc) { for (float& f : camArg) f = (float)std::atof(argv[++i]); haveCam = true; }
        else if (a[0] != '-' && initial.empty()) initial = a;
    }
    if (assets.empty() && !findAndMountGame(gameArg, assets)) return 1;       // (-assets DIR reads an extracted copy instead)
    std::vector<std::string> maps = listMaps(assets);
    if (maps.empty()) { std::fprintf(stderr, "no .tre files under %s/Maps — is the assets path right? (-assets DIR)\n", assets.c_str()); return 1; }
    if (listOnly) { for (auto& m : maps) std::printf("%s\n", m.c_str()); return 0; }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* win = SDL_CreateWindow("Van Buren map viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    glewExperimental = GL_TRUE; glewInit();
    SDL_GL_SetSwapInterval(shot.empty() ? 1 : 0);
    std::printf("GL: %s\n", (const char*)glGetString(GL_RENDERER));

    LevelRenderer R(assets);
    Text text; text.init();
    Level L; Camera cam;
    Mode mode = Mode::Selector;
    std::string status;                          // error / info line for the selector

    // selector state
    std::string filter; std::vector<int> shown; int sel = 0, top = 0;
    auto refilter = [&](const std::string& keep) {
        shown.clear();
        std::string f = filter; for (auto& c : f) c = (char)std::tolower((unsigned char)c);
        for (int i = 0; i < (int)maps.size(); i++) {
            std::string n = maps[i]; for (auto& c : n) c = (char)std::tolower((unsigned char)c);
            if (f.empty() || n.find(f) != std::string::npos) shown.push_back(i);
        }
        sel = 0;
        for (int k = 0; k < (int)shown.size(); k++) if (maps[shown[k]] == keep) sel = k;
        top = std::max(0, sel - 5);
    };
    refilter("");

    int curIndex = -1;                           // index in `maps` of the loaded level
    auto drawLoading = [&](const std::string& n) {
        int w, h; SDL_GL_GetDrawableSize(win, &w, &h);
        glViewport(0, 0, w, h); glClearColor(0.05f, 0.06f, 0.08f, 1.f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        text.begin(w, h); text.draw(w * 0.5f - text.width("Loading " + n + " ...", 2.f) * 0.5f, h * 0.5f - 18, 2.f, 1, 1, 0.6f, "Loading " + n + " ..."); text.end();
        SDL_GL_SwapWindow(win);
    };
    auto openMap = [&](int idx) -> bool {
        drawLoading(maps[idx]);
        std::string err;
        if (!loadLevel(assets, maps[idx], R.lib, L, err)) { status = maps[idx] + ": " + err; return false; }
        R.buildEntities(L);
        curIndex = idx; cam = defaultCamera(L);
        if (haveCam) { cam.eye = {camArg[0], camArg[1], camArg[2]}; cam.yaw = camArg[3] * 3.14159265f / 180.f; cam.pitch = camArg[4] * 3.14159265f / 180.f; haveCam = false; }
        SDL_SetWindowTitle(win, ("Van Buren - " + maps[idx]).c_str());
        std::printf("%s: %zu leaves, %zu triangles, %zu materials, %zu textures, %zu batches%s%s\n", L.name.c_str(), L.tre.leaves.size(), L.tris,
                    L.tre.materials.size(), L.tre.textures.size(), L.batches.size(), L.haveMap ? ", map" : "", L.haveCol ? ", col" : "");
        mode = Mode::Viewer; status.clear();
        return true;
    };
    if (!initial.empty()) {
        auto it = std::find(maps.begin(), maps.end(), initial);
        if (it == maps.end()) { std::fprintf(stderr, "unknown map '%s' (run with -list)\n", initial.c_str()); return 1; }
        if (!openMap((int)(it - maps.begin()))) { std::fprintf(stderr, "%s\n", status.c_str()); return 1; }
    }
    SDL_StartTextInput();

    bool done = false, look = false; int frame = 0, fpsFrames = 0; float fps = 0; Uint32 last = SDL_GetTicks(), fpsT = last;
    while (!done) {
        SDL_Event e;
        int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
        const float rowH = 22.f;
        int rows = std::max(1, (int)((dh - 150) / rowH));
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { done = true; continue; }
            if (mode == Mode::Selector) {
                if (e.type == SDL_TEXTINPUT) { filter += e.text.text; refilter(shown.empty() ? "" : maps[shown[sel]]); }
                else if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE: if (!filter.empty()) { filter.clear(); refilter(""); } else if (curIndex >= 0) mode = Mode::Viewer; else done = true; break;
                        case SDLK_BACKSPACE: if (!filter.empty()) { filter.pop_back(); refilter(shown.empty() ? "" : maps[shown[sel]]); } break;
                        case SDLK_UP: sel = std::max(0, sel - 1); break;
                        case SDLK_DOWN: sel = std::min((int)shown.size() - 1, sel + 1); break;
                        case SDLK_PAGEUP: sel = std::max(0, sel - rows); break;
                        case SDLK_PAGEDOWN: sel = std::min((int)shown.size() - 1, sel + rows); break;
                        case SDLK_HOME: sel = 0; break;
                        case SDLK_END: sel = (int)shown.size() - 1; break;
                        case SDLK_RETURN: case SDLK_KP_ENTER: if (!shown.empty()) openMap(shown[sel]); break;
                    }
                    if (sel < top) top = sel; if (sel >= top + rows) top = sel - rows + 1;
                } else if (e.type == SDL_MOUSEWHEEL) {
                    top = std::max(0, std::min(std::max(0, (int)shown.size() - rows), top - e.wheel.y * 3));
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int k = (int)((e.button.y - 110) / rowH) + top;
                    if (e.button.y >= 110 && k >= 0 && k < (int)shown.size()) { if (k == sel) openMap(shown[k]); else sel = k; }
                }
            } else {
                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE: case SDLK_TAB: mode = Mode::Selector; look = false; SDL_SetRelativeMouseMode(SDL_FALSE);
                            filter.clear(); refilter(maps[curIndex]); if (sel < top) top = sel; if (sel >= top + rows) top = sel - rows + 1; break;
                        case SDLK_m: markers = !markers; break;
                        case SDLK_e: showEnts = !showEnts; break;
                        case SDLK_p: animate = !animate; break;
                        case SDLK_c: colOverlay = !colOverlay; break;
                        case SDLK_l: useLm = !useLm; break;
                        case SDLK_t: useTex = !useTex; break;
                        case SDLK_f: wire = !wire; break;
                        case SDLK_h: hud = !hud; break;
                        case SDLK_F1: help = !help; break;
                        case SDLK_LEFTBRACKET: openMap((curIndex + (int)maps.size() - 1) % (int)maps.size()); break;
                        case SDLK_RIGHTBRACKET: openMap((curIndex + 1) % (int)maps.size()); break;
                    }
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) { look = true; SDL_SetRelativeMouseMode(SDL_TRUE); }
                else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) { look = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
                else if (e.type == SDL_MOUSEMOTION && look) { cam.yaw += e.motion.xrel * 0.0035f; cam.pitch -= e.motion.yrel * 0.0035f; }
                else if (e.type == SDL_MOUSEWHEEL) cam.speed *= e.wheel.y > 0 ? 1.25f : 0.8f;
            }
        }
        Uint32 now = SDL_GetTicks(); float dt = std::min(0.1f, (now - last) / 1000.f); last = now;
        if (++fpsFrames >= 20) { fps = fpsFrames * 1000.f / std::max<Uint32>(1, now - fpsT); fpsFrames = 0; fpsT = now; }

        glViewport(0, 0, dw, dh);
        glClearColor(0.05f, 0.06f, 0.08f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (mode == Mode::Viewer) {
            const Uint8* k = SDL_GetKeyboardState(nullptr);
            if (k[SDL_SCANCODE_LEFT]) cam.yaw -= 1.5f * dt; if (k[SDL_SCANCODE_RIGHT]) cam.yaw += 1.5f * dt;
            if (k[SDL_SCANCODE_UP]) cam.pitch += 1.2f * dt;  if (k[SDL_SCANCODE_DOWN]) cam.pitch -= 1.2f * dt;
            cam.pitch = std::max(-1.55f, std::min(1.55f, cam.pitch));
            V3 fwd{std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), std::cos(cam.yaw) * std::cos(cam.pitch)};   // yaw 0 = +Z
            V3 right = norm(cross({0, 1, 0}, fwd));
            float sp = cam.speed * ((k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) ? 4.f : 1.f) * dt;
            V3 flat = norm(V3{fwd.x, 0, fwd.z});   // WASD moves in the horizontal (ground) plane only; Space/Ctrl change height
            if (k[SDL_SCANCODE_W]) cam.eye = cam.eye + flat * sp;  if (k[SDL_SCANCODE_S]) cam.eye = cam.eye - flat * sp;
            if (k[SDL_SCANCODE_D]) cam.eye = cam.eye + right * sp; if (k[SDL_SCANCODE_A]) cam.eye = cam.eye - right * sp;
            if (k[SDL_SCANCODE_SPACE]) cam.eye.y += sp;            if (k[SDL_SCANCODE_LCTRL]) cam.eye.y -= sp;

            float P[16], Vm[16], VP[16];
            perspective(P, 60.f, (float)dw / dh, 0.1f, 3000.f); lookAt(Vm, cam.eye, fwd);
            for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) { float s = 0; for (int i = 0; i < 4; i++) s += P[i*4+r] * Vm[c*4+i]; VP[c*4+r] = s; }

            LevelRenderer::Opts ro; ro.useTex = useTex; ro.useLm = useLm; ro.wire = wire; ro.showNoTex = showNoTex; ro.showEnts = showEnts; ro.animate = animate;
            R.drawWorld(L, VP, ro);
            glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
            R.drawEntities(L, VP, ro, (float)SDL_GetTicks() * 0.001f);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            if (colOverlay) R.drawCollisionOverlay(L, VP);
            if (markers) R.drawMarkers(L, VP);
            if (hud || help) {
                text.begin(dw, dh);
                if (hud) {
                    char buf[256];
                    float yawDeg = std::fmod(std::fmod(cam.yaw * 180.f / 3.14159265f, 360.f) + 360.f, 360.f), pitchDeg = cam.pitch * 180.f / 3.14159265f;
                    // the "-cam" argument pastes straight back into the command line to reproduce this exact view
                    std::snprintf(buf, sizeof buf, "%s   %zu tris  %zu batches  %.0f fps   -cam %.1f %.1f %.1f %.1f %.1f", L.name.c_str(), L.tris, L.batches.size(), fps, cam.eye.x, cam.eye.y, cam.eye.z, yawDeg, pitchDeg);
                    text.rect(0, 0, text.width(buf, 1.f) + 16, 26, 0, 0, 0, 0.55f);
                    text.draw(8, 3, 1.f, 1, 1, 1, buf);
                    std::string flags = std::string(markers ? "[M]arkers " : "") + (showEnts ? "" : "entities OFF ") + (animate ? "" : "animation OFF ") + (colOverlay ? "[C]ollision " : "") + (!useLm ? "lightmaps OFF " : "") + (!useTex ? "textures OFF " : "") + (wire ? "wireframe " : "");
                    if (!flags.empty()) text.draw(8, 30, 1.f, 1, 0.9f, 0.4f, flags);
                    if (markers && L.tre.has_grid) {
                        auto ids = L.tre.grid.visibleLights(cam.eye.x, cam.eye.z);
                        std::string t = "lights here (" + std::to_string(ids.size()) + "/" + std::to_string(L.tre.lights.size()) + "):";
                        for (size_t i = 0; i < ids.size() && i < 12; i++) t += " " + std::to_string(ids[i]);
                        text.draw(8, 52, 1.f, 0.7f, 1, 0.7f, t);
                    }
                    text.draw(8, dh - 24.f, 1.f, 0.8f, 0.8f, 0.8f, "Tab: maps   [ ]: prev/next   F1: help   H: hide HUD", 0.8f);
                }
                if (help) {
                    const char* lines_[] = {"Controls", "", "W A S D        move", "Space / Ctrl   up / down", "Shift          fast", "Right mouse    look (or arrow keys)",
                                            "Mouse wheel    camera speed", "M  markers (entities, triggers, nav, ...)", "E  entity models on/off", "P  idle animations on/off", "C  collision grid overlay", "L  lightmaps on/off",
                                            "T  textures on/off", "F  wireframe", "H  HUD   F1  this help", "Tab / Esc  map selector", "[ and ]    previous / next map"};
                    float bw = 460, bh = 22.f * (float)(sizeof(lines_) / sizeof(*lines_)) + 20;
                    text.rect(dw * 0.5f - bw * 0.5f, dh * 0.5f - bh * 0.5f, bw, bh, 0, 0, 0, 0.75f);
                    for (size_t i = 0; i < sizeof(lines_) / sizeof(*lines_); i++) text.draw(dw * 0.5f - bw * 0.5f + 16, dh * 0.5f - bh * 0.5f + 10 + 22.f * i, 1.f, i ? 0.9f : 1.f, i ? 0.9f : 0.9f, i ? 0.9f : 0.4f, lines_[i]);
                }
                text.end();
            }
        } else {
            text.begin(dw, dh);
            text.draw(24, 18, 2.2f, 1.f, 0.85f, 0.35f, "VAN BUREN  -  map viewer");
            std::string f = "Filter: " + filter + "_";
            text.draw(24, 68, 1.2f, 0.8f, 0.9f, 1.f, f);
            char cnt[64]; std::snprintf(cnt, sizeof cnt, "%zu / %zu maps", shown.size(), maps.size());
            text.draw(dw - text.width(cnt, 1.f) - 24, 72, 1.f, 0.6f, 0.6f, 0.6f, cnt);
            for (int r = 0; r < rows; r++) {
                int k = top + r; if (k >= (int)shown.size()) break;
                float y = 110 + r * rowH;
                bool cur = shown[k] == curIndex;
                if (k == sel) text.rect(16, y - 1, dw - 32.f, rowH, 0.25f, 0.4f, 0.7f, 0.55f);
                text.draw(28, y, 1.2f, cur ? 0.5f : 1.f, 1.f, cur ? 0.5f : 1.f, maps[shown[k]]);
                if (cur) text.draw(dw - 140.f, y, 1.f, 0.5f, 1.f, 0.5f, "(open)");
            }
            if (shown.empty()) text.draw(28, 110, 1.2f, 1.f, 0.5f, 0.5f, "no maps match");
            text.draw(24, dh - 52.f, 1.f, 0.7f, 0.7f, 0.7f, "Up/Down/PgUp/PgDn/mouse: choose    type: filter    Enter / double-click: open    Esc: back / quit");
            if (!status.empty()) text.draw(24, dh - 28.f, 1.f, 1.f, 0.4f, 0.4f, status);
            text.end();
        }

        SDL_GL_SwapWindow(win);
        if (!shot.empty() && mode == Mode::Viewer && ++frame == frames) {
            std::vector<uint8_t> px((size_t)dw * dh * 3), flip((size_t)dw * dh * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            for (int y = 0; y < dh; y++) std::memcpy(&flip[(size_t)y * dw * 3], &px[(size_t)(dh - 1 - y) * dw * 3], (size_t)dw * 3);
            SDL_Surface* s = SDL_CreateRGBSurfaceFrom(flip.data(), dw, dh, 24, dw * 3, 0x0000FF, 0x00FF00, 0xFF0000, 0);
            if (s) { SDL_SaveBMP(s, shot.c_str()); SDL_FreeSurface(s); std::printf("screenshot: %s\n", shot.c_str()); }
            done = true;
        }
        if (!shot.empty() && mode == Mode::Selector && ++frame == frames) {   // selector screenshot (no map given)
            std::vector<uint8_t> px((size_t)dw * dh * 3), flip((size_t)dw * dh * 3);
            glReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            for (int y = 0; y < dh; y++) std::memcpy(&flip[(size_t)y * dw * 3], &px[(size_t)(dh - 1 - y) * dw * 3], (size_t)dw * 3);
            SDL_Surface* s = SDL_CreateRGBSurfaceFrom(flip.data(), dw, dh, 24, dw * 3, 0x0000FF, 0x00FF00, 0xFF0000, 0);
            if (s) { SDL_SaveBMP(s, shot.c_str()); SDL_FreeSurface(s); std::printf("screenshot: %s\n", shot.c_str()); }
            done = true;
        }
    }
    L.destroy();
    SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
