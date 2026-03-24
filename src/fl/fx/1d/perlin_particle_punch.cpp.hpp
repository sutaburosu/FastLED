#include "fl/fx/1d/perlin_particle_punch.h"
#include "fl/math/math.h"
#include "noise.h"

namespace fl {

struct PerlinParticlePunch::Photon {
    bool alive = false;
    s16x16 velocity;
    s16x16 position;
    s16x16 brightness;
};

PerlinParticlePunch::PerlinParticlePunch(u16 num_leds, u16 n_photons)
    : Fx1d(num_leds) {
    mPhotons.resize(n_photons);
}

PerlinParticlePunch::~PerlinParticlePunch() = default;

fl::string PerlinParticlePunch::fxName() const {
    return "PerlinParticlePunch";
}

void PerlinParticlePunch::setAudioLevel(float vol) { mAudioVol = s16x16(vol); }
void PerlinParticlePunch::setDrag(float drag) { mDrag = s16x16(drag); }
void PerlinParticlePunch::setSpeed(float speed) { mSpeed = s16x16(speed); }

s16x16 PerlinParticlePunch::mapf(s16x16 x, s16x16 in_min, s16x16 in_max,
                                  s16x16 out_min, s16x16 out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

s16x16 PerlinParticlePunch::mapfClamped(s16x16 x, s16x16 in_min,
                                         s16x16 in_max, s16x16 out_min,
                                         s16x16 out_max) {
    s16x16 v = mapf(x, in_min, in_max, out_min, out_max);
    if (v < out_min)
        return out_min;
    if (v > out_max)
        return out_max;
    return v;
}

u8 PerlinParticlePunch::max3(u8 a, u8 b, u8 c) {
    u8 ab = a > b ? a : b;
    return ab > c ? ab : c;
}

s16x16 PerlinParticlePunch::circleNoiseGen(u32 now, s16x16 theta) {
    s16x16 sin_val, cos_val;
    s16x16::sincos(theta, sin_val, cos_val);
    // Map cos/sin from [-1,1] to noise coordinates [0, 2*0xafff]
    u32 x =
        u32((i64(cos_val.raw() + s16x16::SCALE) * 0xafff) >> s16x16::FRAC_BITS);
    u32 y =
        u32((i64(sin_val.raw() + s16x16::SCALE) * 0xafff) >> s16x16::FRAC_BITS);
    u32 z = now * 0x000f;
    u16 val = inoise16(x, y, z);
    // val / 0xcfff, clamped to [0, 1], quartic curve, scaled to 255
    s16x16 tmp = s16x16::from_raw(
        i32((u32(val) << s16x16::FRAC_BITS) / 0xcfffu));
    constexpr s16x16 one(1.0f);
    if (tmp > one)
        tmp = one;
    tmp = tmp * tmp;
    tmp = tmp * tmp;
    tmp = tmp * s16x16(255);
    return tmp;
}

void PerlinParticlePunch::noiseCircleDraw(u32 now, CRGB *dst) {
    // time_factor = now / 2048.0, as s16x16 (wraps, fine for angles)
    s16x16 time_factor = s16x16::from_raw(static_cast<i32>(now * 32u));
    constexpr s16x16 two_pi(6.2831853f);
    s16x16 step = two_pi / s16x16(i32(mNumLeds));
    s16x16 theta = -time_factor;
    constexpr s16x16 threshold(32.0f);
    constexpr s16x16 zero(0.0f);
    constexpr s16x16 max_val(255.0f);
    for (u16 i = 0; i < mNumLeds; ++i) {
        u8 hue = 155;
        s16x16 val = circleNoiseGen(now + 1000, theta);
        if (val < threshold) {
            val = zero;
        } else {
            val = mapf(val, threshold, max_val, zero, max_val);
        }
        u8 val_u8 = u8(val.to_int());
        hsv2rgb_spectrum(CHSV(hue, 255 - val_u8, val_u8), dst[i]);
        theta = theta + step;
    }
}

u8 PerlinParticlePunch::envelopeUpdate(s16x16 vol, u32 duration_ms) {
    s16x16 time = s16x16(i32(duration_ms)) / s16x16(i32(kFrameTimeMs));
    s16x16 next_vol;
    constexpr s16x16 e_val(2.71828183f);
    if (vol < mPrevVol) {
        s16x16 dr = kDecayRate;
        constexpr s16x16 low_thresh(0.2f);
        constexpr s16x16 fp_zero(0.0f);
        constexpr s16x16 fp_one(1.0f);
        if (mPrevVol < low_thresh) {
            dr = dr * mapfClamped(mPrevVol, low_thresh, fp_zero, fp_one,
                                  fp_zero);
        }
        next_vol = mPrevVol * s16x16::pow(e_val, dr * time);
    } else {
        s16x16 maybe = mPrevVol * s16x16::pow(e_val, kAttackRate * time);
        constexpr s16x16 small_thresh(0.01f);
        if (maybe > mPrevVol && maybe > small_thresh) {
            next_vol = vol < maybe ? (vol > mPrevVol ? vol : mPrevVol) : maybe;
        } else {
            next_vol = vol;
        }
    }
    mPrevVol = next_vol;
    return u8((next_vol * s16x16(255)).to_int());
}

PerlinParticlePunch::Photon *PerlinParticlePunch::tryAllocatePhoton() {
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

void PerlinParticlePunch::photonDraw(CRGB *dst, u8 led_value) {
    const bool is_rising = led_value > mLastLedValue;
    mLastLedValue = led_value;

    if (led_value > 16 && is_rising) {
        Photon *p = tryAllocatePhoton();
        if (p) {
            s16x16 energy = s16x16(i32(led_value)) / s16x16(255);
            s16x16 base_speed = s16x16(4) + energy * s16x16(12);
            s16x16 jitter =
                s16x16(i32(random(85, 115))) / s16x16(100);
            p->velocity = base_speed * jitter * mSpeed;
            p->position = s16x16(0);
            p->brightness = s16x16(i32(led_value));
        }
    }

    constexpr s16x16 kBrightnessDecay(0.98f);
    constexpr s16x16 kMinBrightness(12.0f);
    constexpr s16x16 kMinVelocity(0.3f);
    s16x16 num_leds_fp{i32(mNumLeds)};

    u16 n = (u16)mPhotons.size();
    for (u16 i = 0; i < n; ++i) {
        Photon *p = &mPhotons[i];
        if (!p->alive)
            continue;

        s16x16 prev_pos = p->position;
        p->velocity = p->velocity * mDrag;
        p->position = p->position + p->velocity;
        p->brightness = p->brightness * kBrightnessDecay;

        if (p->position > num_leds_fp || p->brightness < kMinBrightness ||
            p->velocity < kMinVelocity) {
            p->alive = false;
            continue;
        }

        CHSV tmp(160, 0, u8(p->brightness.to_int()));
        int idx_start = prev_pos.to_int();
        int idx_end = p->position.to_int();

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

void PerlinParticlePunch::draw(DrawContext context) {
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
