
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

// ============================================================================
// Byte-fidelity reference for the committed kernel quotient (regression gate).
//
// The per-pixel kernel fl::gfx::detail::polyLineT28 divides its 128-bit
// numerator by its 128-bit denominator with the committed (457b21d1b)
// 94-iteration shift-subtract. That loop has a latent shift-carry bug (it
// folds the high word's MSB into the low word instead of the low word's own
// MSB), and the committed pixel output depends on the bug's deterministic
// degeneracy by den range. The reference below is that committed algorithm
// verbatim: the differential fuzz and the regime pins compare the live
// kernel against it, and it must NOT be "fixed" without regenerating every
// golden value in this file.
// ============================================================================

/// Committed 457b21d1b kernel: trapezoid M build + 94-iteration
/// shift-subtract quotient (byte-for-byte, latent bug included).
static fl::i32 polyTestCommittedT28(fl::i32 B, fl::i32 D, fl::i64 u,
                                    fl::i32 ta, fl::i32 tb) {
    if (B == 0) {
        // Vertical line: x is constant, cov = clamp(u0, 0, 1) * (tb-ta)/65536
        // with u0 = u/D exact (u = D * u0).
        fl::i64 w = u / D;
        if (w < 0) w = 0;
        else if (w > 65536) w = 65536;
        return (fl::i32)(((w * (tb - ta)) + 8) >> 4);
    }
    const fl::i64 S = (B < 0) ? -(fl::i64)B : (fl::i64)B;
    const fl::i64 sa = (fl::i64)B * ta;
    const fl::i64 sb = (fl::i64)B * tb;
    const fl::i64 s_lo = (sa < sb) ? sa : sb;
    const fl::i64 s_hi = (sa < sb) ? sb : sa;
    const fl::i64 c0 = -u;
    const fl::i64 c1 = (fl::i64)2 * D * 32768 - u;
    fl::u64 mhi = 0, mlo = 0;
    const fl::i64 lo = (s_lo > c0) ? s_lo : c0;
    const fl::i64 hi = (s_hi < c1) ? s_hi : c1;
    if (hi > lo)
        fl::gfx::detail::polyMul128((fl::u64)(hi - lo),
                                    (fl::u64)((u + lo) + (u + hi)), mhi, mlo);
    const fl::i64 lo2 = (s_lo > c1) ? s_lo : c1;
    if (s_hi > lo2) {
        fl::u64 phi = 0, plo = 0;
        fl::gfx::detail::polyMul128((fl::u64)(s_hi - lo2),
                                    (fl::u64)((fl::i64)4 * D * 32768), phi, plo);
        fl::gfx::detail::polyAdd128(mhi, mlo, phi, plo);
    }
    // den = 32 * D * S as a u64 pair (D*S < 2^62, so den_hi = DS >> 59).
    const fl::u64 DS = (fl::u64)S * (fl::u64)D;
    const fl::u64 dhi = DS >> 59;
    const fl::u64 dlo = DS << 5;
    // Binary long division; note the committed carry `rh >> 63` into rl
    // (the latent bug — it should be `rl >> 63`).
    fl::u64 q = 0, rh = 0, rl = 0;
    for (int i = 93; i >= 0; --i) {
        const fl::u64 bit = (i >= 64) ? ((mhi >> (i - 64)) & 1u)
                                      : ((mlo >> i) & 1u);
        rl = (rl << 1) | (rh >> 63) | bit;
        rh = rh << 1;
        if (rh > dhi || (rh == dhi && rl >= dlo)) {
            if (i < 64) q |= (fl::u64)1 << i;
            if (rl >= dlo) { rl -= dlo; rh -= dhi; }
            else { rl -= dlo; rh -= dhi + 1; }
        }
    }
    // Round half-up: if 2r >= den, q++.
    fl::u64 rh2 = rh << 1, rl2 = rl << 1;
    if (rl >> 63) ++rh2;
    if (rh2 > dhi || (rh2 == dhi && rl2 >= dlo)) ++q;
    if (q > (fl::u64)fl::gfx::detail::kPolyOne) q = (fl::u64)fl::gfx::detail::kPolyOne;
    return (fl::i32)q;
}

/// Deterministic 64-bit LCG (splitmix-free; same constants as the
/// standalone kernel differential fuzz so regressions map to the same seed).
static fl::u64 polyTestLcg(fl::u64* state) {
    *state = (*state) * 6364136223846793005ull + 1442695040888963407ull;
    return *state >> 1;
}

static fl::i64 polyTestRndSigned(fl::u64* state, unsigned bits) {
    const fl::u64 m = (bits >= 64) ? ~0ull : ((1ull << bits) - 1);
    return (fl::i64)(polyTestLcg(state) & m) -
           (fl::i64)(bits >= 64 ? 0 : 1ull << (bits - 1));
}

/// Render n s16x16 raw-Q16 vertices onto a fresh W*H red canvas (OVERWRITE).
static void polyTestRenderS16(CRGB* buf, const fl::i32* vx, const fl::i32* vy,
                              int n, int W, int H) {
    for (int i = 0; i < W * H; ++i) buf[i] = CRGB(0, 0, 0);
    fl::CanvasRGB canvas(fl::span<CRGB>(buf, W * H), W, H);
    fl::PolyPoint<fl::s16x16> pts[32];
    for (int i = 0; i < 32; ++i)
        pts[i] = {i < n ? fl::s16x16::from_raw(vx[i]) : fl::s16x16::from_raw(0),
                  i < n ? fl::s16x16::from_raw(vy[i]) : fl::s16x16::from_raw(0)};
    canvas.drawPolygon(CRGB::Red,
                       fl::span<const fl::PolyPoint<fl::s16x16>>(pts, n),
                       fl::DrawMode::DRAW_MODE_OVERWRITE);
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

FL_TEST_CASE("drawPolygon kernel: differential vs committed shift-subtract") {
    // The live kernel (fl::gfx::detail::polyLineT28) must stay byte-identical
    // to the committed 457b21d1b shift-subtract kernel (see
    // polyTestCommittedT28) in every den regime. The wide stream below keeps
    // |B|*D < 2^59, so every case lands in dhi == 0 (the 64-bit remainder
    // state machine); it can never reach the other regimes, so each is
    // targeted explicitly afterwards. Deterministic LCG (same seed as the
    // standalone kernel differential fuzz).
    fl::u64 state = 0x9E3779B97F4A7C15ull;
    for (int it = 0; it < 40000; ++it) {
        fl::i32 B, D, ta, tb;
        fl::i64 u;
        const int mode = (int)(polyTestLcg(&state) % 100);
        // B: mostly small, sometimes huge, sometimes 0.
        if (mode < 6) {
            B = 0;
        } else if (mode < 30) {
            B = (fl::i32)polyTestRndSigned(&state, 4) *
                ((fl::i32)(polyTestLcg(&state) % 4) + 1);
        } else if (mode < 50) {
            B = (fl::i32)polyTestRndSigned(&state, 16);
        } else if (mode < 95) {
            B = (fl::i32)polyTestRndSigned(&state, 25);
        } else {
            B = (polyTestLcg(&state) & 1) ? (fl::i32)polyTestRndSigned(&state, 30)
                                          : (fl::i32)(polyTestRndSigned(&state, 30) +
                                                      (polyTestLcg(&state) & 1 ? 1 : -1));
        }
        // D: span height, 1..2^29 — structured + random.
        switch (polyTestLcg(&state) % 8) {
            case 0: D = 1; break;
            case 1: D = (fl::i32)(polyTestLcg(&state) % 65536) + 1; break;
            case 2: D = 65536; break;
            case 3: D = (fl::i32)(polyTestLcg(&state) % 65536) + 65536; break;
            case 4: D = 65537; break;
            case 5: D = (fl::i32)(polyTestLcg(&state) % 262144) + 2; break;
            case 6: D = (fl::i32)polyTestRndSigned(&state, 28) | 1; break;
            default: D = (fl::i32)(polyTestLcg(&state) % (1u << 29)) | 1; break;
        }
        if (D < 1) D = 1;
        // ta < tb within [-32768, 32768] (row band bounds).
        ta = (fl::i32)polyTestRndSigned(&state, 15) * 2;
        tb = (fl::i32)polyTestRndSigned(&state, 15) * 2;
        if (ta > tb) { int t = ta; ta = tb; tb = t; }
        if ((polyTestLcg(&state) & 7) == 0)
            tb = ta + (fl::i32)(polyTestLcg(&state) % 32768) + 1;
        if (tb > 32768) tb = 32768;
        if (ta < -32768) ta = -32768;
        // u: structured around the clamp breakpoints + wide random.
        switch (polyTestLcg(&state) % 8) {
            case 0: u = 0; break;
            case 1: u = (fl::i64)D * polyTestRndSigned(&state, 12); break;
            case 2: u = (fl::i64)2 * D * 32768 + polyTestRndSigned(&state, 10); break;
            case 3: u = polyTestRndSigned(&state, 48); break;
            case 4: u = polyTestRndSigned(&state, 30) * (fl::i64)D; break;
            case 5: u = (fl::i64)D * (polyTestRndSigned(&state, 34) + 1) -
                       polyTestRndSigned(&state, 16); break;
            default: u = polyTestRndSigned(&state, 63); break;
        }
        FL_CHECK_EQ(fl::gfx::detail::polyLineT28(B, D, u, ta, tb),
                    polyTestCommittedT28(B, D, u, ta, tb));
    }
    // Regime-targeted: with D = 2^30, dhi = (|B| * D) >> 59 = |B| >> 29, so
    // the buckets are
    //   dhi == 0: |B| in [1, 2^29)
    //   dhi == 1: |B| in [2^29, 2^30)
    //   dhi == 2: |B| in [2^30, 3*2^29)  (|B| >= 3*2^29 already gives dhi 3)
    // The per-iteration dhi check guards the bucket math itself: a wrong
    // span would silently skip an entire regime class, and dhi >= 2 is the
    // class where the committed loop degenerates (exact quotient up to ~2^26
    // vs committed result 0 or 1).
    for (int target = 0; target <= 2; ++target) {
        int cnt = 0;
        for (int it = 0; it < 5000; ++it) {
            const fl::u64 lo = (target == 0) ? 1 : (fl::u64)1 << (28 + target);
            const fl::u64 span = (target == 0) ? ((fl::u64)1 << 28)
                                               : ((fl::u64)1 << 29);
            const fl::i64 absB = (fl::i64)(lo + (polyTestLcg(&state) % span));
            const fl::i32 B = (polyTestLcg(&state) & 1) ? (fl::i32)absB
                                                        : (fl::i32)(-absB);
            const fl::i32 D = (fl::i32)(1u << 30);
            fl::i32 ta = (fl::i32)polyTestRndSigned(&state, 15) * 2;
            fl::i32 tb = (fl::i32)polyTestRndSigned(&state, 15) * 2;
            if (ta > tb) { int t = ta; ta = tb; tb = t; }
            if (tb > 32768) tb = 32768;
            if (ta < -32768) ta = -32768;
            const fl::i64 u = polyTestRndSigned(&state, 30) * (fl::i64)D;
            const fl::u64 dhi = ((fl::u64)((B < 0) ? -(fl::i64)B : (fl::i64)B) *
                                 (fl::u64)D) >> 59;
            FL_CHECK_EQ((int)dhi, target);
            FL_CHECK_EQ(fl::gfx::detail::polyLineT28(B, D, u, ta, tb),
                        polyTestCommittedT28(B, D, u, ta, tb));
            ++cnt;
        }
        FL_CHECK_EQ(cnt, 5000);
    }
}

FL_TEST_CASE("drawPolygon kernel: division-regime pins") {
    // Hard-coded (B, D, u, ta, tb) -> T pins. dhi = (|B| * D) >> 59 selects
    // the den regime; the dhi >= 2 pins matter most — there the committed
    // loop degenerates to T == 0 while the exact quotient is tens of
    // millions, so any "fix" of the latent division bug breaks them.
    struct Pin { fl::i32 B; fl::i32 D; fl::i64 u; fl::i32 ta; fl::i32 tb; fl::i32 want; };
    static const Pin pins[] = {
        // dhi == 0 (den < 2^64): full 64-bit remainder state machine.
        { 1000, 2000000, 12345678, 0, 32768, 29419 },
        { 1, 16, 0, 0, 32768, 2097152 },  // exact: M/den = 2^30/512 = 2^21
        { -77777, 999999, -55555555, -32768, 20000, 2497228 },
        { 1000000000, 3, 12345, -1000, 1000, 4096000 },
        // dhi == 1 (den in [2^64, 2^65)): T collapses to the round-half-up
        // bit of M mod 2^64; the pair pins both outcomes (exact quotient
        // of the first is 18684565).
        { 536870912, 1073741824, 1000000000000LL, 0, 32768, 1 },
        { -536870912, 1073741824, 1000000000000LL, 0, 32768, 0 },
        // dhi >= 2 (den >= 2^65): committed T == 0 (exact quotients 35461781
        // and 69002661 respectively).
        { 1073741824, 1073741824, 1000000000000LL, 0, 32768, 0 },
        { 2147483647, 1073741824, 1000000000000LL, 0, 32768, 0 },
        // Vertical line (B == 0) branch: partial and full rows.
        { 0, 1000, 500000, 0, 32768, 1024000 },
        { 0, 1000, 1500000, -32768, 32768, 6144000 },
    };
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        const Pin& p = pins[i];
        FL_CHECK_EQ(fl::gfx::detail::polyLineT28(p.B, p.D, p.u, p.ta, p.tb),
                    p.want);
    }
}

FL_TEST_CASE("drawPolygon row extents: floor-div-by-16 shift pins") {
    // floorDiv16I32/I64 (arithmetic >> 16) must equal true floor division
    // by 65536: the per-row extent (lx0/lx1 = (x +/- hw) >> 16) and the
    // py0/py1 row bounds rely on it, and a right-shift that truncates
    // toward zero would silently mis-box negative crossings.
    // Dense around each 65536 boundary within +/- 2^30, then strided
    // (prime step) across the full i32 range.
    for (fl::i32 v = -1073741824; v <= 1073741824; v += 65536) {
        for (int d = -8; d <= 8; ++d) {
            const fl::i32 x = v + d;
            FL_CHECK_EQ(fl::gfx::detail::floorDiv16I32(x),
                        (int)fl::gfx::detail::floorDivI64((fl::i64)x, 65536));
        }
    }
    // i64 loop counter: with an i32 counter the final increment
    // (2147478560 + 9973 > INT32_MAX) overflows before the bound check can
    // fail, and the optimizer turns the loop into an infinite one.
    for (fl::i64 vv = -2147483647; vv < 2147483647; vv += 9973) {
        const fl::i32 v = (fl::i32)vv;
        FL_CHECK_EQ(fl::gfx::detail::floorDiv16I32(v),
                    (int)fl::gfx::detail::floorDivI64((fl::i64)v, 65536));
    }
    // i64: the per-row extent can hold |x| up to ~2^45 for steep short
    // edges, so pin the 2^45 neighborhood and the i64 extremes.
    static const fl::i64 samples[] = {
        0, 1, -1, 65535, 65536, 65537, -65536, -65537,
        ((fl::i64)1 << 45) - 1, ((fl::i64)1 << 45),
        ((fl::i64)1 << 45) + 65535, -((fl::i64)1 << 45) - 65536,
        ((fl::i64)1 << 62), -((fl::i64)1 << 62),
        ((fl::i64)1 << 62) + 65535, -((fl::i64)1 << 62) - 65536,
        9007199254740991LL, -9007199254740992LL,  // 2^53, -2^53
    };
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
        FL_CHECK_EQ(fl::gfx::detail::floorDiv16I64(samples[i]),
                    fl::gfx::detail::floorDivI64(samples[i], 65536));
}

FL_TEST_CASE("drawPolygon steep sub-pixel edge crossing a row boundary") {
    // Regression for the per-row extent of single-row edges (dy < 65536).
    // The 16-vertex shape below has a steep edge (dy = 63904) spanning rows
    // 13..14: v6 (478682, 890361) -> v5 (-385625, 954265). The committed
    // carry is a no-op on such edges, so on row 14 the kernel integrates
    // the infinite line through the row-13 center crossing (x = 554760
    // Q16 ~= 8.47 px) — far outside the segment's own endpoint span. The
    // per-row extent must box THAT line (x +/- hw with hw =
    // ceil(32768*|B|/dy) = 443190, giving [1, 15]); a narrower
    // endpoint-span box hits the full/zero shortcut and erases the
    // committed decaying ramp in row 14, px 8..14.
    const int W = 16, H = 16;
    static const fl::i32 kVx[16] = {
        20529, 917504, 310316, 276136, 943055, -385625, 478682, 262144,
        765395, 97277, 1576, 877780, 685035, -137101, 803606, 976026
    };
    static const fl::i32 kVy[16] = {
        469060, 65536, 960611, 619462, -393974, 954265, 890361, 720896,
        720667, 839351, 175535, -367230, -346634, 26997, 619909, -60080
    };
    static const unsigned char kExpected[256] = {
        255, 255, 249, 134, 10, 0, 0, 35,
        227, 245, 36, 23, 151, 251, 136, 0,
        240, 194, 43, 0, 0, 0, 33, 225,
        255, 124, 123, 239, 172, 144, 71, 0,
        127, 82, 0, 0, 0, 31, 223, 253,
        224, 219, 147, 35, 24, 231, 12, 0,
        219, 255, 177, 29, 29, 221, 186, 48,
        80, 18, 0, 0, 177, 197, 0, 0,
        182, 255, 255, 240, 136, 115, 160, 114,
        0, 0, 0, 95, 255, 132, 0, 0,
        145, 255, 254, 122, 67, 54, 156, 4,
        0, 0, 29, 240, 255, 68, 0, 0,
        103, 199, 11, 0, 0, 6, 205, 138,
        10, 1, 185, 255, 248, 10, 0, 0,
        50, 42, 0, 0, 0, 128, 255, 255,
        222, 176, 255, 255, 193, 0, 0, 0,
        176, 0, 0, 0, 46, 249, 255, 255,
        221, 12, 89, 232, 129, 0, 0, 0,
        251, 6, 0, 0, 180, 255, 255, 253,
        62, 0, 0, 17, 32, 0, 0, 0,
        255, 39, 0, 0, 174, 255, 255, 142,
        1, 1, 1, 1, 0, 0, 0, 0,
        255, 75, 0, 0, 48, 8, 19, 180,
        143, 98, 53, 10, 0, 0, 0, 0,
        255, 143, 95, 140, 63, 56, 66, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        255, 254, 240, 221, 179, 119, 145, 10,
        0, 0, 0, 0, 0, 0, 0, 0,
        143, 143, 143, 143, 108, 137, 143, 142,
        127, 108, 89, 70, 51, 33, 14, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    };
    CRGB buf[256];
    polyTestRenderS16(buf, kVx, kVy, 16, W, H);
    int mismatches = 0;
    for (int i = 0; i < W * H; ++i)
        if (buf[i].r != kExpected[i] || buf[i].g != 0 || buf[i].b != 0)
            ++mismatches;
    FL_CHECK_EQ(mismatches, 0);
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
