#pragma once

/// @file    perlin_particle_punch.h
/// @brief   Audio-reactive perlin noise background with photon particle
///          overlay. Ported from github.com/zackees/sailboat-lights.

#include "fl/fx/fx1d.h"
#include "fl/math/math.h"
#include "fl/stl/vector.h"

namespace fl {

FASTLED_SHARED_PTR(PerlinParticlePunch);

class PerlinParticlePunch : public Fx1d {
  public:
    /// @param num_leds   Number of LEDs in the strip.
    /// @param n_photons  Size of the photon pool (more = denser sparkle).
    PerlinParticlePunch(u16 num_leds, u16 n_photons = 1000)
        : Fx1d(num_leds) {
        mPhotons.resize(n_photons);
    }

    void draw(DrawContext context) override;
    fl::string fxName() const override { return "PerlinParticlePunch"; }

    /// Feed a normalised audio volume (0.0 – 1.0) each frame.
    void setAudioLevel(float vol) { mAudioVol = vol; }

    /// Set per-frame drag (0.0 = instant stop, 1.0 = no drag). Default 0.95.
    void setDrag(float drag) { mDrag = drag; }

    /// Set initial velocity multiplier. Default 1.0.
    void setSpeed(float speed) { mSpeed = speed; }

  private:
    // ----- envelope parameters -----
    static constexpr float kAttackRate = 0.4f;
    static constexpr float kDecayRate = -0.04f;
    static constexpr u8 kFrameTimeMs = 3;

    float mPrevVol = 0.f;
    float mAudioVol = 0.f;
    float mDrag = 0.95f;  // per-frame velocity retention (0..1)
    float mSpeed = 1.0f;  // initial velocity multiplier

    // ----- photon particle system -----
    struct Photon {
        bool alive = false;
        float velocity = 0;
        float position = 0;
        float brightness = 0;
    };

    fl::vector<Photon> mPhotons;
    u8 mLastLedValue = 0;

    // ----- helpers -----
    static float mapf(float x, float in_min, float in_max, float out_min,
                      float out_max) {
        return (x - in_min) * (out_max - out_min) / (in_max - in_min) +
               out_min;
    }

    static float mapfClamped(float x, float in_min, float in_max,
                             float out_min, float out_max) {
        float v = mapf(x, in_min, in_max, out_min, out_max);
        if (v < out_min)
            return out_min;
        if (v > out_max)
            return out_max;
        return v;
    }

    static u8 max3(u8 a, u8 b, u8 c) {
        u8 ab = a > b ? a : b;
        return ab > c ? ab : c;
    }

    // Perlin-noise circle generator
    static float circleNoiseGen(u32 now, double theta) {
        double x = (cos(theta) + 1) * 0xafff;
        double y = (sin(theta) + 1) * 0xafff;
        u32 z = now * 0x000f;
        u16 val = inoise16(u32(x), u32(y), z);
        float tmp = float(val) / float(0xcfff);
        if (tmp > 1.0f)
            tmp = 1.0f;
        tmp *= tmp;
        tmp *= tmp;
        tmp *= 255;
        return tmp;
    }

    void noiseCircleDraw(u32 now, CRGB *dst) {
        double time_factor = double(now) / (1024.0 * 2.0);
        for (u16 i = 0; i < mNumLeds; ++i) {
            double theta = i * 2.0 * PI / mNumLeds - time_factor;
            u8 hue = 155;
            float val = circleNoiseGen(now + 1000, theta);
            if (val < 32) {
                val = 0;
            } else {
                val = mapf(val, 32, 255, 0, 255);
            }
            hsv2rgb_spectrum(CHSV(hue, 255 - u8(val), u8(val)), dst[i]);
        }
    }

    u8 envelopeUpdate(float vol, u32 duration_ms) {
        float time = float(duration_ms) / float(kFrameTimeMs);
        float next_vol;
        if (vol < mPrevVol) {
            float dr = kDecayRate;
            if (mPrevVol < 0.2f) {
                dr *= mapfClamped(mPrevVol, 0.2f, 0.f, 1.f, 0.f);
            }
            next_vol = mPrevVol * expf(dr * time);
        } else {
            float maybe = mPrevVol * expf(kAttackRate * time);
            if (maybe > mPrevVol && maybe > 0.01f) {
                next_vol = vol < maybe ? (vol > mPrevVol ? vol : mPrevVol)
                                       : maybe;
            } else {
                next_vol = vol;
            }
        }
        mPrevVol = next_vol;
        return u8(next_vol * 255);
    }

    Photon *tryAllocatePhoton() {
        u16 n = (u16)mPhotons.size();
        for (u16 i = 0; i < n; ++i) {
            if (!mPhotons[i].alive) {
                mPhotons[i] = Photon();
                mPhotons[i].alive = true;
                return &mPhotons[i];
            }
        }
        return nullptr;
    }

    void photonDraw(CRGB *dst, u8 led_value) {
        const bool is_rising = led_value > mLastLedValue;
        mLastLedValue = led_value;

        // Spawn a photon on rising audio edges — punch out fast from
        // LED 0 (lower-left), proportional to energy.
        if (led_value > 16 && is_rising) {
            Photon *p = tryAllocatePhoton();
            if (p) {
                // Energy-proportional launch: louder = faster punch.
                float energy = float(led_value) / 255.0f; // 0..1
                float base_speed = 4.0f + energy * 12.0f; // 4..16
                float jitter = float(random(85, 115)) / 100.0f;
                p->velocity = base_speed * jitter * mSpeed;
                p->position = 0;
                p->brightness = led_value;
            }
        }

        // Update and draw all photons
        u16 n = (u16)mPhotons.size();
        for (u16 i = 0; i < n; ++i) {
            Photon *p = &mPhotons[i];
            if (!p->alive)
                continue;

            float prev_pos = p->position;
            // Apply drag BEFORE moving so the very first step is already
            // the fastest and every subsequent step is shorter.
            p->velocity *= mDrag;
            p->position += p->velocity;
            p->brightness *= 0.98f;

            if (p->position > mNumLeds || p->brightness < 12 ||
                p->velocity < 0.3f) {
                p->alive = false;
                continue;
            }

            CHSV tmp(160, 0, u8(p->brightness));
            int idx_start = int(prev_pos);
            int idx_end = int(p->position);

            for (int j = idx_start; j <= idx_end; ++j) {
                if (j >= 0 && j < mNumLeds) {
                    u8 existing = max3(dst[j].r, dst[j].g, dst[j].b);
                    if (tmp.v > existing) {
                        CRGB rgb;
                        hsv2rgb_spectrum(tmp, rgb);
                        dst[j] = rgb;
                    } else {
                        CRGB rgb;
                        hsv2rgb_spectrum(tmp, rgb);
                        rgb.nscale8_video(existing);
                        dst[j] += rgb;
                    }
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
// draw() — runs one frame of the sailboat-lights animation
// ---------------------------------------------------------------------------
inline void PerlinParticlePunch::draw(DrawContext context) {
    CRGB *leds = context.leds;
    if (leds == nullptr || mNumLeds == 0) {
        return;
    }

    u32 now = context.now;

    // 1. Perlin-noise background
    noiseCircleDraw(now, leds);

    // 2. Run audio envelope (repeated for smoother response)
    u8 led_value = 0;
    for (int i = 0; i < 10; ++i) {
        led_value = envelopeUpdate(mAudioVol, kFrameTimeMs);
    }

    // 3. Photon particles driven by audio envelope
    photonDraw(leds, led_value);
}

} // namespace fl
