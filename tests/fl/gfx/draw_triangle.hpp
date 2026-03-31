#pragma once

#include "test.h"
#include "fl/gfx/gfx.h"

FL_TEST_FILE(FL_FILEPATH) {

FL_TEST_CASE("drawTriangle basic fill") {
    FL_SUBCASE("right triangle fills interior pixels") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        // Right triangle: (2,2), (2,12), (12,12)
        canvas.drawTriangle(CRGB(255, 0, 0), 2.0f, 2.0f, 2.0f, 12.0f, 12.0f, 12.0f);

        // Count non-zero pixels — a 10x10 right triangle should fill ~50 pixels
        int non_zero = 0;
        for (int i = 0; i < 256; ++i) {
            if (buffer[i].r > 0) non_zero++;
        }
        FL_CHECK(non_zero >= 30);  // Should fill a substantial area
    }

    FL_SUBCASE("equilateral-ish triangle centered on canvas") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(0, 255, 0), 8.0f, 1.0f, 1.0f, 14.0f, 15.0f, 14.0f);

        int non_zero = 0;
        for (int i = 0; i < 256; ++i) {
            if (buffer[i].g > 0) non_zero++;
        }
        FL_CHECK(non_zero >= 50);
    }

    FL_SUBCASE("integer coordinates work") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(255, 0, 0), 2, 2, 2, 12, 12, 12);

        int non_zero = 0;
        for (int i = 0; i < 256; ++i) {
            if (buffer[i].r > 0) non_zero++;
        }
        FL_CHECK(non_zero >= 20);
    }

    FL_SUBCASE("s16x16 fixed-point coordinates work") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        fl::s16x16 x0(2.0f), y0(2.0f), x1(2.0f), y1(12.0f), x2(12.0f), y2(12.0f);
        canvas.drawTriangle(CRGB(255, 0, 0), x0, y0, x1, y1, x2, y2);

        int non_zero = 0;
        for (int i = 0; i < 256; ++i) {
            if (buffer[i].r > 0) non_zero++;
        }
        FL_CHECK(non_zero >= 20);
    }
}

FL_TEST_CASE("drawTriangle edge cases") {
    FL_SUBCASE("zero-area triangle (collinear points) — no crash") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(255, 0, 0), 2.0f, 5.0f, 8.0f, 5.0f, 14.0f, 5.0f);

        // Collinear → zero height → should draw nothing (or minimal edge pixels)
        FL_CHECK(true);
    }

    FL_SUBCASE("degenerate single point — no crash") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(255, 0, 0), 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f);
        FL_CHECK(true);
    }

    FL_SUBCASE("fully off-screen — no crash, no pixels modified") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(255, 0, 0), -100.0f, -100.0f, -50.0f, -100.0f, -75.0f, -50.0f);

        int non_zero = 0;
        for (int i = 0; i < 256; ++i) {
            if (buffer[i].r > 0) non_zero++;
        }
        FL_CHECK(non_zero == 0);
    }

    FL_SUBCASE("partially clipped triangle — safe") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(255, 0, 0), -5.0f, 8.0f, 8.0f, -5.0f, 20.0f, 20.0f);

        int non_zero = 0;
        for (int i = 0; i < 256; ++i) {
            if (buffer[i].r > 0) non_zero++;
        }
        FL_CHECK(non_zero > 0);  // Some pixels should be visible
    }
}

FL_TEST_CASE("drawTriangle antialiasing") {
    FL_SUBCASE("fractional coordinates produce AA edge pixels") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(255, 0, 0), 3.3f, 2.7f, 2.1f, 12.8f, 13.6f, 11.2f);

        // Check that some pixels have partial brightness (AA)
        int full_bright = 0;
        int partial = 0;
        for (int i = 0; i < 256; ++i) {
            if (buffer[i].r == 255) full_bright++;
            else if (buffer[i].r > 0 && buffer[i].r < 255) partial++;
        }
        FL_CHECK(partial > 0);  // Should have some AA pixels
        FL_CHECK(full_bright > 0);  // Should also have interior pixels
    }
}

FL_TEST_CASE("drawTriangle overwrite mode") {
    FL_SUBCASE("overwrite replaces existing pixels") {
        CRGB buffer[256];
        for (int i = 0; i < 256; ++i) buffer[i] = CRGB(0, 0, 100);
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(255, 0, 0), 4.0f, 4.0f, 4.0f, 12.0f, 12.0f, 12.0f,
                            fl::DrawMode::DRAW_MODE_OVERWRITE);

        // Check that at least some interior pixels are pure red (no blue)
        bool found_overwritten = false;
        for (int i = 0; i < 256; ++i) {
            if (buffer[i].r == 255 && buffer[i].b == 0) {
                found_overwritten = true;
                break;
            }
        }
        FL_CHECK(found_overwritten);
    }
}

FL_TEST_CASE("drawTriangle vertex ordering invariance") {
    FL_SUBCASE("different vertex orders produce same result") {
        CRGB buf1[256] = {};
        CRGB buf2[256] = {};
        CRGB buf3[256] = {};
        fl::CanvasRGB c1(buf1, 16, 16);
        fl::CanvasRGB c2(buf2, 16, 16);
        fl::CanvasRGB c3(buf3, 16, 16);

        // Same triangle, different vertex orderings
        c1.drawTriangle(CRGB(255, 0, 0), 3.0f, 2.0f, 12.0f, 5.0f, 7.0f, 13.0f);
        c2.drawTriangle(CRGB(255, 0, 0), 12.0f, 5.0f, 7.0f, 13.0f, 3.0f, 2.0f);
        c3.drawTriangle(CRGB(255, 0, 0), 7.0f, 13.0f, 3.0f, 2.0f, 12.0f, 5.0f);

        // All three should produce identical pixel buffers
        bool match12 = true, match13 = true;
        for (int i = 0; i < 256; ++i) {
            if (buf1[i].r != buf2[i].r || buf1[i].g != buf2[i].g || buf1[i].b != buf2[i].b)
                match12 = false;
            if (buf1[i].r != buf3[i].r || buf1[i].g != buf3[i].g || buf1[i].b != buf3[i].b)
                match13 = false;
        }
        FL_CHECK(match12);
        FL_CHECK(match13);
    }
}

}
