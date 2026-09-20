
#pragma once

#include "test.h"
#include "fl/gfx/gfx.h"

FL_TEST_FILE(FL_FILEPATH) {

FL_TEST_CASE("drawTriangle basic rendering") {
    FL_SUBCASE("interior pixel == exact color") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        // Right triangle: x >= 2, y >= 2, x + y <= 15
        canvas.drawTriangle(CRGB(200, 100, 50), 2, 2, 13, 2, 2, 13);

        // Pixel (4, 4) is well inside (farthest corner (5,5) gives x+y=10)
        FL_CHECK_EQ(buffer[4 * 16 + 4].r, 200);
        FL_CHECK_EQ(buffer[4 * 16 + 4].g, 100);
        FL_CHECK_EQ(buffer[4 * 16 + 4].b, 50);
    }

    FL_SUBCASE("exterior bbox pixel == 0") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(200, 100, 50), 2, 2, 13, 2, 2, 13);

        // (15, 15) is outside the triangle's bbox [2..13]x[2..13]
        FL_CHECK_EQ(buffer[15 * 16 + 15].r, 0);
        FL_CHECK_EQ(buffer[15 * 16 + 15].g, 0);
        FL_CHECK_EQ(buffer[15 * 16 + 15].b, 0);
    }
}

FL_TEST_CASE("drawTriangle antialiasing") {
    FL_SUBCASE("AA fringe for fractional triangle") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        // Slanted, fractional edges: (3.5,4.5) (12.5,5.5) (6.5,11.5)
        canvas.drawTriangle(CRGB(255, 0, 0), 3.5f, 4.5f, 12.5f, 5.5f, 6.5f, 11.5f);

        // Slanted edges produce partial-coverage pixels (0 < r < 255)
        bool found_aa = false;
        for (int y = 4; y < 12; ++y) {
            for (int x = 3; x < 13; ++x) {
                uint8_t r = buffer[y * 16 + x].r;
                if (r > 0 && r < 255) {
                    found_aa = true;
                    break;
                }
            }
            if (found_aa) break;
        }
        FL_CHECK(found_aa);
    }
}

FL_TEST_CASE("drawTriangle single column") {
    FL_SUBCASE("thin triangle yields partial-weight pixel") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        // Near-vertical sliver: both edges land in the same column on most
        // rows, exercising the single-column (Rfrac - Lfrac) weight branch
        canvas.drawTriangle(CRGB(255, 0, 0), 7.5f, 2.0f, 8.0f, 12.0f, 7.0f, 12.0f);

        bool found_partial = false;
        uint32_t total = 0;
        for (int y = 2; y < 12; ++y) {
            for (int x = 6; x < 9; ++x) {
                uint8_t r = buffer[y * 16 + x].r;
                total += r;
                if (r > 0 && r < 255) found_partial = true;
            }
        }
        FL_CHECK(found_partial);
        FL_CHECK(total > 0);
    }
}

FL_TEST_CASE("drawTriangle negative x") {
    FL_SUBCASE("negative-x vertex: positive pixels lit, no OOB") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        // Float coords (toFixed8<int> wraps negatives); vertex at x = -3
        canvas.drawTriangle(CRGB(200, 100, 50), -3.0f, 2.0f, 8.0f, 2.0f, -3.0f, 10.0f);

        // (3, 3) is well inside (x >= -3, y >= 2, hypotenuse 8x + 11y <= 86;
        // farthest corner (4,4) gives 8x+11y=76)
        FL_CHECK_EQ(buffer[3 * 16 + 3].r, 200);
        FL_CHECK_EQ(buffer[3 * 16 + 3].g, 100);
        FL_CHECK_EQ(buffer[3 * 16 + 3].b, 50);
        // (15, 15) is outside the triangle
        FL_CHECK_EQ(buffer[15 * 16 + 15].r, 0);
    }
}

FL_TEST_CASE("drawTriangle clipping") {
    FL_SUBCASE("triangle straddling edges writes only visible region") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        // Straddles the top (y < 0) and right (x > 15) canvas edges
        canvas.drawTriangle(CRGB(255, 0, 0), 10.0f, -5.0f, 18.0f, 8.0f, 4.0f, 8.0f);

        // (10, 6) is fully inside the visible region (edges at y=6: x in
        // [4.92, 16.62]; at y=7: x in [4.46, 17.23])
        FL_CHECK_EQ(buffer[6 * 16 + 10].r, 255);
        // (0, 7) is left of the left edge (x = 10 - 6*(y+5)/13 = 4.46 at y=7)
        FL_CHECK_EQ(buffer[7 * 16 + 0].r, 0);
        // (15, 0) is right of the right edge (x = 10 + 8*(y+5)/13 = 13.54 at y=0)
        FL_CHECK_EQ(buffer[0 * 16 + 15].r, 0);
    }
}

FL_TEST_CASE("drawTriangle overwrite vs blend") {
    FL_SUBCASE("blend accumulates, overwrite does not") {
        CRGB buf_blend[256] = {};
        CRGB buf_ow[256] = {};
        fl::CanvasRGB c_blend(buf_blend, 16, 16);
        fl::CanvasRGB c_ow(buf_ow, 16, 16);
        // Same triangle, same color, drawn twice in each mode
        c_blend.drawTriangle(CRGB(100, 100, 100), 2, 2, 13, 2, 2, 13);
        c_blend.drawTriangle(CRGB(100, 100, 100), 2, 2, 13, 2, 2, 13);
        c_ow.drawTriangle(CRGB(100, 100, 100), 2, 2, 13, 2, 2, 13,
                          fl::DrawMode::DRAW_MODE_OVERWRITE);
        c_ow.drawTriangle(CRGB(100, 100, 100), 2, 2, 13, 2, 2, 13,
                          fl::DrawMode::DRAW_MODE_OVERWRITE);
        // Blend: interior pixel (4, 4) accumulated 100 + 100 = 200
        // (no clamping involved, so the value is unambiguous)
        FL_CHECK_EQ(buf_blend[4 * 16 + 4].r, 200);
        FL_CHECK_EQ(buf_blend[4 * 16 + 4].g, 200);
        FL_CHECK_EQ(buf_blend[4 * 16 + 4].b, 200);
        // Overwrite: (4, 4) stayed at a single draw's 100
        FL_CHECK_EQ(buf_ow[4 * 16 + 4].r, 100);
        FL_CHECK_EQ(buf_ow[4 * 16 + 4].g, 100);
        FL_CHECK_EQ(buf_ow[4 * 16 + 4].b, 100);
    }
}

FL_TEST_CASE("drawTriangle degenerate and off-screen") {
    FL_SUBCASE("collinear triangle writes nothing (no crash)") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        // All three vertices on y = 2: zero area
        canvas.drawTriangle(CRGB(255, 0, 0), 2, 2, 5, 2, 8, 2);
        uint32_t total = 0;
        for (int i = 0; i < 256; ++i) total += buffer[i].r + buffer[i].g + buffer[i].b;
        FL_CHECK_EQ(total, 0u);
    }

    FL_SUBCASE("fully off-screen writes nothing (no crash)") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        // Entirely outside the 16x16 canvas
        canvas.drawTriangle(CRGB(255, 0, 0), 100, 100, 120, 100, 100, 120);
        uint32_t total = 0;
        for (int i = 0; i < 256; ++i) total += buffer[i].r + buffer[i].g + buffer[i].b;
        FL_CHECK_EQ(total, 0u);
    }
}

FL_TEST_CASE("drawTriangle coord types") {
    FL_SUBCASE("int / float / s16x16 all light interior") {
        CRGB buf_int[256] = {};
        fl::CanvasRGB c_int(buf_int, 16, 16);
        c_int.drawTriangle(CRGB(200, 100, 50), 2, 2, 13, 2, 2, 13);
        FL_CHECK_EQ(buf_int[4 * 16 + 4].r, 200);
        FL_CHECK_EQ(buf_int[4 * 16 + 4].g, 100);
        FL_CHECK_EQ(buf_int[4 * 16 + 4].b, 50);

        CRGB buf_float[256] = {};
        fl::CanvasRGB c_float(buf_float, 16, 16);
        c_float.drawTriangle(CRGB(200, 100, 50), 2.0f, 2.0f, 13.0f, 2.0f, 2.0f, 13.0f);
        FL_CHECK_EQ(buf_float[4 * 16 + 4].r, 200);
        FL_CHECK_EQ(buf_float[4 * 16 + 4].g, 100);
        FL_CHECK_EQ(buf_float[4 * 16 + 4].b, 50);

        CRGB buf_fixed[256] = {};
        fl::CanvasRGB c_fixed(buf_fixed, 16, 16);
        c_fixed.drawTriangle(CRGB(200, 100, 50),
                             fl::s16x16(2.0f).to_float(), fl::s16x16(2.0f).to_float(),
                             fl::s16x16(13.0f).to_float(), fl::s16x16(2.0f).to_float(),
                             fl::s16x16(2.0f).to_float(), fl::s16x16(13.0f).to_float());
        FL_CHECK_EQ(buf_fixed[4 * 16 + 4].r, 200);
        FL_CHECK_EQ(buf_fixed[4 * 16 + 4].g, 100);
        FL_CHECK_EQ(buf_fixed[4 * 16 + 4].b, 50);
    }
}

FL_TEST_CASE("drawTriangle pixel-exact energy") {
    FL_SUBCASE("total energy (pinned in T6)") {
        // Three reference triangles; energy = sum(p.r + p.g + p.b) as u32.
        // Pinned values are a pixel-exact regression gate.
        CRGB bufA[256] = {};
        fl::CanvasRGB cA(bufA, 16, 16);
        cA.drawTriangle(CRGB(200, 100, 50), 2, 2, 13, 2, 2, 13);

        CRGB bufB[256] = {};
        fl::CanvasRGB cB(bufB, 16, 16);
        cB.drawTriangle(CRGB(200, 100, 50), 3.5f, 4.5f, 12.5f, 5.5f, 6.5f, 11.5f);

        CRGB bufC[1024] = {};
        fl::CanvasRGB cC(bufC, 32, 32);
        cC.drawTriangle(CRGB(200, 100, 50), 8.25f, 16.75f, 24.5f, 7.5f, 18.25f, 23.25f);

        uint32_t eA = 0, eB = 0, eC = 0;
        for (int i = 0; i < 256; ++i) {
            eA += bufA[i].r + bufA[i].g + bufA[i].b;
            eB += bufB[i].r + bufB[i].g + bufB[i].b;
        }
        for (int i = 0; i < 1024; ++i) eC += bufC[i].r + bufC[i].g + bufC[i].b;

        // Print energies for baseline reference
        fl::printf("  tri energy Ta: %u, Tb: %u, Tc: %u\n", eA, eB, eC);
        // Exact energy values (pixel-exact regression gate)
        FL_CHECK_EQ(eA, 21350u);
        FL_CHECK_EQ(eB, 10519u);
        FL_CHECK_EQ(eC, 35081u);
    }
}

} // FL_TEST_FILE
