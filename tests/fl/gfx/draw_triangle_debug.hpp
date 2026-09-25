
#pragma once

#include "test.h"
#include "fl/gfx/gfx.h"
#include "fl/fs/file_handle.h"
#include "fl/math/math.h"

namespace {

constexpr int kDebugW = 48, kDebugH = 32;

inline int lum(const CRGB& c) {
    return c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
}

inline char lumChar(int v) {
    static const char* const scale = " .:-=+*#%@";
    return scale[(v * 9 + 127) / 255];
}

inline float edgeDist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float len = fl::sqrtf(dx * dx + dy * dy);
    return (dx * (py - ay) - dy * (px - ax)) / len;
}

inline float minEdgeDist(float px, float py, const float v[6]) {
    float area2 = (v[2] - v[0]) * (v[4] - v[0]) - (v[3] - v[0]) * (v[5] - v[0]);
    float d0 = edgeDist(px, py, v[0], v[1], v[2], v[3]);
    float d1 = edgeDist(px, py, v[2], v[3], v[4], v[5]);
    float d2 = edgeDist(px, py, v[4], v[5], v[0], v[1]);
    if (area2 < 0) { d0 = -d0; d1 = -d1; d2 = -d2; }
    float m = d0; if (d1 < m) m = d1; if (d2 < m) m = d2;
    return m;
}

struct DumpStats { int spurious; int missing; };

DumpStats dumpCase(const char* name, CRGB* buf, const float v[6]) {
    DumpStats s = { 0, 0 };

    fl::printf("\n--- %s ---\n", name);
    for (int y = 0; y < kDebugH; ++y) {
        fl::printf("%2d |", y);
        for (int x = 0; x < kDebugW; ++x) {
            char ch;
            float d = minEdgeDist(x + 0.5f, y + 0.5f, v);
            int l = lum(buf[y * kDebugW + x]);
            if (l > 0 && d < -1.1f) { ch = 'X'; ++s.spurious; }
            else if (l == 0 && d > 0.25f) { ch = 'O'; ++s.missing; }
            else { ch = lumChar(l); }
            putchar(ch);
        }
        putchar('\n');
    }

    // Write PGM
    fl::string path = fl::string(".cache/gfx_triangles/") + name + ".pgm";
    fl::FILE* f = fl::fopen(path.c_str(), "wb");
    if (!f) {
        path = fl::string("/tmp/gfx_triangles/") + name + ".pgm";
        f = fl::fopen(path.c_str(), "wb");
    }
    if (f) {
        fl::printf("P5\n48 32\n255\n");
        for (int y = 0; y < kDebugH; ++y) {
            for (int x = 0; x < kDebugW; ++x) {
                fl::u8 c = static_cast<fl::u8>(lum(buf[y * kDebugW + x]));
                fl::fwrite(&c, 1, 1, f);
            }
        }
        fl::fclose(f);
    } else {
        fl::printf("  WARNING: could not write PGM to %s\n", path.c_str());
    }

    fl::printf("spurious=%d missing=%d\n", s.spurious, s.missing);
    return s;
}

} // anonymous namespace

FL_TEST_FILE(FL_FILEPATH) {

FL_TEST_CASE("drawTriangle near-horizontal render dump") {

    // -----------------------------------------------------------------------
    // Test battery — 10 subcases
    // -----------------------------------------------------------------------

    FL_SUBCASE("top_gentle_lr") {
        CRGB buffer[kDebugW * kDebugH] = {};
        fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);
        float v[6] = { 1.5f, 1.25f, 46.5f, 3.25f, 24.0f, 30.0f };
        canvas.drawTriangle(CRGB(255, 0, 0), v[0], v[1], v[2], v[3], v[4], v[5]);
        DumpStats s = dumpCase("top_gentle_lr", buffer, v);
        FL_CHECK_EQ(s.spurious, 0);
    }

    FL_SUBCASE("top_gentle_rl") {
        CRGB buffer[kDebugW * kDebugH] = {};
        fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);
        float v[6] = { 46.5f, 1.25f, 1.5f, 3.25f, 24.0f, 30.0f };
        canvas.drawTriangle(CRGB(255, 0, 0), v[0], v[1], v[2], v[3], v[4], v[5]);
        DumpStats s = dumpCase("top_gentle_rl", buffer, v);
        FL_CHECK_EQ(s.spurious, 0);
    }

    FL_SUBCASE("top_same_row") {
        CRGB buffer[kDebugW * kDebugH] = {};
        fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);
        float v[6] = { 1.5f, 1.25f, 46.5f, 1.75f, 24.0f, 30.0f };
        canvas.drawTriangle(CRGB(255, 0, 0), v[0], v[1], v[2], v[3], v[4], v[5]);
        DumpStats s = dumpCase("top_same_row", buffer, v);
        FL_CHECK_EQ(s.spurious, 0);
    }

    FL_SUBCASE("top_exact_horiz") {
        CRGB buffer[kDebugW * kDebugH] = {};
        fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);
        float v[6] = { 1.0f, 2.0f, 46.0f, 2.0f, 24.0f, 30.0f };
        canvas.drawTriangle(CRGB(255, 0, 0), 1, 2, 46, 2, 24, 30);
        DumpStats s = dumpCase("top_exact_horiz", buffer, v);
        FL_CHECK_EQ(s.spurious, 0);
    }

    FL_SUBCASE("bottom_gentle") {
        CRGB buffer[kDebugW * kDebugH] = {};
        fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);
        float v[6] = { 24.0f, 1.5f, 1.5f, 29.5f, 46.5f, 30.5f };
        canvas.drawTriangle(CRGB(255, 0, 0), v[0], v[1], v[2], v[3], v[4], v[5]);
        DumpStats s = dumpCase("bottom_gentle", buffer, v);
        FL_CHECK_EQ(s.spurious, 0);
    }

    FL_SUBCASE("sliver_flat") {
        CRGB buffer[kDebugW * kDebugH] = {};
        fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);
        float v[6] = { 0.5f, 2.5f, 47.5f, 4.5f, 23.5f, 5.75f };
        canvas.drawTriangle(CRGB(255, 0, 0), v[0], v[1], v[2], v[3], v[4], v[5]);
        DumpStats s = dumpCase("sliver_flat", buffer, v);
        FL_CHECK_EQ(s.spurious, 0);
    }

    FL_SUBCASE("negx_flat") {
        CRGB buffer[kDebugW * kDebugH] = {};
        fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);
        float v[6] = { -5.0f, 2.0f, 30.0f, 3.0f, 12.0f, 28.0f };
        canvas.drawTriangle(CRGB(255, 0, 0), v[0], v[1], v[2], v[3], v[4], v[5]);
        DumpStats s = dumpCase("negx_flat", buffer, v);
        FL_CHECK_EQ(s.spurious, 0);
    }

    FL_SUBCASE("tall_sliver_flat") {
        CRGB buffer[kDebugW * kDebugH] = {};
        fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);
        float v[6] = { 23.0f, 1.0f, 25.0f, 2.0f, 24.0f, 31.0f };
        canvas.drawTriangle(CRGB(255, 0, 0), 23, 1, 25, 2, 24, 31);
        DumpStats s = dumpCase("tall_sliver_flat", buffer, v);
        FL_CHECK_EQ(s.spurious, 0);
    }

    FL_SUBCASE("top_gentle_lr_int") {
        CRGB buffer[kDebugW * kDebugH] = {};
        fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);
        float v[6] = { 1.0f, 1.0f, 46.0f, 3.0f, 24.0f, 30.0f };
        canvas.drawTriangle(CRGB(255, 0, 0), 1, 1, 46, 3, 24, 30);
        DumpStats s = dumpCase("top_gentle_lr_int", buffer, v);
        FL_CHECK_EQ(s.spurious, 0);
    }

    FL_SUBCASE("top_gentle_lr_q16") {
        CRGB buffer[kDebugW * kDebugH] = {};
        fl::CanvasRGB canvas(buffer, kDebugW, kDebugH);
        float v[6] = { 1.5f, 1.25f, 46.5f, 3.25f, 24.0f, 30.0f };
        canvas.drawTriangle(CRGB(255, 0, 0),
            fl::s16x16(1.5f).to_float(), fl::s16x16(1.25f).to_float(),
            fl::s16x16(46.5f).to_float(), fl::s16x16(3.25f).to_float(),
            fl::s16x16(24.0f).to_float(), fl::s16x16(30.0f).to_float());
        DumpStats s = dumpCase("top_gentle_lr_q16", buffer, v);
        FL_CHECK_EQ(s.spurious, 0);
    }

}

} // FL_TEST_FILE
