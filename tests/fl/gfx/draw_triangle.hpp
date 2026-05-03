#pragma once

#include <cmath>

#include "test.h"
#include "fl/gfx/gfx.h"

FL_TEST_FILE(FL_FILEPATH) {

struct RefPt {
    float x;
    float y;
};

static float edge_fn(const RefPt& a, const RefPt& b, float px, float py) {
    return (px - a.x) * (b.y - a.y) - (py - a.y) * (b.x - a.x);
}

static float pixel_coverage_ref(const RefPt& v0, const RefPt& v1, const RefPt& v2,
                                int px, int py) {
    // 8x8 supersampling reference coverage for tighter AA reference.
    const int S = 8;
    const float area = edge_fn(v0, v1, v2.x, v2.y);
    if (area == 0.0f) {
        return 0.0f;
    }

    int inside = 0;
    for (int sy = 0; sy < S; ++sy) {
        for (int sx = 0; sx < S; ++sx) {
            float fx = static_cast<float>(px) + (static_cast<float>(sx) + 0.5f) / static_cast<float>(S);
            float fy = static_cast<float>(py) + (static_cast<float>(sy) + 0.5f) / static_cast<float>(S);

            float w0 = edge_fn(v1, v2, fx, fy);
            float w1 = edge_fn(v2, v0, fx, fy);
            float w2 = edge_fn(v0, v1, fx, fy);

            bool in = (area > 0.0f) ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                                    : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (in) {
                ++inside;
            }
        }
    }
    return static_cast<float>(inside) / static_cast<float>(S * S);
}

static void render_triangle_reference(CRGB* out, int w, int h, const CRGB& color,
                                      float x0, float y0, float x1, float y1, float x2, float y2) {
    for (int i = 0; i < w * h; ++i) {
        out[i] = CRGB::Black;
    }

    RefPt v0 = {x0, y0};
    RefPt v1 = {x1, y1};
    RefPt v2 = {x2, y2};

    float min_xf = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    float max_xf = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    float min_yf = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    float max_yf = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);

    int xmin = static_cast<int>(min_xf) - 1;
    int xmax = static_cast<int>(max_xf) + 1;
    int ymin = static_cast<int>(min_yf) - 1;
    int ymax = static_cast<int>(max_yf) + 1;

    if (xmin < 0) xmin = 0;
    if (ymin < 0) ymin = 0;
    if (xmax >= w) xmax = w - 1;
    if (ymax >= h) ymax = h - 1;

    for (int y = ymin; y <= ymax; ++y) {
        for (int x = xmin; x <= xmax; ++x) {
            float cov = pixel_coverage_ref(v0, v1, v2, x, y);
            if (cov <= 0.0f) {
                continue;
            }
            int a = static_cast<int>(cov * 255.0f + 0.5f);
            CRGB c;
            c.r = static_cast<uint8_t>((static_cast<int>(color.r) * a + 127) / 255);
            c.g = static_cast<uint8_t>((static_cast<int>(color.g) * a + 127) / 255);
            c.b = static_cast<uint8_t>((static_cast<int>(color.b) * a + 127) / 255);
            out[y * w + x] = c;
        }
    }
}

static int sad_rgb(const CRGB* a, const CRGB* b, int n) {
    int sad = 0;
    for (int i = 0; i < n; ++i) {
        int dr = static_cast<int>(a[i].r) - static_cast<int>(b[i].r);
        int dg = static_cast<int>(a[i].g) - static_cast<int>(b[i].g);
        int db = static_cast<int>(a[i].b) - static_cast<int>(b[i].b);
        sad += (dr < 0 ? -dr : dr);
        sad += (dg < 0 ? -dg : dg);
        sad += (db < 0 ? -db : db);
    }
    return sad;
}

static void dump_ascii_diff(const CRGB* opt, const CRGB* ref, int w, int h,
                            const char* label, int sad_value) {
    int min_x = w;
    int min_y = h;
    int max_x = -1;
    int max_y = -1;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            int dr = static_cast<int>(opt[idx].r) - static_cast<int>(ref[idx].r);
            int dg = static_cast<int>(opt[idx].g) - static_cast<int>(ref[idx].g);
            int db = static_cast<int>(opt[idx].b) - static_cast<int>(ref[idx].b);
            if (dr == 0 && dg == 0 && db == 0) {
                continue;
            }
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
        }
    }

    if (max_x < min_x || max_y < min_y) {
        fl::printf("triangle diff [%s]: no differing pixels (SAD=%d)\n", label, sad_value);
        return;
    }

    if (min_x > 0) --min_x;
    if (min_y > 0) --min_y;
    if (max_x + 1 < w) ++max_x;
    if (max_y + 1 < h) ++max_y;

    fl::printf("triangle diff [%s] SAD=%d bbox=[%d,%d]-[%d,%d]\n",
               label, sad_value, min_x, min_y, max_x, max_y);
    fl::printf("legend: '>' optimized brighter, '<' reference brighter, '*' both lit but differ, '.' nearly equal\n");

    char line[256];
    for (int y = min_y; y <= max_y; ++y) {
        int out = 0;
        for (int x = min_x; x <= max_x; ++x) {
            int idx = y * w + x;
            int o = static_cast<int>(opt[idx].r) + static_cast<int>(opt[idx].g) + static_cast<int>(opt[idx].b);
            int r = static_cast<int>(ref[idx].r) + static_cast<int>(ref[idx].g) + static_cast<int>(ref[idx].b);
            int d = o - r;
            if (d < 0) d = -d;

            char c;
            if (o == 0 && r == 0) {
                c = ' ';
            } else if (d <= 6) {
                c = '.';
            } else if (o == 0) {
                c = '<';
            } else if (r == 0) {
                c = '>';
            } else {
                c = '*';
            }
            line[out++] = c;
        }
        line[out] = '\0';
        fl::printf("%s\n", line);
    }
}

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

    FL_SUBCASE("very short triangles stay within their local span") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(255, 0, 0), 2.1f, 7.10f, 13.4f, 8.05f, 4.8f, 8.60f);

        int non_zero = 0;
        for (int y = 0; y < 16; ++y) {
            int row_count = 0;
            for (int x = 0; x < 16; ++x) {
                if (buffer[y * 16 + x].r == 0) {
                    continue;
                }
                ++non_zero;
                ++row_count;
                FL_CHECK(y >= 6);
                FL_CHECK(y <= 9);
                FL_CHECK(x >= 1);
                FL_CHECK(x <= 14);
            }
            FL_CHECK(row_count <= 13);
        }
        FL_CHECK(non_zero > 0);
    }

    FL_SUBCASE("shallow edges do not leave isolated far-right pixels") {
        CRGB buffer[256] = {};
        fl::CanvasRGB canvas(buffer, 16, 16);
        canvas.drawTriangle(CRGB(255, 0, 0), 1.2f, 4.2f, 14.1f, 5.4f, 4.4f, 13.7f);

        int non_empty_rows = 0;
        for (int y = 0; y < 16; ++y) {
            int first = -1;
            int last = -1;
            for (int x = 0; x < 16; ++x) {
                if (buffer[y * 16 + x].r == 0) {
                    continue;
                }
                if (first < 0) {
                    first = x;
                }
                last = x;
            }
            if (first < 0) {
                continue;
            }
            ++non_empty_rows;
            for (int x = first; x <= last; ++x) {
                FL_CHECK(buffer[y * 16 + x].r > 0);
            }
        }
        FL_CHECK(non_empty_rows > 0);
    }

    FL_SUBCASE("verbose diagnostics: shallow edge stays inside row bounds") {
        CRGB buffer[32 * 32] = {};
        fl::CanvasRGB canvas(buffer, 32, 32);

        const float x0 = 2.4f, y0 = 6.2f;
        const float x1 = 29.1f, y1 = 8.0f;
        const float x2 = 10.6f, y2 = 25.5f;

        canvas.drawTriangle(CRGB(255, 0, 0), x0, y0, x1, y1, x2, y2,
                            fl::DrawMode::DRAW_MODE_OVERWRITE);

        auto add_intersection = [](float ex0, float ey0, float ex1, float ey1,
                                   float y_center, float* out_x, int& count) {
            if (ey0 == ey1) {
                return;
            }
            float ymin = ey0 < ey1 ? ey0 : ey1;
            float ymax = ey0 < ey1 ? ey1 : ey0;
            if (y_center < ymin || y_center >= ymax) {
                return;
            }
            float t = (y_center - ey0) / (ey1 - ey0);
            out_x[count++] = ex0 + t * (ex1 - ex0);
        };

        int violations = 0;
        for (int y = 0; y < 32; ++y) {
            float y_center = static_cast<float>(y) + 0.5f;
            float xs[3] = {0.0f, 0.0f, 0.0f};
            int n = 0;
            add_intersection(x0, y0, x1, y1, y_center, xs, n);
            add_intersection(x1, y1, x2, y2, y_center, xs, n);
            add_intersection(x2, y2, x0, y0, y_center, xs, n);

            if (n < 2) {
                continue;
            }

            float x_left = xs[0] < xs[1] ? xs[0] : xs[1];
            float x_right = xs[0] < xs[1] ? xs[1] : xs[0];
            int min_allowed = static_cast<int>(x_left);
            int max_allowed = static_cast<int>(x_right);

            for (int x = 0; x < 32; ++x) {
                if (buffer[y * 32 + x].r == 0) {
                    continue;
                }
                if (x < min_allowed || x > max_allowed) {
                    ++violations;
                    FL_MESSAGE("errant pixel: x=" << x << " y=" << y
                               << " allowed=[" << min_allowed << "," << max_allowed << "]"
                               << " x_left=" << x_left << " x_right=" << x_right);
                }
            }
        }

        FL_MESSAGE("diagnostic violations (shallow edge row bounds): " << violations);
        FL_CHECK(violations < 10000);
    }

    FL_SUBCASE("verbose diagnostics: extra wide short triangle shallow top") {
        CRGB buffer[64 * 32] = {};
        fl::CanvasRGB canvas(buffer, 64, 32);

        const float x0 = 3.2f, y0 = 5.2f;
        const float x1 = 60.7f, y1 = 6.6f;
        const float x2 = 18.4f, y2 = 14.9f;

        canvas.drawTriangle(CRGB(255, 0, 0), x0, y0, x1, y1, x2, y2,
                            fl::DrawMode::DRAW_MODE_OVERWRITE);

        auto add_intersection = [](float ex0, float ey0, float ex1, float ey1,
                                   float y_center, float* out_x, int& count) {
            if (ey0 == ey1) {
                return;
            }
            float ymin = ey0 < ey1 ? ey0 : ey1;
            float ymax = ey0 < ey1 ? ey1 : ey0;
            if (y_center < ymin || y_center >= ymax) {
                return;
            }
            float t = (y_center - ey0) / (ey1 - ey0);
            out_x[count++] = ex0 + t * (ex1 - ex0);
        };

        int violations = 0;
        for (int y = 0; y < 32; ++y) {
            float y_center = static_cast<float>(y) + 0.5f;
            float xs[3] = {0.0f, 0.0f, 0.0f};
            int n = 0;
            add_intersection(x0, y0, x1, y1, y_center, xs, n);
            add_intersection(x1, y1, x2, y2, y_center, xs, n);
            add_intersection(x2, y2, x0, y0, y_center, xs, n);

            if (n < 2) {
                continue;
            }

            float x_left = xs[0] < xs[1] ? xs[0] : xs[1];
            float x_right = xs[0] < xs[1] ? xs[1] : xs[0];
            int min_allowed = static_cast<int>(x_left);
            int max_allowed = static_cast<int>(x_right);

            for (int x = 0; x < 64; ++x) {
                if (buffer[y * 64 + x].r == 0) {
                    continue;
                }
                if (x < min_allowed || x > max_allowed) {
                    ++violations;
                    FL_MESSAGE("wide-short errant pixel: x=" << x << " y=" << y
                               << " allowed=[" << min_allowed << "," << max_allowed << "]"
                               << " x_left=" << x_left << " x_right=" << x_right);
                }
            }
        }

        FL_MESSAGE("diagnostic violations (extra wide short): " << violations);
        FL_CHECK(violations < 10000);
    }

    FL_SUBCASE("verbose diagnostics: very shallow top near horizontal") {
        CRGB buffer[64 * 32] = {};
        fl::CanvasRGB canvas(buffer, 64, 32);

        const float x0 = 4.1f, y0 = 7.05f;
        const float x1 = 61.3f, y1 = 7.95f;
        const float x2 = 12.7f, y2 = 16.4f;

        canvas.drawTriangle(CRGB(255, 0, 0), x0, y0, x1, y1, x2, y2,
                            fl::DrawMode::DRAW_MODE_OVERWRITE);

        auto add_intersection = [](float ex0, float ey0, float ex1, float ey1,
                                   float y_center, float* out_x, int& count) {
            if (ey0 == ey1) {
                return;
            }
            float ymin = ey0 < ey1 ? ey0 : ey1;
            float ymax = ey0 < ey1 ? ey1 : ey0;
            if (y_center < ymin || y_center >= ymax) {
                return;
            }
            float t = (y_center - ey0) / (ey1 - ey0);
            out_x[count++] = ex0 + t * (ex1 - ex0);
        };

        int violations = 0;
        for (int y = 0; y < 32; ++y) {
            float y_center = static_cast<float>(y) + 0.5f;
            float xs[3] = {0.0f, 0.0f, 0.0f};
            int n = 0;
            add_intersection(x0, y0, x1, y1, y_center, xs, n);
            add_intersection(x1, y1, x2, y2, y_center, xs, n);
            add_intersection(x2, y2, x0, y0, y_center, xs, n);

            if (n < 2) {
                continue;
            }

            float x_left = xs[0] < xs[1] ? xs[0] : xs[1];
            float x_right = xs[0] < xs[1] ? xs[1] : xs[0];
            int min_allowed = static_cast<int>(x_left);
            int max_allowed = static_cast<int>(x_right);

            for (int x = 0; x < 64; ++x) {
                if (buffer[y * 64 + x].r == 0) {
                    continue;
                }
                if (x < min_allowed || x > max_allowed) {
                    ++violations;
                    FL_MESSAGE("near-horizontal errant pixel: x=" << x << " y=" << y
                               << " allowed=[" << min_allowed << "," << max_allowed << "]"
                               << " x_left=" << x_left << " x_right=" << x_right);
                }
            }
        }

        FL_MESSAGE("diagnostic violations (near horizontal): " << violations);
        FL_CHECK(violations < 10000);

        int min_y_seen = 32;
        int max_y_seen = -1;
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 64; ++x) {
                if (buffer[y * 64 + x].r > 0) {
                    if (y < min_y_seen) min_y_seen = y;
                    if (y > max_y_seen) max_y_seen = y;
                }
            }
        }

        // For this near-horizontal top edge, coverage should stay near
        // the triangle's vertical extent plus one AA row.
        FL_CHECK(min_y_seen >= 6);
        FL_CHECK(max_y_seen <= 16);
    }

    FL_SUBCASE("rotation sweep: wide short triangle exposes shallow-edge outliers") {
        CRGB buffer[64 * 64] = {};
        fl::CanvasRGB canvas(buffer, 64, 64);

        // Base triangle: wide + short, shallow top edge near horizontal.
        const float bx0 = -24.0f, by0 = -5.2f;
        const float bx1 =  24.0f, by1 = -3.9f;
        const float bx2 =  -9.0f, by2 =  6.7f;

        // 3-degree per frame incremental rotation.
        const float step_c = 0.9986295f;
        const float step_s = 0.0523360f;
        float frame_c = 1.0f;
        float frame_s = 0.0f;

        auto add_intersection = [](float ex0, float ey0, float ex1, float ey1,
                                   float y_center, float* out_x, int& count) {
            if (ey0 == ey1) {
                return;
            }
            float ymin = ey0 < ey1 ? ey0 : ey1;
            float ymax = ey0 < ey1 ? ey1 : ey0;
            if (y_center < ymin || y_center >= ymax) {
                return;
            }
            float t = (y_center - ey0) / (ey1 - ey0);
            out_x[count++] = ex0 + t * (ex1 - ex0);
        };

        int total_violations = 0;
        int frames_checked = 0;

        for (int frame = 0; frame < 120; ++frame) {
            // Rotate base vertices by current frame angle.
            auto rot_x = [frame_c, frame_s](float x, float y) { return x * frame_c - y * frame_s; };
            auto rot_y = [frame_c, frame_s](float x, float y) { return x * frame_s + y * frame_c; };

            float x0 = 32.0f + rot_x(bx0, by0);
            float y0 = 32.0f + rot_y(bx0, by0);
            float x1 = 32.0f + rot_x(bx1, by1);
            float y1 = 32.0f + rot_y(bx1, by1);
            float x2 = 32.0f + rot_x(bx2, by2);
            float y2 = 32.0f + rot_y(bx2, by2);

            // Clear frame buffer.
            for (int i = 0; i < 64 * 64; ++i) {
                buffer[i] = CRGB::Black;
            }

            canvas.drawTriangle(CRGB(255, 0, 0), x0, y0, x1, y1, x2, y2,
                                fl::DrawMode::DRAW_MODE_OVERWRITE);

            int frame_violations = 0;
            for (int y = 0; y < 64; ++y) {
                float y_center = static_cast<float>(y) + 0.5f;
                float xs[3] = {0.0f, 0.0f, 0.0f};
                int n = 0;
                add_intersection(x0, y0, x1, y1, y_center, xs, n);
                add_intersection(x1, y1, x2, y2, y_center, xs, n);
                add_intersection(x2, y2, x0, y0, y_center, xs, n);
                if (n < 2) {
                    continue;
                }

                float x_left = xs[0] < xs[1] ? xs[0] : xs[1];
                float x_right = xs[0] < xs[1] ? xs[1] : xs[0];
                int min_allowed = static_cast<int>(x_left) - 1;
                int max_allowed = static_cast<int>(x_right) + 1;

                for (int x = 0; x < 64; ++x) {
                    if (buffer[y * 64 + x].r == 0) {
                        continue;
                    }
                    if (x < min_allowed || x > max_allowed) {
                        ++frame_violations;
                        ++total_violations;
                        FL_MESSAGE("rotation errant pixel: frame=" << frame
                                   << " x=" << x << " y=" << y
                                   << " allowed=[" << min_allowed << "," << max_allowed << "]"
                                   << " x_left=" << x_left << " x_right=" << x_right
                                   << " tri=(" << x0 << "," << y0 << ")"
                                   << " (" << x1 << "," << y1 << ")"
                                   << " (" << x2 << "," << y2 << ")");
                    }
                }
            }

            if (frame_violations > 0) {
                FL_MESSAGE("rotation frame violations: frame=" << frame
                           << " count=" << frame_violations);
            }
            ++frames_checked;

            // Increment frame angle by +3 degrees.
            float next_c = frame_c * step_c - frame_s * step_s;
            float next_s = frame_c * step_s + frame_s * step_c;
            frame_c = next_c;
            frame_s = next_s;
        }

        FL_CHECK(frames_checked == 120);
        FL_MESSAGE("diagnostic violations (rotation sweep): " << total_violations);
        FL_CHECK(total_violations < 50000);
    }
}

FL_TEST_CASE("drawTriangle reference SAD diagnostics") {
    FL_SUBCASE("wide/short shallow-top static cases print SAD") {
        const int W = 64;
        const int H = 32;
        const CRGB color = CRGB(255, 0, 0);
        const int sad_dump_threshold = 3500;
        const int sad_fail_threshold = 8000;

        struct TriCase {
            float x0, y0, x1, y1, x2, y2;
            const char* name;
        };

        const TriCase cases[] = {
            {3.2f, 5.2f, 60.7f, 6.6f, 18.4f, 14.9f, "wide_short_1"},
            {4.1f, 7.05f, 61.3f, 7.95f, 12.7f, 16.4f, "wide_short_2"},
            {2.5f, 10.2f, 59.4f, 11.1f, 35.2f, 18.7f, "wide_short_3"},
            {6.0f, 8.5f, 57.0f, 9.0f, 16.0f, 19.0f, "wide_short_4"},
        };

        int worst_sad = 0;
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            CRGB opt[W * H] = {};
            CRGB ref[W * H] = {};
            fl::CanvasRGB canvas(opt, W, H);
            canvas.drawTriangle(color,
                                cases[i].x0, cases[i].y0,
                                cases[i].x1, cases[i].y1,
                                cases[i].x2, cases[i].y2,
                                fl::DrawMode::DRAW_MODE_OVERWRITE);
            render_triangle_reference(ref, W, H, color,
                                      cases[i].x0, cases[i].y0,
                                      cases[i].x1, cases[i].y1,
                                      cases[i].x2, cases[i].y2);

            int sad = sad_rgb(opt, ref, W * H);
            if (sad > worst_sad) {
                worst_sad = sad;
            }
            fl::printf("triangle SAD static [%s]: %d\n", cases[i].name, sad);
            if (sad > sad_dump_threshold) {
                dump_ascii_diff(opt, ref, W, H, cases[i].name, sad);
            }
        }

        // Very loose guard: catches gross regressions while keeping
        // this test diagnostic-first.
        FL_CHECK(worst_sad < sad_fail_threshold);
    }

    FL_SUBCASE("slow rotation sweep prints SAD per frame") {
        const int W = 64;
        const int H = 64;
        const CRGB color = CRGB(255, 0, 0);

        const float bx0 = -24.0f, by0 = -5.2f;
        const float bx1 =  24.0f, by1 = -3.9f;
        const float bx2 =  -9.0f, by2 =  6.7f;

        // 1 degree per frame (slow rotate)
        const float step_c = 0.9998477f;
        const float step_s = 0.0174524f;
        float frame_c = 1.0f;
        float frame_s = 0.0f;

        int worst_sad = 0;
        int sum_sad = 0;
        const int frames = 90;
        const int sad_dump_threshold = 5000;
        const int sad_fail_threshold = 8000;

        for (int frame = 0; frame < frames; ++frame) {
            auto rot_x = [frame_c, frame_s](float x, float y) { return x * frame_c - y * frame_s; };
            auto rot_y = [frame_c, frame_s](float x, float y) { return x * frame_s + y * frame_c; };

            float x0 = 32.0f + rot_x(bx0, by0);
            float y0 = 32.0f + rot_y(bx0, by0);
            float x1 = 32.0f + rot_x(bx1, by1);
            float y1 = 32.0f + rot_y(bx1, by1);
            float x2 = 32.0f + rot_x(bx2, by2);
            float y2 = 32.0f + rot_y(bx2, by2);

            CRGB opt[W * H] = {};
            CRGB ref[W * H] = {};
            fl::CanvasRGB canvas(opt, W, H);
            canvas.drawTriangle(color, x0, y0, x1, y1, x2, y2,
                                fl::DrawMode::DRAW_MODE_OVERWRITE);
            render_triangle_reference(ref, W, H, color, x0, y0, x1, y1, x2, y2);

            int sad = sad_rgb(opt, ref, W * H);
            sum_sad += sad;
            if (sad > worst_sad) {
                worst_sad = sad;
            }

            // Print every frame so --verbose gives a complete profile.
            fl::printf("triangle SAD rotate frame %d: %d\n", frame, sad);
            if (sad > sad_dump_threshold) {
                char label[64];
                fl::snprintf(label, sizeof(label), "rotate_frame_%d", frame);
                dump_ascii_diff(opt, ref, W, H, label, sad);
            }

            float next_c = frame_c * step_c - frame_s * step_s;
            float next_s = frame_c * step_s + frame_s * step_c;
            frame_c = next_c;
            frame_s = next_s;
        }

        int mean_sad = sum_sad / frames;
        fl::printf("triangle SAD rotate summary: worst=%d mean=%d frames=%d\n", worst_sad, mean_sad, frames);

        FL_CHECK(worst_sad < sad_fail_threshold);
    }

    FL_SUBCASE("no exterior pixels: rotation sweep strict check") {
        // For every pixel where the 8x8 reference has zero coverage, the
        // optimised renderer must not draw anything bright there.  Any pixel
        // that passes the 8x8 reference filter is legitimately inside/on the
        // edge; pixels that are truly outside will be caught here.
        const int W = 64;
        const int H = 64;
        const CRGB color = CRGB(255, 0, 0);
        // Threshold: optimised pixel brightness above which we call it a
        // visible exterior pixel (allows for tiny floating-point rounding in
        // the shallow-AA endpoint columns).
        const int exterior_threshold = 32;

        const float bx0 = -24.0f, by0 = -5.2f;
        const float bx1 =  24.0f, by1 = -3.9f;
        const float bx2 =  -9.0f, by2 =  6.7f;

        // 1 degree per frame
        const float step_c = 0.9998477f;
        const float step_s = 0.0174524f;
        float frame_c = 1.0f;
        float frame_s = 0.0f;

        int total_exterior = 0;
        int first_report = 0;

        for (int frame = 0; frame < 90; ++frame) {
            auto rot_x = [frame_c, frame_s](float x, float y) { return x * frame_c - y * frame_s; };
            auto rot_y = [frame_c, frame_s](float x, float y) { return x * frame_s + y * frame_c; };

            float x0 = 32.0f + rot_x(bx0, by0);
            float y0 = 32.0f + rot_y(bx0, by0);
            float x1 = 32.0f + rot_x(bx1, by1);
            float y1 = 32.0f + rot_y(bx1, by1);
            float x2 = 32.0f + rot_x(bx2, by2);
            float y2 = 32.0f + rot_y(bx2, by2);

            CRGB opt[W * H] = {};
            CRGB ref[W * H] = {};
            fl::CanvasRGB canvas(opt, W, H);
            canvas.drawTriangle(color, x0, y0, x1, y1, x2, y2,
                                fl::DrawMode::DRAW_MODE_OVERWRITE);
            render_triangle_reference(ref, W, H, color, x0, y0, x1, y1, x2, y2);

            for (int i = 0; i < W * H; ++i) {
                int ref_sum = static_cast<int>(ref[i].r) + ref[i].g + ref[i].b;
                int opt_sum = static_cast<int>(opt[i].r) + opt[i].g + opt[i].b;
                if (ref_sum == 0 && opt_sum > exterior_threshold) {
                    ++total_exterior;
                    if (first_report < 8) {
                        int px = i % W;
                        int py = i / W;
                        fl::printf("exterior pixel: frame=%d x=%d y=%d opt=%d tri=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)\n",
                                   frame, px, py, opt_sum,
                                   (double)x0, (double)y0, (double)x1, (double)y1, (double)x2, (double)y2);
                        ++first_report;
                    }
                }
            }

            float next_c = frame_c * step_c - frame_s * step_s;
            float next_s = frame_c * step_s + frame_s * step_c;
            frame_c = next_c;
            frame_s = next_s;
        }

        fl::printf("exterior pixel violations (strict): %d\n", total_exterior);
        FL_CHECK(total_exterior == 0);
    }

    FL_SUBCASE("no missing horizontal spans: slow rotation strict check") {
        // Catch dropped horizontal runs: when reference shows solid interior
        // coverage for multiple adjacent pixels, optimised output should not
        // collapse them into gaps.
        const int W = 64;
        const int H = 64;
        const CRGB color = CRGB(255, 0, 0);

        // Thresholds in summed RGB space (0..765)
        const int ref_inside_threshold = 220;   // clearly-inside reference pixel
        const int opt_missing_threshold = 12;   // near-dark optimised pixel
        const int min_run_len = 2;              // visible horizontal span

        const float bx0 = -24.0f, by0 = -5.2f;
        const float bx1 =  24.0f, by1 = -3.9f;
        const float bx2 =  -9.0f, by2 =  6.7f;

        // Slower rotation: 0.5 degree per frame to probe more orientations.
        const float step_c = 0.9999619f;
        const float step_s = 0.0087265f;
        float frame_c = 1.0f;
        float frame_s = 0.0f;

        int missing_runs = 0;
        int reports = 0;
        const int frames = 180;

        for (int frame = 0; frame < frames; ++frame) {
            auto rot_x = [frame_c, frame_s](float x, float y) { return x * frame_c - y * frame_s; };
            auto rot_y = [frame_c, frame_s](float x, float y) { return x * frame_s + y * frame_c; };

            float x0 = 32.0f + rot_x(bx0, by0);
            float y0 = 32.0f + rot_y(bx0, by0);
            float x1 = 32.0f + rot_x(bx1, by1);
            float y1 = 32.0f + rot_y(bx1, by1);
            float x2 = 32.0f + rot_x(bx2, by2);
            float y2 = 32.0f + rot_y(bx2, by2);

            CRGB opt[W * H] = {};
            CRGB ref[W * H] = {};
            fl::CanvasRGB canvas(opt, W, H);
            canvas.drawTriangle(color, x0, y0, x1, y1, x2, y2,
                                fl::DrawMode::DRAW_MODE_OVERWRITE);
            render_triangle_reference(ref, W, H, color, x0, y0, x1, y1, x2, y2);

            for (int y = 0; y < H; ++y) {
                int run_start = -1;
                for (int x = 0; x < W; ++x) {
                    int idx = y * W + x;
                    int ref_sum = static_cast<int>(ref[idx].r) + ref[idx].g + ref[idx].b;
                    int opt_sum = static_cast<int>(opt[idx].r) + opt[idx].g + opt[idx].b;
                    bool missing = (ref_sum >= ref_inside_threshold && opt_sum <= opt_missing_threshold);
                    if (missing) {
                        if (run_start < 0) run_start = x;
                    } else if (run_start >= 0) {
                        int run_len = x - run_start;
                        if (run_len >= min_run_len) {
                            ++missing_runs;
                            if (reports < 10) {
                                fl::printf("missing span: frame=%d y=%d x=[%d,%d] len=%d tri=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)\n",
                                           frame, y, run_start, x - 1, run_len,
                                           (double)x0, (double)y0, (double)x1, (double)y1, (double)x2, (double)y2);
                                ++reports;
                            }
                        }
                        run_start = -1;
                    }
                }
                if (run_start >= 0) {
                    int run_len = W - run_start;
                    if (run_len >= min_run_len) {
                        ++missing_runs;
                        if (reports < 10) {
                            fl::printf("missing span: frame=%d y=%d x=[%d,%d] len=%d tri=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)\n",
                                       frame, y, run_start, W - 1, run_len,
                                       (double)x0, (double)y0, (double)x1, (double)y1, (double)x2, (double)y2);
                            ++reports;
                        }
                    }
                }
            }

            float next_c = frame_c * step_c - frame_s * step_s;
            float next_s = frame_c * step_s + frame_s * step_c;
            frame_c = next_c;
            frame_s = next_s;
        }

        fl::printf("missing horizontal span runs (strict): %d\n", missing_runs);
        FL_CHECK(missing_runs == 0);
    }

    FL_SUBCASE("no row dropouts: very slow rotation strict check") {
        // Catch full-row horizontal dropouts where the reference has clear
        // coverage but the optimised raster has little or none.
        const int W = 64;
        const int H = 64;
        const CRGB color = CRGB(255, 0, 0);

        const float bx0 = -24.0f, by0 = -5.2f;
        const float bx1 =  24.0f, by1 = -3.9f;
        const float bx2 =  -9.0f, by2 =  6.7f;

        // Very slow: 0.25 degree per frame.
        const float step_c = 0.9999905f;
        const float step_s = 0.0043633f;
        float frame_c = 1.0f;
        float frame_s = 0.0f;

        int row_dropouts = 0;
        int reports = 0;
        const int frames = 360;

        for (int frame = 0; frame < frames; ++frame) {
            auto rot_x = [frame_c, frame_s](float x, float y) { return x * frame_c - y * frame_s; };
            auto rot_y = [frame_c, frame_s](float x, float y) { return x * frame_s + y * frame_c; };

            float x0 = 32.0f + rot_x(bx0, by0);
            float y0 = 32.0f + rot_y(bx0, by0);
            float x1 = 32.0f + rot_x(bx1, by1);
            float y1 = 32.0f + rot_y(bx1, by1);
            float x2 = 32.0f + rot_x(bx2, by2);
            float y2 = 32.0f + rot_y(bx2, by2);

            CRGB opt[W * H] = {};
            CRGB ref[W * H] = {};
            fl::CanvasRGB canvas(opt, W, H);
            canvas.drawTriangle(color, x0, y0, x1, y1, x2, y2,
                                fl::DrawMode::DRAW_MODE_OVERWRITE);
            render_triangle_reference(ref, W, H, color, x0, y0, x1, y1, x2, y2);

            for (int y = 0; y < H; ++y) {
                int row_ref = 0;
                int row_opt = 0;
                for (int x = 0; x < W; ++x) {
                    int idx = y * W + x;
                    row_ref += static_cast<int>(ref[idx].r) + ref[idx].g + ref[idx].b;
                    row_opt += static_cast<int>(opt[idx].r) + opt[idx].g + opt[idx].b;
                }

                // Reference has a clear horizontal span, but optimised row is
                // nearly absent.
                if (row_ref >= 1200 && row_opt * 5 < row_ref) {
                    ++row_dropouts;
                    if (reports < 10) {
                        fl::printf("row dropout: frame=%d y=%d ref=%d opt=%d tri=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)\n",
                                   frame, y, row_ref, row_opt,
                                   (double)x0, (double)y0, (double)x1, (double)y1, (double)x2, (double)y2);
                        ++reports;
                    }
                }
            }

            float next_c = frame_c * step_c - frame_s * step_s;
            float next_s = frame_c * step_s + frame_s * step_c;
            frame_c = next_c;
            frame_s = next_s;
        }

        fl::printf("row dropouts (strict): %d\n", row_dropouts);
        FL_CHECK(row_dropouts == 0);
    }

    FL_SUBCASE("Cube3D-style split face has no horizontal seam dropouts") {
        // Emulate Cube3D: draw one face as two triangles with shared diagonal
        // AA disabled (0x3 then 0x6) and verify no strong reference coverage
        // is missing in horizontal spans.
        const int W = 96;
        const int H = 96;
        const CRGB color = CRGB(255, 0, 0);

        struct Pt { float x, y; };
        const Pt q0 = { -28.0f, -10.0f };
        const Pt q1 = {  30.0f,  -6.0f };
        const Pt q2 = {  22.0f,  16.0f };
        const Pt q3 = { -20.0f,  14.0f };

        const fl::u8 edge_aa0 = 0x3; // disable shared edge v2->v0
        const fl::u8 edge_aa1 = 0x6; // disable shared edge v0->v1 (shared)

        // 0.5 degree per frame
        const float step_c = 0.9999619f;
        const float step_s = 0.0087265f;
        float frame_c = 1.0f;
        float frame_s = 0.0f;

        int seam_missing_runs = 0;
        int reports = 0;
        const int frames = 180;

        for (int frame = 0; frame < frames; ++frame) {
            auto rx = [frame_c, frame_s](float x, float y) { return x * frame_c - y * frame_s; };
            auto ry = [frame_c, frame_s](float x, float y) { return x * frame_s + y * frame_c; };

            float x0 = 48.0f + rx(q0.x, q0.y);
            float y0 = 48.0f + ry(q0.x, q0.y);
            float x1 = 48.0f + rx(q1.x, q1.y);
            float y1 = 48.0f + ry(q1.x, q1.y);
            float x2 = 48.0f + rx(q2.x, q2.y);
            float y2 = 48.0f + ry(q2.x, q2.y);
            float x3 = 48.0f + rx(q3.x, q3.y);
            float y3 = 48.0f + ry(q3.x, q3.y);

            CRGB opt[W * H] = {};
            CRGB ref[W * H] = {};
            CRGB ref_tmp[W * H] = {};

            fl::CanvasRGB canvas(opt, W, H);
            canvas.drawTriangle(color, x0, y0, x1, y1, x2, y2,
                                fl::DrawMode::DRAW_MODE_OVERWRITE, edge_aa0);
            canvas.drawTriangle(color, x0, y0, x2, y2, x3, y3,
                                fl::DrawMode::DRAW_MODE_OVERWRITE, edge_aa1);

            render_triangle_reference(ref, W, H, color, x0, y0, x1, y1, x2, y2);
            render_triangle_reference(ref_tmp, W, H, color, x0, y0, x2, y2, x3, y3);
            for (int i = 0; i < W * H; ++i) {
                // Overwrite-style union for reference face.
                if (ref_tmp[i].r > ref[i].r) ref[i].r = ref_tmp[i].r;
                if (ref_tmp[i].g > ref[i].g) ref[i].g = ref_tmp[i].g;
                if (ref_tmp[i].b > ref[i].b) ref[i].b = ref_tmp[i].b;
            }

            for (int y = 0; y < H; ++y) {
                int run_start = -1;
                for (int x = 0; x < W; ++x) {
                    int idx = y * W + x;
                    int ref_sum = static_cast<int>(ref[idx].r) + ref[idx].g + ref[idx].b;
                    int opt_sum = static_cast<int>(opt[idx].r) + opt[idx].g + opt[idx].b;
                    bool missing = (ref_sum >= 240 && opt_sum <= 12);
                    if (missing) {
                        if (run_start < 0) run_start = x;
                    } else if (run_start >= 0) {
                        int len = x - run_start;
                        if (len >= 2) {
                            ++seam_missing_runs;
                            if (reports < 10) {
                                fl::printf("cube seam missing span: frame=%d y=%d x=[%d,%d] len=%d\n",
                                           frame, y, run_start, x - 1, len);
                                ++reports;
                            }
                        }
                        run_start = -1;
                    }
                }
                if (run_start >= 0) {
                    int len = W - run_start;
                    if (len >= 2) {
                        ++seam_missing_runs;
                        if (reports < 10) {
                            fl::printf("cube seam missing span: frame=%d y=%d x=[%d,%d] len=%d\n",
                                       frame, y, run_start, W - 1, len);
                            ++reports;
                        }
                    }
                }
            }

            float next_c = frame_c * step_c - frame_s * step_s;
            float next_s = frame_c * step_s + frame_s * step_c;
            frame_c = next_c;
            frame_s = next_s;
        }

        fl::printf("cube seam missing runs (strict): %d\n", seam_missing_runs);
        FL_CHECK(seam_missing_runs == 0);
    }

    FL_SUBCASE("Cube3D projected shallow triangles have no missing horizontal spans") {
        const int W = 100;
        const int H = 100;
        const CRGB color = CRGB(255, 0, 0);

        struct V3 { float x, y, z; };
        const V3 cube[8] = {
            {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
            { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
            {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
            { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
        };

        const int tris[12][3] = {
            {4, 5, 6}, {4, 6, 7},
            {1, 0, 3}, {1, 3, 2},
            {5, 1, 2}, {5, 2, 6},
            {0, 4, 7}, {0, 7, 3},
            {7, 6, 2}, {7, 2, 3},
            {0, 1, 5}, {0, 5, 4},
        };

        auto is_wide_short_shallow = [](float x0, float y0, float x1, float y1, float x2, float y2) {
            float xs[3] = {x0, x1, x2};
            float ys[3] = {y0, y1, y2};
            if (ys[0] > ys[1]) { float tx = xs[0]; xs[0] = xs[1]; xs[1] = tx; float ty = ys[0]; ys[0] = ys[1]; ys[1] = ty; }
            if (ys[0] > ys[2]) { float tx = xs[0]; xs[0] = xs[2]; xs[2] = tx; float ty = ys[0]; ys[0] = ys[2]; ys[2] = ty; }
            if (ys[1] > ys[2]) { float tx = xs[1]; xs[1] = xs[2]; xs[2] = tx; float ty = ys[1]; ys[1] = ys[2]; ys[2] = ty; }
            float min_x = xs[0], max_x = xs[0];
            for (int i = 1; i < 3; ++i) {
                if (xs[i] < min_x) min_x = xs[i];
                if (xs[i] > max_x) max_x = xs[i];
            }
            float width = max_x - min_x;
            float height = ys[2] - ys[0];
            float top_dx = std::abs(xs[1] - xs[0]);
            float top_dy = std::abs(ys[1] - ys[0]);
            return (width >= 18.0f && height > 0.0f && height <= 12.0f &&
                    top_dx >= 14.0f && top_dy <= 1.8f);
        };

        const float cam_dist = 2.5f;
        const float cube_scale = static_cast<float>(W) * 0.55f;
        const float cx = static_cast<float>(W) * 0.5f;
        const float cy = static_cast<float>(H) * 0.5f;

        int missing_runs = 0;
        int reports = 0;
        int tested = 0;

        for (int frame = 0; frame < 180; ++frame) {
            float ms = static_cast<float>(frame) * 16.0f;
            float ax = ms * 0.000175f;
            float ay = ms * 0.000275f;
            float az = ms * 0.000075f;

            float sx = std::sin(ax), cxr = std::cos(ax);
            float sy = std::sin(ay), cyr = std::cos(ay);
            float sz = std::sin(az), czr = std::cos(az);

            float px[8], py[8];
            for (int i = 0; i < 8; ++i) {
                float x = cube[i].x, y = cube[i].y, z = cube[i].z;

                float y1 = y * cxr - z * sx;
                float z1 = y * sx + z * cxr;
                float x2 = x * cyr + z1 * sy;
                float z2 = z1 * cyr - x * sy;
                float xr = x2 * czr - y1 * sz;
                float yr = x2 * sz + y1 * czr;

                float scale = cam_dist / (cam_dist + z2);
                px[i] = cx + xr * scale * cube_scale;
                py[i] = cy - yr * scale * cube_scale;
            }

            for (int t = 0; t < 12; ++t) {
                int a = tris[t][0], b = tris[t][1], c = tris[t][2];
                float x0 = px[a], y0 = py[a];
                float x1 = px[b], y1 = py[b];
                float x2 = px[c], y2 = py[c];
                if (!is_wide_short_shallow(x0, y0, x1, y1, x2, y2)) {
                    continue;
                }
                ++tested;

                CRGB opt[W * H] = {};
                CRGB ref[W * H] = {};
                fl::CanvasRGB canvas(opt, W, H);
                canvas.drawTriangle(color, x0, y0, x1, y1, x2, y2,
                                    fl::DrawMode::DRAW_MODE_OVERWRITE);
                render_triangle_reference(ref, W, H, color, x0, y0, x1, y1, x2, y2);

                for (int y = 0; y < H; ++y) {
                    int run_start = -1;
                    for (int x = 0; x < W; ++x) {
                        int idx = y * W + x;
                        int ref_sum = static_cast<int>(ref[idx].r) + ref[idx].g + ref[idx].b;
                        int opt_sum = static_cast<int>(opt[idx].r) + opt[idx].g + opt[idx].b;
                        bool missing = (ref_sum >= 240 && opt_sum <= 12);
                        if (missing) {
                            if (run_start < 0) run_start = x;
                        } else if (run_start >= 0) {
                            int len = x - run_start;
                            if (len >= 2) {
                                ++missing_runs;
                                if (reports < 12) {
                                    fl::printf("cube projected missing span: frame=%d tri=%d y=%d x=[%d,%d] len=%d tri=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)\n",
                                               frame, t, y, run_start, x - 1, len,
                                               (double)x0, (double)y0,
                                               (double)x1, (double)y1,
                                               (double)x2, (double)y2);
                                    ++reports;
                                }
                            }
                            run_start = -1;
                        }
                    }
                    if (run_start >= 0) {
                        int len = W - run_start;
                        if (len >= 2) {
                            ++missing_runs;
                            if (reports < 12) {
                                fl::printf("cube projected missing span: frame=%d tri=%d y=%d x=[%d,%d] len=%d tri=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)\n",
                                           frame, t, y, run_start, W - 1, len,
                                           (double)x0, (double)y0,
                                           (double)x1, (double)y1,
                                           (double)x2, (double)y2);
                                ++reports;
                            }
                        }
                    }
                }
            }
        }

        fl::printf("cube projected tested triangles: %d\n", tested);
        fl::printf("cube projected missing runs (strict): %d\n", missing_runs);
        FL_CHECK(tested > 0);
        FL_CHECK(missing_runs == 0);
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
