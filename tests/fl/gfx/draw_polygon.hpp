
#pragma once

#include "test.h"
#include "fl/gfx/gfx.h"
#include "fl/stl/chrono.h"
#include "fl/math/fixed_point/s16x16.h"

#include <cmath>

// ============================================================================
// drawPolygon test helpers (test-only; the double-precision reference math
// below is independent of the implementation's Q16 integer model)
// ============================================================================

namespace {

/// Even-odd point-in-polygon test (ray cast to the right).
static bool polyTestInPoly(const double* px, const double* py, int n,
                           double x, double y) {
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if ((py[i] > y) != (py[j] > y)) {
            const double xint = px[j] + (y - py[j]) * (px[i] - px[j]) / (py[i] - py[j]);
            if (x < xint) inside = !inside;
        }
    }
    return inside;
}

/// Coverage estimate: even-odd point test on a 32x32 subgrid of the pixel's
/// sample points (reference only). The implementation computes the exact
/// sub-pixel area; the 4 u8 parity tolerance absorbs this reference's own
/// grid error (straight edges vs a finite point grid).
static int polyTestSubsampleCov(const double* px, const double* py, int n,
                                int cx, int cy) {
    const int N = 32;
    int count = 0;
    for (int sy = 0; sy < N; ++sy)
        for (int sx = 0; sx < N; ++sx)
            if (polyTestInPoly(px, py, n, cx + (sx + 0.5) / N,
                               cy + (sy + 0.5) / N))
                ++count;
    const int nn = N * N;
    return (count * 255 + nn / 2) / nn;
}

/// Shoelace (signed) polygon area.
static double polyTestShoelace(const double* px, const double* py, int n) {
    double a = 0;
    for (int i = 0, j = n - 1; i < n; j = i++)
        a += px[j] * py[i] - px[i] * py[j];
    return std::fabs(a) * 0.5;
}

/// Regular star-shaped polygon: radius radii[i] at angle (angle0 + step*i)°.
static void polyTestRegular(int n, double cx, double cy, const double* radii,
                            double angle0, double step, double* px, double* py) {
    for (int i = 0; i < n; ++i) {
        const double a = (angle0 + step * i) * 3.14159265358979323846 / 180.0;
        px[i] = cx + radii[i] * std::cos(a);
        py[i] = cy + radii[i] * std::sin(a);
    }
}

static void polyTestZero(CRGB* buf) {
    for (int i = 0; i < 1024; ++i) buf[i] = CRGB(0, 0, 0);
}

static int polyTestEnergy(const CRGB* buf) {
    int e = 0;
    for (int i = 0; i < 1024; ++i) e += buf[i].r;
    return e;
}

static bool polyTestAllZero(const CRGB* buf) {
    for (int i = 0; i < 1024; ++i)
        if (buf[i].r || buf[i].g || buf[i].b) return false;
    return true;
}

static bool polyTestBuffersEqual(const CRGB* a, const CRGB* b) {
    for (int i = 0; i < 1024; ++i)
        if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b)
            return false;
    return true;
}

/// Render n double-precision vertices onto a fresh 32x32 red canvas.
/// The real count is passed as an explicit span: the array overload would
/// deduce N from the full array size and include the unused tail.
static void polyTestRender(CRGB* buf, const double* px, const double* py,
                           int n, fl::DrawMode mode) {
    polyTestZero(buf);
    fl::CanvasRGB canvas(fl::span<CRGB>(buf, 1024), 32, 32);
    fl::PolyPoint<float> pts[32];
    for (int i = 0; i < 32; ++i)
        pts[i] = {i < n ? static_cast<float>(px[i]) : 0.0f,
                  i < n ? static_cast<float>(py[i]) : 0.0f};
    canvas.drawPolygon(CRGB::Red,
                       fl::span<const fl::PolyPoint<float>>(pts, n), mode);
}

struct PolyTestShape {
    const char* name;
    const double* px;
    const double* py;
    int n;
    bool simple;  // simple (non-self-intersecting): parity + area checks
};

/// The test shapes (double coordinates). The implementation truncates each
/// float vertex to Q16.16; the double reference uses the raw values — the
/// sub-Q16 difference is far below the parity tolerance.
static const PolyTestShape* polyTestShapes() {
    static double pent17[10], star12[20], sq23[8], diamond[8], axissq[8];
    static double hexa12[24], tri[6], shallow[6], vcenter[6], bowtie[8];
    static bool init = false;
    if (!init) {
        static const double r9[5] = {9, 9, 9, 9, 9};
        static const double r1045[10] = {10, 4.5, 10, 4.5, 10, 4.5, 10, 4.5, 10, 4.5};
        static const double r8[4] = {8, 8, 8, 8};
        polyTestRegular(5, 15.5, 15.5, r9, 17, 72, pent17, pent17 + 5);
        polyTestRegular(10, 15.5, 15.5, r1045, 12, 36, star12, star12 + 10);
        polyTestRegular(4, 15.5, 15.5, r8, 23, 90, sq23, sq23 + 4);
        polyTestRegular(4, 15.5, 15.5, r8, 0, 90, diamond, diamond + 4);
        polyTestRegular(4, 15.5, 15.5, r8, 45, 90, axissq, axissq + 4);
        for (int k = 0; k < 12; ++k) {
            const double a = (90 + 30 * k) * 3.14159265358979323846 / 180.0;
            const double rad = (k % 2 == 0) ? 10.0 : 10.0 / 3.0;
            hexa12[k] = 15.5 + rad * std::cos(a);
            hexa12[12 + k] = 15.5 + rad * std::sin(a);
        }
        // Separate halves: (x0, x1, x2, ..., y0, y1, y2, ...)
        tri[0] = 5;   tri[1] = 20;  tri[2] = 10;
        tri[3] = 5;   tri[4] = 8;   tri[5] = 22;
        shallow[0] = 2.0;  shallow[1] = 30.0;  shallow[2] = 16.0;
        shallow[3] = 15.3; shallow[4] = 15.7;  shallow[5] = 17.0;
        vcenter[0] = 15.5; vcenter[1] = 26.5; vcenter[2] = 15.5;
        vcenter[3] = 5.5;  vcenter[4] = 15.5; vcenter[5] = 25.5;
        bowtie[0] = 10.5; bowtie[1] = 25.5;
        bowtie[2] = 25.5; bowtie[3] = 10.5;
        bowtie[4] = 10.5; bowtie[5] = 24.5;
        bowtie[6] = 10.5; bowtie[7] = 24.5;
        init = true;
    }
    static const PolyTestShape list[] = {
        {"pent17", pent17, pent17 + 5, 5, true},
        {"star12", star12, star12 + 10, 10, true},
        {"sq23", sq23, sq23 + 4, 4, true},
        {"diamond", diamond, diamond + 4, 4, true},
        {"axissq", axissq, axissq + 4, 4, true},
        {"hexa12", hexa12, hexa12 + 12, 12, true},
        {"tri", tri, tri + 3, 3, true},
        {"shallow", shallow, shallow + 3, 3, true},
        {"vertex_center", vcenter, vcenter + 3, 3, true},
        // Self-intersecting: even-odd span pairing, structural checks only
        {"bowtie", bowtie, bowtie + 4, 4, false},
        {0, 0, 0, 0, false},
    };
    return list;
}

static const PolyTestShape* polyTestFind(const char* name) {
    for (const PolyTestShape* s = polyTestShapes(); s->name; ++s)
        if (fl::strcmp(s->name, name) == 0) return s;
    return 0;
}

}  // namespace

FL_TEST_FILE(FL_FILEPATH) {

FL_TEST_CASE("drawPolygon basic rendering") {
    FL_SUBCASE("triangle: interior full, exterior black") {
        CRGB buffer[1024];
        const PolyTestShape* tri = polyTestFind("tri");
        polyTestRender(buffer, tri->px, tri->py, tri->n,
                       fl::DrawMode::DRAW_MODE_OVERWRITE);

        // Centroid (11.67, 11.67) is well inside: full red
        FL_CHECK_EQ(buffer[11 * 32 + 11].r, 255);
        FL_CHECK_EQ(buffer[12 * 32 + 12].r, 255);
        // Corners are far outside: black
        FL_CHECK_EQ(buffer[0].r, 0);
        FL_CHECK_EQ(buffer[31].r, 0);
        FL_CHECK_EQ(buffer[31 * 32].r, 0);
        FL_CHECK_EQ(buffer[31 * 32 + 31].r, 0);
    }

    FL_SUBCASE("triangle: antialiased fringe present at edges") {
        CRGB buffer[1024];
        const PolyTestShape* tri = polyTestFind("tri");
        polyTestRender(buffer, tri->px, tri->py, tri->n,
                       fl::DrawMode::DRAW_MODE_OVERWRITE);

        int partials = 0;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                if (buffer[y * 32 + x].r > 0 && buffer[y * 32 + x].r < 255)
                    ++partials;
        FL_CHECK(partials > 0);
    }

    FL_SUBCASE("pentagon: center full, far corners black") {
        CRGB buffer[1024];
        const PolyTestShape* pent = polyTestShapes();
        polyTestRender(buffer, pent->px, pent->py, pent->n,
                       fl::DrawMode::DRAW_MODE_OVERWRITE);
        FL_CHECK_EQ(buffer[15 * 32 + 15].r, 255);
        FL_CHECK_EQ(buffer[0].r, 0);
        FL_CHECK_EQ(buffer[31 * 32 + 31].r, 0);
    }
}

FL_TEST_CASE("drawPolygon per-pixel parity vs subsample reference") {
    // Every simple shape must match the independent 32x32 subsample
    // even-odd reference within 4 u8 per pixel.
    for (const PolyTestShape* s = polyTestShapes(); s->name; ++s) {
        if (!s->simple) continue;
        CRGB buffer[1024];
        polyTestRender(buffer, s->px, s->py, s->n,
                       fl::DrawMode::DRAW_MODE_OVERWRITE);
        int maxd = 0;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                int d = buffer[y * 32 + x].r - polyTestSubsampleCov(s->px, s->py, s->n, x, y);
                if (d < 0) d = -d;
                if (d > maxd) maxd = d;
            }
        fl::printf("  %s: max parity diff %d u8\n", s->name, maxd);
        FL_CHECK_LE(maxd, 4);
    }
}

FL_TEST_CASE("drawPolygon total energy equals shoelace area") {
    // The implementation computes the exact sub-pixel area, so total red
    // energy / 255 must equal the true polygon area up to u8 quantization
    // (<= ~2 px^2 worst case; 1 px^2 catches real bugs).
    for (const PolyTestShape* s = polyTestShapes(); s->name; ++s) {
        if (!s->simple) continue;
        CRGB buffer[1024];
        polyTestRender(buffer, s->px, s->py, s->n,
                       fl::DrawMode::DRAW_MODE_OVERWRITE);
        const double energy = polyTestEnergy(buffer) / 255.0;
        const double area = polyTestShoelace(s->px, s->py, s->n);
        double d = energy - area;
        if (d < 0) d = -d;
        fl::printf("  %s: energy %.3f px^2, area %.3f px^2\n", s->name, energy, area);
        FL_CHECK(d <= 1.0);
    }
}

FL_TEST_CASE("drawPolygon exact pixel values (regression gates)") {
    FL_SUBCASE("integer triangle: energy and pixel values are stable") {
        CRGB buffer[1024];
        const PolyTestShape* tri = polyTestFind("tri");
        polyTestRender(buffer, tri->px, tri->py, tri->n,
                       fl::DrawMode::DRAW_MODE_OVERWRITE);
        const int e = polyTestEnergy(buffer);
        fl::printf("  tri energy: %d\n", e);
        FL_CHECK_EQ(e, 30611);
        FL_CHECK_EQ(buffer[11 * 32 + 11].r, 255);
        FL_CHECK_EQ(buffer[12 * 32 + 12].r, 255);
    }

    FL_SUBCASE("vertex exactly at row center: energy stable") {
        CRGB buffer[1024];
        const PolyTestShape* vc = polyTestFind("vertex_center");
        polyTestRender(buffer, vc->px, vc->py, vc->n,
                       fl::DrawMode::DRAW_MODE_OVERWRITE);
        const int e = polyTestEnergy(buffer);
        fl::printf("  vertex_center energy: %d\n", e);
        FL_CHECK_EQ(e, 28059);
    }

    FL_SUBCASE("12-vertex hexagram: energy and structure stable") {
        CRGB buffer[1024];
        const PolyTestShape* hx = polyTestFind("hexa12");
        polyTestRender(buffer, hx->px, hx->py, hx->n,
                       fl::DrawMode::DRAW_MODE_OVERWRITE);
        const int e = polyTestEnergy(buffer);
        fl::printf("  hexa12 energy: %d\n", e);
        FL_CHECK_EQ(e, 25495);
        // Center is fully covered (no hole at the star's core)
        FL_CHECK_EQ(buffer[15 * 32 + 15].r, 255);
        // AA fringe at the top tip (15.5, 5.5) and at the left inner
        // vertex (12.17, 15.5)
        FL_CHECK_EQ(buffer[5 * 32 + 15].r, 15);
        FL_CHECK_EQ(buffer[6 * 32 + 15].r, 119);
        FL_CHECK_EQ(buffer[15 * 32 + 12].r, 248);
        FL_CHECK_EQ(buffer[0].r, 0);
        FL_CHECK_EQ(buffer[31 * 32 + 31].r, 0);
    }

    FL_SUBCASE("self-intersecting bowtie: even-odd span pairing stable") {
        CRGB buffer[1024];
        const PolyTestShape* bt = polyTestFind("bowtie");
        polyTestRender(buffer, bt->px, bt->py, bt->n,
                       fl::DrawMode::DRAW_MODE_OVERWRITE);
        const int e = polyTestEnergy(buffer);
        fl::printf("  bowtie energy: %d\n", e);
        FL_CHECK_EQ(e, 26918);
        // The two petals are filled; the crossing point (18, 17.5) is a
        // measure-zero X, so the crossing row is full and the lens above
        // and below it stays dark (even-odd: the X center is outside).
        FL_CHECK_EQ(buffer[12 * 32 + 11].r, 255);   // left petal interior
        FL_CHECK_EQ(buffer[12 * 32 + 24].r, 255);   // right petal interior
        FL_CHECK_EQ(buffer[12 * 32 + 12].r, 160);   // petal edge AA
        FL_CHECK_EQ(buffer[17 * 32 + 17].r, 255);   // crossing row
        FL_CHECK_EQ(buffer[15 * 32 + 17].r, 0);     // gap between petals
        FL_CHECK_EQ(buffer[15 * 32 + 18].r, 0);     // gap between petals
        FL_CHECK_EQ(buffer[16 * 32 + 16].r, 211);   // narrowing near X
        FL_CHECK_EQ(buffer[16 * 32 + 17].r, 26);
        FL_CHECK_EQ(buffer[16 * 32 + 18].r, 26);
        FL_CHECK_EQ(buffer[10 * 32 + 10].r, 34);    // petal tip AA
        FL_CHECK_EQ(buffer[10 * 32 + 25].r, 34);    // petal tip AA
        FL_CHECK_EQ(buffer[0].r, 0);
        FL_CHECK_EQ(buffer[31 * 32 + 31].r, 0);
    }

    FL_SUBCASE("far off-canvas vertices: i64 crossing extrapolation stable") {
        CRGB buffer[1024];
        polyTestZero(buffer);
        fl::CanvasRGB canvas(buffer, 32, 32);
        // Vertices at +/-8191 px with a 0.002 px vertical span: the crossing
        // extrapolates to ~+/-4400 px in Q16 (beyond i32-range patterns),
        // stressing the i64 edge state. Exact Q16 vertices (s16x16) keep
        // this gate platform-deterministic.
        const fl::PolyPoint<fl::s16x16> pts[3] = {
            {fl::s16x16::from_raw(-536805376), fl::s16x16::from_raw(327614)},
            {fl::s16x16::from_raw(536805376), fl::s16x16::from_raw(327746)},
            {fl::s16x16::from_raw(0), fl::s16x16::from_raw(655360)},
        };
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);
        const int e = polyTestEnergy(buffer);
        fl::printf("  extreme energy: %d\n", e);
        FL_CHECK_EQ(e, 40720);
    }

    FL_SUBCASE("polygon covering the whole canvas: every pixel full") {
        CRGB buffer[1024];
        const double px[4] = {-5.0, 37.0, 37.0, -5.0};
        const double py[4] = {-5.0, -5.0, 37.0, 37.0};
        polyTestRender(buffer, px, py, 4, fl::DrawMode::DRAW_MODE_OVERWRITE);
        const int e = polyTestEnergy(buffer);
        fl::printf("  whole-canvas energy: %d\n", e);
        FL_CHECK_EQ(e, 255 * 1024);
        FL_CHECK_EQ(buffer[0].r, 255);
        FL_CHECK_EQ(buffer[31].r, 255);
        FL_CHECK_EQ(buffer[31 * 32].r, 255);
        FL_CHECK_EQ(buffer[31 * 32 + 31].r, 255);
    }

    FL_SUBCASE("out-of-range vertices clamp to +/-8191 px") {
        CRGB buffer[1024];
        // (1e6, 2), (-1e6, 2), (0, 16) clamp to (8191, 2), (-8191, 2),
        // (0, 16): a full-canvas-width triangle. Rows 2..15 are fully
        // covered; row 15 also carries the apex AA; rows 0..1, 16..31
        // are black.
        const double px[3] = {1e6, -1e6, 0.0};
        const double py[3] = {2.0, 2.0, 16.0};
        polyTestRender(buffer, px, py, 3, fl::DrawMode::DRAW_MODE_OVERWRITE);
        const int e = polyTestEnergy(buffer);
        fl::printf("  clamp-1e6 energy: %d\n", e);
        FL_CHECK_EQ(e, 114017);
        FL_CHECK_EQ(buffer[15 * 32 + 16].r, 248);  // apex AA at row 15
        FL_CHECK_EQ(buffer[16 * 32 + 16].r, 0);    // below the apex
        FL_CHECK_EQ(buffer[2 * 32].r, 255);
        FL_CHECK_EQ(buffer[2 * 32 + 31].r, 255);
        FL_CHECK_EQ(buffer[1 * 32 + 16].r, 0);     // above the base
    }

    FL_SUBCASE("duplicate vertices render the same polygon") {
        CRGB buffer[1024];
        const double px[5] = {5, 5, 20, 10, 10};
        const double py[5] = {5, 5, 8, 22, 22};
        polyTestRender(buffer, px, py, 5, fl::DrawMode::DRAW_MODE_OVERWRITE);
        const int e = polyTestEnergy(buffer);
        fl::printf("  duplicate-vertex energy: %d\n", e);
        FL_CHECK_EQ(e, 30611);  // identical to the 3-vertex triangle
    }
}

FL_TEST_CASE("drawPolygon degenerate inputs") {
    FL_SUBCASE("fewer than 3 vertices: no-op") {
        CRGB buffer[1024];
        polyTestZero(buffer);
        {
            fl::CanvasRGB canvas(buffer, 32, 32);
            const fl::PolyPoint<float> p1 = {5.0f, 5.0f};
            canvas.drawPolygon(CRGB::Red,
                               fl::span<const fl::PolyPoint<float>>(&p1, 1),
                               fl::DrawMode::DRAW_MODE_OVERWRITE);
        }
        {
            fl::CanvasRGB canvas(buffer, 32, 32);
            const fl::PolyPoint<float> p2[2] = {{5.0f, 5.0f}, {20.0f, 8.0f}};
            canvas.drawPolygon(CRGB::Red, p2, fl::DrawMode::DRAW_MODE_OVERWRITE);
        }
        FL_CHECK(polyTestAllZero(buffer));
    }

    FL_SUBCASE("horizontal-only polygon: no-op") {
        CRGB buffer[1024];
        const double px[3] = {5, 20, 12};
        const double py[3] = {5, 5, 5};
        polyTestRender(buffer, px, py, 3, fl::DrawMode::DRAW_MODE_OVERWRITE);
        FL_CHECK(polyTestAllZero(buffer));
    }

    FL_SUBCASE("fully off-canvas polygon: no pixels") {
        CRGB buffer[1024];
        const double px[3] = {50, 60, 55};
        const double py[3] = {50, 50, 60};
        polyTestRender(buffer, px, py, 3, fl::DrawMode::DRAW_MODE_OVERWRITE);
        FL_CHECK(polyTestAllZero(buffer));
    }
}

FL_TEST_CASE("drawPolygon more than 32 vertices truncates to the first 32") {
    CRGB bufferA[1024], bufferB[1024];
    polyTestZero(bufferA);
    polyTestZero(bufferB);
    // 34-gon: all vertices initialized (the array overload deduces N=34).
    fl::PolyPoint<float> pts[34];
    for (int i = 0; i < 34; ++i) {
        const double a = (5.0 + 360.0 / 33.0 * i) * 3.14159265358979323846 / 180.0;
        pts[i] = {static_cast<float>(15.5 + 9 * std::cos(a)),
                  static_cast<float>(15.5 + 9 * std::sin(a))};
    }
    {
        fl::CanvasRGB canvas(bufferA, 32, 32);
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);
    }
    {
        fl::CanvasRGB canvas(bufferB, 32, 32);
        canvas.drawPolygon(CRGB::Red,
                           fl::span<const fl::PolyPoint<float>>(pts, 32),
                           fl::DrawMode::DRAW_MODE_OVERWRITE);
    }
    FL_CHECK(polyTestBuffersEqual(bufferA, bufferB));
}

FL_TEST_CASE("drawPolygon blend vs overwrite") {
    CRGB bufferA[1024], bufferB[1024], bufferC[1024], bufferD[1024];
    // Pentagon (double coords) shared by all four renders.
    double px[5], py[5];
    {
        const double r[5] = {9, 9, 9, 9, 9};
        polyTestRegular(5, 15.5, 15.5, r, 17, 72, px, py);
    }
    {
        fl::CanvasRGB canvas(bufferA, 32, 32);
        polyTestZero(bufferA);
        fl::PolyPoint<float> pts[5];
        for (int i = 0; i < 5; ++i)
            pts[i] = {static_cast<float>(px[i]), static_cast<float>(py[i])};
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);
    }
    {
        fl::CanvasRGB canvas(bufferB, 32, 32);
        polyTestZero(bufferB);
        fl::PolyPoint<float> pts[5];
        for (int i = 0; i < 5; ++i)
            pts[i] = {static_cast<float>(px[i]), static_cast<float>(py[i])};
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_BLEND);
    }
    {
        // OVERWRITE is idempotent: a second draw replaces, not accumulates.
        fl::CanvasRGB canvas(bufferC, 32, 32);
        polyTestZero(bufferC);
        fl::PolyPoint<float> pts[5];
        for (int i = 0; i < 5; ++i)
            pts[i] = {static_cast<float>(px[i]), static_cast<float>(py[i])};
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);
    }
    {
        // BLEND accumulates: two draws = saturated qadd8(v, v).
        fl::CanvasRGB canvas(bufferD, 32, 32);
        polyTestZero(bufferD);
        fl::PolyPoint<float> pts[5];
        for (int i = 0; i < 5; ++i)
            pts[i] = {static_cast<float>(px[i]), static_cast<float>(py[i])};
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_BLEND);
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_BLEND);
    }
    // One blend on a black canvas is identical to one overwrite.
    FL_CHECK(polyTestBuffersEqual(bufferA, bufferB));
    // Two overwrites leave the first result untouched.
    FL_CHECK(polyTestBuffersEqual(bufferA, bufferC));
    // Two blends double each channel with saturation at 255.
    for (int i = 0; i < 1024; ++i) {
        int expect = bufferA[i].r * 2;
        if (expect > 255) expect = 255;
        FL_CHECK_EQ(bufferD[i].r, expect);
    }
}

FL_TEST_CASE("drawPolygon coordinate type consistency") {
    // The same integer coordinates via int, float, and s16x16 must produce
    // identical buffers (float->Q16 and int->Q16 are exact for integers).
    CRGB bufferI[1024], bufferF[1024], bufferS[1024];
    polyTestZero(bufferI);
    polyTestZero(bufferF);
    polyTestZero(bufferS);
    {
        fl::CanvasRGB canvas(bufferI, 32, 32);
        const fl::PolyPoint<int> pts[3] = {{5, 5}, {20, 8}, {10, 22}};
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);
    }
    {
        fl::CanvasRGB canvas(bufferF, 32, 32);
        const fl::PolyPoint<float> pts[3] = {
            {5.0f, 5.0f}, {20.0f, 8.0f}, {10.0f, 22.0f}};
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);
    }
    {
        fl::CanvasRGB canvas(bufferS, 32, 32);
        const fl::PolyPoint<fl::s16x16> pts[3] = {
            {fl::s16x16(5), fl::s16x16(5)},
            {fl::s16x16(20), fl::s16x16(8)},
            {fl::s16x16(10), fl::s16x16(22)}};
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);
    }
    FL_CHECK(polyTestBuffersEqual(bufferI, bufferF));
    FL_CHECK(polyTestBuffersEqual(bufferI, bufferS));
    FL_CHECK_EQ(polyTestEnergy(bufferI), 30611);
}

FL_TEST_CASE("Graphics performance - drawPolygon") {
    static const fl::u16 kPolyIters = 1000;
    CRGB buffer[1024];
    fl::CanvasRGB canvas(buffer, 32, 32);
    fl::PolyPoint<fl::s16x16> pts[5];
    {
        const double r[5] = {9, 9, 9, 9, 9};
        double px[5], py[5];
        polyTestRegular(5, 15.5, 15.5, r, 17, 72, px, py);
        for (int i = 0; i < 5; ++i)
            pts[i] = {fl::s16x16::from_raw((fl::i32)std::lround(65536.0 * px[i])),
                      fl::s16x16::from_raw((fl::i32)std::lround(65536.0 * py[i]))};
    }
    canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);

    auto start = fl::chrono::steady_clock::now();
    for (fl::u16 i = 0; i < kPolyIters; ++i)
        canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);
    auto end = fl::chrono::steady_clock::now();
    fl::u32 us = (fl::u32)fl::chrono::duration_cast<fl::chrono::microseconds>(end - start).count();
    fl::printf("  drawPolygon(pentagon): %u us total, %u ns/call\n",
               (unsigned)us, (unsigned)((us * 1000) / kPolyIters));
    FL_CHECK(us < 100000);
}

}  // FL_TEST_FILE
