
#pragma once

#include "test.h"
#include "fl/gfx/gfx.h"
#include "fl/gfx/crgb16.h"

using fl::CRGB16;
using fl::u8x8;

FL_TEST_FILE(FL_FILEPATH) {

FL_TEST_CASE("Canvas<CRGB16> drawTriangle") {
    FL_SUBCASE("center pixel is lit") {
        CRGB16 buffer[256] = {};
        fl::Canvas<CRGB16> canvas(fl::span<CRGB16>(buffer, 256), 16, 16);
        // Right triangle: x >= 2, y >= 2, x + y <= 15; (4, 4) is well inside
        canvas.drawTriangle(CRGB16(u8x8(255.0f), u8x8(0.0f), u8x8(0.0f)), 2, 2, 13, 2, 2, 13);
        FL_CHECK(buffer[4 * 16 + 4].r.raw() > 0);
    }

    FL_SUBCASE("pixel beyond extent is black") {
        CRGB16 buffer[256] = {};
        fl::Canvas<CRGB16> canvas(fl::span<CRGB16>(buffer, 256), 16, 16);
        canvas.drawTriangle(CRGB16(u8x8(255.0f), u8x8(0.0f), u8x8(0.0f)), 2, 2, 13, 2, 2, 13);
        // (15, 15) is outside the triangle's bbox [2..13]x[2..13]
        FL_CHECK(buffer[15 * 16 + 15].r.raw() == 0);
    }

    FL_SUBCASE("AA fringe has intermediate brightness") {
        CRGB16 buffer[256] = {};
        fl::Canvas<CRGB16> canvas(fl::span<CRGB16>(buffer, 256), 16, 16);
        // Slanted, fractional edges: (3.5,4.5) (12.5,5.5) (6.5,11.5)
        canvas.drawTriangle(CRGB16(u8x8(255.0f), u8x8(0.0f), u8x8(0.0f)), 3.5f, 4.5f, 12.5f, 5.5f, 6.5f, 11.5f);

        // Slanted edges produce partial-coverage pixels (0 < r < max)
        bool found_aa = false;
        for (int y = 4; y < 12; ++y) {
            for (int x = 3; x < 13; ++x) {
                fl::u16 rv = buffer[y * 16 + x].r.raw();
                if (rv > 0 && rv < 0xFF00) {
                    found_aa = true;
                    break;
                }
            }
            if (found_aa) break;
        }
        FL_CHECK(found_aa);
    }
}

} // FL_TEST_FILE
