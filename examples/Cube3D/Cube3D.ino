/// @file    Cube3D.ino
/// @brief   Morphing polyhedra demo with automatic convex hull triangulation
/// @example Cube3D.ino

#include <Arduino.h>
#include <FastLED.h>
#include <fl/gfx/gfx.h>
#include <fl/math/fixed_point/s16x16.h>
#include <fl/math/xymap.h>
#include <math.h>

#ifndef PIN_DATA
#define PIN_DATA 3
#endif

static const int W = 100;
static const int H = 100;
static const int NUM_LEDS = W * H;

CRGB leds[NUM_LEDS];
fl::XYMap xymap(W, H, false);

using fp = fl::s16x16;

static const int MAX_SOLID_VERTS = 24;
static const int MAX_SOLID_PLANES = 36;
static const int MAX_SOLID_TRIS = 96;
static const int MAX_RENDER_TRIS = 96;

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Tri {
    uint8_t a;
    uint8_t b;
    uint8_t c;
};

struct Plane {
    Vec3 n;
    float d;
};

struct SolidSeed {
    const char* name;
    const Vec3* verts;
    uint8_t vert_count;
};

struct SolidMesh {
    const char* name;
    Vec3 verts[MAX_SOLID_VERTS];
    uint8_t vert_count;
    Tri tris[MAX_SOLID_TRIS];
    uint8_t tri_count;
};

struct DrawTri {
    fp sx0, sy0;
    fp sx1, sy1;
    fp sx2, sy2;
    fp avg_z;
    fp nx, ny, nz;
    CRGB color;
    uint8_t edge_mask;
};

static Vec3 v_add(const Vec3& a, const Vec3& b) {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vec3 v_sub(const Vec3& a, const Vec3& b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

static Vec3 v_mul(const Vec3& v, float s) {
    return Vec3{v.x * s, v.y * s, v.z * s};
}

static float v_dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 v_cross(const Vec3& a, const Vec3& b) {
    return Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static float v_len(const Vec3& v) {
    return sqrtf(v_dot(v, v));
}

static Vec3 v_norm(const Vec3& v) {
    float l = v_len(v);
    if (l < 1e-7f) return Vec3{0.0f, 0.0f, 0.0f};
    return v_mul(v, 1.0f / l);
}

static Vec3 v_lerp(const Vec3& a, const Vec3& b, float t) {
    return Vec3{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

static float smoothstep(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static const float PHI = 1.61803398875f;
static const float INV_PHI = 0.61803398875f;

static const Vec3 TETRA_VERTS[] = {
    { 1.0f,  1.0f,  1.0f},
    {-1.0f, -1.0f,  1.0f},
    {-1.0f,  1.0f, -1.0f},
    { 1.0f, -1.0f, -1.0f},
};

static const Vec3 CUBE_VERTS[] = {
    {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f},
};

static const Vec3 OCTA_VERTS[] = {
    { 1.0f,  0.0f,  0.0f}, {-1.0f,  0.0f,  0.0f},
    { 0.0f,  1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f},
    { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f},
};

static const Vec3 ICOSA_VERTS[] = {
    { 0.0f,  1.0f,  PHI}, { 0.0f, -1.0f,  PHI},
    { 0.0f,  1.0f, -PHI}, { 0.0f, -1.0f, -PHI},
    { 1.0f,  PHI,  0.0f}, {-1.0f,  PHI,  0.0f},
    { 1.0f, -PHI,  0.0f}, {-1.0f, -PHI,  0.0f},
    { PHI,  0.0f,  1.0f}, {-PHI,  0.0f,  1.0f},
    { PHI,  0.0f, -1.0f}, {-PHI,  0.0f, -1.0f},
};

static const Vec3 DODECA_VERTS[] = {
    {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f},
    { 0.0f, -INV_PHI, -PHI}, { 0.0f,  INV_PHI, -PHI},
    { 0.0f, -INV_PHI,  PHI}, { 0.0f,  INV_PHI,  PHI},
    {-INV_PHI, -PHI,  0.0f}, { INV_PHI, -PHI,  0.0f},
    {-INV_PHI,  PHI,  0.0f}, { INV_PHI,  PHI,  0.0f},
    {-PHI,  0.0f, -INV_PHI}, { PHI,  0.0f, -INV_PHI},
    {-PHI,  0.0f,  INV_PHI}, { PHI,  0.0f,  INV_PHI},
};

static const Vec3 CUBOCTA_VERTS[] = {
    {-1.0f, -1.0f,  0.0f}, {-1.0f,  1.0f,  0.0f},
    { 1.0f, -1.0f,  0.0f}, { 1.0f,  1.0f,  0.0f},
    {-1.0f,  0.0f, -1.0f}, {-1.0f,  0.0f,  1.0f},
    { 1.0f,  0.0f, -1.0f}, { 1.0f,  0.0f,  1.0f},
    { 0.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  1.0f},
    { 0.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  1.0f},
};

static const Vec3 RHOMBIC_DODECA_VERTS[] = {
    {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f},
    { 2.0f,  0.0f,  0.0f}, {-2.0f,  0.0f,  0.0f},
    { 0.0f,  2.0f,  0.0f}, { 0.0f, -2.0f,  0.0f},
    { 0.0f,  0.0f,  2.0f}, { 0.0f,  0.0f, -2.0f},
};

static const Vec3 TRIANGULAR_PRISM_VERTS[] = {
    {-1.00f, -0.70f, -0.577f}, { 1.00f, -0.70f, -0.577f}, { 0.00f, -0.70f,  1.154f},
    {-1.00f,  0.70f, -0.577f}, { 1.00f,  0.70f, -0.577f}, { 0.00f,  0.70f,  1.154f},
};

static const Vec3 PENTAGONAL_PRISM_VERTS[] = {
    { 0.0000f, -0.80f,  1.0000f}, { 0.9511f, -0.80f,  0.3090f},
    { 0.5878f, -0.80f, -0.8090f}, {-0.5878f, -0.80f, -0.8090f},
    {-0.9511f, -0.80f,  0.3090f},
    { 0.0000f,  0.80f,  1.0000f}, { 0.9511f,  0.80f,  0.3090f},
    { 0.5878f,  0.80f, -0.8090f}, {-0.5878f,  0.80f, -0.8090f},
    {-0.9511f,  0.80f,  0.3090f},
};

static const Vec3 HEXAGONAL_PRISM_VERTS[] = {
    { 1.0000f, -0.75f,  0.0000f}, { 0.5000f, -0.75f,  0.8660f},
    {-0.5000f, -0.75f,  0.8660f}, {-1.0000f, -0.75f,  0.0000f},
    {-0.5000f, -0.75f, -0.8660f}, { 0.5000f, -0.75f, -0.8660f},
    { 1.0000f,  0.75f,  0.0000f}, { 0.5000f,  0.75f,  0.8660f},
    {-0.5000f,  0.75f,  0.8660f}, {-1.0000f,  0.75f,  0.0000f},
    {-0.5000f,  0.75f, -0.8660f}, { 0.5000f,  0.75f, -0.8660f},
};

static const Vec3 PENTAGONAL_BIPYRAMID_VERTS[] = {
    { 0.0000f,  1.2000f,  0.0000f},
    { 0.0000f,  0.0000f,  1.0000f}, { 0.9511f,  0.0000f,  0.3090f},
    { 0.5878f,  0.0000f, -0.8090f}, {-0.5878f,  0.0000f, -0.8090f},
    {-0.9511f,  0.0000f,  0.3090f},
    { 0.0000f, -1.2000f,  0.0000f},
};

static const Vec3 SQUARE_PYRAMID_VERTS[] = {
    { 0.0000f,  1.2500f,  0.0000f},
    {-1.0000f, -0.7500f, -1.0000f}, { 1.0000f, -0.7500f, -1.0000f},
    { 1.0000f, -0.7500f,  1.0000f}, {-1.0000f, -0.7500f,  1.0000f},
};

static const SolidSeed SOLID_SEEDS[] = {
    {"Tetrahedron", TETRA_VERTS, static_cast<uint8_t>(sizeof(TETRA_VERTS) / sizeof(TETRA_VERTS[0]))},
    {"Cube", CUBE_VERTS, static_cast<uint8_t>(sizeof(CUBE_VERTS) / sizeof(CUBE_VERTS[0]))},
    {"Octahedron", OCTA_VERTS, static_cast<uint8_t>(sizeof(OCTA_VERTS) / sizeof(OCTA_VERTS[0]))},
    {"Icosahedron", ICOSA_VERTS, static_cast<uint8_t>(sizeof(ICOSA_VERTS) / sizeof(ICOSA_VERTS[0]))},
    {"Dodecahedron", DODECA_VERTS, static_cast<uint8_t>(sizeof(DODECA_VERTS) / sizeof(DODECA_VERTS[0]))},
    {"Cuboctahedron", CUBOCTA_VERTS, static_cast<uint8_t>(sizeof(CUBOCTA_VERTS) / sizeof(CUBOCTA_VERTS[0]))},
    {"RhombicDodeca", RHOMBIC_DODECA_VERTS, static_cast<uint8_t>(sizeof(RHOMBIC_DODECA_VERTS) / sizeof(RHOMBIC_DODECA_VERTS[0]))},
    {"TriPrism", TRIANGULAR_PRISM_VERTS, static_cast<uint8_t>(sizeof(TRIANGULAR_PRISM_VERTS) / sizeof(TRIANGULAR_PRISM_VERTS[0]))},
    {"PentaPrism", PENTAGONAL_PRISM_VERTS, static_cast<uint8_t>(sizeof(PENTAGONAL_PRISM_VERTS) / sizeof(PENTAGONAL_PRISM_VERTS[0]))},
    {"HexPrism", HEXAGONAL_PRISM_VERTS, static_cast<uint8_t>(sizeof(HEXAGONAL_PRISM_VERTS) / sizeof(HEXAGONAL_PRISM_VERTS[0]))},
    {"PentaBipyr", PENTAGONAL_BIPYRAMID_VERTS, static_cast<uint8_t>(sizeof(PENTAGONAL_BIPYRAMID_VERTS) / sizeof(PENTAGONAL_BIPYRAMID_VERTS[0]))},
    {"SquarePyr", SQUARE_PYRAMID_VERTS, static_cast<uint8_t>(sizeof(SQUARE_PYRAMID_VERTS) / sizeof(SQUARE_PYRAMID_VERTS[0]))},
};

static const int NUM_SOLIDS = static_cast<int>(sizeof(SOLID_SEEDS) / sizeof(SOLID_SEEDS[0]));
static SolidMesh g_meshes[NUM_SOLIDS];

static const CRGB SOLID_COLORS[NUM_SOLIDS] = {
    CRGB(255, 120, 120),  // Tetrahedron
    CRGB(120, 170, 255),  // Cube
    CRGB(120, 255, 190),  // Octahedron
    CRGB(255, 205, 120),  // Icosahedron
    CRGB(235, 150, 255),  // Dodecahedron
    CRGB(255, 255, 140),  // Cuboctahedron
    CRGB(140, 255, 255),  // Rhombic dodecahedron
    CRGB(255, 160, 100),  // Triangular prism
    CRGB(170, 255, 150),  // Pentagonal prism
    CRGB(120, 220, 255),  // Hexagonal prism
    CRGB(255, 150, 190),  // Pentagonal bipyramid
    CRGB(210, 180, 255),  // Square pyramid
};

static const fp LIGHT_X = fp(0.3f);
static const fp LIGHT_Y = fp(-0.5f);
static const fp LIGHT_Z = fp(-0.7f);

static int g_from_solid = 0;
static int g_to_solid = 1;
static uint32_t g_phase_start_ms = 0;

static const uint32_t DWELL_MS = 2600;
static const uint32_t MORPH_MS = 2200;

static void rotate(fp ix, fp iy, fp iz,
                   fp sx, fp cx, fp sy, fp cy, fp sz, fp cz,
                   fp& ox, fp& oy, fp& oz) {
    fp y1 = iy * cx - iz * sx;
    fp z1 = iy * sx + iz * cx;
    fp x2 = ix * cy + z1 * sy;
    fp z2 = z1 * cy - ix * sy;
    ox = x2 * cz - y1 * sz;
    oy = x2 * sz + y1 * cz;
    oz = z2;
}

static void project(fp x, fp y, fp z, fp& sx, fp& sy) {
    const fp cam_dist = fp(2.8f);
    fp scale = cam_dist / (cam_dist + z);
    const fp half_w = fp(static_cast<float>(W) * 0.5f);
    const fp half_h = fp(static_cast<float>(H) * 0.5f);
    const fp model_scale = fp(static_cast<float>(W) * 0.60f);
    sx = half_w + x * scale * model_scale;
    sy = half_h - y * scale * model_scale;
}

static bool nearly_same_plane(const Plane& a, const Plane& b) {
    const float cos_eps = 0.999f;
    const float d_eps = 0.03f;
    return (v_dot(a.n, b.n) > cos_eps) && (fabsf(a.d - b.d) < d_eps);
}

static void sort_face_cycle(uint8_t* ids, int count, const Vec3* verts, const Vec3& n) {
    Vec3 center{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < count; ++i) {
        center = v_add(center, verts[ids[i]]);
    }
    center = v_mul(center, 1.0f / static_cast<float>(count));

    Vec3 axis = fabsf(n.x) < 0.7f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
    Vec3 u = v_norm(v_cross(axis, n));
    Vec3 v = v_cross(n, u);

    float angles[MAX_SOLID_VERTS];
    for (int i = 0; i < count; ++i) {
        Vec3 r = v_sub(verts[ids[i]], center);
        angles[i] = atan2f(v_dot(r, v), v_dot(r, u));
    }

    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (angles[j] < angles[i]) {
                float ta = angles[i];
                angles[i] = angles[j];
                angles[j] = ta;
                uint8_t tv = ids[i];
                ids[i] = ids[j];
                ids[j] = tv;
            }
        }
    }
}

static bool build_convex_hull_mesh(const SolidSeed& seed, SolidMesh& out) {
    if (seed.vert_count < 4 || seed.vert_count > MAX_SOLID_VERTS) return false;

    out.name = seed.name;
    out.vert_count = seed.vert_count;
    out.tri_count = 0;
    for (int i = 0; i < seed.vert_count; ++i) {
        out.verts[i] = seed.verts[i];
    }

    Plane planes[MAX_SOLID_PLANES];
    int plane_count = 0;

    const float side_eps = 0.02f;

    for (int i = 0; i < seed.vert_count - 2; ++i) {
        for (int j = i + 1; j < seed.vert_count - 1; ++j) {
            for (int k = j + 1; k < seed.vert_count; ++k) {
                Vec3 e1 = v_sub(out.verts[j], out.verts[i]);
                Vec3 e2 = v_sub(out.verts[k], out.verts[i]);
                Vec3 n = v_cross(e1, e2);
                float len = v_len(n);
                if (len < 1e-5f) continue;
                n = v_mul(n, 1.0f / len);
                float d = v_dot(n, out.verts[i]);

                bool has_pos = false;
                bool has_neg = false;
                for (int m = 0; m < seed.vert_count; ++m) {
                    float dist = v_dot(n, out.verts[m]) - d;
                    if (dist > side_eps) has_pos = true;
                    if (dist < -side_eps) has_neg = true;
                    if (has_pos && has_neg) break;
                }
                if (has_pos && has_neg) continue;

                if (has_pos) {
                    n = v_mul(n, -1.0f);
                    d = -d;
                }

                Plane p{n, d};
                bool duplicate = false;
                for (int pi = 0; pi < plane_count; ++pi) {
                    if (nearly_same_plane(p, planes[pi])) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate && plane_count < MAX_SOLID_PLANES) {
                    planes[plane_count++] = p;
                }
            }
        }
    }

    const float on_plane_eps = 0.03f;
    for (int pi = 0; pi < plane_count; ++pi) {
        uint8_t face_ids[MAX_SOLID_VERTS];
        int face_count = 0;

        for (int vi = 0; vi < out.vert_count; ++vi) {
            float dist = fabsf(v_dot(planes[pi].n, out.verts[vi]) - planes[pi].d);
            if (dist < on_plane_eps) {
                face_ids[face_count++] = static_cast<uint8_t>(vi);
            }
        }

        if (face_count < 3) continue;
        sort_face_cycle(face_ids, face_count, out.verts, planes[pi].n);

        for (int i = 1; i < face_count - 1; ++i) {
            if (out.tri_count >= MAX_SOLID_TRIS) break;
            uint8_t a = face_ids[0];
            uint8_t b = face_ids[i];
            uint8_t c = face_ids[i + 1];

            Vec3 tn = v_cross(v_sub(out.verts[b], out.verts[a]), v_sub(out.verts[c], out.verts[a]));
            if (v_dot(tn, planes[pi].n) < 0.0f) {
                uint8_t t = b;
                b = c;
                c = t;
            }
            out.tris[out.tri_count++] = Tri{a, b, c};
        }
    }

    if (out.tri_count == 0) return false;

    Vec3 center{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < out.vert_count; ++i) center = v_add(center, out.verts[i]);
    center = v_mul(center, 1.0f / static_cast<float>(out.vert_count));
    for (int i = 0; i < out.vert_count; ++i) out.verts[i] = v_sub(out.verts[i], center);

    float max_r = 0.0f;
    for (int i = 0; i < out.vert_count; ++i) {
        float r = v_len(out.verts[i]);
        if (r > max_r) max_r = r;
    }
    if (max_r < 1e-6f) max_r = 1.0f;
    float scale = 0.95f / max_r;
    for (int i = 0; i < out.vert_count; ++i) out.verts[i] = v_mul(out.verts[i], scale);

    return true;
}

static Vec3 tri_centroid(const SolidMesh& mesh, uint8_t tri_index) {
    const Tri& t = mesh.tris[tri_index];
    Vec3 c = v_add(mesh.verts[t.a], mesh.verts[t.b]);
    c = v_add(c, mesh.verts[t.c]);
    return v_mul(c, 1.0f / 3.0f);
}

// Keep winding but allow cyclic phase shift to minimize per-vertex travel.
static void align_triangle_cyclic(const Vec3& a0, const Vec3& a1, const Vec3& a2,
                                  const Vec3& b0, const Vec3& b1, const Vec3& b2,
                                  Vec3& out0, Vec3& out1, Vec3& out2) {
    const Vec3 b[3] = {b0, b1, b2};
    int best_shift = 0;
    float best_cost = 1e30f;

    for (int shift = 0; shift < 3; ++shift) {
        Vec3 d0 = v_sub(a0, b[(0 + shift) % 3]);
        Vec3 d1 = v_sub(a1, b[(1 + shift) % 3]);
        Vec3 d2 = v_sub(a2, b[(2 + shift) % 3]);
        float cost = v_dot(d0, d0) + v_dot(d1, d1) + v_dot(d2, d2);
        if (cost < best_cost) {
            best_cost = cost;
            best_shift = shift;
        }
    }

    out0 = b[(0 + best_shift) % 3];
    out1 = b[(1 + best_shift) % 3];
    out2 = b[(2 + best_shift) % 3];
}

// Greedy nearest-neighbor matching between triangle centroids.
// Produces up to max(na, nb) pairs with every larger-side triangle represented.
static int build_triangle_pairs(const SolidMesh& a,
                                const SolidMesh& b,
                                uint8_t* out_a,
                                uint8_t* out_b,
                                int max_pairs) {
    int na = a.tri_count;
    int nb = b.tri_count;
    if (na <= 0 || nb <= 0 || max_pairs <= 0) return 0;

    Vec3 ca[MAX_SOLID_TRIS];
    Vec3 cb[MAX_SOLID_TRIS];
    for (int i = 0; i < na; ++i) ca[i] = tri_centroid(a, static_cast<uint8_t>(i));
    for (int i = 0; i < nb; ++i) cb[i] = tri_centroid(b, static_cast<uint8_t>(i));

    int pairs = 0;
    if (na >= nb) {
        bool used_b[MAX_SOLID_TRIS] = {false};
        for (int i = 0; i < nb && pairs < max_pairs; ++i) {
            int best_a = 0;
            int best_b = -1;
            float best_d2 = 1e30f;
            for (int ai = 0; ai < na; ++ai) {
                for (int bi = 0; bi < nb; ++bi) {
                    if (used_b[bi]) continue;
                    Vec3 d = v_sub(ca[ai], cb[bi]);
                    float d2 = v_dot(d, d);
                    if (d2 < best_d2) {
                        best_d2 = d2;
                        best_a = ai;
                        best_b = bi;
                    }
                }
            }
            if (best_b >= 0) {
                used_b[best_b] = true;
                out_a[pairs] = static_cast<uint8_t>(best_a);
                out_b[pairs] = static_cast<uint8_t>(best_b);
                ++pairs;
            }
        }
        for (int ai = 0; ai < na && pairs < max_pairs; ++ai) {
            int best_b = 0;
            float best_d2 = 1e30f;
            for (int bi = 0; bi < nb; ++bi) {
                Vec3 d = v_sub(ca[ai], cb[bi]);
                float d2 = v_dot(d, d);
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best_b = bi;
                }
            }
            out_a[pairs] = static_cast<uint8_t>(ai);
            out_b[pairs] = static_cast<uint8_t>(best_b);
            ++pairs;
        }
    } else {
        bool used_a[MAX_SOLID_TRIS] = {false};
        for (int i = 0; i < na && pairs < max_pairs; ++i) {
            int best_a = -1;
            int best_b = 0;
            float best_d2 = 1e30f;
            for (int ai = 0; ai < na; ++ai) {
                if (used_a[ai]) continue;
                for (int bi = 0; bi < nb; ++bi) {
                    Vec3 d = v_sub(ca[ai], cb[bi]);
                    float d2 = v_dot(d, d);
                    if (d2 < best_d2) {
                        best_d2 = d2;
                        best_a = ai;
                        best_b = bi;
                    }
                }
            }
            if (best_a >= 0) {
                used_a[best_a] = true;
                out_a[pairs] = static_cast<uint8_t>(best_a);
                out_b[pairs] = static_cast<uint8_t>(best_b);
                ++pairs;
            }
        }
        for (int bi = 0; bi < nb && pairs < max_pairs; ++bi) {
            int best_a = 0;
            float best_d2 = 1e30f;
            for (int ai = 0; ai < na; ++ai) {
                Vec3 d = v_sub(ca[ai], cb[bi]);
                float d2 = v_dot(d, d);
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best_a = ai;
                }
            }
            out_a[pairs] = static_cast<uint8_t>(best_a);
            out_b[pairs] = static_cast<uint8_t>(bi);
            ++pairs;
        }
    }
    return pairs;
}

static inline bool near_fp(fp a, fp b, fp eps) {
    fp d = a - b;
    if (d < fp(0.0f)) d = -d;
    return d <= eps;
}

static inline void tri_edge(const DrawTri& t, int edge,
                            fp& x0, fp& y0, fp& x1, fp& y1) {
    if (edge == 0) {
        x0 = t.sx0; y0 = t.sy0;
        x1 = t.sx1; y1 = t.sy1;
    } else if (edge == 1) {
        x0 = t.sx1; y0 = t.sy1;
        x1 = t.sx2; y1 = t.sy2;
    } else {
        x0 = t.sx2; y0 = t.sy2;
        x1 = t.sx0; y1 = t.sy0;
    }
}

// Disable AA on shared/internal edges. Keep AA on silhouette edges.
static void resolve_edge_aa_masks(DrawTri* tris, int count) {
    const fp eps = fp(0.35f);
    const fp coplanar_dot_min = fp(0.985f);  // ~10 deg max normal deviation
    for (int i = 0; i < count; ++i) {
        tris[i].edge_mask = 0x7;
    }

    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            for (int ei = 0; ei < 3; ++ei) {
                fp aix0, aiy0, aix1, aiy1;
                tri_edge(tris[i], ei, aix0, aiy0, aix1, aiy1);

                for (int ej = 0; ej < 3; ++ej) {
                    fp bjx0, bjy0, bjx1, bjy1;
                    tri_edge(tris[j], ej, bjx0, bjy0, bjx1, bjy1);

                    bool same_dir = near_fp(aix0, bjx0, eps) && near_fp(aiy0, bjy0, eps) &&
                                    near_fp(aix1, bjx1, eps) && near_fp(aiy1, bjy1, eps);
                    bool opposite_dir = near_fp(aix0, bjx1, eps) && near_fp(aiy0, bjy1, eps) &&
                                        near_fp(aix1, bjx0, eps) && near_fp(aiy1, bjy0, eps);

                    if (same_dir || opposite_dir) {
                        // Shared edge only becomes "internal" when triangles are coplanar.
                        // If normals differ, this is a real plane boundary (crease) and keeps AA.
                        fp dot = tris[i].nx * tris[j].nx + tris[i].ny * tris[j].ny + tris[i].nz * tris[j].nz;
                        if (dot >= coplanar_dot_min) {
                            tris[i].edge_mask &= static_cast<uint8_t>(~(1 << ei));
                            tris[j].edge_mask &= static_cast<uint8_t>(~(1 << ej));
                        }
                    }
                }
            }
        }
    }
}

void setup() {
    FastLED.addLeds<NEOPIXEL, PIN_DATA>(leds, NUM_LEDS)
        .setScreenMap(xymap);

    for (int i = 0; i < NUM_SOLIDS; ++i) {
        if (!build_convex_hull_mesh(SOLID_SEEDS[i], g_meshes[i])) {
            g_meshes[i].name = "FallbackCube";
            g_meshes[i].vert_count = static_cast<uint8_t>(sizeof(CUBE_VERTS) / sizeof(CUBE_VERTS[0]));
            for (int v = 0; v < g_meshes[i].vert_count; ++v) g_meshes[i].verts[v] = CUBE_VERTS[v];
            g_meshes[i].tri_count = 12;
            static const Tri cube_tris[12] = {
                {4, 5, 6}, {4, 6, 7}, {1, 0, 3}, {1, 3, 2},
                {5, 1, 2}, {5, 2, 6}, {0, 4, 7}, {0, 7, 3},
                {7, 6, 2}, {7, 2, 3}, {0, 1, 5}, {0, 5, 4},
            };
            for (int t = 0; t < 12; ++t) g_meshes[i].tris[t] = cube_tris[t];
        }
    }
}

void loop() {
    for (int i = 0; i < NUM_LEDS; ++i) leds[i] = CRGB::Black;
    fl::CanvasRGB canvas(fl::span<CRGB>(leds, NUM_LEDS), W, H);

    uint32_t ms = millis();
    uint32_t elapsed = ms - g_phase_start_ms;
    if (elapsed >= DWELL_MS + MORPH_MS) {
        g_from_solid = g_to_solid;
        g_to_solid = (g_to_solid + 1) % NUM_SOLIDS;
        g_phase_start_ms = ms;
        elapsed = 0;
    }

    float morph_t = elapsed < DWELL_MS
        ? 0.0f
        : smoothstep(static_cast<float>(elapsed - DWELL_MS) / static_cast<float>(MORPH_MS));

    const SolidMesh& from_mesh = g_meshes[g_from_solid];
    const SolidMesh& to_mesh = g_meshes[g_to_solid];

    CRGB base_color = blend(SOLID_COLORS[g_from_solid],
                            SOLID_COLORS[g_to_solid],
                            static_cast<uint8_t>(morph_t * 255.0f));

    fp angle_x = fp(static_cast<float>(ms) * 0.00021f);
    fp angle_y = fp(static_cast<float>(ms) * 0.00031f);
    fp angle_z = fp(static_cast<float>(ms) * 0.00009f);

    fp sx, cx, sy, cy, sz, cz;
    fp::sincos(angle_x, sx, cx);
    fp::sincos(angle_y, sy, cy);
    fp::sincos(angle_z, sz, cz);

    DrawTri draw_tris[MAX_RENDER_TRIS];
    int draw_count = 0;

    uint8_t pair_from[MAX_RENDER_TRIS];
    uint8_t pair_to[MAX_RENDER_TRIS];
    int pair_count = build_triangle_pairs(from_mesh, to_mesh, pair_from, pair_to, MAX_RENDER_TRIS);

    for (int i = 0; i < pair_count; ++i) {
        const Tri& ta = from_mesh.tris[pair_from[i]];
        const Tri& tb = to_mesh.tris[pair_to[i]];

        Vec3 a0 = from_mesh.verts[ta.a];
        Vec3 a1 = from_mesh.verts[ta.b];
        Vec3 a2 = from_mesh.verts[ta.c];
        Vec3 b0 = to_mesh.verts[tb.a];
        Vec3 b1 = to_mesh.verts[tb.b];
        Vec3 b2 = to_mesh.verts[tb.c];

        Vec3 ab0, ab1, ab2;
        align_triangle_cyclic(a0, a1, a2, b0, b1, b2, ab0, ab1, ab2);

        Vec3 m0 = v_lerp(a0, ab0, morph_t);
        Vec3 m1 = v_lerp(a1, ab1, morph_t);
        Vec3 m2 = v_lerp(a2, ab2, morph_t);

        fp r0x, r0y, r0z;
        fp r1x, r1y, r1z;
        fp r2x, r2y, r2z;
        rotate(fp(m0.x), fp(m0.y), fp(m0.z), sx, cx, sy, cy, sz, cz, r0x, r0y, r0z);
        rotate(fp(m1.x), fp(m1.y), fp(m1.z), sx, cx, sy, cy, sz, cz, r1x, r1y, r1z);
        rotate(fp(m2.x), fp(m2.y), fp(m2.z), sx, cx, sy, cy, sz, cz, r2x, r2y, r2z);

        fp s0x, s0y, s1x, s1y, s2x, s2y;
        project(r0x, r0y, r0z, s0x, s0y);
        project(r1x, r1y, r1z, s1x, s1y);
        project(r2x, r2y, r2z, s2x, s2y);

        fp cross = (s1x - s0x) * (s2y - s0y) - (s1y - s0y) * (s2x - s0x);
        if (cross <= fp(0.0f)) continue;

        fp ex = r1x - r0x;
        fp ey = r1y - r0y;
        fp ez = r1z - r0z;
        fp fx = r2x - r0x;
        fp fy = r2y - r0y;
        fp fz = r2z - r0z;
        fp nx = ey * fz - ez * fy;
        fp ny = ez * fx - ex * fz;
        fp nz = ex * fy - ey * fx;
        fp len_sq = nx * nx + ny * ny + nz * nz;
        if (len_sq > fp(0.0001f)) {
            fp inv_len = fp::rsqrt(len_sq);
            nx = nx * inv_len;
            ny = ny * inv_len;
            nz = nz * inv_len;
        }

        fp dot = nx * LIGHT_X + ny * LIGHT_Y + nz * LIGHT_Z;
        if (dot < fp(0.15f)) dot = fp(0.15f);
        if (dot > fp(1.0f)) dot = fp(1.0f);

        uint8_t brightness = static_cast<uint8_t>((dot * fp(255.0f)).to_int());
        if (brightness < 40) brightness = 40;

        CRGB color = base_color;
        color.nscale8(brightness);

        if (draw_count < MAX_RENDER_TRIS) {
            draw_tris[draw_count++] = DrawTri{
                s0x, s0y,
                s1x, s1y,
                s2x, s2y,
                (r0z + r1z + r2z) / fp(3.0f),
                nx, ny, nz,
                color,
                0x7
            };
        }
    }

    for (int i = 0; i < draw_count - 1; ++i) {
        for (int j = i + 1; j < draw_count; ++j) {
            if (draw_tris[j].avg_z > draw_tris[i].avg_z) {
                DrawTri t = draw_tris[i];
                draw_tris[i] = draw_tris[j];
                draw_tris[j] = t;
            }
        }
    }

    resolve_edge_aa_masks(draw_tris, draw_count);

    for (int i = 0; i < draw_count; ++i) {
        canvas.drawTriangle(draw_tris[i].color,
                            draw_tris[i].sx0, draw_tris[i].sy0,
                            draw_tris[i].sx1, draw_tris[i].sy1,
                            draw_tris[i].sx2, draw_tris[i].sy2,
                            fl::DrawMode::DRAW_MODE_OVERWRITE,
                            draw_tris[i].edge_mask);
    }

    FastLED.show();
}
