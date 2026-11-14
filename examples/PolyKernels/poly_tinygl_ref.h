#pragma once

/// @file tests/fl/gfx/poly_tinygl_ref.h
/// @brief Test/benchmark-only faithful C++ port of aykevl/tinygl's polygon fill
///
/// Faithful port of the A-buffer scanline polygon fill from aykevl/tinygl
/// (https://github.com/aykevl/tinygl, MIT license, Copyright (c) 2023 Ayke
/// van Laethem; see https://aykevl.nl/2024/02/tinygl-polygon/). Source
/// files: gfx/polygon.go (render loop) and gfx/line.go (edge construction
/// and slope math).
///
/// This is a BENCHMARK REFERENCE, not a product backend: it is not part of
/// the public API (no PolyFill enumerator) and is kept in tests/ so it can
/// be retired without an API change. It is deliberately unoptimized beyond
/// the port itself — the point is to measure the reference algorithm's
/// structure (monotonic edge pointers, per-pixel sub-scanline recomputation,
/// per-pixel popcount) against fl::gfx::PolyFill::Exact and
/// fl::gfx::PolyFill::ABuffer.
///
/// # Go int32 semantics (emulated)
///
/// The Go reference does all scanline math in int32 with wrapping products
/// and truncating division (a latent overflow for steep/sub-pixel edges).
/// This port emulates that faithfully:
///  - wrapping i32 products go through unsigned math (no UB),
///  - division truncates toward zero (C++ i32 division, like Go),
///  - right shifts are arithmetic (gcc/clang, like Go on the targets
///    tinygl ships for).
///
/// # Documented deviations from the Go reference
///
/// 1. **xinc setup.** Go computes `int32((x2-x1) << 16 / (y2-y1))`, which
///    is only defined for whole-pixel dy (tinygl lines have int16
///    endpoints). Our vertices are fractional Q16.16, so the slope is
///    computed as i32-trunc(i64(dx16) * 65536 / dy16) — bit-identical to
///    the Go expression for whole-pixel vertices, defined for all
///    non-horizontal edges.
/// 2. **Edge order.** The reference line shapes use a fixed edge order
///    that is sorted left-to-right; the blog's general polygon requires a
///    static edge order, so this port sorts edges by xstart once at setup
///    (stable). The monotonic edgeStart/edgeEnd pointer walk is kept
///    verbatim, including its limitation for self-intersecting polygons
///    (the x-order flips at the crossing and the far side mis-renders).
/// 3. **Left of the canvas.** The reference starts the x-walk at the
///    unclamped xleft of the first edge (possibly negative) and
///    bounds-checks every write (blendPixel/drawLine). This port does the
///    same: the walk starts at the unclamped xleft and writes are
///    clamped to the canvas — identical behavior, no out-of-bounds access.
/// 4. **Compositing.** The reference's naiveBlend is an "over" blend and
///    its full-span fast path overwrites even mid-composition (its canvas
///    is repainted from scratch every frame). Here both use FastLED's
///    model — OVERWRITE: assign; BLEND: additive — so all three polygon
///    backends compose the same way and quality comparisons measure the
///    sampling, not the compositing.
/// 5. **Division by 4.** Go's `v / 4` (int32) truncates toward zero; this
///    port writes it as the exact shift form
///    `v >= 0 ? (v >> 2) : ((v + 3) >> 2)` (polyTGDiv4). The result is
///    bit-identical for every int32 (4 is a power of two), but the shift
///    form is required, not merely faster: the clang AVR backend used by
///    the fbuild firmware build (clang++ --target=avr -Os) miscompiles the
///    plain `product / 4` form in this file, inverting the operands of the
///    product division — it emits `4 / product` (verified by
///    disassembly: `__mulsi3` followed by `__divmodsi4` with the constant
///    4 loaded as the dividend). In the x-walk that garbage makes the
///    reference walk non-terminating (a 300M-instruction AVR profile spent
///    ~60% of its instructions in `__udivmodsi4` and completed 0 frames).
///    The shift form compiles to a few instructions on every target.
/// 6. **Loop form.** That same backend also miscompiles the reference's
///    plain scanline loops in this file: it inverts the induction
///    increment without flipping the loop condition. The y-walk
///    `for (y = ymin; y < ymax; ++y)` is emitted as "keep looping while
///    y < ymax" with a `y--` latch (disassembly: condition at 0x1416,
///    decrement at 0x14b2 in the affected firmware), and the x-walk
///    `while (x < width) { ...; ++x }` the same way (0x155a / 0x1a16).
///    Both loops therefore never terminate: a 300M-instruction AVR
///    profile of the affected firmware completed 0 frames with ~79% of
///    its cycles spinning inside the walk. The bug is code-layout-
///    dependent — the same source in the fbuild ATmega328P build
///    (different layout) completed scenes — so the port writes both
///    loops as explicit countdowns, which are behavior-identical: the
///    y-rows in bottom-to-top order (row order is irrelevant — each row
///    touches only its own pixels and the per-pixel compositing is
///    commutative), and the x-walk as a countdown over the remaining
///    pixel count with the pixel derived as `x = width - n` (strictly
///    left-to-right, which the mask-extension semantics require). Both
///    forms were verified directionally correct by disassembly of the
///    fixed firmware.
/// 7. **Fast-path termination on the sentinel.** Once the loops are fixed,
///    the full-span fast path (`mask == 0xffffffff`) must end the row when
///    `edgeEndLeft` has become the `0x7fffffff` "no more edges" sentinel,
///    but two AVR-specific defects made it spin forever (0 frames):
///    (a) the backend cached `edgeEndLeft` in a register that the
///    subline loop's `__mulsi3` clobbers without a reload, so the fast
///    path read a stale value (the last real `polyTGXleft` result) rather
///    than the fresh slot — made `volatile` so every read comes from the
///    slot; and (b) `int` is 16-bit on AVR, so the Go-faithful
///    `n = width - (int)edgeEndLeft` truncated the sentinel to `0xffff`
///    (-1) and wrapped `n` to `width+1` — a small positive value that
///    re-armed the walk at `x = -1` (both the stale -1 and the fresh
///    0x7fffffff truncate to the same 0xffff, which is why (a) alone
///    fixed nothing). The port now tests the at/past-right-edge case in
///    full 32-bit and ends the row there; a real in-range edge is always
///    `< width`, so the remaining 16-bit casts are safe. A register probe
///    of the affected firmware showed the walk stuck at `n=width+1,
///    x=-1` with the advance-check reading the correct sentinel while the
///    fast path used the truncated one (~8.7k fast-path spins per row).
///
/// # Sampling geometry (kept faithful)
///
/// Sub-scanline s = 4*y + r is in range for an edge when 0 <= s - ytop <
/// ybottom - ytop (half-open .2 bands). The edge crossing is
/// `xstart + (s - ytop) * xinc / 4` (wrapping i32 product, truncating
/// division by 4). A sub-column c of pixel x is covered when
/// `bitOffsetX = (xoffset - x*2^16) >> 13` falls in [0, 8): sub-columns
/// bitOffsetX..7 (right edge of the sub-column strictly right of the
/// crossing). Mask rows extend with `(mask & 0x01010101) * 255`. Alpha is
/// `popcount(mask) * 8` (max 248; the all-32 case is the full-span fast
/// path).

#include "fl/stl/noexcept.h"
#include "fl/stl/int.h"
#include "fl/math/fixed_point/s16x16.h"
#include "fl/gfx/canvas.h"
#include "fl/gfx/poly_abuffer.h"  // polyABToQ16 (vertex -> Q16.16, clamped)

namespace fl {
namespace gfx {
namespace detail {

/// Wrapping int32 multiply (Go's int32 * int32), without UB.
inline fl::i32 polyTGWrapMul(fl::i32 a, fl::i32 b) FL_NO_EXCEPT {
    return static_cast<fl::i32>(static_cast<fl::u32>(a) * static_cast<fl::u32>(b));
}

/// Signed division by 4, truncating toward zero — Go/C++ semantics, written
/// as shifts (deviation 5 below). Bit-identical to `p / 4` for every int32,
/// and immune to the clang-avr miscompilation that inverts the plain form.
inline fl::i32 polyTGDiv4(fl::i32 p) FL_NO_EXCEPT {
    return (p >= 0) ? (p >> 2) : ((p + 3) >> 2);
}

/// One reference edge (trapezoid), verbatim from polygon.go.
struct TgEdge {
    fl::i32 ytop;     // .2 (4 sub-scanlines per pixel), half-open
    fl::i32 ybottom;  // .2
    fl::i32 xstart;   // .16, X at ytop
    fl::i32 xinc;     // .16 per pixel
};

/// polygon.go: (e polygonEdge) inrange — Go int32 division (truncating,
/// written as shifts; deviation 5).
inline bool polyTGInrange(const TgEdge& e, int y) FL_NO_EXCEPT {
    return y >= polyTGDiv4(e.ytop) && y < polyTGDiv4(e.ybottom + 3);
}

/// polygon.go: (e polygonEdge) xleft — Go precedence: the product/division
/// first, then the add of xstart. Wrapping product, truncating /4
/// (deviation 5), arithmetic >>16 (all Go semantics).
inline fl::i32 polyTGXleft(const TgEdge& e, int y) FL_NO_EXCEPT {
    const fl::i32 edgeY = y * 4 - e.ytop;
    if (e.xinc > 0)
        return (e.xstart + polyTGDiv4(polyTGWrapMul(edgeY, e.xinc))) >> 16;
    return (e.xstart + polyTGDiv4(polyTGWrapMul(edgeY + 3, e.xinc))) >> 16;
}

/// polygon.go: (e polygonEdge) xright.
inline fl::i32 polyTGXright(const TgEdge& e, int y) FL_NO_EXCEPT {
    const fl::i32 edgeY = y * 4 - e.ytop;
    if (e.xinc > 0)
        return (e.xstart + polyTGDiv4(polyTGWrapMul(edgeY + 3, e.xinc))) >> 16;
    return (e.xstart + polyTGDiv4(polyTGWrapMul(edgeY, e.xinc))) >> 16;
}

/// Popcount of a u32 (Go's bits.OnesCount32; gcc/clang builtin on host and
/// AVR).
inline fl::u8 polyTGPopcount32(fl::u32 v) FL_NO_EXCEPT {
#if defined(__GNUC__) || defined(__clang__)
    return (fl::u8)__builtin_popcount(v);
#else
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    return (fl::u8)((v + (v >> 4) & 0x0F0F0F0Fu) * 0x01010101u >> 24);
#endif
}

/// Faithful port of polygon.go drawPolygon (see file header).
/// MAXN caps the vertex count like the other backends.
template<typename PixelT, typename Coord, bool Overwrite, int MAXN>
inline void drawPolygonTinyGLRef(Canvas<PixelT>& canvas, const PixelT& color,
                                 fl::span<const PolyPoint<Coord>> pts) FL_NO_EXCEPT {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return;
    PixelT* pixels = canvas.pixels;
    const int width = canvas.width;
    const int height = canvas.height;
    if (width <= 0 || height <= 0) return;

    const int nv = (n < MAXN) ? n : MAXN;

    // Vertices to Q16.16 (clamped to ±8191 px, same domain as the other
    // backends).
    fl::i32 vx[MAXN];
    fl::i32 vy[MAXN];
    for (int i = 0; i < nv; ++i) {
        vx[i] = polyABToQ16(pts[i].x);
        vy[i] = polyABToQ16(pts[i].y);
    }

    // Edge setup: orient top-to-bottom (line.go), skip horizontal edges,
    // xinc = i32-trunc(dx16 * 65536 / dy16) (deviation 1).
    TgEdge edges[MAXN];
    int ne = 0;
    for (int i = 0; i < nv; ++i) {
        const int j = (i + 1 == nv) ? 0 : i + 1;
        fl::i32 y1 = vy[i], y2 = vy[j];
        fl::i32 x1 = vx[i], x2 = vx[j];
        if (y1 > y2) {
            const fl::i32 ty = y1; y1 = y2; y2 = ty;
            const fl::i32 tx = x1; x1 = x2; x2 = tx;
        }
        if (y1 == y2) continue;
        TgEdge& e = edges[ne++];
        e.ytop = y1 >> 14;      // .2 (arithmetic shift, Go semantics)
        e.ybottom = y2 >> 14;
        e.xstart = x1;
        // C++ i64 division truncates toward zero, like Go.
        const fl::i64 num = (fl::i64)(x2 - x1) * 65536L;
        const fl::i64 den = (fl::i64)(y2 - y1);
        e.xinc = static_cast<fl::i32>(num / den);
    }
    if (ne == 0) return;

    // Static edge order (deviation 2): stable sort by xstart.
    for (int a = 1; a < ne; ++a) {
        const TgEdge v = edges[a];
        int b = a - 1;
        while (b >= 0 && edges[b].xstart > v.xstart) { edges[b + 1] = edges[b]; --b; }
        edges[b + 1] = v;
    }

    // Vertical bounds (polygon.go updateBounds, Go truncating division
    // written as shifts; deviation 5).
    int ymin = height, ymax = -1;
    for (int i = 0; i < ne; ++i) {
        const int yt = polyTGDiv4(edges[i].ytop);
        const int yb = polyTGDiv4(edges[i].ybottom + 3);
        if (yt < ymin) ymin = yt;
        if (yb > ymax) ymax = yb;
    }
    if (ymin < 0) ymin = 0;
    if (ymax > height) ymax = height;
    if (ymin >= ymax) return;

    // The scanline walk (polygon.go drawPolygon), verbatim structure.
    // (deviation 6: explicit bottom-to-top countdown; row order is
    // irrelevant — each row touches only its own pixels.)
    for (int y = ymax - 1; y >= ymin; --y) {
        int edgeStart = 0;
        while (edgeStart < ne && !polyTGInrange(edges[edgeStart], y)) ++edgeStart;
        if (edgeStart >= ne) continue;

        fl::i32 edgeStartRight = polyTGXright(edges[edgeStart], y);
        int edgeEnd = edgeStart;
        // volatile: deviation 7. The full-span fast path below reads
        // edgeEndLeft *after* the per-pixel subline loop, and under
        // clang-avr -Os the subline loop's __mulsi3 clobbers the register
        // edgeEndLeft is cached in without it being reloaded from its
        // stack slot. The advance-loop condition reads the (correct,
        // up-to-date) slot, but the fast path read the stale register —
        // the last real polyTGXleft result instead of the 0x7fffffff
        // sentinel — and jumped the walk backwards, spinning forever.
        // volatile forces every read (in particular the fast-path read)
        // to come from the slot.
        volatile fl::i32 edgeEndLeft = polyTGXleft(edges[edgeEnd], y);

        fl::u32 mask = 0;
        // The x-walk starts at the UNCLAMPED xleft (possibly negative;
        // deviation 3 keeps the walk faithful, writes are clamped).
        // (deviation 6: countdown over the remaining pixel count with x
        // derived per iteration — strictly left-to-right, but in a form
        // the clang AVR backend compiles correctly.)
        const int x0 = static_cast<int>(polyTGXleft(edges[edgeStart], y));
        int n = width - x0;
        while (n > 0) {
            const int x = width - n;
            --n;
            while (edgeStartRight < (fl::i32)x) {
                ++edgeStart;
                while (edgeStart < ne && !polyTGInrange(edges[edgeStart], y)) ++edgeStart;
                edgeStartRight = (edgeStart < ne) ? polyTGXright(edges[edgeStart], y)
                                                  : 0x7fffffff;
            }
            if (edgeStart >= ne) break;
            while (edgeEndLeft <= (fl::i32)x) {
                ++edgeEnd;
                while (edgeEnd < ne && !polyTGInrange(edges[edgeEnd], y)) ++edgeEnd;
                edgeEndLeft = (edgeEnd < ne) ? polyTGXleft(edges[edgeEnd], y)
                                             : 0x7fffffff;
            }

            for (int i = edgeStart; i < edgeEnd; ++i) {
                const TgEdge& ed = edges[i];
                if (!polyTGInrange(ed, y)) continue;
                for (int subline = 0; subline < 4; ++subline) {
                    const fl::i32 sublineY = y * 4 + subline - ed.ytop;  // .2
                    if (static_cast<fl::u32>(sublineY) >=
                        static_cast<fl::u32>(ed.ybottom - ed.ytop)) continue;
                    const fl::i32 xoffset = ed.xstart + polyTGDiv4(polyTGWrapMul(sublineY, ed.xinc));
                    const fl::i32 bitOffsetX = (xoffset - ((fl::i32)x << 16)) >> 13;
                    if (static_cast<fl::u32>(bitOffsetX) >= 8u) continue;
                    const fl::u32 bits = (1u << (8u - static_cast<fl::u32>(bitOffsetX))) - 1u;
                    mask ^= bits << (subline * 8);
                }
            }

            if (mask != 0) {
                if (mask == 0xffffffffu) {
                    // Full-span fast path (polygon.go drawLine): the whole
                    // run up to the next edge's xleft is solid color.
                    //
                    // edgeEndLeft is the 0x7fffffff sentinel once the edge
                    // walk has advanced past the last in-range edge. `int`
                    // is 16-bit on AVR, so the Go-faithful
                    // `n = width - (int)edgeEndLeft` would truncate the
                    // sentinel to 0xffff (-1) and wrap n to width+1 — a
                    // small positive value that re-arms the walk and spins
                    // it forever instead of ending the row (deviation 7).
                    // Test the at/past-right-edge case in full 32-bit first
                    // (a real in-range edge is always < width, so the
                    // 16-bit casts below are safe for it) and end the row.
                    const bool runToRightEdge = edgeEndLeft >= (fl::i32)width;
                    int runX0 = x;
                    if (runX0 < 0) runX0 = 0;
                    int runX1 = runToRightEdge ? width
                                               : static_cast<int>(edgeEndLeft);
                    if (runX1 > runX0) {
                        PixelT* row = pixels + y * width + runX0;
                        if (Overwrite) {
                            for (int i = 0; i < runX1 - runX0; ++i) row[i] = color;
                        } else {
                            for (int i = 0; i < runX1 - runX0; ++i) row[i] += color;
                        }
                    }
                    if (runToRightEdge) break;  // run reached the right edge
                    n = width - runX1;  // jump (skips the --n)
                    continue;
                }
                // alpha = popcount(mask) << 3 (0..248; all-32 is the fast
                // path above, so the u8 cast never wraps).
                const fl::u8 alpha = (fl::u8)(polyTGPopcount32(mask) * 8u);
                PixelT c = color;
                if (alpha < 255u) c.nscale8(alpha);
                // Go's blendPixel bounds-checks every write; y is in
                // range (the y-walk is clamped to the canvas) but x may
                // be negative (deviation 3).
                if (x >= 0) {
                    PixelT& px = pixels[y * width + x];
                    if (Overwrite) px = c;
                    else px += c;
                }
                // Extend rightmost subpixels to full sub-scanlines.
                mask = (mask & 0x01010101u) * 255u;
            }
        }
    }
}

}  // namespace detail
}  // namespace gfx
}  // namespace fl
