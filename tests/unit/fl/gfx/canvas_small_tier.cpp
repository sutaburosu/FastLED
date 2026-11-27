// Compile-only constrained-tier default probe. This target intentionally
// uses no shared test PCH: the -D override changes the default template
// arguments the headers resolve, so compiling through the host-tier PCH
// would bake in the large-tier defaults. It does link the host-tier
// library, which is safe: the polygon kernel is header-inline and no
// linked symbol's body branches on the tier flag, so the override changes
// only default arguments, not any linked body (same probe pattern as
// color_profile_tiny_layout.cpp).

#include "FastLED.h"
#include "fl/gfx/gfx.h"
#include "fl/stl/static_assert.h"

// Forced small stack, host scratch default (FL_SCRATCH_STACK_BYTES = 0) =>
// the constrained polygon tier: MAXN 8, CHUNK 16, and — the point of this
// probe — the default drawPolygon fill follows the tier to ABuffer.
FL_STATIC_ASSERT(FL_PLATFORM_HAS_SMALL_STACK == 1,
                 "small-tier probe must compile with FL_PLATFORM_HAS_SMALL_STACK=1");
FL_STATIC_ASSERT(FL_GFX_POLYGON_LARGE_TIER_DEFAULT == 0,
                 "small-stack, no-arena build must take the constrained tier");
FL_STATIC_ASSERT(FL_GFX_POLYGON_DEFAULT_MAXN == 8,
                 "constrained tier must cap the default at 8 vertices");
FL_STATIC_ASSERT(FL_GFX_POLYGON_DEFAULT_CHUNK == 16,
                 "constrained tier must default to CHUNK 16");
FL_STATIC_ASSERT(FL_GFX_POLYGON_DEFAULT_FILL == fl::gfx::PolyFill::ABuffer,
                 "constrained tier must default to the ABuffer fill");

namespace {
// The default-argument overloads must resolve and instantiate on this
// tier. Only its address is taken: the probe is compile-only, and the
// whole kernel is header-inline, so the small-tier instantiation lives in
// this TU.
void probeDefaultDraw() {
    CRGB buf[16 * 8];
    fl::CanvasRGB canvas(buf, 16, 8);
    fl::PolyPoint<fl::s16x16> pts[5] = {
        {fl::s16x16::from_raw(8 * 65536), fl::s16x16::from_raw(1 * 65536)},
        {fl::s16x16::from_raw(14 * 65536), fl::s16x16::from_raw(6 * 65536)},
        {fl::s16x16::from_raw(11 * 65536), fl::s16x16::from_raw(7 * 65536)},
        {fl::s16x16::from_raw(5 * 65536), fl::s16x16::from_raw(7 * 65536)},
        {fl::s16x16::from_raw(2 * 65536), fl::s16x16::from_raw(6 * 65536)},
    };
    canvas.drawPolygon(CRGB::Red, pts, fl::DrawMode::DRAW_MODE_OVERWRITE);
}
}  // namespace

// Taking the address satisfies -Werror=unused-function while keeping the
// probe non-executing: the check is that the default-argument overloads
// resolve and instantiate on this tier.
// allow-test-main: compile-only probe, deliberately no test harness.
int main() {
    (void)&probeDefaultDraw;
    return 0;
}
