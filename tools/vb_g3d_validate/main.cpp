// vb_g3d_validate — validates the G3D parser across many files.
//
// Usage: vb_g3d_validate <test.g3d>           // single file
//    or  vb_g3d_validate <dir>/              // all .g3d in directory
//    or  vb_g3d_validate -a <dir>            // all .g3d, aggregate stats
//
// Outputs vertex count, face count, material count, texture name,
// and validates: all face indices in [0, nverts), finite coords,
// consistent normals, no NaN/inf.

#include "assets/g3d.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

static bool is_dir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_g3d(const std::string& path) {
    return path.size() > 4 && path.substr(path.size() - 4) == ".g3d";
}

static std::vector<std::string> list_g3d(const std::string& dir) {
    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (!d) { fprintf(stderr, "Cannot open dir: %s\n", dir.c_str()); return files; }
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (is_g3d(name)) {
            files.push_back(dir + "/" + name);
        }
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    return files;
}

struct Result {
    std::string file;
    bool ok = true;
    uint32_t nverts = 0, nfaces = 0, nnormals = 0, nnormals_with_lm = 0;
    uint32_t nmaterials = 0, ntextures = 0;
    std::string tex_name;
    float min_x = 1e30f, max_x = -1e30f, min_y = 1e30f, max_y = -1e30f,
           min_z = 1e30f, max_z = -1e30f;
    std::vector<std::string> errors;

    void add_error(const std::string& msg) {
        ok = false;
        errors.push_back(msg);
    }
};

static Result validate(const std::string& path) {
    Result r;
    r.file = path;

    try {
        vb::Mesh mesh = vb::parseG3D(path);
        r.nverts = mesh.nverts();
        r.nfaces = mesh.nfaces();
        r.nmaterials = (uint32_t)mesh.materials.size();
        r.ntextures = (uint32_t)(!mesh.texture_name.empty() ? 1 : 0);
        r.tex_name = mesh.texture_name;
        r.nnormals = (uint32_t)mesh.vertices.size();

        // Validate face indices
        for (uint32_t i = 0; i < mesh.faces.size(); i++) {
            for (int j = 0; j < 3; j++) {
                if (mesh.faces[i].vert[j] >= mesh.nverts()) {
                    r.add_error(std::string("Face ") + std::to_string(i) +
                               " vert[" + std::to_string(j) + "] = " +
                               std::to_string(mesh.faces[i].vert[j]) +
                               " >= nverts(" + std::to_string(mesh.nverts()) + ")");
                }
            }
        }

        // Validate coordinates and normals
        for (uint32_t i = 0; i < mesh.vertices.size(); i++) {
            for (int j = 0; j < 3; j++) {
                float v = mesh.vertices[i].pos[j];
                if (std::isnan(v) || std::isinf(v)) {
                    r.add_error(std::string("Vertex ") + std::to_string(i) +
                               " pos[" + std::to_string(j) + "] is NaN/inf");
                } else {
                    if (j == 0) { r.min_x = std::min(r.min_x, v); r.max_x = std::max(r.max_x, v); }
                    if (j == 1) { r.min_y = std::min(r.min_y, v); r.max_y = std::max(r.max_y, v); }
                    if (j == 2) { r.min_z = std::min(r.min_z, v); r.max_z = std::max(r.max_z, v); }
                }
            }
            for (int j = 0; j < 3; j++) {
                float v = mesh.vertices[i].norm[j];
                if (std::isnan(v) || std::isinf(v)) {
                    r.add_error(std::string("Vertex ") + std::to_string(i) +
                               " norm[" + std::to_string(j) + "] is NaN/inf");
                }
            }
            for (int j = 0; j < 4; j++) {
                float v = mesh.vertices[i].color[j];
                if (std::isnan(v) || std::isinf(v)) {
                    r.add_error(std::string("Vertex ") + std::to_string(i) +
                               " color[" + std::to_string(j) + "] is NaN/inf");
                }
            }
            for (int j = 0; j < 2; j++) {
                float v = mesh.vertices[i].uv[j];
                if (std::isnan(v) || std::isinf(v)) {
                    r.add_error(std::string("Vertex ") + std::to_string(i) +
                               " uv[" + std::to_string(j) + "] is NaN/inf");
                }
            }
            for (int j = 0; j < 2; j++) {
                float v = mesh.vertices[i].lm[j];
                if (std::isnan(v) || std::isinf(v)) {
                    r.add_error(std::string("Vertex ") + std::to_string(i) +
                               " lm[" + std::to_string(j) + "] is NaN/inf");
                } else if (v != 0.0f) {
                    r.nnormals_with_lm++;
                }
            }
        }
    } catch (const std::exception& e) {
        r.ok = false;
        r.add_error(std::string("Exception: ") + e.what());
    }
    return r;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.g3d> | <dir>/ | -a <dir>/ [-n N]\n", argv[0]);
        return 1;
    }

    int arg = 1;
    bool aggregate = (std::string(argv[arg]) == "-a" && argc > 2);
    if (aggregate) { arg++; }

    bool limit = (argc > arg + 1 && std::string(argv[arg + 1]) == "-n");
    uint32_t limit_n = 0;
    if (limit) {
        limit_n = atoi(argv[arg + 2]);
        arg += 3;
    }

    if (argc > arg + 1 && std::string(argv[arg + 1]) == "-n") {
        limit_n = atoi(argv[arg + 2]);
        arg += 3;
    }

    if (argc > arg + 1 && std::string(argv[arg + 1]) == "-n") {
        limit_n = atoi(argv[arg + 2]);
        arg += 3;
    }

    std::string target(argv[arg]);
    std::vector<std::string> files;

    if (is_dir(target)) {
        files = list_g3d(target);
        if (limit_n > 0 && limit_n < files.size()) {
            files.resize(limit_n);
        }
    } else {
        files.push_back(target);
    }

    if (files.empty()) {
        fprintf(stderr, "No .g3d files found.\n");
        return 1;
    }

    int total = files.size();
    int ok_count = 0;
    int fail_count = 0;
    std::vector<Result> results;

    for (size_t i = 0; i < files.size(); i++) {
        Result r = validate(files[i]);
        results.push_back(r);
        if (r.ok) {
            ok_count++;
            printf("[OK] %s: %u verts, %u faces, %u mats, tex=%s\n",
                   files[i].c_str(), r.nverts, r.nfaces, r.nmaterials,
                   r.tex_name.empty() ? "(none)" : r.tex_name.c_str());
        } else {
            fail_count++;
            printf("[FAIL] %s:\n", files[i].c_str());
            for (const auto& e : r.errors) {
                printf("  %s\n", e.c_str());
            }
        }
    }

    printf("\n=== SUMMARY ===\n");
    printf("Total: %d | OK: %d | Fail: %d\n", total, ok_count, fail_count);

    if (aggregate && !results.empty()) {
        // Aggregate stats
        uint64_t total_verts = 0, total_faces = 0;
        int max_verts = 0, max_faces = 0;
        int lm_count = 0;
        for (const auto& r : results) {
            total_verts += r.nverts;
            total_faces += r.nfaces;
            max_verts = std::max(max_verts, (int)r.nverts);
            max_faces = std::max(max_faces, (int)r.nfaces);
            lm_count += r.nnormals_with_lm;
        }
        printf("Total vertices: %lu, Total faces: %lu\n", (unsigned long)total_verts, (unsigned long)total_faces);
        printf("Max verts: %d, Max faces: %d\n", max_verts, max_faces);
        printf("Files with lightmap UVs: %d\n", lm_count);

        // Distribution of face counts
        std::vector<int> face_dist;
        for (const auto& r : results) {
            face_dist.push_back(r.nfaces);
        }
        std::sort(face_dist.begin(), face_dist.end());
        printf("Face count distribution:\n");
        for (size_t i = 0; i < face_dist.size(); i += std::max(size_t(1), face_dist.size() / 20)) {
            printf("  %d: %d faces\n", i * 5, face_dist[i]);
        }
    }

    return fail_count == 0 ? 0 : 1;
}
