/// @file    flowfield.h
/// @brief   2D flow field visualization: emitters paint color, noise advects it

#pragma once

#include "fl/stl/stdint.h"

#include "crgb.h"
#include "fl/stl/shared_ptr.h"
#include "fl/fx/fx2d.h"
#include "fl/math/xymap.h"
#include "fl/stl/align.h"
#include "fl/stl/vector.h"
#include "fl/math/fixed_point/s16x16.h"

namespace fl {

/// SoA (Structure-of-Arrays) state for FlowFieldFP.
/// All grid arrays are sized to (W*H + 3) & ~3 (padded to next multiple of 4)
/// so SIMD-width loads never read past the allocation.
///
/// Color values are stored as s16x16 raw i32 values.
/// Range [0, 255.0] maps to raw [0, 255 << 16] = [0, 16711680].
struct FlowFieldFPState {
    // Color grids — raw s16x16 values
    fl::vector<fl::i32> r, g, b;        // Main RGB grid
    fl::vector<fl::i32> tr, tg, tb;     // Temp grid for advection pass 1

    // Flow profiles — raw s16x16 values in [-1.0, 1.0] range
    fl::vector<fl::i32> x_prof;          // Per-column vertical shift driver
    fl::vector<fl::i32> y_prof;          // Per-row horizontal shift driver

    // Perlin fade LUT (257 entries, Q8.24 format)
    FL_ALIGNAS(16) fl::i32 fade_lut[257] = {};
    bool fade_lut_initialized = false;

    int count = 0;   // W*H pixel count (padded to multiple of 4)
    int width = 0;
    int height = 0;

    FlowFieldFPState() {}

    void init(int w, int h) {
        width = w;
        height = h;
        count = (w * h + 3) & ~3;  // Pad to multiple of 4

        r.resize(count, 0);
        g.resize(count, 0);
        b.resize(count, 0);
        tr.resize(count, 0);
        tg.resize(count, 0);
        tb.resize(count, 0);
        x_prof.resize(w, 0);
        y_prof.resize(h, 0);
    }
};

FASTLED_SHARED_PTR(FlowField);
FASTLED_SHARED_PTR(FlowFieldFloat);
FASTLED_SHARED_PTR(FlowFieldFP);

/// Configuration parameters for FlowField.
struct FlowFieldParams {
    float persistence = 0.86f;       ///< Trail half-life in seconds
    float color_shift = 0.04f;       ///< Color shift speed
    float flow_amp_x = 1.0f;        ///< Flow amplitude X
    float flow_amp_y = 1.0f;        ///< Flow amplitude Y
    float flow_shift = 1.8f;        ///< Pixel shift amount
    float flow_speed_x = 0.10f;     ///< Noise scroll speed X
    float flow_speed_y = 0.10f;     ///< Noise scroll speed Y
    float noise_freq_x = 0.33f;     ///< Noise frequency X
    float noise_freq_y = 0.32f;     ///< Noise frequency Y
    int dot_count = 3;               ///< Number of orbital dots
    int emitter_mode = 0;            ///< 0=Lissajous, 1=Dots, 2=Both
    float endpoint_speed = 0.80f;    ///< Lissajous endpoint speed
    bool reverse_x_profile = true;   ///< Reverse X profile (matches Python)
    bool show_flow_vectors = false;  ///< Draw flow profiles as overlay
};

/// @brief Abstract base class for 2D flow field effects.
///
/// Manages common timing and parameters. Concrete subclasses (FlowFieldFloat,
/// FlowFieldFP) implement the actual rendering in drawImpl().
///
/// Concept by Stefan Petrick. Original C++ implementation by 4wheeljive
/// (ColorTrails project). Distilled into a self-contained FastLED Fx2d class.
///
/// Example usage:
/// @code
/// XYMap xymap(22, 22, true);
/// FlowFieldFloat floatFx(xymap);   // float-precision
/// FlowFieldFP    fpFx(xymap);      // fixed-point (fast)
/// FlowField &fx = floatFx;         // use via base pointer/reference
/// fx.draw(ctx);
/// @endcode
class FlowField : public Fx2d {
  public:
    using Params = FlowFieldParams;

    ~FlowField() override = default;

    /// Handles timing, then delegates to drawImpl().
    /// Caps dt to prevent huge jumps when effect was inactive.
    void draw(DrawContext context) override;

    // Parameter setters.
    void setPersistence(float halfLife) { mParams.persistence = halfLife; }
    void setColorShift(float speed) { mParams.color_shift = speed; }
    void setFlowAmplitudeX(float amp) { mParams.flow_amp_x = amp; }
    void setFlowAmplitudeY(float amp) { mParams.flow_amp_y = amp; }
    void setFlowShift(float shift) { mParams.flow_shift = shift; }
    void setFlowSpeedX(float speed) { mParams.flow_speed_x = speed; }
    void setFlowSpeedY(float speed) { mParams.flow_speed_y = speed; }
    void setNoiseFrequencyX(float freq) { mParams.noise_freq_x = freq; }
    void setNoiseFrequencyY(float freq) { mParams.noise_freq_y = freq; }
    void setNoiseFrequency(float freq) {
        mParams.noise_freq_x = freq;
        mParams.noise_freq_y = freq;
    }
    void setDotCount(int count) { mParams.dot_count = count; }
    void setEmitterMode(int mode) { mParams.emitter_mode = mode; }
    void setEndpointSpeed(float speed) { mParams.endpoint_speed = speed; }
    void setReverseXProfile(bool rev) { mParams.reverse_x_profile = rev; }
    void setShowFlowVectors(bool show) { mParams.show_flow_vectors = show; }

    Params &getParams() { return mParams; }
    const Params &getParams() const { return mParams; }

  protected:
    explicit FlowField(const XYMap &xyMap, const Params &params = Params());

    /// Subclasses implement rendering given the time delta and total time.
    /// @param dt_ms  Milliseconds since last draw (capped to prevent huge jumps).
    /// @param t_ms   Milliseconds since first draw.
    virtual void drawImpl(DrawContext context, u32 dt_ms, u32 t_ms) = 0;

    Params mParams;

  private:
    u32 mLastFrameMs = 0;
    u32 mT0 = 0;
    bool mInitialized = false;
};

/// @brief Float-precision flow field implementation.
class FlowFieldFloat : public FlowField {
  public:
    explicit FlowFieldFloat(const XYMap &xyMap, const Params &params = Params());

    fl::string fxName() const override { return "FlowFieldFloat"; }

  protected:
    void drawImpl(DrawContext context, u32 dt_ms, u32 t_ms) override;

  private:
    // Seeded 2D Perlin noise generator.
    class Perlin2D {
      public:
        void init(u32 seed);
        float noise(float x, float y) const;

      private:
        u8 perm[512];
        static float fade(float t) {
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        }
        static float lerp(float a, float b, float t) {
            return a + t * (b - a);
        }
        static float grad(int h, float x, float y);
    };

    // Grid access helpers (row-major layout).
    int idx(int y, int x) const {
        return y * (int)getWidth() + x;
    }

    // Helpers.
    static float fmodPos(float x, float m);
    static float clampf(float v, float lo, float hi);
    static u8 f2u8(float v);
    static CRGB rainbow(float t, float speed, float phase);

    // Sub-steps called from drawImpl().
    void flowPrepare(float t);
    void emitOrbitalDots(float t);
    void flowAdvect(float dt);
    void drawDot(float cx, float cy, float diam,
                 u8 cr, u8 cg, u8 cb);
    void drawAALine(float x0, float y0, float x1, float y1,
                    float t, float colorShift);
    void emitLissajousLine(float t);

    void drawFlowVectors(CRGB *leds);

    // Float-precision RGB grids (main + temp for advection).
    fl::vector<float> mR, mG, mB;
    fl::vector<float> mTR, mTG, mTB;

    // Noise generators and profiles.
    Perlin2D mNoiseGenX, mNoiseGenY;
    fl::vector<float> mXProf; ///< Per-column values; drives vertical shift
    fl::vector<float> mYProf; ///< Per-row values; drives horizontal shift
};

/// @brief Pure fixed-point (s16x16) flow field implementation for maximum speed.
///
/// All internal computation uses Q16.16 fixed-point arithmetic — no float
/// operations in any code path. Public API accepts float parameters which
/// are converted to s16x16 before each draw.
class FlowFieldFP : public FlowField {
  public:
    explicit FlowFieldFP(const XYMap &xyMap, const Params &params = Params());

    fl::string fxName() const override { return "FlowFieldFP"; }

  protected:
    void drawImpl(DrawContext context, u32 dt_ms, u32 t_ms) override;

  private:
    friend struct FlowFieldFPProfiler;  // For per-phase profiling

    // Grid access helpers (row-major layout).
    int idx(int y, int x) const {
        return y * mState.width + x;
    }

    // Helpers.
    static i32 clamp_q16(i32 v, i32 lo, i32 hi);
    static u8 q16_to_u8(i32 v);
    static CRGB rainbow(s16x16 t, s16x16 speed, s16x16 phase);

    // Sub-steps — all pure fixed-point.
    void flowPrepare(s16x16 t);
    void emitOrbitalDots(s16x16 t);
    void flowAdvect(i32 dt_raw);
    void drawDot(s16x16 cx, s16x16 cy, s16x16 diam,
                 u8 cr, u8 cg, u8 cb);
    void drawAALine(s16x16 x0, s16x16 y0, s16x16 x1, s16x16 y1,
                    s16x16 t, s16x16 colorShift);
    void emitLissajousLine(s16x16 t);

    void drawFlowVectors(CRGB *leds);

    // Convert float params to cached s16x16 values.
    void syncParams();

    // Perm table initialization (Fisher-Yates shuffle).
    static void initPerm256(u8 *perm, u32 seed);

    FlowFieldFPState mState;
    u8 mPermX[256] = {};
    u8 mPermY[256] = {};

    // Cached s16x16 versions of float params.
    s16x16 mColorShift_fp;
    s16x16 mFlowShift_fp;
    s16x16 mEndpointSpeed_fp;
    s16x16 mPersistence_fp;
    s16x16 mNoiseFreqX_fp, mNoiseFreqY_fp;
    s16x16 mFlowSpeedX_fp, mFlowSpeedY_fp;
    s16x16 mFlowAmpX_fp, mFlowAmpY_fp;
};

} // namespace fl
