#pragma once

/// @file    perlin_particle_punch.h
/// @brief   Audio-reactive perlin noise background with photon particle
///          overlay. Ported from github.com/zackees/sailboat-lights.

#include "fl/fx/fx1d.h"
#include "fl/math/fixed_point/s16x16.h"
#include "fl/stl/vector.h"

namespace fl {

FASTLED_SHARED_PTR(PerlinParticlePunch);

class PerlinParticlePunch : public Fx1d {
  public:
    /// @param num_leds   Number of LEDs in the strip.
    /// @param n_photons  Size of the photon pool (more = denser sparkle).
    PerlinParticlePunch(u16 num_leds, u16 n_photons = 1000);
    ~PerlinParticlePunch();

    void draw(DrawContext context) override;
    fl::string fxName() const override;

    /// Feed a normalised audio volume (0.0 – 1.0) each frame.
    void setAudioLevel(float vol);

    /// Set per-frame drag (0.0 = instant stop, 1.0 = no drag). Default 0.95.
    void setDrag(float drag);

    /// Set initial velocity multiplier. Default 1.0.
    void setSpeed(float speed);

  private:
    struct Photon;

    static constexpr s16x16 kAttackRate = s16x16(0.4f);
    static constexpr s16x16 kDecayRate = s16x16(-0.04f);
    static constexpr u8 kFrameTimeMs = 3;

    s16x16 mPrevVol;
    s16x16 mAudioVol;
    s16x16 mDrag = s16x16(0.95f);
    s16x16 mSpeed = s16x16(1.0f);

    fl::vector<Photon> mPhotons;
    u8 mLastLedValue = 0;

    static s16x16 mapf(s16x16 x, s16x16 in_min, s16x16 in_max, s16x16 out_min,
                       s16x16 out_max);
    static s16x16 mapfClamped(s16x16 x, s16x16 in_min, s16x16 in_max,
                              s16x16 out_min, s16x16 out_max);
    static u8 max3(u8 a, u8 b, u8 c);
    static s16x16 circleNoiseGen(u32 now, s16x16 theta);
    void noiseCircleDraw(u32 now, CRGB *dst);
    u8 envelopeUpdate(s16x16 vol, u32 duration_ms);
    Photon *tryAllocatePhoton();
    void photonDraw(CRGB *dst, u8 led_value);
};

} // namespace fl
