/// @file    flowfield.h
/// @brief   2D flow field visualization: emitters paint color, noise advects it

#pragma once

#include "fl/stl/stdint.h"

#include "crgb.h"
#include "fl/stl/shared_ptr.h"
#include "fl/fx/fx2d.h"
#include "fl/math/xymap.h"
#include "fl/stl/vector.h"
#include "fl/math/math.h"

namespace fl {

FASTLED_SHARED_PTR(FlowField);

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
};

/// @brief 2D flow field effect: emitters inject color, noise-driven advection
///        creates fluid-like patterns.
///
/// Concept by Stefan Petrick. Original C++ implementation by 4wheeljive
/// (ColorTrails project). Distilled into a self-contained FastLED Fx2d class.
///
/// Example usage:
/// @code
/// XYMap xymap(22, 22, true);
/// FlowField flowField(xymap);
/// @endcode
class FlowField : public Fx2d {
  public:
    using Params = FlowFieldParams;

    explicit FlowField(const XYMap &xyMap, const Params &params = Params());

    void draw(DrawContext context) override;

    fl::string fxName() const override { return "FlowField"; }

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

    Params &getParams() { return mParams; }
    const Params &getParams() const { return mParams; }

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

    // Sub-steps called from draw().
    void flowPrepare(float t);
    void emitOrbitalDots(float t);
    void flowAdvect(float dt);
    void drawDot(float cx, float cy, float diam,
                 u8 cr, u8 cg, u8 cb);
    void drawAALine(float x0, float y0, float x1, float y1,
                    float t, float colorShift);
    void emitLissajousLine(float t);

    Params mParams;

    // Float-precision RGB grids (main + temp for advection).
    fl::vector<float> mR, mG, mB;
    fl::vector<float> mTR, mTG, mTB;

    // Noise generators and profiles.
    Perlin2D mNoiseGenX, mNoiseGenY;
    fl::vector<float> mXProf; ///< Per-column values; drives vertical shift
    fl::vector<float> mYProf; ///< Per-row values; drives horizontal shift

    // Timing state.
    u32 mLastFrameMs = 0;
    u32 mT0 = 0;
    bool mInitialized = false;
};

} // namespace fl
