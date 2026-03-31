/// @file    Cube3D.ino
/// @brief   Rotating 3D cube with flat-shaded faces using drawTriangle
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

// Unit cube vertices in model space (±0.5)
static const int NUM_VERTS = 8;
static fp CUBE_VERTS[NUM_VERTS][3] = {
    { fp(-0.5f), fp(-0.5f), fp(-0.5f) },  // 0: left  bottom back
    { fp( 0.5f), fp(-0.5f), fp(-0.5f) },  // 1: right bottom back
    { fp( 0.5f), fp( 0.5f), fp(-0.5f) },  // 2: right top    back
    { fp(-0.5f), fp( 0.5f), fp(-0.5f) },  // 3: left  top    back
    { fp(-0.5f), fp(-0.5f), fp( 0.5f) },  // 4: left  bottom front
    { fp( 0.5f), fp(-0.5f), fp( 0.5f) },  // 5: right bottom front
    { fp( 0.5f), fp( 0.5f), fp( 0.5f) },  // 6: right top    front
    { fp(-0.5f), fp( 0.5f), fp( 0.5f) },  // 7: left  top    front
};

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

// Face normals (one per pair of triangles) — used for lighting
static const fp FACE_NORMALS[6][3] = {
    { fp( 0.0f), fp( 0.0f), fp( 1.0f) },  // front
    { fp( 0.0f), fp( 0.0f), fp(-1.0f) },  // back
    { fp( 1.0f), fp( 0.0f), fp( 0.0f) },  // right
    { fp(-1.0f), fp( 0.0f), fp( 0.0f) },  // left
    { fp( 0.0f), fp( 1.0f), fp( 0.0f) },  // top
    { fp( 0.0f), fp(-1.0f), fp( 0.0f) },  // bottom
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

void setup() {
    FastLED.addLeds<NEOPIXEL, PIN_DATA>(leds, NUM_LEDS)
        .setScreenMap(xymap);
}

void loop() {
    // Clear canvas
    for (int i = 0; i < NUM_LEDS; ++i) leds[i] = CRGB::Black;
    fl::CanvasRGB canvas(fl::span<CRGB>(leds, NUM_LEDS), W, H);

    // Time-based rotation angles (radians)
    uint32_t ms = millis();
    fp angle_x = fp(static_cast<float>(ms) * 0.000175f);
    fp angle_y = fp(static_cast<float>(ms) * 0.000275f);
    fp angle_z = fp(static_cast<float>(ms) * 0.000075f);

    // Precompute sin/cos for each rotation axis
    fp sx, cx, sy, cy, sz, cz;
    fp::sincos(angle_x, sx, cx);
    fp::sincos(angle_y, sy, cy);
    fp::sincos(angle_z, sz, cz);

    // Transform all vertices
    for (int i = 0; i < NUM_VERTS; ++i) {
        fp rx, ry, rz;
        rotate(CUBE_VERTS[i][0], CUBE_VERTS[i][1], CUBE_VERTS[i][2],
               sx, cx, sy, cy, sz, cz,
               rx, ry, rz);
        project(rx, ry, rz, screen_x[i], screen_y[i]);
        depth_z[i] = rz;
    }

    // Collect visible faces with depth for painter's algorithm (back-to-front)
    struct FaceInfo {
        int face;
        fp avg_z;  // average depth of face vertices (higher = further)
    };
    FaceInfo visible[6];
    int num_visible = 0;

    for (int face = 0; face < 6; ++face) {
        // Screen-space back-face culling via winding order
        // Camera is at -Z looking toward +Z; Y-flip reverses winding,
        // so front-facing triangles have positive cross product.
        uint8_t a0 = FACE_TRIS[face * 2][0];
        uint8_t b0 = FACE_TRIS[face * 2][1];
        uint8_t c0 = FACE_TRIS[face * 2][2];
        fp cross = (screen_x[b0] - screen_x[a0]) * (screen_y[c0] - screen_y[a0])
                 - (screen_y[b0] - screen_y[a0]) * (screen_x[c0] - screen_x[a0]);
        if (cross <= fp(0.0f)) continue;  // negative cross = back-facing, skip

        // Average depth of the face's 4 unique vertices for sorting
        uint8_t d0 = FACE_TRIS[face * 2 + 1][1];  // 4th vertex from second tri
        fp avg = (depth_z[a0] + depth_z[b0] + depth_z[c0] + depth_z[d0]) / fp(4.0f);
        visible[num_visible++] = {face, avg};
    }

    // Sort back-to-front (highest avg_z = furthest from camera, draw first)
    for (int i = 0; i < num_visible - 1; ++i) {
        for (int j = i + 1; j < num_visible; ++j) {
            if (visible[j].avg_z > visible[i].avg_z) {
                FaceInfo tmp = visible[i];
                visible[i] = visible[j];
                visible[j] = tmp;
            }
        }
    }

    // Draw visible faces back-to-front
    for (int vi = 0; vi < num_visible; ++vi) {
        int face = visible[vi].face;

        // Rotate the face normal for lighting
        fp nx, ny, nz;
        rotate(FACE_NORMALS[face][0], FACE_NORMALS[face][1], FACE_NORMALS[face][2],
               sx, cx, sy, cy, sz, cz,
               nx, ny, nz);

        // Flat shading: dot product of rotated normal with light direction
        fp dot = nx * LIGHT_X + ny * LIGHT_Y + nz * LIGHT_Z;
        // Clamp to [0.15, 1.0] — minimum ambient so back-lit faces aren't black
        if (dot < fp(0.15f)) dot = fp(0.15f);
        if (dot > fp(1.0f)) dot = fp(1.0f);
        uint8_t brightness = static_cast<uint8_t>((dot * fp(255.0f)).to_int());
        if (brightness < 38) brightness = 38;  // ambient floor

        CRGB color = FACE_COLORS[face];
        color.nscale8(brightness);

        // Edge AA mask: bit 0=v0→v1, bit 1=v1→v2, bit 2=v2→v0.
        // Disable AA on the shared diagonal: edge 2 for first tri, edge 0 for second.
        static const uint8_t EDGE_AA[2] = { 0x3, 0x6 };

        // Draw the two triangles for this face
        for (int t = 0; t < 2; ++t) {
            int tri_idx = face * 2 + t;
            uint8_t a = FACE_TRIS[tri_idx][0];
            uint8_t b = FACE_TRIS[tri_idx][1];
            uint8_t c = FACE_TRIS[tri_idx][2];
            canvas.drawTriangle(color,
                                screen_x[a], screen_y[a],
                                screen_x[b], screen_y[b],
                                screen_x[c], screen_y[c],
                                fl::DrawMode::DRAW_MODE_OVERWRITE,
                                EDGE_AA[t]);
        }
    }

    FastLED.show();
}
