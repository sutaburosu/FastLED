#pragma once

#include "fl/stl/noexcept.h"

#include "fl/gfx/canvas.h"  // IWYU pragma: keep
#include "fl/gfx/draw_mode.h"
#include "fl/math/distance_lut.h"  // IWYU pragma: keep
#include "fl/math/integer_math.h"  // IWYU pragma: keep
#include "fl/math/fixed_point/s16x16.h"
#include "fl/math/math.h"
#include "fl/math/scale8.h"

namespace fl {
namespace gfx {

/// sqrt is provided by fl::sqrt overloads in math.h for:
/// - All floating-point types (float, double)
/// - All fixed-point types (s8x8, s12x4, s16x16, s24x8, u8x8, u12x4, u16x16, u24x8)
/// - Integer types (via cast to float)
/// Using: using fl::sqrt; (in drawStrokeLine template to find overloads via ADL)

/// Convert a normalized [0,1] Coord value to u8 (0-255) for nscale8.
/// Uses integer arithmetic for fixed-point types; float for float/double.
/// Generic: for fixed-point types (those with .to_int())
template<typename T>
inline fl::u8 coordToU8(T alpha) FL_NO_EXCEPT {
    return (fl::u8)fl::clamp((alpha * T(255.0f) + T(0.5f)).to_int(), 0, 255);
}

// Specialization for float
template<>
inline fl::u8 coordToU8<float>(float alpha) FL_NO_EXCEPT {
    return (fl::u8)fl::round(alpha * 255.0f);
}

// Specialization for double
template<>
inline fl::u8 coordToU8<double>(double alpha) FL_NO_EXCEPT {
    return (fl::u8)fl::round(alpha * 255.0);
}

// Specialization for int
template<>
inline fl::u8 coordToU8<int>(int alpha) FL_NO_EXCEPT {
    return (fl::u8)fl::clamp(alpha, 0, 255);
}

// Specialization for s16x16: uses from_raw, no float at all
template<>
inline fl::u8 coordToU8<fl::s16x16>(fl::s16x16 alpha) FL_NO_EXCEPT {
    constexpr fl::s16x16 c255 = fl::s16x16::from_raw(255 * fl::s16x16::SCALE);
    constexpr fl::s16x16 half = fl::s16x16::from_raw(1 << 15);
    return (fl::u8)fl::clamp((alpha * c255 + half).to_int(), (fl::i32)0, (fl::i32)255);
}

/// Convert an integer to Coord without float intermediate.
/// For s16x16: uses from_raw (pure integer shift). For float: implicit cast.
template<typename T>
inline T fromInt(int n) FL_NO_EXCEPT { return T(static_cast<float>(n)); }

template<>
inline fl::s16x16 fromInt<fl::s16x16>(int n) FL_NO_EXCEPT {
    return fl::s16x16::from_raw(n * fl::s16x16::SCALE);
}

/// Convert rational p/q to Coord without float.
/// For s16x16: integer multiply+divide. For float: float divide.
template<typename T>
inline T fromFrac(int p, int q) FL_NO_EXCEPT { return T(static_cast<float>(p) / static_cast<float>(q)); }

template<>
inline fl::s16x16 fromFrac<fl::s16x16>(int p, int q) FL_NO_EXCEPT {
    return fl::s16x16::from_raw((p * fl::s16x16::SCALE) / q);
}

/// Divide Coord by 2 using shift (avoids expensive division on embedded).
/// s16x16: raw right-shift (1 instruction). float: multiply by 0.5.
template<typename T>
inline T halfOf(T val) FL_NO_EXCEPT { return val / fromInt<T>(2); }

template<>
inline fl::s16x16 halfOf<fl::s16x16>(fl::s16x16 val) FL_NO_EXCEPT {
    return fl::s16x16::from_raw(val.raw() >> 1);
}

template<>
inline float halfOf<float>(float val) { return val * 0.5f; }

template<>
inline double halfOf<double>(double val) { return val * 0.5; }

template<>
inline int halfOf<int>(int val) FL_NO_EXCEPT { return val >> 1; }

/// Helper to convert any coordinate type to int
/// Supports: s16x16 (via to_int()), float, int, and other arithmetic types
template<typename T>
inline int toInt(const T& val) FL_NO_EXCEPT {
    return val.to_int();  // For s16x16 and similar
}

// Specialization for float
template<>
inline int toInt<float>(const float& val) FL_NO_EXCEPT {
    return static_cast<int>(val);
}

// Specialization for int (passthrough)
template<>
inline int toInt<int>(const int& val) FL_NO_EXCEPT {
    return val;
}

// Specialization for double
template<>
inline int toInt<double>(const double& val) FL_NO_EXCEPT {
    return static_cast<int>(val);
}

/// Internal helper: add or set pixel to rectangular buffer with bounds checking
/// Direct row-major indexing: pixels[y * width + x]
/// Templated on Overwrite for compile-time dispatch (no per-pixel branch).
template<typename PixelT, bool Overwrite>
inline void addPixelToBuffer(PixelT* pixels, int width, int height,
                              int x, int y, const PixelT& color) FL_NO_EXCEPT {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        if (Overwrite)
            pixels[y * width + x] = color;
        else
            pixels[y * width + x] += color;
    }
}

namespace detail {

// void_t for C++11 SFINAE (detects member existence)
template<typename...> struct voider { typedef void type; };

// Trait: does this Coord type need division instead of reciprocal multiply?
// Default (float, double, int — no FRAC_BITS member): use multiply
template<typename Coord, typename = void>
struct needs_division : fl::false_type {};

// Fixed-point types with FRAC_BITS < 16: reciprocal too imprecise, use division
template<typename Coord>
struct needs_division<Coord,
    typename voider<fl::bool_constant<(Coord::FRAC_BITS < 16)>>::type>
    : fl::bool_constant<(Coord::FRAC_BITS < 16)> {};

// Tag-dispatch: high precision → multiply by precomputed reciprocal
template<typename Coord>
inline Coord aaRatioDispatch(Coord num, Coord, Coord inv_denom, fl::false_type) FL_NO_EXCEPT {
    return num * inv_denom;
}

// Tag-dispatch: low precision → direct division (more accurate)
template<typename Coord>
inline Coord aaRatioDispatch(Coord num, Coord denom, Coord, fl::true_type) FL_NO_EXCEPT {
    return num / denom;
}

}  // namespace detail

/// Compute num/denom using the best strategy for the Coord type:
/// - float, s16x16, s8x24 (FRAC_BITS >= 16): multiply by precomputed reciprocal
/// - s8x8, s12x4, s24x8 (FRAC_BITS < 16): direct division
template<typename Coord>
inline Coord aaRatio(Coord num, Coord denom, Coord inv_denom) FL_NO_EXCEPT {
    return detail::aaRatioDispatch(num, denom, inv_denom,
        typename detail::needs_division<Coord>::type());
}

// ============================================================================
// Integer-optimized helpers for disc/ring/line drawing
// ============================================================================

namespace detail {

/// Convert any Coord type to 8.8 fixed-point (fl::i32).
/// Generic fallback: use to_float() conversion.
template<typename Coord>
inline fl::i32 toFixed8(Coord val) FL_NO_EXCEPT {
    return static_cast<fl::i32>(val.to_float() * 256.0f);
}

template<>
inline fl::i32 toFixed8<float>(float val) FL_NO_EXCEPT {
    return static_cast<fl::i32>(val * 256.0f);
}

template<>
inline fl::i32 toFixed8<double>(double val) FL_NO_EXCEPT {
    return static_cast<fl::i32>(val * 256.0);
}

template<>
inline fl::i32 toFixed8<int>(int val) FL_NO_EXCEPT {
    return static_cast<fl::i32>(static_cast<fl::u32>(val) << 8);
}

template<>
inline fl::i32 toFixed8<fl::s16x16>(fl::s16x16 val) FL_NO_EXCEPT {
    // Q16.16 → Q8.8: shift right by 8
    return static_cast<fl::i32>(val.raw() >> 8);
}

/// Precompute right-shift and reciprocal multiplier for AA division.
/// Converts 32-bit AA division: (diff * 255u) / band  (~60 cycles on AVR)
/// into a multiply-by-reciprocal: ((diff >> shift) * inv) >> 8  (~14 cycles on AVR)
/// where inv = round(255 * 256 / scaled).
inline void computeBandShift(fl::i32 band, fl::u8 &shift_out, fl::u16 &inv_out) FL_NO_EXCEPT {
    fl::u8 sh = 0;
    fl::u32 tmp = static_cast<fl::u32>(band);
    while (tmp > 255u) { tmp >>= 1; ++sh; }
    shift_out = sh;
    // Reciprocal: (255 * 256 + scaled/2) / scaled — rounds to nearest
    fl::u8 scaled = static_cast<fl::u8>(tmp);
    inv_out = static_cast<fl::u16>((65280u + (scaled >> 1)) / scaled);
}

/// Convert any Coord type to Q16.16 raw i32 for integer inner loops.
/// s16x16: direct raw access (already Q16.16, zero overhead).
/// float/double: multiply by 65536. int: shift left by 16.
template<typename Coord>
inline fl::i32 toQ16(Coord val) FL_NO_EXCEPT {
    return static_cast<fl::i32>(val.to_float() * 65536.0f);
}

template<>
inline fl::i32 toQ16<float>(float val) FL_NO_EXCEPT {
    return static_cast<fl::i32>(val * 65536.0f);
}

template<>
inline fl::i32 toQ16<double>(double val) FL_NO_EXCEPT {
    return static_cast<fl::i32>(val * 65536.0);
}

template<>
inline fl::i32 toQ16<int>(int val) FL_NO_EXCEPT {
    return static_cast<fl::i32>(val) << 16;
}

template<>
inline fl::i32 toQ16<fl::s16x16>(fl::s16x16 val) FL_NO_EXCEPT {
    return val.raw();
}

/// Maximum |vertex coordinate| accepted by drawPolygon, in Q16.16 units.
/// 8191 px keeps all Q16.16 values and edge deltas in i32 with headroom.
static const fl::i32 kPolyQ16Max = 8191 << 16;

/// Convert any Coord type to Q16.16, clamped to ±8191 px.
/// Out-of-range inputs (e.g. 1e6 px floats) saturate instead of overflowing
/// the i32 result. s16x16 raw values always fit (±32767 px).
template<typename Coord>
inline fl::i32 toQ16Clamped(Coord val) FL_NO_EXCEPT {
    fl::i64 q = static_cast<fl::i64>(val.to_float() * 65536.0f);
    if (q > kPolyQ16Max) return kPolyQ16Max;
    if (q < -static_cast<fl::i64>(kPolyQ16Max)) return -kPolyQ16Max;
    return static_cast<fl::i32>(q);
}

template<>
inline fl::i32 toQ16Clamped<float>(float val) FL_NO_EXCEPT {
    if (val > 8191.0f) return kPolyQ16Max;
    if (val < -8191.0f) return -kPolyQ16Max;
    return static_cast<fl::i32>(val * 65536.0f);
}

template<>
inline fl::i32 toQ16Clamped<double>(double val) FL_NO_EXCEPT {
    if (val > 8191.0) return kPolyQ16Max;
    if (val < -8191.0) return -kPolyQ16Max;
    return static_cast<fl::i32>(val * 65536.0);
}

template<>
inline fl::i32 toQ16Clamped<int>(int val) FL_NO_EXCEPT {
    if (val > 8191) return kPolyQ16Max;
    if (val < -8191) return -kPolyQ16Max;
    return static_cast<fl::i32>(val) << 16;
}

template<>
inline fl::i32 toQ16Clamped<fl::s16x16>(fl::s16x16 val) FL_NO_EXCEPT {
    return val.raw();
}

/// Floor-divide a by positive b (rounds toward -inf).
inline fl::i64 floorDivI64(fl::i64 a, fl::i64 b) FL_NO_EXCEPT {
    fl::i64 q = a / b;
    if ((a % b) < 0) --q;
    return q;
}

/// Per-edge scanline state for drawPolygon (even-odd fill, band model).
/// The crossing x (at the current row center) advances exactly per row via
/// a Bresenham-style remainder recurrence — zero divisions in the inner
/// loop:
///   x += step; t += rem; if (t >= dy) { t -= dy; ++x; }
/// where step = floor(65536 * dx / dy) and rem = 65536 * dx - step * dy
/// with rem in [0, dy). Single-row edges (span < 1 px) keep step = rem = 0
/// (and t = 0); dy always holds the true span height, which polyLineT28
/// needs for the line slope. An edge is active in every canvas row it
/// geometrically occupies (py0..py1, including the partial row containing
/// each endpoint).
/// x/step/rem/t are i64: for a nearly-horizontal edge entering the canvas
/// from above, the initial crossing extrapolates to x = xtop + w0*dx/dy
/// with tiny dy, which can exceed i32 (verified reachable: ~2^45).
struct PolyEdge {
    fl::i64 x;      // crossing x at current row center, Q16.16
    fl::i64 step;   // per-row increment, Q16.16 (0 for single-row edges)
    fl::i64 rem;    // remainder in [0, dy) (0 for single-row edges)
    fl::i64 t;      // running remainder accumulator
    fl::i32 dy;     // span height, Q16.16
    fl::i32 B;      // dx in the top-to-bottom direction, Q16.16 (signed)
    fl::i32 ytop;   // top endpoint y, Q16.16
    fl::i32 ybot;   // bottom endpoint y, Q16.16
    fl::i32 lx0;    // leftmost column the line can touch in the current row
    fl::i32 lx1;    // rightmost column the line can touch in the current row
    int py0;        // first active canvas row (row containing ytop)
    int py1;        // last active canvas row (row containing ybot)
};

/// One sub-band within a row for drawPolygon: the interval [a, b) in Q16.16
/// relative to the row center (band boundaries are row ends and vertex y's
/// strictly inside the row), plus the active edges sorted by x at the band
/// midpoint.
struct PolyBand {
    fl::i32 a;        // band bottom, Q16.16 (relative to row center)
    fl::i32 b;        // band top, Q16.16 (relative to row center)
    fl::u8 count;     // active edge count (even for closed polygons; an odd
                      // trailing crossing is dropped — see drawPolygonCore)
    fl::u8 idx[32];   // active edge indices, sorted by x at band midpoint
};

/// Full-pixel coverage in T units (see polyLineT28).
static const fl::i32 kPolyOne = 1 << 28;

/// Multiply two unsigned values < 2^48, returning the 128-bit product as a
/// (hi, lo) u64 pair. 32-bit lanes — no __int128 (MSVC host portability).
inline void polyMul128(fl::u64 a, fl::u64 b, fl::u64 &hi, fl::u64 &lo) FL_NO_EXCEPT {
    const fl::u32 a0 = (fl::u32)a, a1 = (fl::u32)(a >> 32);
    const fl::u32 b0 = (fl::u32)b, b1 = (fl::u32)(b >> 32);
    const fl::u64 p0 = (fl::u64)a0 * b0;
    const fl::u64 p1 = (fl::u64)a0 * b1 + (fl::u64)a1 * b0;
    const fl::u64 lo_ = p0 + (p1 << 32);
    hi = (fl::u64)a1 * b1 + (p1 >> 32) + (lo_ < p0 ? 1u : 0u);
    lo = lo_;
}

/// 128-bit add: (hi, lo) += (ahi, alo).
inline void polyAdd128(fl::u64 &hi, fl::u64 &lo, fl::u64 ahi, fl::u64 alo) FL_NO_EXCEPT {
    const fl::u64 lo_ = lo + alo;
    hi += ahi + (lo_ < lo ? 1u : 0u);
    lo = lo_;
}

/// Exact left-of-line coverage of one pixel for one polygon edge.
///
/// Line: x(tau) = x_q + (B/D) * tau with tau in Q16.16 relative to the row
/// center; u = D * (x_q - px * 65536). Returns T = round(2^28 * cov) where
/// cov = int_{ta}^{tb} clamp(x(tau) - px_left, 0, 1) dtau/65536, i.e. the
/// fraction of the pixel lying to the LEFT of the line over the row
/// sub-interval [ta, tb]. Pixel coverage between span lines L (left) and R
/// (right) is then T_left(R) - T_left(L).
///
/// In s = B * tau space the clamp breakpoints are integers (s = -u and
/// s = 2*D*32768 - u), so the integral is a sum of at most two
/// trapezoid/rectangle pieces:
///   M = (hi-lo) * ((u+lo)+(u+hi)) + (s_hi-lo2) * (4*D*32768)
///   T = round(M / (32*D*|B|))
/// M < 2^94 (u64 pair), den = 32*D*S < 2^65 (u64 pair); the quotient comes
/// from 94 shift-subtract iterations — no FPU, no __int128. Result: [0, 2^28].
inline fl::i32 polyLineT28(fl::i32 B, fl::i32 D, fl::i64 u,
                           fl::i32 ta, fl::i32 tb) FL_NO_EXCEPT {
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
    // s = B*tau runs over [sa, sb] as tau runs [ta, tb]; for B < 0 the
    // interval is reversed — use min/max (the area is orientation-free).
    const fl::i64 s_lo = (sa < sb) ? sa : sb;
    const fl::i64 s_hi = (sa < sb) ? sb : sa;
    const fl::i64 c0 = -u;
    const fl::i64 c1 = (fl::i64)2 * D * 32768 - u;
    fl::u64 mhi = 0, mlo = 0;
    const fl::i64 lo = (s_lo > c0) ? s_lo : c0;
    const fl::i64 hi = (s_hi < c1) ? s_hi : c1;
    if (hi > lo) {
        // (u+lo) and (u+hi) each lie in [0, 2*D*32768] (lo >= c0, hi <= c1),
        // so both factors are non-negative and < 2^48.
        polyMul128((fl::u64)(hi - lo),
                   (fl::u64)((u + lo) + (u + hi)), mhi, mlo);
    }
    const fl::i64 lo2 = (s_lo > c1) ? s_lo : c1;
    if (s_hi > lo2) {
        fl::u64 phi = 0, plo = 0;
        polyMul128((fl::u64)(s_hi - lo2),
                   (fl::u64)((fl::i64)4 * D * 32768), phi, plo);
        polyAdd128(mhi, mlo, phi, plo);
    }
    // den = 32 * D * S as a u64 pair (D*S < 2^61, so den_hi = DS >> 59).
    const fl::u64 DS = (fl::u64)S * (fl::u64)D;
    const fl::u64 dhi = DS >> 59;
    const fl::u64 dlo = DS << 5;
    // Binary long division: q = floor(M / den), r = M - q*den with r < den.
    // The remainder (rh:rl) = rh*2^64 + rl stays < 2*den at every step (so a
    // single subtraction suffices); numerator bits are produced top-down and
    // enter the LOW word of the remainder register.
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
    {
        fl::u64 rh2 = rh << 1;
        fl::u64 rl2 = rl << 1;
        if (rl >> 63) ++rh2;
        if (rh2 > dhi || (rh2 == dhi && rl2 >= dlo)) ++q;
    }
    if (q > (fl::u64)kPolyOne) q = (fl::u64)kPolyOne;  // physical bound
    return (fl::i32)q;
}

/// Disc context: bundles per-circle constants into a struct passed by reference.
/// This reduces function-call overhead on register-poor architectures (AVR: 12 params
/// → 1 pointer), matching the struct-based approach used in hand-optimized code.
template<typename PixelT>
struct DiscCtx {
    fl::i32 xdelta0;
    int xmin, xmax;
    fl::i32 rin2, rout2;
    fl::u8 band_shift;
    fl::u16 band_inv;      // reciprocal: (255 * 256 + scaled/2) / scaled
    PixelT color;
};

/// Ring context: bundles all per-circle constants into a struct passed by reference.
/// This reduces function-call overhead on register-poor architectures (AVR: 16 params
/// → 1 pointer), matching the struct-based approach used in hand-optimized code.
template<typename PixelT>
struct RingCtx {
    fl::i32 xdelta0;
    int xmin, xmax;
    fl::i32 ii2, io2, oi2, oo2;
    fl::u8 inner_shift, outer_shift;
    fl::u16 inner_inv, outer_inv;  // reciprocals: (255 * 256 + scaled/2) / scaled
    PixelT color;
};

/// Render one scanline of a disc using incremental d².
/// Templated on Overwrite for compile-time dispatch (no per-pixel branch).
/// Uses (n+1)² = n² + 2n + 1 identity — zero multiplies in the inner loop.
template<typename PixelT, bool Overwrite>
inline void renderDiscRow(PixelT* buf, int w, int py,
                          fl::i32 d2_row,
                          const DiscCtx<PixelT>& f) FL_NO_EXCEPT {
    PixelT* ptr = &buf[py * w + f.xmin];
    fl::i32 d2 = d2_row;
    fl::i32 xd = f.xdelta0;
    int px = f.xmin;
    // Phase 1: Skip outside pixels (left, d2 decreasing)
    while (px <= f.xmax && d2 >= f.rout2) {
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
    // Phase 2: Outer AA fringe (left)
    while (px <= f.xmax && d2 >= f.rin2 && d2 < f.rout2) {
        fl::u16 diff = static_cast<fl::u16>(static_cast<fl::u32>(f.rout2 - d2) >> f.band_shift);
        fl::u8 br = static_cast<fl::u8>((diff * f.band_inv) >> 8);
        PixelT c = f.color; c.nscale8(br);
        if (Overwrite) *ptr = c; else *ptr += c;
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
    // Phase 3: Full brightness interior (d2 hits minimum, then increases)
    while (px <= f.xmax && d2 < f.rin2) {
        if (Overwrite) *ptr = f.color; else *ptr += f.color;
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
    // Phase 4: Outer AA fringe (right, d2 increasing)
    while (px <= f.xmax && d2 < f.rout2) {
        fl::u16 diff = static_cast<fl::u16>(static_cast<fl::u32>(f.rout2 - d2) >> f.band_shift);
        fl::u8 br = static_cast<fl::u8>((diff * f.band_inv) >> 8);
        PixelT c = f.color; c.nscale8(br);
        if (Overwrite) *ptr = c; else *ptr += c;
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
}

/// Render one scanline of a ring using incremental d² with phase-based scanning.
/// Templated on Overwrite for compile-time dispatch (no per-pixel branch).
/// Zones (left to right): outside → outer-AA → solid → inner-AA → hole →
///                         inner-AA → solid → outer-AA → outside.
template<typename PixelT, bool Overwrite>
inline void renderRingRow(PixelT* buf, int w, int py,
                          fl::i32 d2_row,
                          const RingCtx<PixelT>& g) FL_NO_EXCEPT {
    PixelT* ptr = &buf[py * w + g.xmin];
    fl::i32 d2 = d2_row;
    fl::i32 xd = g.xdelta0;
    int px = g.xmin;
    // Preamble: left exterior (d2 >= oo2)
    while (px <= g.xmax && d2 >= g.oo2) {
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
    // Phase 1: outer-left AA fringe (oi2 <= d2 < oo2)
    while (px <= g.xmax && d2 >= g.oi2 && d2 < g.oo2) {
        fl::u16 diff = static_cast<fl::u16>(static_cast<fl::u32>(g.oo2 - d2) >> g.outer_shift);
        fl::u8 br = static_cast<fl::u8>((diff * g.outer_inv) >> 8);
        PixelT c = g.color; c.nscale8(br);
        if (Overwrite) *ptr = c; else *ptr += c;
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
    // Phase 2: full-brightness band left (io2 <= d2 < oi2)
    while (px <= g.xmax && d2 >= g.io2 && d2 < g.oi2) {
        if (Overwrite) *ptr = g.color; else *ptr += g.color;
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
    // Phase 3: inner-left AA fringe (ii2 <= d2 < io2)
    while (px <= g.xmax && d2 >= g.ii2 && d2 < g.io2) {
        fl::u16 diff = static_cast<fl::u16>(static_cast<fl::u32>(d2 - g.ii2) >> g.inner_shift);
        fl::u8 br = static_cast<fl::u8>((diff * g.inner_inv) >> 8);
        PixelT c = g.color; c.nscale8(br);
        if (Overwrite) *ptr = c; else *ptr += c;
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
    // Phase 4: transparent hole (d2 < ii2)
    while (px <= g.xmax && d2 < g.ii2) {
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
    // Phase 5: inner-right AA fringe (ii2 <= d2 < io2)
    while (px <= g.xmax && d2 >= g.ii2 && d2 < g.io2) {
        fl::u16 diff = static_cast<fl::u16>(static_cast<fl::u32>(d2 - g.ii2) >> g.inner_shift);
        fl::u8 br = static_cast<fl::u8>((diff * g.inner_inv) >> 8);
        PixelT c = g.color; c.nscale8(br);
        if (Overwrite) *ptr = c; else *ptr += c;
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
    // Phase 6: full-brightness band right (io2 <= d2 < oi2)
    while (px <= g.xmax && d2 >= g.io2 && d2 < g.oi2) {
        if (Overwrite) *ptr = g.color; else *ptr += g.color;
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
    // Phase 7: outer-right AA fringe (oi2 <= d2 < oo2)
    while (px <= g.xmax && d2 >= g.oi2 && d2 < g.oo2) {
        fl::u16 diff = static_cast<fl::u16>(static_cast<fl::u32>(g.oo2 - d2) >> g.outer_shift);
        fl::u8 br = static_cast<fl::u8>((diff * g.outer_inv) >> 8);
        PixelT c = g.color; c.nscale8(br);
        if (Overwrite) *ptr = c; else *ptr += c;
        d2 += xd; xd += 131072; ++ptr; ++px;
    }
}

/// Stroke line context: bundles per-line constants into a struct passed by
/// reference. This reduces function-call overhead on register-poor architectures
/// (AVR: many i32 params → 1 pointer), matching the struct-based approach used
/// in drawDisc/drawRing.
template<typename PixelT>
struct StrokeCtx {
    fl::i32 threshold_q;
    fl::i32 len2_q;
    fl::i32 dx_q, dy_q;
    fl::u8 aa_shift;
    fl::u16 aa_inv;
    fl::i32 x0_8, y0_8, x1_8, y1_8;
    fl::i32 r_max2_8;
    fl::u8 cap_shift;
    fl::u16 cap_inv;
    fl::i32 dot_ext_q;
    LineCap cap;
    PixelT color;
    int xmin, xmax;
};

/// Render one scanline of a stroke line using phase-based scanning.
/// Templated on Overwrite for compile-time dispatch (no per-pixel branch).
/// AA uses precomputed shift+reciprocal — zero Coord multiplies.
template<typename PixelT, bool Overwrite>
inline void renderStrokeRow(PixelT* buf, int w, int py,
                            fl::i32 cross_start, fl::i32 dot_start,
                            const StrokeCtx<PixelT>& sc) FL_NO_EXCEPT {
    PixelT* ptr = &buf[py * w + sc.xmin];
    fl::i32 cross = cross_start;
    fl::i32 dot = dot_start;
    int px = sc.xmin;
    const fl::i32 thr = sc.threshold_q;
    const fl::i32 neg_thr = -thr;

    // Phase 1: Skip pixels outside the band
    while (px <= sc.xmax && (cross >= thr || cross < neg_thr)) {
        cross += sc.dy_q;
        dot += sc.dx_q;
        ++ptr; ++px;
    }

    // Hoist per-row endcap ey values (py is constant within a row)
    fl::i32 py8 = static_cast<fl::i32>(py) << 8;
    fl::i32 ey8_y0 = py8 - sc.y0_8;
    fl::i32 ey8_y1 = py8 - sc.y1_8;

    // Phase 2: Process visible pixels (|cross| < threshold)
    while (px <= sc.xmax && cross < thr && cross >= neg_thr) {
        fl::i32 abs_cross = (cross < 0) ? -cross : cross;
        bool on_segment = (dot >= 0 && dot <= sc.len2_q);

        if (on_segment) {
            // Integer AA: shift+multiply (no Coord multiply)
            fl::u16 shifted = static_cast<fl::u16>(
                static_cast<fl::u32>(abs_cross) >> sc.aa_shift);
            fl::u8 linear_idx = static_cast<fl::u8>((shifted * sc.aa_inv) >> 8);
            // Fast approximate x/255 ≈ (x + 1 + (x >> 8)) >> 8
            fl::u16 sq = static_cast<fl::u16>(linear_idx) * linear_idx;
            fl::u8 sq_idx = static_cast<fl::u8>((sq + 1 + (sq >> 8)) >> 8);
            PixelT c = sc.color;
            c.nscale8(FL_PGM_READ_BYTE_NEAR(&distanceAA_LUT[sq_idx]));
            if (Overwrite) *ptr = c; else *ptr += c;
        } else if (sc.cap == LineCap::ROUND) {
            // Endpoint distance in 8.8 fixed-point
            fl::i32 ex8;
            fl::i16 ey_s;
            if (dot < 0) {
                ex8 = (static_cast<fl::i32>(px) << 8) - sc.x0_8;
                ey_s = static_cast<fl::i16>(ey8_y0);
            } else {
                ex8 = (static_cast<fl::i32>(px) << 8) - sc.x1_8;
                ey_s = static_cast<fl::i16>(ey8_y1);
            }
            fl::i16 ex_s = static_cast<fl::i16>(ex8);
            // 16×16→32 multiply (4 MUL on AVR vs ~70 for 32×32)
            fl::i32 ed2 = static_cast<fl::i32>(ex_s) * ex_s +
                          static_cast<fl::i32>(ey_s) * ey_s;
            if (ed2 < sc.r_max2_8) {
                fl::u16 shifted = static_cast<fl::u16>(
                    static_cast<fl::u32>(ed2) >> sc.cap_shift);
                fl::u8 idx = static_cast<fl::u8>((shifted * sc.cap_inv) >> 8);
                PixelT c = sc.color;
                c.nscale8(FL_PGM_READ_BYTE_NEAR(&distanceAA_LUT[idx]));
                if (Overwrite) *ptr = c; else *ptr += c;
            }
        } else if (sc.cap == LineCap::SQUARE) {
            if (dot >= -sc.dot_ext_q && dot <= sc.len2_q + sc.dot_ext_q) {
                fl::u16 shifted = static_cast<fl::u16>(
                    static_cast<fl::u32>(abs_cross) >> sc.aa_shift);
                fl::u8 linear_idx = static_cast<fl::u8>((shifted * sc.aa_inv) >> 8);
                fl::u16 sq = static_cast<fl::u16>(linear_idx) * linear_idx;
                fl::u8 sq_idx = static_cast<fl::u8>((sq + 1 + (sq >> 8)) >> 8);
                PixelT c = sc.color;
                c.nscale8(FL_PGM_READ_BYTE_NEAR(&distanceAA_LUT[sq_idx]));
                if (Overwrite) *ptr = c; else *ptr += c;
            }
        }
        // FLAT: pixels outside [0, len2] are silently dropped

        cross += sc.dy_q;
        dot += sc.dx_q;
        ++ptr; ++px;
    }
    // Phase 3: remaining pixels are outside the band — nothing to do
}

}  // namespace detail

/// ============================================================================
/// CANVAS API (Primary - Cache Optimal)
/// ============================================================================
/// Graphics operations work on rectangular Canvas objects with direct row-major indexing.
/// All drawing functions use row-major indexing for cache-optimal performance.

/// @brief Wu antialiased line drawing
/// @param canvas Canvas with pixel buffer and dimensions
/// @param x0, y0, x1, y1 Line start and end coordinates (any arithmetic type)
/// @param color Pixel color
template<typename PixelT, typename Coord>
void drawLine(Canvas<PixelT>& canvas, const PixelT& color,
              Coord x0, Coord y0, Coord x1, Coord y1,
              fl::DrawMode mode) FL_NO_EXCEPT;

template<typename PixelT, typename Coord>
void drawDisc(Canvas<PixelT>& canvas, const PixelT& color,
              Coord cx, Coord cy, Coord r,
              fl::DrawMode mode) FL_NO_EXCEPT;

template<typename PixelT, typename Coord>
void drawRing(Canvas<PixelT>& canvas, const PixelT& color,
              Coord cx, Coord cy, Coord r, Coord thickness,
              fl::DrawMode mode) FL_NO_EXCEPT;

template<typename PixelT, typename Coord>
void drawStrokeLine(Canvas<PixelT>& canvas, const PixelT& color,
                    Coord x0, Coord y0, Coord x1, Coord y1, Coord thickness,
                    LineCap cap, fl::DrawMode mode) FL_NO_EXCEPT;

template<typename PixelT, typename Coord>
void drawPolygon(Canvas<PixelT>& canvas, const PixelT& color,
                 fl::span<const PolyPoint<Coord>> pts,
                 fl::DrawMode mode) FL_NO_EXCEPT;

template<typename PixelT, typename Coord, int N>
void drawPolygon(Canvas<PixelT>& canvas, const PixelT& color,
                 const PolyPoint<Coord> (&pts)[N],
                 fl::DrawMode mode) FL_NO_EXCEPT;

/// ============================================================================
/// LEGACY RASTERTARGET API (Deprecated - for backward compatibility)
/// ============================================================================

/// ============================================================================
/// CANVAS IMPLEMENTATIONS
/// ============================================================================

// ---------------------------------------------------------------------------
// drawLine: Integer Wu antialiased line (8.8 fixed-point internally)
// Uses int loop counter and bit-shift for AA — no Coord ops in inner loop.
// ---------------------------------------------------------------------------
namespace detail {

template<typename PixelT, typename Coord, bool Overwrite>
inline void drawLineCore(Canvas<PixelT>& canvas, const PixelT& color,
                         Coord x0, Coord y0, Coord x1, Coord y1) FL_NO_EXCEPT {
    PixelT* pixels = canvas.pixels;
    int width = canvas.width;
    int height = canvas.height;

    // Convert to 8.8 fixed-point
    fl::i32 fx0 = detail::toFixed8(x0);
    fl::i32 fy0 = detail::toFixed8(y0);
    fl::i32 fx1 = detail::toFixed8(x1);
    fl::i32 fy1 = detail::toFixed8(y1);

    bool steep = fl::abs(fy1 - fy0) > fl::abs(fx1 - fx0);
    if (steep) { fl::swap(fx0, fy0); fl::swap(fx1, fy1); }
    if (fx0 > fx1) { fl::swap(fx0, fx1); fl::swap(fy0, fy1); }

    fl::i32 dx8 = fx1 - fx0;
    fl::i32 dy8 = fy1 - fy0;

    // Gradient per pixel step in 8.8: (dy * 256) / dx
    fl::i32 gradient = (dx8 == 0) ? 0 : ((dy8 << 8) / dx8);

    // First endpoint: round x to nearest pixel boundary
    fl::i32 xend = (fx0 + 128) & ~0xFF;
    fl::i32 yend = fy0 + ((gradient * (xend - fx0)) >> 8);
    fl::i32 xgap = 256 - ((fx0 + 128) & 0xFF);  // rfpart(x0 + 0.5)
    int xpxl1 = xend >> 8;
    int ypxl1 = yend >> 8;
    fl::u8 yfrac1 = static_cast<fl::u8>(yend & 0xFF);

    // Second endpoint
    fl::i32 xend2 = (fx1 + 128) & ~0xFF;
    fl::i32 yend2 = fy1 + ((gradient * (xend2 - fx1)) >> 8);
    fl::i32 xgap2 = (fx1 + 128) & 0xFF;  // fpart(x1 + 0.5)
    int xpxl2 = xend2 >> 8;
    int ypxl2 = yend2 >> 8;
    fl::u8 yfrac2 = static_cast<fl::u8>(yend2 & 0xFF);

    // Draw first endpoint
    {
        fl::u8 b1 = static_cast<fl::u8>(((255 - yfrac1) * xgap) >> 8);
        fl::u8 b2 = static_cast<fl::u8>((yfrac1 * xgap) >> 8);
        if (steep) {
            PixelT c = color; c.nscale8(b1);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, ypxl1, xpxl1, c);
            c = color; c.nscale8(b2);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, ypxl1 + 1, xpxl1, c);
        } else {
            PixelT c = color; c.nscale8(b1);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, xpxl1, ypxl1, c);
            c = color; c.nscale8(b2);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, xpxl1, ypxl1 + 1, c);
        }
    }

    // Draw second endpoint
    {
        fl::u8 b1 = static_cast<fl::u8>(((255 - yfrac2) * xgap2) >> 8);
        fl::u8 b2 = static_cast<fl::u8>((yfrac2 * xgap2) >> 8);
        if (steep) {
            PixelT c = color; c.nscale8(b1);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, ypxl2, xpxl2, c);
            c = color; c.nscale8(b2);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, ypxl2 + 1, xpxl2, c);
        } else {
            PixelT c = color; c.nscale8(b1);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, xpxl2, ypxl2, c);
            c = color; c.nscale8(b2);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, xpxl2, ypxl2 + 1, c);
        }
    }

    // Main loop: integer counter, 8.8 intery accumulation
    fl::i32 intery = yend + gradient;
    if (steep) {
        for (int x = xpxl1 + 1; x < xpxl2; ++x) {
            int y = intery >> 8;
            fl::u8 frac = static_cast<fl::u8>(intery & 0xFF);
            PixelT c = color; c.nscale8(255 - frac);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, y, x, c);
            c = color; c.nscale8(frac);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, y + 1, x, c);
            intery += gradient;
        }
    } else {
        for (int x = xpxl1 + 1; x < xpxl2; ++x) {
            int y = intery >> 8;
            fl::u8 frac = static_cast<fl::u8>(intery & 0xFF);
            PixelT c = color; c.nscale8(255 - frac);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, x, y, c);
            c = color; c.nscale8(frac);
            addPixelToBuffer<PixelT, Overwrite>(pixels, width, height, x, y + 1, c);
            intery += gradient;
        }
    }
}

template<typename PixelT, typename Coord, bool Overwrite>
inline void drawDiscCore(Canvas<PixelT>& canvas, const PixelT& color,
                         Coord cx, Coord cy, Coord r) FL_NO_EXCEPT;

template<typename PixelT, typename Coord, bool Overwrite>
inline void drawRingCore(Canvas<PixelT>& canvas, const PixelT& color,
                         Coord cx, Coord cy, Coord r, Coord thickness) FL_NO_EXCEPT;

template<typename PixelT, typename Coord, bool Overwrite>
inline void drawStrokeLineCore(Canvas<PixelT>& canvas, const PixelT& color,
                               Coord x0, Coord y0, Coord x1, Coord y1,
                               Coord thickness, LineCap cap) FL_NO_EXCEPT;

template<typename PixelT, typename Coord, bool Overwrite>
inline void drawPolygonCore(Canvas<PixelT>& canvas, const PixelT& color,
                            fl::span<const PolyPoint<Coord>> pts) FL_NO_EXCEPT;

}  // namespace detail

template<typename PixelT, typename Coord>
inline void drawLine(Canvas<PixelT>& canvas, const PixelT& color,
                     Coord x0, Coord y0, Coord x1, Coord y1,
                     fl::DrawMode mode) FL_NO_EXCEPT {
    if (mode == fl::DrawMode::DRAW_MODE_OVERWRITE)
        detail::drawLineCore<PixelT, Coord, true>(canvas, color, x0, y0, x1, y1);
    else
        detail::drawLineCore<PixelT, Coord, false>(canvas, color, x0, y0, x1, y1);
}

// ---------------------------------------------------------------------------
// drawDiscCore: Templated on Overwrite for compile-time dispatch.
// ---------------------------------------------------------------------------
template<typename PixelT, typename Coord, bool Overwrite>
inline void detail::drawDiscCore(Canvas<PixelT>& canvas, const PixelT& color,
                                 Coord cx, Coord cy, Coord r) FL_NO_EXCEPT {
    PixelT* pixels = canvas.pixels;
    int width = canvas.width;
    int height = canvas.height;

    fl::i32 cx8 = detail::toFixed8(cx);
    fl::i32 cy8 = detail::toFixed8(cy);
    fl::i32 r8 = detail::toFixed8(r);

    fl::i32 rin8 = r8 - 128;   // r - 0.5 in 8.8
    fl::i32 rout8 = r8 + 128;  // r + 0.5 in 8.8

    fl::i32 rin2 = rin8 * rin8;
    fl::i32 rout2 = rout8 * rout8;
    fl::i32 band = rout2 - rin2;
    if (band <= 0) return;

    int ri = (rout8 >> 8) + 1;
    int cxi = cx8 >> 8;
    int cyi = cy8 >> 8;

    int xmin = cxi - ri; if (xmin < 0) xmin = 0;
    int xmax = cxi + ri; if (xmax >= width) xmax = width - 1;
    if (xmin > xmax) return;
    if (cyi + ri < 0 || cyi - ri >= height) return;

    fl::i32 dx8 = (static_cast<fl::i32>(xmin) << 8) - cx8;
    fl::i32 dx2 = dx8 * dx8;

    detail::DiscCtx<PixelT> fc;
    fc.xdelta0 = 512 * dx8 + 65536;
    fc.xmin = xmin; fc.xmax = xmax;
    fc.rin2 = rin2; fc.rout2 = rout2;
    detail::computeBandShift(band, fc.band_shift, fc.band_inv);
    fc.color = color;

    fl::i32 cyfrac = (static_cast<fl::i32>(cyi) << 8) - cy8;
    fl::i32 d2c = dx2 + cyfrac * cyfrac;

    if (cyi >= 0 && cyi < height)
        detail::renderDiscRow<PixelT, Overwrite>(pixels, width, cyi, d2c, fc);

    fl::i32 botd2 = d2c, botdelta = 512 * cyfrac + 65536;
    fl::i32 topd2 = d2c, topdelta = -512 * cyfrac + 65536;
    botd2 += botdelta; botdelta += 131072;
    topd2 += topdelta; topdelta += 131072;

    for (int dy = 1; dy <= ri; ++dy) {
        bool cbot = (botd2 - dx2 <= rout2);
        bool ctop = (topd2 - dx2 <= rout2);
        if (!cbot && !ctop) break;
        int pyb = cyi + dy, pyt = cyi - dy;
        if (cbot && pyb >= 0 && pyb < height)
            detail::renderDiscRow<PixelT, Overwrite>(pixels, width, pyb, botd2, fc);
        if (ctop && pyt >= 0 && pyt < height)
            detail::renderDiscRow<PixelT, Overwrite>(pixels, width, pyt, topd2, fc);
        botd2 += botdelta; botdelta += 131072;
        topd2 += topdelta; topdelta += 131072;
    }
}

template<typename PixelT, typename Coord>
inline void drawDisc(Canvas<PixelT>& canvas, const PixelT& color,
                     Coord cx, Coord cy, Coord r,
                     fl::DrawMode mode) FL_NO_EXCEPT {
    if (mode == fl::DrawMode::DRAW_MODE_OVERWRITE)
        detail::drawDiscCore<PixelT, Coord, true>(canvas, color, cx, cy, r);
    else
        detail::drawDiscCore<PixelT, Coord, false>(canvas, color, cx, cy, r);
}

// ---------------------------------------------------------------------------
// drawRingCore: Templated on Overwrite for compile-time dispatch.
// ---------------------------------------------------------------------------
template<typename PixelT, typename Coord, bool Overwrite>
inline void detail::drawRingCore(Canvas<PixelT>& canvas, const PixelT& color,
                                 Coord cx, Coord cy, Coord r, Coord thickness) FL_NO_EXCEPT {
    PixelT* pixels = canvas.pixels;
    int width = canvas.width;
    int height = canvas.height;

    fl::i32 cx8 = detail::toFixed8(cx);
    fl::i32 cy8 = detail::toFixed8(cy);
    fl::i32 r8 = detail::toFixed8(r);
    fl::i32 t8 = detail::toFixed8(thickness);

    fl::i32 r_ii8 = r8 - 128;
    fl::i32 r_io8 = r8 + 128;
    fl::i32 r_oi8 = r8 + t8 - 128;
    fl::i32 r_oo8 = r8 + t8 + 128;

    if (r_ii8 < 0) r_ii8 = 0;
    if (r_io8 < 0) r_io8 = 0;

    fl::i32 ii2 = r_ii8 * r_ii8;
    fl::i32 io2 = r_io8 * r_io8;
    fl::i32 oi2 = r_oi8 * r_oi8;
    fl::i32 oo2 = r_oo8 * r_oo8;

    fl::i32 inner_band = io2 - ii2;
    fl::i32 outer_band = oo2 - oi2;

    int ri = (r_oo8 >> 8) + 1;
    int cxi = cx8 >> 8;
    int cyi = cy8 >> 8;

    int xmin = cxi - ri; if (xmin < 0) xmin = 0;
    int xmax = cxi + ri; if (xmax >= width) xmax = width - 1;
    if (xmin > xmax) return;
    if (cyi + ri < 0 || cyi - ri >= height) return;

    fl::i32 dx8 = (static_cast<fl::i32>(xmin) << 8) - cx8;
    fl::i32 dx2 = dx8 * dx8;

    detail::RingCtx<PixelT> gc;
    gc.xdelta0 = 512 * dx8 + 65536;
    gc.xmin = xmin; gc.xmax = xmax;
    gc.ii2 = ii2; gc.io2 = io2; gc.oi2 = oi2; gc.oo2 = oo2;
    if (inner_band > 0) detail::computeBandShift(inner_band, gc.inner_shift, gc.inner_inv);
    else { gc.inner_shift = 0; gc.inner_inv = 65280u; }
    if (outer_band > 0) detail::computeBandShift(outer_band, gc.outer_shift, gc.outer_inv);
    else { gc.outer_shift = 0; gc.outer_inv = 65280u; }
    gc.color = color;

    fl::i32 cyfrac = (static_cast<fl::i32>(cyi) << 8) - cy8;
    fl::i32 d2c = dx2 + cyfrac * cyfrac;

    if (cyi >= 0 && cyi < height)
        detail::renderRingRow<PixelT, Overwrite>(pixels, width, cyi, d2c, gc);

    fl::i32 botd2 = d2c, botdelta = 512 * cyfrac + 65536;
    fl::i32 topd2 = d2c, topdelta = -512 * cyfrac + 65536;
    botd2 += botdelta; botdelta += 131072;
    topd2 += topdelta; topdelta += 131072;

    for (int dy = 1; dy <= ri; ++dy) {
        bool cbot = (botd2 - dx2 <= oo2);
        bool ctop = (topd2 - dx2 <= oo2);
        if (!cbot && !ctop) break;
        int pyb = cyi + dy, pyt = cyi - dy;
        if (cbot && pyb >= 0 && pyb < height)
            detail::renderRingRow<PixelT, Overwrite>(pixels, width, pyb, botd2, gc);
        if (ctop && pyt >= 0 && pyt < height)
            detail::renderRingRow<PixelT, Overwrite>(pixels, width, pyt, topd2, gc);
        botd2 += botdelta; botdelta += 131072;
        topd2 += topdelta; topdelta += 131072;
    }
}

template<typename PixelT, typename Coord>
inline void drawRing(Canvas<PixelT>& canvas, const PixelT& color,
                     Coord cx, Coord cy, Coord r, Coord thickness,
                     fl::DrawMode mode) FL_NO_EXCEPT {
    if (mode == fl::DrawMode::DRAW_MODE_OVERWRITE)
        detail::drawRingCore<PixelT, Coord, true>(canvas, color, cx, cy, r, thickness);
    else
        detail::drawRingCore<PixelT, Coord, false>(canvas, color, cx, cy, r, thickness);
}

// ---------------------------------------------------------------------------
// drawStrokeLineCore: Templated on Overwrite for compile-time dispatch.
// ---------------------------------------------------------------------------
template<typename PixelT, typename Coord, bool Overwrite>
inline void detail::drawStrokeLineCore(Canvas<PixelT>& canvas, const PixelT& color,
                                       Coord x0, Coord y0, Coord x1, Coord y1,
                                       Coord thickness, LineCap cap) FL_NO_EXCEPT {
    PixelT* pixels = canvas.pixels;
    int width = canvas.width;
    int height = canvas.height;

    fl::i32 x0_8 = detail::toFixed8(x0), y0_8 = detail::toFixed8(y0);
    fl::i32 x1_8 = detail::toFixed8(x1), y1_8 = detail::toFixed8(y1);
    fl::i32 dx_8 = x1_8 - x0_8;
    fl::i32 dy_8 = y1_8 - y0_8;
    if (dx_8 == 0 && dy_8 == 0) return;

    fl::i32 len2_16 = dx_8 * dx_8 + dy_8 * dy_8;
    fl::i32 len_8 = static_cast<fl::i32>(
        fl::isqrt32(static_cast<fl::u32>(len2_16)));
    fl::i32 thickness_8 = detail::toFixed8(thickness);
    fl::i32 r_max_8 = thickness_8 >> 1;
    fl::i32 threshold_q = r_max_8 * len_8;

    if (threshold_q <= 0) return;

    int x0i = x0_8 >> 8, y0i = y0_8 >> 8;
    int x1i = x1_8 >> 8, y1i = y1_8 >> 8;
    int margin = ((r_max_8 + 255) >> 8) + 2;
    int xmin = (x0i < x1i ? x0i : x1i) - margin;
    int xmax = (x0i > x1i ? x0i : x1i) + margin;
    int ymin = (y0i < y1i ? y0i : y1i) - margin;
    int ymax = (y0i > y1i ? y0i : y1i) + margin;

    if (xmin < 0) xmin = 0;
    if (xmax >= width) xmax = width - 1;
    if (ymin < 0) ymin = 0;
    if (ymax >= height) ymax = height - 1;
    if (xmin > xmax || ymin > ymax) return;

    fl::i32 dx_q = dx_8 << 8;
    fl::i32 dy_q = dy_8 << 8;

    fl::i32 rx_base_8 = (static_cast<fl::i32>(xmin) << 8) - x0_8;
    fl::i32 ry_row_8 = (static_cast<fl::i32>(ymin) << 8) - y0_8;
    fl::i32 cross_row_q = rx_base_8 * dy_8 - ry_row_8 * dx_8;
    fl::i32 dot_row_q = rx_base_8 * dx_8 + ry_row_8 * dy_8;

    fl::i32 len2_q = len_8 * len_8;
    fl::i32 dot_ext_q = (cap == LineCap::SQUARE) ? threshold_q : 0;

    detail::StrokeCtx<PixelT> sc;
    sc.threshold_q = threshold_q;
    sc.len2_q = len2_q;
    sc.dx_q = dx_q;
    sc.dy_q = dy_q;
    sc.dot_ext_q = dot_ext_q;
    detail::computeBandShift(threshold_q, sc.aa_shift, sc.aa_inv);
    sc.x0_8 = x0_8; sc.y0_8 = y0_8;
    sc.x1_8 = x1_8; sc.y1_8 = y1_8;
    sc.r_max2_8 = r_max_8 * r_max_8;
    sc.cap_shift = 0; sc.cap_inv = 0;
    if (cap == LineCap::ROUND && sc.r_max2_8 > 0) {
        detail::computeBandShift(sc.r_max2_8, sc.cap_shift, sc.cap_inv);
    }
    sc.cap = cap;
    sc.color = color;
    sc.xmin = xmin; sc.xmax = xmax;

    for (int py = ymin; py <= ymax; ++py) {
        if (py >= 0 && py < height) {
            detail::renderStrokeRow<PixelT, Overwrite>(pixels, width, py,
                                                       cross_row_q, dot_row_q, sc);
        }
        cross_row_q -= sc.dx_q;
        dot_row_q += sc.dy_q;
    }
}

template<typename PixelT, typename Coord>
inline void drawStrokeLine(Canvas<PixelT>& canvas, const PixelT& color,
                           Coord x0, Coord y0, Coord x1, Coord y1, Coord thickness,
                           LineCap cap, fl::DrawMode mode) FL_NO_EXCEPT {
    if (mode == fl::DrawMode::DRAW_MODE_OVERWRITE)
        detail::drawStrokeLineCore<PixelT, Coord, true>(canvas, color, x0, y0, x1, y1, thickness, cap);
    else
        detail::drawStrokeLineCore<PixelT, Coord, false>(canvas, color, x0, y0, x1, y1, thickness, cap);
}

// ---------------------------------------------------------------------------
// drawPolygonCore: even-odd fill with an exact per-pixel area model,
// organized as row bands.
//
// Each non-horizontal edge is active in every canvas row it geometrically
// occupies (py0 = the row containing ytop, py1 = the row containing ybot),
// and each row is split into sub-bands at vertex events (vertices whose y
// falls strictly inside the row). Within a band the crossing set is
// constant: an edge participates iff ytop < band_top && ybot > band_bottom
// (strict — at each vertex one edge ends and the other begins, so the
// crossing count is the same in every band of a closed polygon). Edges in a
// band are sorted by their actual x at the band midpoint (one exact floor
// division per edge per band) and paired (0,1),(2,3),... For simple
// polygons this is exactly the even-odd rule; self-intersecting polygons
// follow the same deterministic span-pairing convention.
//
// Pixel coverage is accumulated additively over the row's bands into a
// 64-px chunk accumulator in T units (kPolyOne = 2^28 = full pixel), then
// quantized once per pixel: br = (acc*255 + ONE/2) >> 28. Coverage is
// exact for linear edges (see polyLineT28): for a simple polygon the
// per-pixel sum equals the true sub-pixel area, so total energy equals the
// shoelace area up to u8 rounding.
//
// Vertices beyond 32 are ignored (the polygon closes from #31 back to #0).
// Worst-case stack: edges[32] (2048 B) + bands[33] (1452 B) +
// ev/keys/idx/accb (~650 B) ≈ 4.2 KB.
// ---------------------------------------------------------------------------
template<typename PixelT, typename Coord, bool Overwrite>
inline void detail::drawPolygonCore(Canvas<PixelT>& canvas, const PixelT& color,
                                    fl::span<const PolyPoint<Coord>> pts) FL_NO_EXCEPT {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return;
    PixelT* pixels = canvas.pixels;
    int width = canvas.width;
    int height = canvas.height;
    if (width <= 0 || height <= 0) return;

    const int nv = (n < 32) ? n : 32;

    // Convert vertices to Q16.16 (clamped to ±8191 px — i32-safe)
    fl::i32 vx[32];
    fl::i32 vy[32];
    for (int i = 0; i < nv; ++i) {
        vx[i] = detail::toQ16Clamped(pts[i].x);
        vy[i] = detail::toQ16Clamped(pts[i].y);
    }

    // Build per-edge scanline state
    PolyEdge edges[32];
    int nedges = 0;
    int ymin = height, ymax = -1;
    for (int i = 0; i < nv; ++i) {
        const int j = (i + 1 == nv) ? 0 : (i + 1);
        fl::i32 dy = vy[j] - vy[i];
        if (dy == 0) continue;  // horizontal edge: ignored (even-odd convention)
        // Orient edge top-to-bottom: B measured from the top vertex
        fl::i32 ytop, xtop, B;
        if (dy > 0) { ytop = vy[i]; xtop = vx[i]; B = vx[j] - vx[i]; }
        else        { ytop = vy[j]; xtop = vx[j]; B = vx[i] - vx[j]; }
        const fl::i32 dy2 = (dy > 0) ? dy : -dy;  // span height, > 0
        const fl::i32 ybot = ytop + dy2;

        // Active pixel rows: the row containing ytop through the row
        // containing ybot (floor/ceil rule; rows are half-open so a vertex
        // on a row boundary belongs to exactly one edge's row range).
        int py0 = static_cast<int>(detail::floorDivI64(ytop, 65536));
        int py1 = static_cast<int>(detail::floorDivI64((fl::i64)ybot - 1, 65536));
        if (py0 < 0) py0 = 0;
        if (py1 >= height) py1 = height - 1;
        if (py0 > py1) continue;  // edge off-canvas vertically

        PolyEdge& ed = edges[nedges++];
        ed.py0 = py0;
        ed.py1 = py1;
        ed.dy = dy2;
        ed.B = B;
        ed.ytop = ytop;
        ed.ybot = ybot;

        // Exact crossing at py0's row center (one i64 division at setup):
        // x = xtop + floor(w0 * B / dy2), w0 = (py0<<16) + 32768 - ytop
        const fl::i64 w0 = (static_cast<fl::i64>(py0) << 16) + 32768 - ytop;
        const fl::i64 prod = w0 * B;
        const fl::i64 q = detail::floorDivI64(prod, dy2);
        ed.x = xtop + q;
        if (dy2 >= 65536) {
            // Multi-row: 65536*B = step*dy2 + rem, rem in [0, dy2)
            const fl::i64 p2 = static_cast<fl::i64>(65536) * B;
            const fl::i64 s = detail::floorDivI64(p2, dy2);
            ed.step = s;
            ed.rem = p2 - s * dy2;
            ed.t = prod - q * dy2;  // in [0, dy2)
        } else {
            // Span < 1 px: at most one active row (py0 == py1), no carry
            ed.step = 0;
            ed.rem = 0;
            ed.t = 0;
        }
        if (ed.py0 < ymin) ymin = ed.py0;
        if (ed.py1 > ymax) ymax = ed.py1;
    }
    if (nedges == 0) return;

    // Scan rows
    fl::i32 ev[34];      // band boundaries: row ends + vertex y's inside row
    fl::u8 idx[32];      // a band's active edges, sorted by x at midpoint
    fl::i64 keys[32];    // sort keys: actual x at band midpoint (Q16.16)
    PolyBand bands[33];  // up to 34 boundaries -> 33 bands per row
    fl::i32 accb[64];    // per-row 64-px chunk coverage accumulator (T units)

    for (int py = ymin; py <= ymax; ++py) {
        const fl::i32 yc = (static_cast<fl::i32>(py) << 16) + 32768;

        // Band boundaries: the row ends plus every vertex strictly inside
        // the row (a vertex exactly on a row boundary coincides with a band
        // edge — the floor/ceil row rule already splits the edges there).
        int nev = 0;
        ev[nev++] = -32768;
        ev[nev++] = 32768;
        for (int i = 0; i < nv; ++i) {
            if (detail::floorDivI64(vy[i], 65536) == py && vy[i] % 65536 != 0)
                ev[nev++] = vy[i] - yc;
        }
        // Insertion sort (nev <= 34)
        for (int a = 1; a < nev; ++a) {
            const fl::i32 v = ev[a];
            int b = a - 1;
            while (b >= 0 && ev[b] > v) { ev[b + 1] = ev[b]; --b; }
            ev[b + 1] = v;
        }

        // Per-row x extents of each active edge (x is at the row center):
        // the line stays within x +/- hw over the row, so px outside
        // [lx0, lx1] is a full/zero shortcut for polyLineT28.
        for (int e = 0; e < nedges; ++e) {
            PolyEdge& ed = edges[e];
            if (py < ed.py0 || py > ed.py1) continue;
            const fl::i32 absB = (ed.B < 0) ? -ed.B : ed.B;
            const fl::i64 hw = (absB == 0) ? 0
                : ((static_cast<fl::i64>(absB) * 32768 + ed.dy - 1) / ed.dy);
            ed.lx0 = static_cast<fl::i32>(detail::floorDivI64(ed.x - hw, 65536));
            ed.lx1 = static_cast<fl::i32>(detail::floorDivI64(ed.x + hw, 65536));
        }

        // Build this row's bands: active edges sorted by x at the midpoint.
        // mid = a + b (2x the midpoint) keeps the key an exact integer:
        // x_mid = floor((2*dy*x + B*mid) / (2*dy)).
        int nbands = 0;
        for (int k = 0; k + 1 < nev; ++k) {
            const fl::i32 a = ev[k], b = ev[k + 1];
            if (a >= b) continue;
            int nc = 0;
            for (int e = 0; e < nedges; ++e) {
                const PolyEdge& ed = edges[e];
                if (py >= ed.py0 && py <= ed.py1 &&
                    ed.ytop < yc + b && ed.ybot > yc + a)
                    idx[nc++] = (fl::u8)e;
            }
            if (nc < 2) continue;
            const fl::i32 mid = a + b;
            for (int e = 0; e < nc; ++e) {
                const PolyEdge& ed = edges[idx[e]];
                keys[e] = detail::floorDivI64(
                    (fl::i64)2 * ed.dy * ed.x + (fl::i64)ed.B * mid,
                    (fl::i64)2 * ed.dy);
            }
            // Insertion sort of (key, idx) pairs
            for (int e = 1; e < nc; ++e) {
                const fl::i64 kv = keys[e];
                const fl::u8 iv = idx[e];
                int m = e - 1;
                while (m >= 0 && keys[m] > kv) {
                    keys[m + 1] = keys[m];
                    idx[m + 1] = idx[m];
                    --m;
                }
                keys[m + 1] = kv;
                idx[m + 1] = iv;
            }
            PolyBand& bd = bands[nbands++];
            bd.a = a;
            bd.b = b;
            bd.count = (fl::u8)nc;
            for (int e = 0; e < nc; ++e) bd.idx[e] = idx[e];
        }

        // Row extent actually touched (skip chunks outside it)
        int xmin = width, xmax = -1;
        for (int bnd = 0; bnd < nbands; ++bnd) {
            const PolyBand& bd = bands[bnd];
            for (int p = 0; p + 1 < bd.count; p += 2) {
                const PolyEdge& L = edges[bd.idx[p]];
                const PolyEdge& R = edges[bd.idx[p + 1]];
                int pl = (L.lx0 < R.lx0) ? L.lx0 : R.lx0;
                if (pl < 0) pl = 0;
                int ph = (L.lx1 > R.lx1) ? L.lx1 : R.lx1;
                if (ph >= width) ph = width - 1;
                if (pl > ph) continue;
                if (pl < xmin) xmin = pl;
                if (ph > xmax) xmax = ph;
            }
        }

        // Fill 64-px chunks: accumulate coverage over the bands, then
        // quantize once per pixel.
        for (int cs = xmin & ~63; cs <= xmax; cs += 64) {
            for (int i = 0; i < 64; ++i) accb[i] = 0;
            const int ce = (cs + 64 <= xmax + 1) ? cs + 64 : xmax + 1;
            for (int bnd = 0; bnd < nbands; ++bnd) {
                const PolyBand& bd = bands[bnd];
                const fl::i32 a = bd.a, b = bd.b;
                // Full-pixel coverage over this band = the band height
                const fl::i64 full =
                    (static_cast<fl::i64>(b - a) * kPolyOne) >> 16;
                for (int p = 0; p + 1 < bd.count; p += 2) {
                    const PolyEdge& L = edges[bd.idx[p]];
                    const PolyEdge& R = edges[bd.idx[p + 1]];
                    int pl = (L.lx0 < R.lx0) ? L.lx0 : R.lx0;
                    if (pl < cs) pl = cs;
                    int ph = (L.lx1 > R.lx1) ? L.lx1 : R.lx1;
                    if (ph >= ce) ph = ce - 1;
                    if (pl > ph) continue;
                    fl::i64 uL = (fl::i64)L.dy * (L.x - ((fl::i64)pl << 16));
                    fl::i64 uR = (fl::i64)R.dy * (R.x - ((fl::i64)pl << 16));
                    const fl::i64 suL = -((fl::i64)L.dy << 16);
                    const fl::i64 suR = -((fl::i64)R.dy << 16);
                    for (int px = pl; px <= ph; ++px) {
                        fl::i32 TL = (px > L.lx1) ? 0
                            : (px < L.lx0) ? (fl::i32)full
                            : polyLineT28(L.B, L.dy, uL, a, b);
                        fl::i32 TR = (px > R.lx1) ? 0
                            : (px < R.lx0) ? (fl::i32)full
                            : polyLineT28(R.B, R.dy, uR, a, b);
                        const fl::i32 d = TR - TL;
                        if (d > 0) accb[px - cs] += d;
                        uL += suL;
                        uR += suR;
                    }
                }
            }
            PixelT* row = pixels + py * width + cs;
            for (int i = 0; i < ce - cs; ++i) {
                const fl::i32 v = accb[i];
                if (v <= 0) continue;
                const fl::u8 br = (v >= kPolyOne)
                    ? 255
                    : static_cast<fl::u8>(((fl::u64)v * 255u
                        + (fl::u64)(kPolyOne >> 1)) >> 28);
                if (br == 0) continue;
                PixelT c = color;
                c.nscale8(br);
                if (Overwrite) row[i] = c;
                else row[i] += c;
            }
        }

        // Carry edge crossings to the next row's center (exact; t + rem
        // < 2*dy < 2^31, so the signed compare is safe)
        for (int e = 0; e < nedges; ++e) {
            PolyEdge& ed = edges[e];
            if (py >= ed.py0 && py <= ed.py1) {
                ed.x += ed.step;
                ed.t += ed.rem;
                if (ed.t >= ed.dy) {
                    ed.t -= ed.dy;
                    ++ed.x;
                }
            }
        }
    }
}

template<typename PixelT, typename Coord>
inline void drawPolygon(Canvas<PixelT>& canvas, const PixelT& color,
                        fl::span<const PolyPoint<Coord>> pts,
                        fl::DrawMode mode) FL_NO_EXCEPT {
    if (mode == fl::DrawMode::DRAW_MODE_OVERWRITE)
        detail::drawPolygonCore<PixelT, Coord, true>(canvas, color, pts);
    else
        detail::drawPolygonCore<PixelT, Coord, false>(canvas, color, pts);
}

template<typename PixelT, typename Coord, int N>
inline void drawPolygon(Canvas<PixelT>& canvas, const PixelT& color,
                        const PolyPoint<Coord> (&pts)[N],
                        fl::DrawMode mode) FL_NO_EXCEPT {
    drawPolygon(canvas, color, fl::span<const PolyPoint<Coord>>(pts, N), mode);
}

}  // namespace gfx
}  // namespace fl
