#pragma once
// Walkability grid + A* pathfinding over the map's collision grid (.col, 2 cells per world unit).
// Cell values (seen in the Junktown grid): 1 dirt, 9 pavement, 5/12/16/23/27/31/34/38/42 other floor types (walkable);
// 44 = prop/vehicle footprints and 47 = walls/void (blocked).
#include "assets/col.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

struct NavGrid {
    int w = 0, h = 0;
    float ox = 0, oz = 0;                 // world position of the grid's min corner
    static constexpr float kCell = 0.5f;
    static constexpr size_t kMinWall = 20;     // cells (5 square units): smaller 47-blobs do not stop light
    std::vector<uint8_t> walk, clear, raw, wallish, doorOcc;     // doorOcc: cells of closed doors (they stop light)
        // wallish: cells of 47-blobs big enough to be walls / buildings / cliffs (small blobs are poles, bushes, props)
        // raw = the baked collision value of every cell
    // clear = walkable with all 8 neighbours walkable (one cell of clearance around the player)

    static bool blockedValue(int v) { return v == 44 || v == 47; }
    void build(const vb::Col& c, float minX, float minZ) {
        w = c.width; h = c.height; ox = minX; oz = minZ;
        walk.assign((size_t)w * h, 0); clear.assign((size_t)w * h, 0); raw.assign((size_t)w * h, 0);
        for (int z = 0; z < h; z++) for (int x = 0; x < w; x++) raw[(size_t)z * w + x] = (uint8_t)c.at(x, z);
        wallish.assign((size_t)w * h, 0); doorOcc.assign((size_t)w * h, 0);
        { std::vector<uint8_t> seen((size_t)w * h, 0); std::vector<int> st, cells;           // blobs of 8-connected 47 cells; blobs of at least kMinWall cells are walls
          for (int s0 = 0; s0 < w * h; s0++) { if (raw[(size_t)s0] != 47 || seen[(size_t)s0]) continue; st.assign(1, s0); cells.clear(); seen[(size_t)s0] = 1;
            while (!st.empty()) { int c0 = st.back(); st.pop_back(); cells.push_back(c0); int cx = c0 % w, cz = c0 / w;
              for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) { int nx = cx + dx, nz = cz + dz; if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue; size_t ni = (size_t)nz * w + nx; if (raw[ni] == 47 && !seen[ni]) { seen[ni] = 1; st.push_back((int)ni); } } }
            if (cells.size() >= kMinWall) for (int c0 : cells) wallish[(size_t)c0] = 1; } }
        for (int z = 0; z < h; z++) for (int x = 0; x < w; x++) walk[(size_t)z * w + x] = !blockedValue(c.at(x, z));
        for (int z = 0; z < h; z++) for (int x = 0; x < w; x++) {
            bool ok = walk[(size_t)z * w + x];
            for (int dz = -1; dz <= 1 && ok; dz++) for (int dx = -1; dx <= 1 && ok; dx++) { int nx = x + dx, nz = z + dz; if (nx < 0 || nz < 0 || nx >= w || nz >= h || !walk[(size_t)nz * w + nx]) ok = false; }
            clear[(size_t)z * w + x] = ok;
        }
    }
    bool valid() const { return w > 0; }
    // light is stopped by walls, fences and closed doors but not by props (traffic lights, barrels, cars: the baked value 44): a blocked cell that is not a prop footprint
    bool blocksLight(size_t idx) const { return wallish[idx] || doorOcc[idx]; }
    void setDoorOcc(const std::vector<int>& cells, bool v) { for (int c : cells) if (c >= 0 && (size_t)c < doorOcc.size()) doorOcc[(size_t)c] = v ? 1 : 0; }       // walls (big 47 blobs), fences and closed doors block light; props and small blobs (traffic light poles, bushes) do not
    // Doors: the collision grid was baked with every door closed. Opening one makes the blocked cells inside its (oriented) footprint walkable; the returned cell list restores them.
    // M = column-major model matrix of the door (rotation + translation), [lo,hi] = its local bounding box.
    std::vector<int> openBox(const float* M, const float* lo, const float* hi, float pad) {
        std::vector<int> changed;
        float cxs[4] = {lo[0], hi[0], lo[0], hi[0]}, czs[4] = {lo[2], lo[2], hi[2], hi[2]}, minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
        for (int i = 0; i < 4; i++) { float wx = M[0] * cxs[i] + M[8] * czs[i] + M[12], wz = M[2] * cxs[i] + M[10] * czs[i] + M[14]; minX = std::min(minX, wx); maxX = std::max(maxX, wx); minZ = std::min(minZ, wz); maxZ = std::max(maxZ, wz); }
        int x0, z0, x1, z1; cellOf(minX - pad, minZ - pad, x0, z0); cellOf(maxX + pad, maxZ + pad, x1, z1);
        for (int z = std::max(0, z0); z <= std::min(h - 1, z1); z++) for (int x = std::max(0, x0); x <= std::min(w - 1, x1); x++) {
            if (walk[(size_t)z * w + x]) continue;
            float wx, wz; centerOf(x, z, wx, wz); float dx = wx - M[12], dz = wz - M[14];
            float lx = M[0] * dx + M[2] * dz, lz = M[8] * dx + M[10] * dz;
            if (lx >= lo[0] - pad && lx <= hi[0] + pad && lz >= lo[2] - pad && lz <= hi[2] + pad) { walk[(size_t)z * w + x] = 1; changed.push_back(z * w + x); }
        }
        refreshClear(x0 - 2, z0 - 2, x1 + 2, z1 + 2);
        return changed;
    }
    // Block the cells inside a closed door's footprint (the baked grid does not contain most doors); returns the cells that were walkable.
    std::vector<int> blockBox(const float* M, const float* lo, const float* hi, float pad) {
        std::vector<int> changed;
        float cxs[4] = {lo[0], hi[0], lo[0], hi[0]}, czs[4] = {lo[2], lo[2], hi[2], hi[2]}, minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
        for (int i = 0; i < 4; i++) { float wx = M[0] * cxs[i] + M[8] * czs[i] + M[12], wz = M[2] * cxs[i] + M[10] * czs[i] + M[14]; minX = std::min(minX, wx); maxX = std::max(maxX, wx); minZ = std::min(minZ, wz); maxZ = std::max(maxZ, wz); }
        int x0, z0, x1, z1; cellOf(minX - pad, minZ - pad, x0, z0); cellOf(maxX + pad, maxZ + pad, x1, z1);
        for (int z = std::max(0, z0); z <= std::min(h - 1, z1); z++) for (int x = std::max(0, x0); x <= std::min(w - 1, x1); x++) {
            if (!walk[(size_t)z * w + x]) continue;
            float wx, wz; centerOf(x, z, wx, wz); float dx = wx - M[12], dz = wz - M[14];
            float lx = M[0] * dx + M[2] * dz, lz = M[8] * dx + M[10] * dz;
            if (lx >= lo[0] - pad && lx <= hi[0] + pad && lz >= lo[2] - pad && lz <= hi[2] + pad) { walk[(size_t)z * w + x] = 0; changed.push_back(z * w + x); }
        }
        refreshClear(x0 - 2, z0 - 2, x1 + 2, z1 + 2);
        return changed;
    }
    void openCells(const std::vector<int>& cells) {      // undo blockBox
        if (cells.empty()) return;
        int x0 = w, z0 = h, x1 = 0, z1 = 0;
        for (int c : cells) { walk[(size_t)c] = 1; int x = c % w, z = c / w; x0 = std::min(x0, x); x1 = std::max(x1, x); z0 = std::min(z0, z); z1 = std::max(z1, z); }
        refreshClear(x0 - 2, z0 - 2, x1 + 2, z1 + 2);
    }
    void closeCells(const std::vector<int>& cells) {
        if (cells.empty()) return;
        int x0 = w, z0 = h, x1 = 0, z1 = 0;
        for (int c : cells) { walk[(size_t)c] = 0; int x = c % w, z = c / w; x0 = std::min(x0, x); x1 = std::max(x1, x); z0 = std::min(z0, z); z1 = std::max(z1, z); }
        refreshClear(x0 - 2, z0 - 2, x1 + 2, z1 + 2);
    }
    void refreshClear(int x0, int z0, int x1, int z1) {
        for (int z = std::max(0, z0); z <= std::min(h - 1, z1); z++) for (int x = std::max(0, x0); x <= std::min(w - 1, x1); x++) {
            bool ok = walk[(size_t)z * w + x];
            for (int dz = -1; dz <= 1 && ok; dz++) for (int dx = -1; dx <= 1 && ok; dx++) { int nx = x + dx, nz = z + dz; if (nx < 0 || nz < 0 || nx >= w || nz >= h || !walk[(size_t)nz * w + nx]) ok = false; }
            clear[(size_t)z * w + x] = ok;
        }
    }
    bool in(int cx, int cz) const { return cx >= 0 && cz >= 0 && cx < w && cz < h; }
    void cellOf(float x, float z, int& cx, int& cz) const { cx = (int)std::floor((x - ox) / kCell); cz = (int)std::floor((z - oz) / kCell); }
    void centerOf(int cx, int cz, float& x, float& z) const { x = ox + (cx + 0.5f) * kCell; z = oz + (cz + 0.5f) * kCell; }
    bool walkableAt(float x, float z) const { int cx, cz; cellOf(x, z, cx, cz); return in(cx, cz) && walk[(size_t)cz * w + cx]; }
    // nearest cell satisfying `grid` within maxR cells of (cx,cz)
    bool nearest(const std::vector<uint8_t>& grid, int cx, int cz, int maxR, int& rx, int& rz) const {
        float best = 1e30f; bool found = false;
        for (int r = 0; r <= maxR; r++) {
            for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                int x = cx + dx, z = cz + dz; if (!in(x, z) || !grid[(size_t)z * w + x]) continue;
                float d = (float)(dx * dx + dz * dz); if (d < best) { best = d; rx = x; rz = z; found = true; }
            }
            if (found) return true;
        }
        return false;
    }
    bool lineClear(float x0, float z0, float x1, float z1, const std::vector<uint8_t>& grid) const {
        float dx = x1 - x0, dz = z1 - z0, len = std::sqrt(dx * dx + dz * dz);
        int n = std::max(1, (int)(len / (kCell * 0.5f)));
        for (int i = 0; i <= n; i++) {
            int cx, cz; cellOf(x0 + dx * i / n, z0 + dz * i / n, cx, cz);
            if (!in(cx, cz) || !grid[(size_t)cz * w + cx]) return false;
        }
        return true;
    }
    // Path to the nearest (by walking distance) reachable cell from which (tx,tz) is within `reach` world units; `out` is empty when the start is already that close.
    // Used to walk up to a door / container / person: the target itself may stand on blocked cells (a closed door, a crate), so the goal is "any cell near it", on whichever side the player can reach.
    bool findPathWithin(float sx, float sz, float tx, float tz, float reach, std::vector<std::pair<float, float>>& out) const {
        out.clear();
        if (!valid()) return true;
        if (std::hypot(sx - tx, sz - tz) <= reach) return true;
        for (const std::vector<uint8_t>* gridp : {&clear, &walk}) {
            const std::vector<uint8_t>& grid = *gridp; int scx, scz, rx, rz; cellOf(sx, sz, scx, scz);
            if (!nearest(grid, scx, scz, 6, rx, rz)) continue;
            std::vector<float> g((size_t)w * h, 1e30f); std::vector<int> prev((size_t)w * h, -1);
            using QE = std::pair<float, int>; std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
            int s0 = rz * w + rx; g[s0] = 0; q.push({0.f, s0}); int goal = -1;
            while (!q.empty()) {
                auto [f, cur] = q.top(); q.pop(); if (f > g[cur]) continue;
                int cx = cur % w, cz = cur / w; float wx, wz; centerOf(cx, cz, wx, wz);
                if (std::hypot(wx - tx, wz - tz) <= reach) { goal = cur; break; }
                for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dz) continue;
                    int nx = cx + dx, nz = cz + dz; if (!in(nx, nz) || !grid[(size_t)nz * w + nx]) continue;
                    if (dx && dz && (!grid[(size_t)cz * w + nx] || !grid[(size_t)nz * w + cx])) continue;
                    float ng = g[cur] + ((dx && dz) ? 1.41421356f : 1.f); int ni = nz * w + nx;
                    if (ng < g[ni]) { g[ni] = ng; prev[ni] = cur; q.push({ng, ni}); }
                }
            }
            if (goal < 0) continue;
            std::vector<std::pair<float, float>> pts; pts.push_back({sx, sz}); std::vector<std::pair<float, float>> raw;
            for (int c = goal; c != -1; c = prev[c]) { float x, z; centerOf(c % w, c / w, x, z); raw.push_back({x, z}); }
            std::reverse(raw.begin(), raw.end()); for (auto& p : raw) pts.push_back(p);
            size_t i = 0; out.push_back(pts[0]);
            while (i + 1 < pts.size()) { size_t j = pts.size() - 1; while (j > i + 1 && !lineClear(pts[i].first, pts[i].second, pts[j].first, pts[j].second, grid)) j--; out.push_back(pts[j]); i = j; }
            out.erase(out.begin());
            return true;
        }
        return false;
    }
    // Path from (sx,sz) to (gx,gz) as world-space waypoints (the goal snapped to the nearest reachable cell). False if nothing is reachable.
    bool findPath(float sx, float sz, float gx, float gz, std::vector<std::pair<float, float>>& out) const {
        out.clear();
        if (!valid()) { out.push_back({gx, gz}); return true; }
        int scx, scz, gcx, gcz; cellOf(sx, sz, scx, scz); cellOf(gx, gz, gcx, gcz);
        for (const std::vector<uint8_t>* gridp : {&clear, &walk}) {
            const std::vector<uint8_t>& grid = *gridp;
            int rx, rz;
            if (!nearest(grid, scx, scz, 6, rx, rz)) continue; int sCx = rx, sCz = rz;
            if (!nearest(grid, gcx, gcz, 40, rx, rz)) continue; int tCx = rx, tCz = rz;
            // A* (8-connected, no corner cutting)
            std::vector<float> g((size_t)w * h, 1e30f); std::vector<int> prev((size_t)w * h, -1);
            using QE = std::pair<float, int>; std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
            auto hfn = [&](int x, int z) { float dx = (float)std::abs(x - tCx), dz = (float)std::abs(z - tCz); return (dx + dz) + (1.41421356f - 2.f) * std::min(dx, dz); };
            int s = sCz * w + sCx; g[s] = 0; q.push({hfn(sCx, sCz), s});
            bool reached = false;
            while (!q.empty()) {
                auto [f, cur] = q.top(); q.pop();
                int cx = cur % w, cz = cur / w;
                if (cx == tCx && cz == tCz) { reached = true; break; }
                if (f > g[cur] + hfn(cx, cz) + 1e-3f) continue;
                for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dz) continue;
                    int nx = cx + dx, nz = cz + dz; if (!in(nx, nz) || !grid[(size_t)nz * w + nx]) continue;
                    if (dx && dz && (!grid[(size_t)cz * w + nx] || !grid[(size_t)nz * w + cx])) continue;
                    float ng = g[cur] + ((dx && dz) ? 1.41421356f : 1.f);
                    int ni = nz * w + nx; if (ng < g[ni]) { g[ni] = ng; prev[ni] = cur; q.push({ng + hfn(nx, nz), ni}); }
                }
            }
            if (!reached) continue;
            std::vector<std::pair<float, float>> raw;
            for (int c = tCz * w + tCx; c != -1; c = prev[c]) { float x, z; centerOf(c % w, c / w, x, z); raw.push_back({x, z}); }
            std::reverse(raw.begin(), raw.end());
            // snap the exact goal on the end when it is itself reachable, then string-pull
            if (gcx == tCx && gcz == tCz) raw.back() = {gx, gz};
            std::vector<std::pair<float, float>> pts; pts.push_back({sx, sz});
            for (auto& p : raw) pts.push_back(p);
            size_t i = 0; out.push_back(pts[0]);
            while (i + 1 < pts.size()) {
                size_t j = pts.size() - 1;
                while (j > i + 1 && !lineClear(pts[i].first, pts[i].second, pts[j].first, pts[j].second, grid)) j--;
                out.push_back(pts[j]); i = j;
            }
            out.erase(out.begin());     // drop the start point itself
            return true;
        }
        return false;
    }
};
