

#include "fl/stl/stdint.h"

#define FASTLED_INTERNAL
#include "fl/fastled.h"

#include "crgb.h"
#include "fl/gfx/blur.h"
#include "fl/gfx/colorutils_misc.h"
#include "fl/stl/compiler_control.h"
#include "fl/system/log.h"
#include "fl/gfx/xymap.h"
#include "lib8tion/scale8.h"
#include "fl/stl/int.h"
#include "fl/stl/span.h"
#include "fl/gfx/crgb.h"
#include "fl/gfx/crgb16.h"
#include "fl/stl/thread_local.h"
#include "fl/stl/vector.h"

// Legacy XY function. This is a weak symbol that can be overridden by the user.
// IMPORTANT: This MUST be in the global namespace (not fl::) for backward compatibility
// with user code from FastLED 3.7.6 that defines: uint16_t XY(uint8_t x, uint8_t y)
fl::u16 XY(fl::u8 x, fl::u8 y) FL_LINK_WEAK;

FL_LINK_WEAK fl::u16 XY(fl::u8 x, fl::u8 y) {
    FASTLED_UNUSED(x);
    FASTLED_UNUSED(y);
    FL_ERROR("XY function not provided - using default [0][0]. Use blur2d with XYMap instead");
    return 0;
}

namespace fl {

// make this a weak symbol
namespace {
fl::u16 xy_legacy_wrapper(fl::u16 x, fl::u16 y, fl::u16 width,
                           fl::u16 height) {
    FASTLED_UNUSED(width);
    FASTLED_UNUSED(height);
    return ::XY(x, y);  // Call global namespace XY
}
} // namespace

namespace gfx {

// blur1d: one-dimensional blur filter. Spreads light to 2 line neighbors.
// blur2d: two-dimensional blur filter. Spreads light to 8 XY neighbors.
//
//           0 = no spread at all
//          64 = moderate spreading
//         172 = maximum smooth, even spreading
//
//         173..255 = wider spreading, but increasing flicker
//
//         Total light is NOT entirely conserved, so many repeated
//         calls to 'blur' will also result in the light fading,
//         eventually all the way to black; this is by design so that
//         it can be used to (slowly) clear the LEDs to black.
void blur1d(fl::span<CRGB> leds, fract8 blur_amount) {
    const fl::u16 numLeds = static_cast<fl::u16>(leds.size());
    fl::u8 keep = 255 - blur_amount;
    fl::u8 seep = blur_amount >> 1;
    CRGB carryover = CRGB::Black;
    for (fl::u16 i = 0; i < numLeds; ++i) {
        CRGB cur = leds[i];
        CRGB part = cur;
        part.nscale8(seep);
        cur.nscale8(keep);
        cur += carryover;
        if (i)
            leds[i - 1] += part;
        leds[i] = cur;
        carryover = part;
    }
}

void blur2d(fl::span<CRGB> leds, fl::u8 width, fl::u8 height,
            fract8 blur_amount, const XYMap &xymap) {
    FASTLED_UNUSED(xymap);
    Canvas<CRGB> canvas(leds, width, height);
    gfx::blur2d(canvas, alpha8(blur_amount));
}

void blur2d(CRGB *leds, fl::u8 width, fl::u8 height, fract8 blur_amount) {
    // Legacy path: uses global XY() via XYMap for user-defined layouts.
    // Keeps its own blur algorithm copy because XYMap indexing differs from
    // the cache-coherent rectangular layout used by Canvas.
    XYMap xyMap =
        XYMap::constructWithUserFunction(width, height, xy_legacy_wrapper);
    fl::u8 keep = 255 - blur_amount;
    fl::u8 seep = blur_amount >> 1;
    // blur rows
    for (fl::u8 row = 0; row < height; ++row) {
        CRGB carryover = CRGB::Black;
        for (fl::u8 i = 0; i < width; ++i) {
            CRGB cur = leds[xyMap.mapToIndex(i, row)];
            CRGB part = cur;
            part.nscale8(seep);
            cur.nscale8(keep);
            cur += carryover;
            if (i)
                leds[xyMap.mapToIndex(i - 1, row)] += part;
            leds[xyMap.mapToIndex(i, row)] = cur;
            carryover = part;
        }
    }
    // blur columns
    for (fl::u8 col = 0; col < width; ++col) {
        CRGB carryover = CRGB::Black;
        for (fl::u8 i = 0; i < height; ++i) {
            CRGB cur = leds[xyMap.mapToIndex(col, i)];
            CRGB part = cur;
            part.nscale8(seep);
            cur.nscale8(keep);
            cur += carryover;
            if (i)
                leds[xyMap.mapToIndex(col, i - 1)] += part;
            leds[xyMap.mapToIndex(col, i)] = cur;
            carryover = part;
        }
    }
}

void blurRows(fl::span<CRGB> leds, fl::u8 width, fl::u8 height,
              fract8 blur_amount, const XYMap &xyMap) {
    FASTLED_UNUSED(xyMap);
    Canvas<CRGB> canvas(leds, width, height);
    gfx::blurRows(canvas, alpha8(blur_amount));
}

void blurColumns(fl::span<CRGB> leds, fl::u8 width, fl::u8 height,
                 fract8 blur_amount, const XYMap &xyMap) {
    FASTLED_UNUSED(xyMap);
    Canvas<CRGB> canvas(leds, width, height);
    gfx::blurColumns(canvas, alpha8(blur_amount));
}

void blurRows(Canvas<CRGB> &canvas, alpha8 blur_amount) {
    const int w = canvas.width;
    const int h = canvas.height;
    CRGB *pixels = canvas.pixels;
    fl::u8 keep = 255 - blur_amount;
    fl::u8 seep = blur_amount >> 1;
    for (int row = 0; row < h; ++row) {
        CRGB carryover = CRGB::Black;
        CRGB *rowBase = pixels + row * w;
        for (int col = 0; col < w; ++col) {
            CRGB cur = rowBase[col];
            CRGB part = cur;
            part.nscale8(seep);
            cur.nscale8(keep);
            cur += carryover;
            if (col)
                rowBase[col - 1] += part;
            rowBase[col] = cur;
            carryover = part;
        }
    }
}

void blurColumns(Canvas<CRGB> &canvas, alpha8 blur_amount) {
    const int w = canvas.width;
    const int h = canvas.height;
    CRGB *pixels = canvas.pixels;
    fl::u8 keep = 255 - blur_amount;
    fl::u8 seep = blur_amount >> 1;
    for (int col = 0; col < w; ++col) {
        CRGB carryover = CRGB::Black;
        for (int row = 0; row < h; ++row) {
            CRGB cur = pixels[row * w + col];
            CRGB part = cur;
            part.nscale8(seep);
            cur.nscale8(keep);
            cur += carryover;
            if (row)
                pixels[(row - 1) * w + col] += part;
            pixels[row * w + col] = cur;
            carryover = part;
        }
    }
}

void blur2d(Canvas<CRGB> &canvas, alpha8 blur_amount) {
    gfx::blurRows(canvas, blur_amount);
    gfx::blurColumns(canvas, blur_amount);
}

} // namespace gfx
} // namespace fl

// ── Separable Gaussian blur — two-pass binomial convolution ──────────
//
// Based on sutaburosu's SKIPSM Gaussian blur implementation.
// Uses binomial coefficient weights (Pascal's triangle) for a fast and
// flexible Gaussian blur approximation, optimized for kernel sizes up to 9×9.
// https://people.videolan.org/~tmatth/papers/Gaussian%20blur%20using%20finite-state%20machines.pdf
//
// Two separable passes (horizontal then vertical), each applying the 1D
// binomial kernel for the given radius:
//   radius 0: [1]                                    sum = 1    shift = 0
//   radius 1: [1, 2, 1]                              sum = 4    shift = 2
//   radius 2: [1, 4, 6, 4, 1]                        sum = 16   shift = 4
//   radius 3: [1, 6, 15, 20, 15, 6, 1]               sum = 64   shift = 6
//   radius 4: [1, 8, 28, 56, 70, 56, 28, 8, 1]       sum = 256  shift = 8
//
// Out-of-bounds pixels are treated as zero (zero-padding).
// Normalization by right-shift of 2*radius bits per pass.

namespace fl {
namespace gfx {

namespace blur_detail {

// Identity alpha value for each type (no dimming).
template <typename AlphaT> constexpr AlphaT alpha_identity();
template <> constexpr alpha8 alpha_identity<alpha8>() { return alpha8(255); }
template <> constexpr alpha16 alpha_identity<alpha16>() { return alpha16(65535); }

// Channel extraction and alpha-scaled pixel construction.
template <typename RGB_T>
struct pixel_ops;

template <>
struct pixel_ops<CRGB> {
    FL_ALWAYS_INLINE u16 ch(u8 v) { return v; }
    FL_ALWAYS_INLINE CRGB zero() { return CRGB(0, 0, 0); }

    FL_ALWAYS_INLINE CRGB make(u16 r, u16 g, u16 b) {
        return CRGB(static_cast<u8>(r), static_cast<u8>(g),
                    static_cast<u8>(b));
    }

    FL_ALWAYS_INLINE CRGB make(u16 r, u16 g, u16 b, alpha8 a) {
        if (a.value == 255) return make(r, g, b);
        u16 a1 = static_cast<u16>(a.value) + 1;
        return CRGB(static_cast<u8>((r * a1) >> 8),
                    static_cast<u8>((g * a1) >> 8),
                    static_cast<u8>((b * a1) >> 8));
    }

    FL_ALWAYS_INLINE CRGB make(u16 r, u16 g, u16 b, alpha16 a) {
        if (a.value >= 65535) return make(r, g, b);
        u32 a1 = static_cast<u32>(a.value) + 1;
        return CRGB(static_cast<u8>((r * a1) >> 16),
                    static_cast<u8>((g * a1) >> 16),
                    static_cast<u8>((b * a1) >> 16));
    }
};

template <>
struct pixel_ops<CRGB16> {
    FL_ALWAYS_INLINE u32 ch(u8x8 v) { return v.raw(); }
    FL_ALWAYS_INLINE CRGB16 zero() { return CRGB16(u8x8(0), u8x8(0), u8x8(0)); }

    FL_ALWAYS_INLINE CRGB16 make(u32 r, u32 g, u32 b) {
        return CRGB16(u8x8::from_raw(static_cast<u16>(r)),
                      u8x8::from_raw(static_cast<u16>(g)),
                      u8x8::from_raw(static_cast<u16>(b)));
    }

    FL_ALWAYS_INLINE CRGB16 make(u32 r, u32 g, u32 b, alpha8 a) {
        if (a.value == 255) return make(r, g, b);
        u32 a1 = static_cast<u32>(a.value) + 1;
        return make((r * a1) >> 8, (g * a1) >> 8, (b * a1) >> 8);
    }

    FL_ALWAYS_INLINE CRGB16 make(u32 r, u32 g, u32 b, alpha16 a) {
        if (a.value >= 65535) return make(r, g, b);
        u32 a1 = static_cast<u32>(a.value) + 1;
        return make((r * a1) >> 16, (g * a1) >> 16, (b * a1) >> 16);
    }
};

// Thread-local padded pixel buffer for zero-padding approach.
template <typename RGB_T>
static RGB_T *get_padbuf(int minSize) {
    static fl::ThreadLocal<fl::vector<RGB_T>> tl_padbuf;
    fl::vector<RGB_T> &buf = tl_padbuf.access();
    if (static_cast<int>(buf.size()) < minSize) {
        buf.resize(minSize);
    }
    return buf.data();
}

// Interior row pixel — fully-unrolled, no bounds checks.
// Also reused for vertical pass via linearized column data.
// Template-specialized per radius for direct hardcoded weights.
template <int R, typename RGB_T, typename acc_t>
struct interior_row;

template <typename RGB_T, typename acc_t>
struct interior_row<0, RGB_T, acc_t> {
    FL_ALWAYS_INLINE void apply(const RGB_T *row, int x,
                             acc_t &r, acc_t &g, acc_t &b) {
        using P = pixel_ops<RGB_T>;
        r = P::ch(row[x].r); g = P::ch(row[x].g); b = P::ch(row[x].b);
    }
};

template <typename RGB_T, typename acc_t>
struct interior_row<1, RGB_T, acc_t> {
    FL_ALWAYS_INLINE void apply(const RGB_T *row, int x,
                             acc_t &r, acc_t &g, acc_t &b) {
        using P = pixel_ops<RGB_T>;
        P p;
        r = 0; g = 0; b = 0;
        { const RGB_T &px = row[x-1]; r += p.ch(px.r);     g += p.ch(px.g);     b += p.ch(px.b); }
        { const RGB_T &px = row[x];   r += p.ch(px.r) * 2; g += p.ch(px.g) * 2; b += p.ch(px.b) * 2; }
        { const RGB_T &px = row[x+1]; r += p.ch(px.r);     g += p.ch(px.g);     b += p.ch(px.b); }
    }
};

template <typename RGB_T, typename acc_t>
struct interior_row<2, RGB_T, acc_t> {
    FL_ALWAYS_INLINE void apply(const RGB_T *row, int x,
                             acc_t &r, acc_t &g, acc_t &b) {
        using P = pixel_ops<RGB_T>;
        P p;
        // [1, 4, 6, 4, 1] — symmetric: (e0+e4) + 4*(e1+e3) + 6*e2
        const RGB_T &pm2 = row[x-2], &pm1 = row[x-1], &pc = row[x], &pp1 = row[x+1], &pp2 = row[x+2];
        const acc_t s04r = p.ch(pm2.r) + p.ch(pp2.r), s13r = p.ch(pm1.r) + p.ch(pp1.r);
        const acc_t s04g = p.ch(pm2.g) + p.ch(pp2.g), s13g = p.ch(pm1.g) + p.ch(pp1.g);
        const acc_t s04b = p.ch(pm2.b) + p.ch(pp2.b), s13b = p.ch(pm1.b) + p.ch(pp1.b);
        r = s04r + s13r * 4 + p.ch(pc.r) * 6;
        g = s04g + s13g * 4 + p.ch(pc.g) * 6;
        b = s04b + s13b * 4 + p.ch(pc.b) * 6;
    }
};

template <typename RGB_T, typename acc_t>
struct interior_row<3, RGB_T, acc_t> {
    FL_ALWAYS_INLINE void apply(const RGB_T *row, int x,
                             acc_t &r, acc_t &g, acc_t &b) {
        using P = pixel_ops<RGB_T>;
        P p;
        // [1, 6, 15, 20, 15, 6, 1] — symmetric: (e0+e6) + 6*(e1+e5) + 15*(e2+e4) + 20*e3
        const RGB_T &pm3 = row[x-3], &pm2 = row[x-2], &pm1 = row[x-1], &pc = row[x];
        const RGB_T &pp1 = row[x+1], &pp2 = row[x+2], &pp3 = row[x+3];
        const acc_t s06r = p.ch(pm3.r) + p.ch(pp3.r), s15r = p.ch(pm2.r) + p.ch(pp2.r), s24r = p.ch(pm1.r) + p.ch(pp1.r);
        const acc_t s06g = p.ch(pm3.g) + p.ch(pp3.g), s15g = p.ch(pm2.g) + p.ch(pp2.g), s24g = p.ch(pm1.g) + p.ch(pp1.g);
        const acc_t s06b = p.ch(pm3.b) + p.ch(pp3.b), s15b = p.ch(pm2.b) + p.ch(pp2.b), s24b = p.ch(pm1.b) + p.ch(pp1.b);
        r = s06r + s15r * 6 + s24r * 15 + p.ch(pc.r) * 20;
        g = s06g + s15g * 6 + s24g * 15 + p.ch(pc.g) * 20;
        b = s06b + s15b * 6 + s24b * 15 + p.ch(pc.b) * 20;
    }
};

template <typename RGB_T, typename acc_t>
struct interior_row<4, RGB_T, acc_t> {
    FL_ALWAYS_INLINE void apply(const RGB_T *row, int x,
                             acc_t &r, acc_t &g, acc_t &b) {
        using P = pixel_ops<RGB_T>;
        P p;
        // [1, 8, 28, 56, 70, 56, 28, 8, 1] — symmetric: (e0+e8) + 8*(e1+e7) + 28*(e2+e6) + 56*(e3+e5) + 70*e4
        const RGB_T &pm4 = row[x-4], &pm3 = row[x-3], &pm2 = row[x-2], &pm1 = row[x-1], &pc = row[x];
        const RGB_T &pp1 = row[x+1], &pp2 = row[x+2], &pp3 = row[x+3], &pp4 = row[x+4];
        const acc_t s08r = p.ch(pm4.r) + p.ch(pp4.r), s17r = p.ch(pm3.r) + p.ch(pp3.r);
        const acc_t s26r = p.ch(pm2.r) + p.ch(pp2.r), s35r = p.ch(pm1.r) + p.ch(pp1.r);
        const acc_t s08g = p.ch(pm4.g) + p.ch(pp4.g), s17g = p.ch(pm3.g) + p.ch(pp3.g);
        const acc_t s26g = p.ch(pm2.g) + p.ch(pp2.g), s35g = p.ch(pm1.g) + p.ch(pp1.g);
        const acc_t s08b = p.ch(pm4.b) + p.ch(pp4.b), s17b = p.ch(pm3.b) + p.ch(pp3.b);
        const acc_t s26b = p.ch(pm2.b) + p.ch(pp2.b), s35b = p.ch(pm1.b) + p.ch(pp1.b);
        r = s08r + s17r * 8 + s26r * 28 + s35r * 56 + p.ch(pc.r) * 70;
        g = s08g + s17g * 8 + s26g * 28 + s35g * 56 + p.ch(pc.g) * 70;
        b = s08b + s17b * 8 + s26b * 28 + s35b * 56 + p.ch(pc.b) * 70;
    }
};

// ── AVR per-channel convolution kernels ──────────────────────────────
// On AVR, processing one color channel at a time cuts live accumulator
// registers from 6 (r,g,b as u16 pairs) to 2, dramatically reducing
// register spilling in the 32-register AVR architecture.
//
// conv1ch<R>::apply(center): computes the 1D binomial kernel for a
// single u8 channel, where `center` points to the center pixel's
// channel byte and the pixel stride is sizeof(CRGB) = 3 (compile-time
// constant).
#if defined(FL_IS_AVR)

template <int R> struct conv1ch;

// All conv1ch specializations use positive-only offsets from the window
// start (p = center - R*S). This maps cleanly to AVR's LDD instruction
// which supports displacement 0-63.  Max offset = 2*R*S = 24 for R4.
template <> struct conv1ch<0> {
    static inline u16 __attribute__((always_inline)) apply(const u8 *c) {
        return (u16)c[0];
    }
};

template <> struct conv1ch<1> {
    static inline u16 __attribute__((always_inline)) apply(const u8 *c) {
        constexpr int S = sizeof(CRGB); // 3
        const u8 *p = c - S;
        // [64, 128, 64] (rescaled from [1,2,1] so sum=256, shift-by-8 is free)
        return (u16)p[0] * 64 + (u16)p[S] * 128 + (u16)p[2*S] * 64;
    }
};

template <> struct conv1ch<2> {
    static inline u16 __attribute__((always_inline)) apply(const u8 *c) {
        constexpr int S = sizeof(CRGB);
        const u8 *p = c - 2*S;
        // [16, 64, 96, 64, 16] (rescaled from [1,4,6,4,1] so sum=256, shift-by-8 is free)
        return (u16)p[0] * 16 + (u16)p[S] * 64 + (u16)p[2*S] * 96
             + (u16)p[3*S] * 64 + (u16)p[4*S] * 16;
    }
};

template <> struct conv1ch<3> {
    static inline u16 __attribute__((always_inline)) apply(const u8 *c) {
        constexpr int S = sizeof(CRGB);
        const u8 *p = c - 3*S;
        // [4, 24, 60, 80, 60, 24, 4] (rescaled from [1,6,15,20,15,6,1] so sum=256, shift-by-8 is free)
        return (u16)p[0] * 4 + (u16)p[S] * 24  + (u16)p[2*S] * 60
             + (u16)p[3*S] * 80 + (u16)p[4*S] * 60 + (u16)p[5*S] * 24
             + (u16)p[6*S] * 4;
    }
};

template <> struct conv1ch<4> {
    static inline u16 __attribute__((always_inline)) apply(const u8 *c) {
        constexpr int S = sizeof(CRGB);
        const u8 *p = c - 4*S;
        // [1, 8, 28, 56, 70, 56, 28, 8, 1]
        return (u16)p[0] + (u16)p[S] * 8  + (u16)p[2*S] * 28
             + (u16)p[3*S] * 56 + (u16)p[4*S] * 70 + (u16)p[5*S] * 56
             + (u16)p[6*S] * 28 + (u16)p[7*S] * 8 + (u16)p[8*S];
    }
};

// AVR noinline per-channel pass for CRGB (no alpha).
template <int R>
__attribute__((noinline)) FL_OPTIMIZE_FUNCTION
static void apply_pass_1ch(const CRGB *pad, CRGB *out, int count, int stride) {
    constexpr int shift = (R == 0) ? 0 : 8;
    for (int i = 0; i < count; ++i) {
        const u8 *base = pad[R + i].raw;
        out->r = static_cast<u8>(conv1ch<R>::apply(base + 0) >> shift);
        out->g = static_cast<u8>(conv1ch<R>::apply(base + 1) >> shift);
        out->b = static_cast<u8>(conv1ch<R>::apply(base + 2) >> shift);
        out += stride;
    }
}

// AVR noinline per-channel pass for CRGB with alpha dim.
template <int R>
__attribute__((noinline)) FL_OPTIMIZE_FUNCTION
static void apply_pass_alpha_1ch(const CRGB *pad, CRGB *out, int count,
                                  int stride, alpha8 alpha) {
    constexpr int shift = (R == 0) ? 0 : 8;
    u16 a1 = static_cast<u16>(alpha.value) + 1;
    for (int i = 0; i < count; ++i) {
        const u8 *base = pad[R + i].raw;
        u16 r = conv1ch<R>::apply(base + 0) >> shift;
        u16 g = conv1ch<R>::apply(base + 1) >> shift;
        u16 b = conv1ch<R>::apply(base + 2) >> shift;
        out->r = static_cast<u8>((r * a1) >> 8);
        out->g = static_cast<u8>((g * a1) >> 8);
        out->b = static_cast<u8>((b * a1) >> 8);
        out += stride;
    }
}

// AVR noinline per-channel pass for CRGB with alpha16 dim.
template <int R>
__attribute__((noinline)) FL_OPTIMIZE_FUNCTION
static void apply_pass_alpha_1ch(const CRGB *pad, CRGB *out, int count,
                                  int stride, alpha16 alpha) {
    constexpr int shift = (R == 0) ? 0 : 8;
    u32 a1 = static_cast<u32>(alpha.value) + 1;
    for (int i = 0; i < count; ++i) {
        const u8 *base = pad[R + i].raw;
        u16 r = conv1ch<R>::apply(base + 0) >> shift;
        u16 g = conv1ch<R>::apply(base + 1) >> shift;
        u16 b = conv1ch<R>::apply(base + 2) >> shift;
        out->r = static_cast<u8>((r * a1) >> 16);
        out->g = static_cast<u8>((g * a1) >> 16);
        out->b = static_cast<u8>((b * a1) >> 16);
        out += stride;
    }
}

#endif // FL_IS_AVR

// Row-level kernel application — noinline on AVR to isolate register pressure.
// On non-AVR platforms (or CRGB16 on AVR), processes all 3 channels
// simultaneously using the interior_row kernel.
template <int R, typename RGB_T, typename acc_t>
FL_NO_INLINE_IF_AVR FL_OPTIMIZE_FUNCTION
static void apply_pass(const RGB_T *pad, RGB_T *out, int count, int stride) {
    constexpr int shift = 2 * R;
    using P = pixel_ops<RGB_T>;
    for (int i = 0; i < count; ++i) {
        acc_t r, g, b;
        interior_row<R, RGB_T, acc_t>::apply(pad, R + i, r, g, b);
        *out = P::make(static_cast<acc_t>(r >> shift),
                       static_cast<acc_t>(g >> shift),
                       static_cast<acc_t>(b >> shift));
        out += stride;
    }
}

template <int R, typename RGB_T, typename acc_t, typename AlphaT>
FL_NO_INLINE_IF_AVR FL_OPTIMIZE_FUNCTION
static void apply_pass_alpha(const RGB_T *pad, RGB_T *out, int count,
                             int stride, AlphaT alpha) {
    constexpr int shift = 2 * R;
    using P = pixel_ops<RGB_T>;
    for (int i = 0; i < count; ++i) {
        acc_t r, g, b;
        interior_row<R, RGB_T, acc_t>::apply(pad, R + i, r, g, b);
        *out = P::make(static_cast<acc_t>(r >> shift),
                       static_cast<acc_t>(g >> shift),
                       static_cast<acc_t>(b >> shift), alpha);
        out += stride;
    }
}


// ── Row-major vertical pass (non-AVR) ──────────────────────────────────
// Processes vertical convolution in row-major order for cache efficiency.
// Instead of gathering individual columns into a linear buffer (strided
// reads + writes), iterates row by row with sequential memory access.
// Uses a ring buffer of R+1 saved rows to hold originals of overwritten rows.
// scratch must have at least (R+2)*w elements.
#if !defined(FL_IS_AVR)

template <int R, typename RGB_T, typename acc_t, bool ApplyAlpha, typename AlphaT>
FL_OPTIMIZE_FUNCTION
static void vpass_rowmajor_impl(
    RGB_T *pixels, int w, int h,
    RGB_T *scratch, AlphaT alpha)
{
    constexpr int shift = 2 * R;
    using P = pixel_ops<RGB_T>;

    // Ring buffer: bufs[0..R-1] = saved previous rows, bufs[R] = save slot.
    // Extra zero_row for bottom-boundary padding.
    RGB_T *bufs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i <= R; ++i)
        bufs[i] = scratch + i * w;
    RGB_T *zero_row = scratch + (R + 1) * w;

    // Zero all: first R buffers (top-boundary padding) + zero_row.
    __builtin_memset(scratch, 0, (R + 2) * w * sizeof(RGB_T));

    for (int y = 0; y < h; ++y) {
        RGB_T *out_row = pixels + y * w;

        // Save current row before we overwrite it.
        FL_BUILTIN_MEMCPY(bufs[R], out_row, w * sizeof(RGB_T));

        // Forward row pointers (rows y+1 .. y+R, or zero_row if OOB).
        const RGB_T *fwd[4] = {zero_row, zero_row, zero_row, zero_row};
        for (int k = 0; k < R; ++k)
            fwd[k] = (y + 1 + k < h) ? (pixels + (y + 1 + k) * w) : zero_row;

        // Process all pixels in this output row.
        // For u8-channel types (CRGB), process as raw byte stream — all
        // channels use the same kernel weights, so we treat the row as a
        // flat u8 array of w*sizeof(RGB_T) bytes. This produces a simpler
        // loop that the compiler can optimize better at low -O levels.
        if (sizeof(typename RGB_T::fp) == 1 && !ApplyAlpha) {
            // Raw byte fast path (CRGB without alpha).
            const int nbytes = w * (int)sizeof(RGB_T);
            const u8 *b0 = (const u8 *)bufs[0];
            const u8 *bc = (const u8 *)bufs[R]; // center
            u8 *ob = (u8 *)out_row;

            if (R == 1) {
                const u8 *f0 = (const u8 *)fwd[0];
                for (int i = 0; i < nbytes; ++i)
                    ob[i] = (u8)(((u16)b0[i] + ((u16)bc[i] << 1) + (u16)f0[i]) >> 2);
            } else if (R == 2) {
                const u8 *b1 = (const u8 *)bufs[1];
                const u8 *f0 = (const u8 *)fwd[0];
                const u8 *f1 = (const u8 *)fwd[1];
                for (int i = 0; i < nbytes; ++i) {
                    u16 s04 = (u16)b0[i] + (u16)f1[i];
                    u16 s13 = (u16)b1[i] + (u16)f0[i];
                    ob[i] = (u8)((s04 + s13 * 4 + (u16)bc[i] * 6) >> 4);
                }
            } else if (R == 3) {
                const u8 *b1 = (const u8 *)bufs[1];
                const u8 *b2 = (const u8 *)bufs[2];
                const u8 *f0 = (const u8 *)fwd[0];
                const u8 *f1 = (const u8 *)fwd[1];
                const u8 *f2 = (const u8 *)fwd[2];
                for (int i = 0; i < nbytes; ++i) {
                    u16 s06 = (u16)b0[i] + (u16)f2[i];
                    u16 s15 = (u16)b1[i] + (u16)f1[i];
                    u16 s24 = (u16)b2[i] + (u16)f0[i];
                    ob[i] = (u8)((s06 + s15 * 6 + s24 * 15 + (u16)bc[i] * 20) >> 6);
                }
            } else { // R == 4
                const u8 *b1 = (const u8 *)bufs[1];
                const u8 *b2 = (const u8 *)bufs[2];
                const u8 *b3 = (const u8 *)bufs[3];
                const u8 *f0 = (const u8 *)fwd[0];
                const u8 *f1 = (const u8 *)fwd[1];
                const u8 *f2 = (const u8 *)fwd[2];
                const u8 *f3 = (const u8 *)fwd[3];
                for (int i = 0; i < nbytes; ++i) {
                    u16 s08 = (u16)b0[i] + (u16)f3[i];
                    u16 s17 = (u16)b1[i] + (u16)f2[i];
                    u16 s26 = (u16)b2[i] + (u16)f1[i];
                    u16 s35 = (u16)b3[i] + (u16)f0[i];
                    ob[i] = (u8)((s08 + s17 * 8 + s26 * 28 + s35 * 56 + (u16)bc[i] * 70) >> 8);
                }
            }
        } else {
            // Generic path: per-pixel struct access (CRGB16 or alpha case).
            for (int x = 0; x < w; ++x) {
                acc_t r, g, b;

                if (R == 1) {
                    r = (P::ch(bufs[0][x].r) + P::ch(fwd[0][x].r)) + (P::ch(bufs[1][x].r) << 1);
                    g = (P::ch(bufs[0][x].g) + P::ch(fwd[0][x].g)) + (P::ch(bufs[1][x].g) << 1);
                    b = (P::ch(bufs[0][x].b) + P::ch(fwd[0][x].b)) + (P::ch(bufs[1][x].b) << 1);
                } else if (R == 2) {
                    const acc_t sr04 = P::ch(bufs[0][x].r) + P::ch(fwd[1][x].r);
                    const acc_t sg04 = P::ch(bufs[0][x].g) + P::ch(fwd[1][x].g);
                    const acc_t sb04 = P::ch(bufs[0][x].b) + P::ch(fwd[1][x].b);
                    const acc_t sr13 = P::ch(bufs[1][x].r) + P::ch(fwd[0][x].r);
                    const acc_t sg13 = P::ch(bufs[1][x].g) + P::ch(fwd[0][x].g);
                    const acc_t sb13 = P::ch(bufs[1][x].b) + P::ch(fwd[0][x].b);
                    r = sr04 + sr13 * 4 + P::ch(bufs[2][x].r) * 6;
                    g = sg04 + sg13 * 4 + P::ch(bufs[2][x].g) * 6;
                    b = sb04 + sb13 * 4 + P::ch(bufs[2][x].b) * 6;
                } else if (R == 3) {
                    const acc_t sr06 = P::ch(bufs[0][x].r) + P::ch(fwd[2][x].r);
                    const acc_t sg06 = P::ch(bufs[0][x].g) + P::ch(fwd[2][x].g);
                    const acc_t sb06 = P::ch(bufs[0][x].b) + P::ch(fwd[2][x].b);
                    const acc_t sr15 = P::ch(bufs[1][x].r) + P::ch(fwd[1][x].r);
                    const acc_t sg15 = P::ch(bufs[1][x].g) + P::ch(fwd[1][x].g);
                    const acc_t sb15 = P::ch(bufs[1][x].b) + P::ch(fwd[1][x].b);
                    const acc_t sr24 = P::ch(bufs[2][x].r) + P::ch(fwd[0][x].r);
                    const acc_t sg24 = P::ch(bufs[2][x].g) + P::ch(fwd[0][x].g);
                    const acc_t sb24 = P::ch(bufs[2][x].b) + P::ch(fwd[0][x].b);
                    r = sr06 + sr15 * 6 + sr24 * 15 + P::ch(bufs[3][x].r) * 20;
                    g = sg06 + sg15 * 6 + sg24 * 15 + P::ch(bufs[3][x].g) * 20;
                    b = sb06 + sb15 * 6 + sb24 * 15 + P::ch(bufs[3][x].b) * 20;
                } else { // R == 4
                    const acc_t sr08 = P::ch(bufs[0][x].r) + P::ch(fwd[3][x].r);
                    const acc_t sg08 = P::ch(bufs[0][x].g) + P::ch(fwd[3][x].g);
                    const acc_t sb08 = P::ch(bufs[0][x].b) + P::ch(fwd[3][x].b);
                    const acc_t sr17 = P::ch(bufs[1][x].r) + P::ch(fwd[2][x].r);
                    const acc_t sg17 = P::ch(bufs[1][x].g) + P::ch(fwd[2][x].g);
                    const acc_t sb17 = P::ch(bufs[1][x].b) + P::ch(fwd[2][x].b);
                    const acc_t sr26 = P::ch(bufs[2][x].r) + P::ch(fwd[1][x].r);
                    const acc_t sg26 = P::ch(bufs[2][x].g) + P::ch(fwd[1][x].g);
                    const acc_t sb26 = P::ch(bufs[2][x].b) + P::ch(fwd[1][x].b);
                    const acc_t sr35 = P::ch(bufs[3][x].r) + P::ch(fwd[0][x].r);
                    const acc_t sg35 = P::ch(bufs[3][x].g) + P::ch(fwd[0][x].g);
                    const acc_t sb35 = P::ch(bufs[3][x].b) + P::ch(fwd[0][x].b);
                    r = sr08 + sr17 * 8 + sr26 * 28 + sr35 * 56 + P::ch(bufs[4][x].r) * 70;
                    g = sg08 + sg17 * 8 + sg26 * 28 + sg35 * 56 + P::ch(bufs[4][x].g) * 70;
                    b = sb08 + sb17 * 8 + sb26 * 28 + sb35 * 56 + P::ch(bufs[4][x].b) * 70;
                }

                if (ApplyAlpha) {
                    out_row[x] = P::make(static_cast<acc_t>(r >> shift),
                                         static_cast<acc_t>(g >> shift),
                                         static_cast<acc_t>(b >> shift), alpha);
                } else {
                    out_row[x] = P::make(static_cast<acc_t>(r >> shift),
                                         static_cast<acc_t>(g >> shift),
                                         static_cast<acc_t>(b >> shift));
                }
            }
        }

        // Rotate ring buffer: discard oldest, current becomes newest saved.
        RGB_T *recycled = bufs[0];
        for (int i = 0; i < R; ++i) bufs[i] = bufs[i + 1];
        bufs[R] = recycled;
    }
}

#endif // !FL_IS_AVR

} // namespace blur_detail

// Separable Gaussian blur: horizontal pass then vertical pass.
// Zero-padding approach: copy row/column to a padded buffer with zeros,
// then apply the fast unrolled interior_row kernel to ALL positions.
// This eliminates slow edge handling and reuses interior_row for both passes.
// Dim (alpha) is applied once at the final output.
template <int hRadius, int vRadius, typename RGB_T, typename AlphaT>
FL_OPTIMIZE_FUNCTION
void blurGaussianImpl(Canvas<RGB_T> &canvas, AlphaT alpha) {
    const int w = canvas.width;
    const int h = canvas.height;
    if (w <= 0 || h <= 0)
        return;

    using P = blur_detail::pixel_ops<RGB_T>;

    // Accumulator: u16 for 8-bit channels (max per-pass sum: 255*256=65280),
    // u32 for wider channels.
    using acc_t = fl::conditional_t<sizeof(typename RGB_T::fp) == 1, u16, u32>;

    const bool applyAlpha = !(alpha == blur_detail::alpha_identity<AlphaT>());

    // Handle no-blur case (radius 0 in both dimensions).
    if (hRadius == 0 && vRadius == 0) {
        if (applyAlpha) {
            RGB_T *pixels = canvas.pixels;
            for (int i = 0; i < w * h; ++i) {
                RGB_T &p = pixels[i];
                p = P::make(P::ch(p.r), P::ch(p.g), P::ch(p.b), alpha);
            }
        }
        return;
    }

    // Padded pixel buffer: max of horizontal pad and vertical scratch.
    const int hPadSize = 2 * hRadius + w;
#if defined(FL_IS_AVR)
    const int vPadSize = 2 * vRadius + h;
#else
    // Row-major vertical pass needs (R+2)*w: R+1 ring buffers + 1 zero row.
    const int vPadSize = vRadius > 0 ? (vRadius + 2) * w : 0;
#endif
    const int padSize = hPadSize > vPadSize ? hPadSize : vPadSize;
    RGB_T *pad = blur_detail::get_padbuf<RGB_T>(padSize);

    RGB_T *pixels = canvas.pixels;

    // ── Horizontal pass ──────────────────────────────────────────────
    if (hRadius > 0) {
        constexpr int hShift = 2 * hRadius;

        // Zero the fixed padding regions once (reused for every row).
        __builtin_memset(pad, 0, hRadius * sizeof(RGB_T));
        __builtin_memset(pad + hRadius + w, 0, hRadius * sizeof(RGB_T));

        for (int y = 0; y < h; ++y) {
            RGB_T *row = pixels + y * w;

            // Copy row data into padded region.
            FL_BUILTIN_MEMCPY(pad + hRadius, row, w * sizeof(RGB_T));

            // Apply interior kernel to ALL positions (zero-padding handles edges).
#if defined(FL_IS_AVR)
            // AVR: per-channel noinline + O3 for all radii.
            // conv1ch processes one color channel at a time, cutting live
            // accumulator registers from 6 (r,g,b as u16 pairs) to 2.
            // FL_OPTIMIZE_FUNCTION on the pass functions overrides -Os with
            // -O3 for better register allocation in the kernel loop.
            if (vRadius == 0 && applyAlpha)
                blur_detail::apply_pass_alpha_1ch<hRadius>(
                    pad, row, w, 1, alpha);
            else
                blur_detail::apply_pass_1ch<hRadius>(
                    pad, row, w, 1);
#else
            if (hRadius <= 1) {
                if (vRadius == 0 && applyAlpha) {
                    for (int x = 0; x < w; ++x) {
                        acc_t r, g, b;
                        blur_detail::interior_row<hRadius, RGB_T, acc_t>::apply(
                            pad, hRadius + x, r, g, b);
                        row[x] = P::make(static_cast<acc_t>(r >> hShift),
                                         static_cast<acc_t>(g >> hShift),
                                         static_cast<acc_t>(b >> hShift), alpha);
                    }
                } else {
                    for (int x = 0; x < w; ++x) {
                        acc_t r, g, b;
                        blur_detail::interior_row<hRadius, RGB_T, acc_t>::apply(
                            pad, hRadius + x, r, g, b);
                        row[x] = P::make(static_cast<acc_t>(r >> hShift),
                                         static_cast<acc_t>(g >> hShift),
                                         static_cast<acc_t>(b >> hShift));
                    }
                }
            } else {
                if (vRadius == 0 && applyAlpha) {
                    blur_detail::apply_pass_alpha<hRadius, RGB_T, acc_t>(
                        pad, row, w, 1, alpha);
                } else {
                    blur_detail::apply_pass<hRadius, RGB_T, acc_t>(
                        pad, row, w, 1);
                }
            }
#endif
        }
    }

    // ── Vertical pass ──────────────────────────────────────────────────
    if (vRadius > 0) {
#if defined(FL_IS_AVR)
        // AVR: column-by-column with per-channel noinline + O3.
        constexpr int vShift = 2 * vRadius;

        // Zero the fixed padding regions once (reused for every column).
        __builtin_memset(pad, 0, vRadius * sizeof(RGB_T));
        __builtin_memset(pad + vRadius + h, 0, vRadius * sizeof(RGB_T));

        for (int x = 0; x < w; ++x) {
            // Linearize column into padded region.
            {
                const RGB_T *src = pixels + x;
                RGB_T *dst = pad + vRadius;
                for (int i = 0; i < h; ++i) {
                    *dst++ = *src;
                    src += w;
                }
            }
            if (applyAlpha)
                blur_detail::apply_pass_alpha_1ch<vRadius>(
                    pad, pixels + x, h, w, alpha);
            else
                blur_detail::apply_pass_1ch<vRadius>(
                    pad, pixels + x, h, w);
        }
#else
        // Non-AVR: row-major vertical pass for cache efficiency.
        // Processes all columns simultaneously per row (sequential access),
        // using a ring buffer of R+1 saved rows for overwritten history.
        if (applyAlpha) {
            blur_detail::vpass_rowmajor_impl<vRadius, RGB_T, acc_t, true>(
                pixels, w, h, pad, alpha);
        } else {
            blur_detail::vpass_rowmajor_impl<vRadius, RGB_T, acc_t, false>(
                pixels, w, h, pad, alpha);
        }
#endif
    }
}

// ── alpha8 overload (UNORM8 dim) ─────────────────────────────────────────

template <int hRadius, int vRadius, typename RGB_T>
void blurGaussian(Canvas<RGB_T> &canvas, alpha8 dimFactor) {
    blurGaussianImpl<hRadius, vRadius>(canvas, dimFactor);
}

// ── alpha16 overload (UNORM16 dim — true 16-bit precision) ───────────────

template <int hRadius, int vRadius, typename RGB_T>
void blurGaussian(Canvas<RGB_T> &canvas, alpha16 dimFactor) {
    blurGaussianImpl<hRadius, vRadius>(canvas, dimFactor);
}

// ── Explicit instantiations: alpha8 overload ─────────────────────────────

#define BLUR_INST_F8(H, V, T) \
    template void blurGaussian<H, V, T>(Canvas<T> &, alpha8);

// CRGB — symmetric, h-only, v-only, asymmetric.
BLUR_INST_F8(0, 0, CRGB)  BLUR_INST_F8(1, 1, CRGB)
BLUR_INST_F8(2, 2, CRGB)  BLUR_INST_F8(3, 3, CRGB)  BLUR_INST_F8(4, 4, CRGB)
BLUR_INST_F8(1, 0, CRGB)  BLUR_INST_F8(2, 0, CRGB)
BLUR_INST_F8(3, 0, CRGB)  BLUR_INST_F8(4, 0, CRGB)
BLUR_INST_F8(0, 1, CRGB)  BLUR_INST_F8(0, 2, CRGB)
BLUR_INST_F8(0, 3, CRGB)  BLUR_INST_F8(0, 4, CRGB)
BLUR_INST_F8(1, 2, CRGB)  BLUR_INST_F8(2, 1, CRGB)

// CRGB16 — same combos (not available on AVR due to RAM constraints).
#if !defined(FL_IS_AVR)
BLUR_INST_F8(0, 0, CRGB16)  BLUR_INST_F8(1, 1, CRGB16)
BLUR_INST_F8(2, 2, CRGB16)  BLUR_INST_F8(3, 3, CRGB16)  BLUR_INST_F8(4, 4, CRGB16)
BLUR_INST_F8(1, 0, CRGB16)  BLUR_INST_F8(2, 0, CRGB16)
BLUR_INST_F8(3, 0, CRGB16)  BLUR_INST_F8(4, 0, CRGB16)
BLUR_INST_F8(0, 1, CRGB16)  BLUR_INST_F8(0, 2, CRGB16)
BLUR_INST_F8(0, 3, CRGB16)  BLUR_INST_F8(0, 4, CRGB16)
BLUR_INST_F8(1, 2, CRGB16)  BLUR_INST_F8(2, 1, CRGB16)
#endif

#undef BLUR_INST_F8

// ── Explicit instantiations: alpha16 overload ────────────────────────────

#define BLUR_INST_F16(H, V, T) \
    template void blurGaussian<H, V, T>(Canvas<T> &, alpha16);

// CRGB
BLUR_INST_F16(0, 0, CRGB)  BLUR_INST_F16(1, 1, CRGB)
BLUR_INST_F16(2, 2, CRGB)  BLUR_INST_F16(3, 3, CRGB)  BLUR_INST_F16(4, 4, CRGB)
BLUR_INST_F16(1, 0, CRGB)  BLUR_INST_F16(2, 0, CRGB)
BLUR_INST_F16(3, 0, CRGB)  BLUR_INST_F16(4, 0, CRGB)
BLUR_INST_F16(0, 1, CRGB)  BLUR_INST_F16(0, 2, CRGB)
BLUR_INST_F16(0, 3, CRGB)  BLUR_INST_F16(0, 4, CRGB)
BLUR_INST_F16(1, 2, CRGB)  BLUR_INST_F16(2, 1, CRGB)

// CRGB16
#if !defined(FL_IS_AVR)
BLUR_INST_F16(0, 0, CRGB16)  BLUR_INST_F16(1, 1, CRGB16)
BLUR_INST_F16(2, 2, CRGB16)  BLUR_INST_F16(3, 3, CRGB16)  BLUR_INST_F16(4, 4, CRGB16)
BLUR_INST_F16(1, 0, CRGB16)  BLUR_INST_F16(2, 0, CRGB16)
BLUR_INST_F16(3, 0, CRGB16)  BLUR_INST_F16(4, 0, CRGB16)
BLUR_INST_F16(0, 1, CRGB16)  BLUR_INST_F16(0, 2, CRGB16)
BLUR_INST_F16(0, 3, CRGB16)  BLUR_INST_F16(0, 4, CRGB16)
BLUR_INST_F16(1, 2, CRGB16)  BLUR_INST_F16(2, 1, CRGB16)
#endif

#undef BLUR_INST_F16

} // namespace gfx
} // namespace fl
