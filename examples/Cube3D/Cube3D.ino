/// @file    Cube3D.ino
/// @brief   Morphing 3D solids with flat-shaded faces using drawTriangle
/// @example Cube3D.ino

#include <Arduino.h>
#include <FastLED.h>
#include <fl/gfx/gfx.h>
#include <fl/math/fixed_point/s16x16.h>
#include <fl/math/xymap.h>

#ifndef PIN_DATA
#define PIN_DATA 3
#endif

static const int W = 100;
static const int H = 100;
static const int NUM_LEDS = W * H;

CRGB leds[NUM_LEDS];
fl::XYMap xymap(W, H, false);  // rectangular grid (not serpentine)

using fp = fl::s16x16;

// All shapes share 8 vertices with cube connectivity.
// Vertex layout: 0=LBBk 1=RBBk 2=RTBk 3=LTBk 4=LBFr 5=RBFr 6=RTFr 7=LTFr
//                (L/R = left/right, B/T = bottom/top, Bk/Fr = back/front)
static const int NUM_VERTS = 8;

// Each face is two triangles (quad split). 6 faces × 2 triangles × 3 verts.
// Wound counter-clockwise when viewed from outside.
static const uint8_t FACE_TRIS[][3] = {
    // Front face  (+Z)
    {4, 5, 6}, {4, 6, 7},
    // Back face   (-Z)
    {1, 0, 3}, {1, 3, 2},
    // Right face  (+X)
    {5, 1, 2}, {5, 2, 6},
    // Left face   (-X)
    {0, 4, 7}, {0, 7, 3},
    // Top face    (+Y)
    {7, 6, 2}, {7, 2, 3},
    // Bottom face (-Y)
    {0, 1, 5}, {0, 5, 4},
};

// Face base colors (one per face)
static const CRGB FACE_COLORS[6] = {
    CRGB(255,  50,  50),  // front  — red
    CRGB( 50, 255,  50),  // back   — green
    CRGB( 50,  50, 255),  // right  — blue
    CRGB(255, 255,  50),  // left   — yellow
    CRGB(255,  50, 255),  // top    — magenta
    CRGB( 50, 255, 255),  // bottom — cyan
};

// ── Morph target shapes ───────────────────────────────────────────────────
// Stored as float to save flash; converted to fp at interpolation time.
static const int NUM_SHAPES = 5;

static const float SHAPE_VERTS[NUM_SHAPES][NUM_VERTS][3] = {
    // 0: Cube — uniform box
    {
        {-0.50f, -0.50f, -0.50f},
        { 0.50f, -0.50f, -0.50f},
        { 0.50f,  0.50f, -0.50f},
        {-0.50f,  0.50f, -0.50f},
        {-0.50f, -0.50f,  0.50f},
        { 0.50f, -0.50f,  0.50f},
        { 0.50f,  0.50f,  0.50f},
        {-0.50f,  0.50f,  0.50f},
    },
    // 1: Square antiprism — bottom unchanged, top rotated 45° around Y
    //    cos(45°) = sin(45°) ≈ 0.7071
    //    v2(0.5, 0.5,-0.5) → (0,    0.5,-0.7071)
    //    v3(-0.5,0.5,-0.5) → (-0.7071,0.5, 0   )
    //    v6(0.5, 0.5, 0.5) → (0.7071,0.5, 0   )
    //    v7(-0.5,0.5, 0.5) → (0,    0.5, 0.7071)
    {
        {-0.50f,   -0.50f, -0.50f},
        { 0.50f,   -0.50f, -0.50f},
        { 0.00f,    0.50f, -0.7071f},
        {-0.7071f,  0.50f,  0.00f},
        {-0.50f,   -0.50f,  0.50f},
        { 0.50f,   -0.50f,  0.50f},
        { 0.7071f,  0.50f,  0.00f},
        { 0.00f,    0.50f,  0.7071f},
    },
    // 2: Obelisk — taller (±0.65 Y), top face shrunk to 40% of bottom
    {
        {-0.50f, -0.65f, -0.50f},
        { 0.50f, -0.65f, -0.50f},
        { 0.20f,  0.65f, -0.20f},
        {-0.20f,  0.65f, -0.20f},
        {-0.50f, -0.65f,  0.50f},
        { 0.50f, -0.65f,  0.50f},
        { 0.20f,  0.65f,  0.20f},
        {-0.20f,  0.65f,  0.20f},
    },
    // 3: Disc / UFO — flat (±0.15 Y) and wide (±0.70 XZ)
    {
        {-0.70f, -0.15f, -0.70f},
        { 0.70f, -0.15f, -0.70f},
        { 0.70f,  0.15f, -0.70f},
        {-0.70f,  0.15f, -0.70f},
        {-0.70f, -0.15f,  0.70f},
        { 0.70f, -0.15f,  0.70f},
        { 0.70f,  0.15f,  0.70f},
        {-0.70f,  0.15f,  0.70f},
    },
    // 4: Rhombohedron — cube sheared by x' = x + 0.4y, z' = z + 0.4y
    {
        {-0.70f, -0.50f, -0.70f},
        { 0.30f, -0.50f, -0.70f},
        { 0.70f,  0.50f, -0.30f},
        {-0.30f,  0.50f, -0.30f},
        {-0.70f, -0.50f,  0.30f},
        { 0.30f, -0.50f,  0.30f},
        { 0.70f,  0.50f,  0.70f},
        {-0.30f,  0.50f,  0.70f},
    },
};

// Time each shape is held still before morphing to the next
static const uint32_t DWELL_MS = 3000;
// Duration of the vertex interpolation transition
static const uint32_t MORPH_MS = 2000;

// Current interpolated vertex positions (in model space)
static fp morph_verts[NUM_VERTS][3];

// Morphing state
static int    shape_from     = 0;
static int    shape_to       = 1;
static uint32_t shape_start_ms = 0;

// Projected screen coordinates (filled each frame)
static fp screen_x[NUM_VERTS];
static fp screen_y[NUM_VERTS];
static fp depth_z[NUM_VERTS];

// Light direction (fixed, pointing into screen and slightly right+down)
static const fp LIGHT_X = fp(0.3f);
static const fp LIGHT_Y = fp(-0.5f);
static const fp LIGHT_Z = fp(-0.7f);

// Rotate a 3D point around X then Y then Z using s16x16 sincos.
static void rotate(fp ix, fp iy, fp iz,
                   fp sx, fp cx, fp sy, fp cy, fp sz, fp cz,
                   fp &ox, fp &oy, fp &oz) {
    // Rotate around X
    fp y1 = iy * cx - iz * sx;
    fp z1 = iy * sx + iz * cx;
    // Rotate around Y
    fp x2 = ix * cy + z1 * sy;
    fp z2 = z1 * cy - ix * sy;
    // Rotate around Z
    ox = x2 * cz - y1 * sz;
    oy = x2 * sz + y1 * cz;
    oz = z2;
}

// Simple perspective projection: scale & center onto W×H canvas.
static void project(fp x, fp y, fp z,
                    fp &sx, fp &sy) {
    // Camera distance: objects at z=0 have scale ~1, closer objects are larger
    const fp cam_dist = fp(2.5f);
    fp scale = cam_dist / (cam_dist + z);
    const fp half_w = fp(static_cast<float>(W) * 0.5f);
    const fp half_h = fp(static_cast<float>(H) * 0.5f);
    const fp cube_scale = fp(static_cast<float>(W) * 0.55f);  // cube fills ~55% of width
    sx = half_w + x * scale * cube_scale;
    sy = half_h - y * scale * cube_scale;  // invert Y: screen Y grows down
}

// Smoothstep easing: t in [0,1] → smooth 0..1 with zero first derivative at endpoints
static float smoothstep(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

void setup() {
    FastLED.addLeds<NEOPIXEL, PIN_DATA>(leds, NUM_LEDS)
        .setScreenMap(xymap);
}

void loop() {
    // Clear canvas
    for (int i = 0; i < NUM_LEDS; ++i) leds[i] = CRGB::Black;
    fl::CanvasRGB canvas(fl::span<CRGB>(leds, NUM_LEDS), W, H);

    uint32_t ms = millis();

    // ── Advance morph state ──────────────────────────────────────────────
    uint32_t elapsed = ms - shape_start_ms;
    if (elapsed >= DWELL_MS + MORPH_MS) {
        shape_from     = shape_to;
        shape_to       = (shape_to + 1) % NUM_SHAPES;
        shape_start_ms = ms;
        elapsed        = 0;
    }
    float morph_t = elapsed < DWELL_MS
                  ? 0.0f
                  : smoothstep(float(elapsed - DWELL_MS) / float(MORPH_MS));

    // ── Interpolate vertices ─────────────────────────────────────────────
    for (int i = 0; i < NUM_VERTS; ++i) {
        for (int j = 0; j < 3; ++j) {
            float a = SHAPE_VERTS[shape_from][i][j];
            float b = SHAPE_VERTS[shape_to][i][j];
            morph_verts[i][j] = fp(a + (b - a) * morph_t);
        }
    }

    // ── Rotation angles ──────────────────────────────────────────────────
    fp angle_x = fp(static_cast<float>(ms) * 0.000175f);
    fp angle_y = fp(static_cast<float>(ms) * 0.000275f);
    fp angle_z = fp(static_cast<float>(ms) * 0.000075f);

    fp sx, cx, sy, cy, sz, cz;
    fp::sincos(angle_x, sx, cx);
    fp::sincos(angle_y, sy, cy);
    fp::sincos(angle_z, sz, cz);

    // ── Transform all vertices ───────────────────────────────────────────
    for (int i = 0; i < NUM_VERTS; ++i) {
        fp rx, ry, rz;
        rotate(morph_verts[i][0], morph_verts[i][1], morph_verts[i][2],
               sx, cx, sy, cy, sz, cz,
               rx, ry, rz);
        project(rx, ry, rz, screen_x[i], screen_y[i]);
        depth_z[i] = rz;
    }

    // ── Collect visible faces (painter's algorithm, back-to-front) ───────
    struct FaceInfo {
        int face;
        fp avg_z;
    };
    FaceInfo visible[6];
    int num_visible = 0;

    for (int face = 0; face < 6; ++face) {
        // Screen-space back-face culling via winding order.
        // Camera at -Z; Y-flip reverses winding, so front-faces have positive cross.
        uint8_t a0 = FACE_TRIS[face * 2][0];
        uint8_t b0 = FACE_TRIS[face * 2][1];
        uint8_t c0 = FACE_TRIS[face * 2][2];
        fp cross = (screen_x[b0] - screen_x[a0]) * (screen_y[c0] - screen_y[a0])
                 - (screen_y[b0] - screen_y[a0]) * (screen_x[c0] - screen_x[a0]);
        if (cross <= fp(0.0f)) continue;

        uint8_t d0 = FACE_TRIS[face * 2 + 1][1];  // 4th unique vertex
        fp avg = (depth_z[a0] + depth_z[b0] + depth_z[c0] + depth_z[d0]) / fp(4.0f);
        visible[num_visible++] = {face, avg};
    }

    // Sort: furthest (highest avg_z) first
    for (int i = 0; i < num_visible - 1; ++i) {
        for (int j = i + 1; j < num_visible; ++j) {
            if (visible[j].avg_z > visible[i].avg_z) {
                FaceInfo tmp = visible[i];
                visible[i] = visible[j];
                visible[j] = tmp;
            }
        }
    }

    // ── Draw visible faces ───────────────────────────────────────────────
    static const uint8_t EDGE_AA[2] = { 0x3, 0x6 };  // disable shared diagonal AA

    for (int vi = 0; vi < num_visible; ++vi) {
        int face = visible[vi].face;

        // Compute face normal from current morph_verts via cross product
        uint8_t ai = FACE_TRIS[face * 2][0];
        uint8_t bi = FACE_TRIS[face * 2][1];
        uint8_t ci = FACE_TRIS[face * 2][2];
        fp ex = morph_verts[bi][0] - morph_verts[ai][0];
        fp ey = morph_verts[bi][1] - morph_verts[ai][1];
        fp ez = morph_verts[bi][2] - morph_verts[ai][2];
        fp fx = morph_verts[ci][0] - morph_verts[ai][0];
        fp fy = morph_verts[ci][1] - morph_verts[ai][1];
        fp fz = morph_verts[ci][2] - morph_verts[ai][2];
        fp mn_x = ey * fz - ez * fy;
        fp mn_y = ez * fx - ex * fz;
        fp mn_z = ex * fy - ey * fx;
        fp len_sq = mn_x * mn_x + mn_y * mn_y + mn_z * mn_z;
        if (len_sq > fp(0.0001f)) {
            fp inv_len = fp::rsqrt(len_sq);
            mn_x = mn_x * inv_len;
            mn_y = mn_y * inv_len;
            mn_z = mn_z * inv_len;
        }

        // Rotate normal to world space and compute flat shading
        fp nx, ny, nz;
        rotate(mn_x, mn_y, mn_z, sx, cx, sy, cy, sz, cz, nx, ny, nz);

        fp dot = nx * LIGHT_X + ny * LIGHT_Y + nz * LIGHT_Z;
        if (dot < fp(0.15f)) dot = fp(0.15f);
        if (dot > fp(1.0f))  dot = fp(1.0f);
        uint8_t brightness = static_cast<uint8_t>((dot * fp(255.0f)).to_int());
        if (brightness < 38) brightness = 38;  // ambient floor

        CRGB color = FACE_COLORS[face];
        color.nscale8(brightness);

        for (int ti = 0; ti < 2; ++ti) {
            int tri_idx = face * 2 + ti;
            uint8_t a = FACE_TRIS[tri_idx][0];
            uint8_t b = FACE_TRIS[tri_idx][1];
            uint8_t c = FACE_TRIS[tri_idx][2];
            canvas.drawTriangle(color,
                                screen_x[a], screen_y[a],
                                screen_x[b], screen_y[b],
                                screen_x[c], screen_y[c],
                                fl::DrawMode::DRAW_MODE_OVERWRITE,
                                EDGE_AA[ti]);
        }
    }

    FastLED.show();
}
