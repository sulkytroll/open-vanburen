#pragma once
// The player's light as a field of visibility instead of a plain disc (the tech demo's light "radiates from the player like a sphere" and responds to the surroundings):
// a cell of the collision grid (0.5 world units) is lit when a straight line from the player reaches it without crossing a blocked cell (walls, props, closed doors; the
// first blocked cell hit is lit itself, so wall faces facing the player light up). The occlusion mask is blurred a little and uploaded as an R8 texture that the world and
// model shaders multiply with a spherical fall-off (distance in 3D from the light point, see gmodel.h Fog); the CPU keeps the same mask for the game rules (who can be seen /
// clicked) through at().
#include "common/nav.h"
#include <GL/glew.h>
#include <algorithm>
#include <cmath>
#include <vector>

struct LightField {
    std::vector<uint8_t> px; int w = 0, h = 0; float ox = 0, oz = 0; GLuint tex = 0;
    int lastCx = -99999, lastCz = -99999; unsigned lastSig = 0xffffffffu; bool dirty = false;
    int dx0 = 0, dz0 = 0, dx1 = -1, dz1 = -1;                  // rectangle that still has to be uploaded
    int rx0 = 0, rz0 = 0, rx1 = -1, rz1 = -1;                  // the region written last time (cleared before the next update)

    // the same grid as the collision grid (cell index = z * w + x, origin = the grid's min corner)
    void init(int gw, int gh, float gox, float goz) {
        ox = gox; oz = goz; w = gw; h = gh;
        px.assign((size_t)w * h, 0); lastCx = lastCz = -99999; lastSig = 0xffffffffu; rx0 = rz0 = 0; rx1 = rz1 = -1; dirty = true;
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[4] = {0, 0, 0, 0}; glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    }

    // Recompute around (x, z) out to `radius` world units; does work only when the player has moved to another cell or `sig` (a signature of the door states) changed.
    // `blocked(cellIndex)` tells the occluders. Returns true when the field changed.
    template <class Blocked> bool update(float x, float z, float radius, unsigned sig, Blocked blocked) {
        int cx = (int)std::floor((x - ox) / 0.5f), cz = (int)std::floor((z - oz) / 0.5f);
        if (cx == lastCx && cz == lastCz && sig == lastSig) return false;
        lastCx = cx; lastCz = cz; lastSig = sig;
        int ux0 = rx0, uz0 = rz0, ux1 = rx1, uz1 = rz1;                                      // dirty rectangle = old region + new region
        for (int iz = std::max(0, rz0); iz <= std::min(h - 1, rz1); iz++) for (int ix = std::max(0, rx0); ix <= std::min(w - 1, rx1); ix++) px[(size_t)iz * w + ix] = 0;
        int rc = (int)std::ceil(radius / 0.5f) + 2; int n = 2 * rc + 1;
        std::vector<float> a((size_t)n * n, 0.f), b((size_t)n * n, 0.f);
        const float r2 = (radius + 1.f) * (radius + 1.f) * 4.f;                             // in cells squared
        for (int dz = -rc; dz <= rc; dz++) for (int dx = -rc; dx <= rc; dx++) {
            if ((float)(dx * dx + dz * dz) > r2) continue;
            int tx = cx + dx, tz = cz + dz; if (tx < 0 || tz < 0 || tx >= w || tz >= h) continue;
            if (visible(cx, cz, tx, tz, blocked)) a[(size_t)(dz + rc) * n + (dx + rc)] = 1.f;
        }
        for (int pass = 0; pass < 2; pass++) {                                              // a soft edge: two passes of a 3 x 3 box blur
            std::vector<float>& s = pass == 0 ? a : b; std::vector<float>& d = pass == 0 ? b : a;
            for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) { float acc = 0.f; for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) { int ii = i + di, jj = j + dj; if (ii >= 0 && jj >= 0 && ii < n && jj < n) acc += s[(size_t)jj * n + ii]; } d[(size_t)j * n + i] = acc / 9.f; }
        }
        for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) { int ix = cx + i - rc, iz = cz + j - rc; if (ix < 0 || iz < 0 || ix >= w || iz >= h) continue; px[(size_t)iz * w + ix] = (uint8_t)std::min(255.f, a[(size_t)j * n + i] * 255.f + 0.5f); }
        rx0 = cx - rc; rz0 = cz - rc; rx1 = cx + rc; rz1 = cz + rc;
        if (ux1 < ux0) { ux0 = rx0; uz0 = rz0; ux1 = rx1; uz1 = rz1; } else { ux0 = std::min(ux0, rx0); uz0 = std::min(uz0, rz0); ux1 = std::max(ux1, rx1); uz1 = std::max(uz1, rz1); }
        if (!dirty) { dx0 = ux0; dz0 = uz0; dx1 = ux1; dz1 = uz1; } else { dx0 = std::min(dx0, ux0); dz0 = std::min(dz0, uz0); dx1 = std::max(dx1, ux1); dz1 = std::max(dz1, uz1); }
        dirty = true; return true;
    }
    void upload() {
        if (!dirty) return; dirty = false;
        int x0 = std::max(0, dx0), z0 = std::max(0, dz0), x1 = std::min(w - 1, dx1), z1 = std::min(h - 1, dz1); if (x1 < x0 || z1 < z0) return;
        glBindTexture(GL_TEXTURE_2D, tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ROW_LENGTH, w);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x0, z0, x1 - x0 + 1, z1 - z0 + 1, GL_RED, GL_UNSIGNED_BYTE, px.data() + (size_t)z0 * w + x0); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    // occlusion (0 .. 1) at a world position, bilinear like the texture
    float at(float x, float z) const {
        if (w <= 0) return 1.f;
        float fx = (x - ox) / 0.5f - 0.5f, fz = (z - oz) / 0.5f - 0.5f; int ix = (int)std::floor(fx), iz = (int)std::floor(fz); float tx = fx - (float)ix, tz = fz - (float)iz;
        auto g = [&](int i, int j) { return (i < 0 || j < 0 || i >= w || j >= h) ? 0.f : (float)px[(size_t)j * w + i] / 255.f; };
        return (g(ix, iz) * (1 - tx) + g(ix + 1, iz) * tx) * (1 - tz) + (g(ix, iz + 1) * (1 - tx) + g(ix + 1, iz + 1) * tx) * tz;
    }

private:
    // grid traversal (Amanatides & Woo) from the centre of cell (sx, sz) to the centre of (tx, tz): lit when nothing blocked is crossed before the target cell
    template <class Blocked> bool visible(int sx, int sz, int tx, int tz, Blocked& blocked) const {
        if (sx == tx && sz == tz) return true;
        int ddx = tx - sx, ddz = tz - sz, stepX = ddx > 0 ? 1 : -1, stepZ = ddz > 0 ? 1 : -1;
        float tDeltaX = ddx ? std::fabs(1.f / (float)ddx) : 1e30f, tDeltaZ = ddz ? std::fabs(1.f / (float)ddz) : 1e30f;
        float tMaxX = ddx ? 0.5f * tDeltaX : 1e30f, tMaxZ = ddz ? 0.5f * tDeltaZ : 1e30f;
        int x = sx, z = sz;
        for (int guard = 0; guard < 400; guard++) {
            if (tMaxX < tMaxZ) { x += stepX; tMaxX += tDeltaX; } else { z += stepZ; tMaxZ += tDeltaZ; }
            if (x == tx && z == tz) return true;                                          // reached: lit (a blocked target cell is lit too: the wall face)
            if (x < 0 || z < 0 || x >= w || z >= h) return false;
            if (blocked(z * w + x)) return false;
        }
        return false;
    }
};
