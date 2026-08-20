#pragma once

/// @file Fireworks3D.h
/// @brief A small 3D fireworks scene rendered with the fl::gfx primitives.
///
/// The scene is simulated in a world coordinate system (y up, units ~ metres)
/// with a fixed axis-aligned perspective camera, then projected onto a 128x64
/// matrix:
///
///   - RomanCandle — a rocket that ascends trailing sparks, then bursts into
///     a spherical shell of gravity-bound sparks plus a brief flash.
///   - CatherineWheel — a spoke wheel on a stand that spins up and ejects
///     white-hot sparks tangentially from its rim, drawing a spiral trail.
///
/// Particles share one fixed-size pool; trails come from fading the previous
/// frame (fl::fadeToBlackBy) and re-adding every particle each frame.
/// All drawing goes through fl::CanvasRGB (drawDisc / drawLine / drawRing),
/// which blends additively by default — the classic "long exposure" look.

#include "FastLED.h"
#include "fl/gfx/gfx.h"
#include "fl/gfx/colorutils.h"
#include "fl/math/geometry.h"

namespace fireworks {

using fl::vec3f;

// ---- Scene tuning ----------------------------------------------------------

static constexpr int kWidth = 128;
static constexpr int kHeight = 64;

static constexpr float kGravity = 1.4f;      // world units / s^2
static constexpr float kDrag = 0.55f;        // 1/s exponential velocity damping
static constexpr float kTrailFade = 44;      // fadeToBlackBy amount per frame

// Camera: kCamHeight above ground, kCamZ behind the scene plane, looking
// along +z. kFocal is the focal length in px at depth 1, i.e. a point at
// depth d renders at kFocal / d px per world unit.
static constexpr float kCamHeight = 1.5f;
static constexpr float kCamZ = -20.0f;
static constexpr float kFocal = 150.0f;
static constexpr float kNear = 1.0f;

// Particles store their radius in "px at kRefDepth"; it scales with depth.
static constexpr float kRefDepth = 20.0f;
static constexpr float kRefScale = kFocal / kRefDepth;

// ---- Small helpers ---------------------------------------------------------

inline float randomFloat(float lo, float hi) FL_NO_EXCEPT {
    return lo + (hi - lo) * (random16() / 65535.0f);
}

inline float jitter(float amount) FL_NO_EXCEPT {
    return randomFloat(-amount, amount);
}

// ---- Camera ----------------------------------------------------------------

struct Camera {
    vec3f pos;
    float focal;

    // Projects a world point to screen coords. Fails for points behind the
    // near plane. `scale` is px per world unit at the point's depth.
    bool project(const vec3f &p, float &sx, float &sy, float &scale) const FL_NO_EXCEPT {
        const float dz = p.z - pos.z;
        if (dz < kNear)
            return false;
        scale = focal / dz;
        sx = 0.5f * kWidth + (p.x - pos.x) * scale;
        sy = 0.5f * kHeight - (p.y - pos.y) * scale;  // world y is up, screen y down
        return true;
    }
};

// ---- Particles -------------------------------------------------------------

struct Particle {
    vec3f pos;
    vec3f vel;
    float hue;      // base hue 0..255
    float sat;      // base saturation 0..255
    float size;     // radius in px at kRefDepth
    float life;     // seconds remaining
    float maxLife;  // seconds at birth
};

class ParticlePool {
  public:
    static constexpr int kMaxParticles = 480;

    Particle *alloc() FL_NO_EXCEPT {
        if (mCount >= kMaxParticles)
            return nullptr;
        return &mParts[mCount++];
    }

    int count() const FL_NO_EXCEPT { return mCount; }
    Particle &operator[](int i) FL_NO_EXCEPT { return mParts[i]; }
    const Particle &operator[](int i) const FL_NO_EXCEPT { return mParts[i]; }
    void clear() FL_NO_EXCEPT { mCount = 0; }

    // Swap-remove; caller must re-check index i afterwards.
    void kill(int i) FL_NO_EXCEPT {
        mParts[i] = mParts[--mCount];
    }

  private:
    Particle mParts[kMaxParticles] = {};
    int mCount = 0;
};

// A short bright pop drawn where a shell bursts.
struct FlashState {
    float sx = 0.0f;
    float sy = 0.0f;
    float scale = 0.0f;
    float hue = 0.0f;
    float t = 0.0f;
    static constexpr float kMaxT = 0.12f;

    void trigger(const vec3f &p, const Camera &cam, float h) FL_NO_EXCEPT {
        cam.project(p, sx, sy, scale);
        hue = h;
        t = kMaxT;
    }

    void update(float dt) FL_NO_EXCEPT {
        if (t > 0.0f)
            t -= dt;
    }

    void draw(fl::CanvasRGB &c) const FL_NO_EXCEPT {
        if (t <= 0.0f)
            return;
        const float k = t / kMaxT;  // 1 at trigger, 0 at fade-out
        const float r = (2.0f + 10.0f * (1.0f - k)) * scale / kRefScale;
        c.drawDisc(CRGB(CHSV((uint8_t)hue, 80, (uint8_t)(255.0f * k))), sx, sy, r);
    }
};

// Shared color ramp: white-hot at birth, settling into the spark's hue,
// dying orange-red.
inline CRGB sparkColor(const Particle &p) FL_NO_EXCEPT {
    const float t = p.life / p.maxLife;  // 1 at birth, 0 at death
    const uint8_t hue = (uint8_t)(16.0f + (p.hue - 16.0f) * t);
    const uint8_t sat = (uint8_t)(p.sat * (1.0f - 0.55f * t));
    const uint8_t val = (uint8_t)(255.0f * t * t);
    return CRGB(CHSV(hue, sat, val));
}

// ---- Roman candle ------------------------------------------------------------

class RomanCandle {
  public:
    void reset(float x, float z, float firstDelay, const Camera &cam) FL_NO_EXCEPT {
        mCam = &cam;
        mLaunch = vec3f(x, 0.0f, z);
        mT = 0.0f;
        mPhase = kPhaseWait;
        mWaitFor = firstDelay;
        reroll();
    }

    void update(float dt, ParticlePool &pool, FlashState &flash) FL_NO_EXCEPT {
        mT += dt;
        switch (mPhase) {
            case kPhaseWait:
                if (mT >= mWaitFor) {
                    mPhase = kPhaseAscent;
                    mT = 0.0f;
                }
                break;
            case kPhaseAscent: {
                const float s = mT / mAscentT;
                if (s >= 1.0f) {
                    burst(pool, flash);
                    mPhase = kPhaseCooldown;
                    mT = 0.0f;
                } else {
                    emitTrail(headAt(s), pool);
                }
                break;
            }
            case kPhaseCooldown:
                if (mT >= mCooldownT) {
                    mPhase = kPhaseWait;
                    mT = 0.0f;
                    mWaitFor = randomFloat(0.5f, 2.0f);
                    reroll();
                }
                break;
        }
    }

    // True while ascending; the head's world position comes out in `out`.
    bool head(vec3f &out) const FL_NO_EXCEPT {
        if (mPhase != kPhaseAscent)
            return false;
        out = headAt(mT / mAscentT);
        return true;
    }

    const vec3f &launchPos() const FL_NO_EXCEPT { return mLaunch; }

  private:
    enum Phase : uint8_t { kPhaseWait, kPhaseAscent, kPhaseCooldown };

    void reroll() FL_NO_EXCEPT {
        mAscentT = randomFloat(1.4f, 2.1f);
        mBurstHeight = randomFloat(2.8f, 4.3f);
        mDrift = randomFloat(-0.8f, 0.8f);
        mCooldownT = 1.2f;
        mWhite = (random8(8) == 0);
        mHue = kShellHues[random8(7)];
    }

    // y = h(2s - s^2): fast start, zero vertical speed at apogee.
    vec3f headAt(float s) const FL_NO_EXCEPT {
        return vec3f(mLaunch.x + mDrift * s, mBurstHeight * (2.0f * s - s * s), mLaunch.z);
    }

    void emitTrail(const vec3f &head, ParticlePool &pool) FL_NO_EXCEPT {
        for (int i = 0; i < 2; i++) {
            Particle *p = pool.alloc();
            if (!p)
                return;
            p->pos = head + vec3f(jitter(0.05f), jitter(0.03f), jitter(0.05f));
            p->vel = vec3f(jitter(0.3f), randomFloat(-0.1f, 0.35f), jitter(0.3f));
            p->hue = 32.0f;
            p->sat = 190.0f;
            p->size = randomFloat(0.8f, 1.3f);
            p->life = p->maxLife = randomFloat(0.25f, 0.55f);
        }
        if (random8(5) == 0) {  // occasional bright crackle star
            Particle *p = pool.alloc();
            if (!p)
                return;
            p->pos = head + vec3f(jitter(0.1f), jitter(0.1f), jitter(0.1f));
            p->vel = vec3f(jitter(0.7f), jitter(0.7f), jitter(0.7f));
            p->hue = 30.0f;
            p->sat = 40.0f;
            p->size = 1.6f;
            p->life = p->maxLife = 0.5f;
        }
    }

    void burst(ParticlePool &pool, FlashState &flash) FL_NO_EXCEPT {
        const vec3f c = headAt(1.0f);
        flash.trigger(c, *mCam, mHue);

        const int n = 56 + random8(24);
        const float sat = mWhite ? 36.0f : 215.0f;
        for (int i = 0; i < n; i++) {
            Particle *p = pool.alloc();
            if (!p)
                break;
            // Uniform direction on the sphere, y being the up axis.
            const float y = 1.0f - 2.0f * (random16() / 65535.0f);
            const float phi = (random16() / 65535.0f) * 6.2831853f;
            const float rr = fl::sqrt(1.0f - y * y);
            const float speed = randomFloat(1.7f, 3.3f);
            p->pos = c;
            p->vel = vec3f(fl::cosf(phi) * rr, y, fl::sinf(phi) * rr) * speed;
            p->hue = mHue;
            p->sat = sat;
            p->size = randomFloat(1.0f, 2.0f);
            p->life = p->maxLife = randomFloat(1.0f, 1.8f);
        }
    }

    const Camera *mCam = nullptr;
    vec3f mLaunch;
    float mT = 0.0f;
    float mWaitFor = 0.0f;
    float mAscentT = 1.8f;
    float mBurstHeight = 3.5f;
    float mDrift = 0.0f;
    float mCooldownT = 1.2f;
    float mHue = 40.0f;
    bool mWhite = false;
    Phase mPhase = kPhaseWait;

    // Gold-heavy shell palette (CHSV hues).
    static constexpr float kShellHues[7] = {40, 40, 40, 8, 100, 168, 196};
};

constexpr float RomanCandle::kShellHues[7];

// ---- Catherine wheel ---------------------------------------------------------

class CatherineWheel {
  public:
    void reset(float x, float z, float radius) FL_NO_EXCEPT {
        mCenter = vec3f(x, kStandHeight, z);
        mRadius = radius;
        mTheta = 0.0f;
        mOmega = 0.0f;
        mT = 0.0f;
        mEmit = 0.0f;
        mPhase = kPhaseIdle;
        mIdleFor = randomFloat(0.5f, 1.5f);
        mSpinFor = randomFloat(4.5f, 6.5f);
    }

    void update(float dt, ParticlePool &pool) FL_NO_EXCEPT {
        mT += dt;
        switch (mPhase) {
            case kPhaseIdle:
                if (mT >= mIdleFor) {
                    mPhase = kPhaseSpinup;
                    mT = 0.0f;
                }
                break;
            case kPhaseSpinup:
                mOmega = kOmegaMax * (mT / kSpinupT);
                if (mT >= kSpinupT) {
                    mPhase = kPhaseSpin;
                    mT = 0.0f;
                }
                break;
            case kPhaseSpin:
                if (mT >= mSpinFor) {
                    mPhase = kPhaseIdle;
                    mT = 0.0f;
                    mOmega = 0.0f;
                    mIdleFor = randomFloat(1.5f, 3.0f);
                    mSpinFor = randomFloat(4.5f, 6.5f);
                }
                break;
        }
        mTheta += mOmega * dt;

        if (mOmega > 0.1f) {
            mEmit += dt * kEmitRate;
            while (mEmit >= 1.0f) {
                mEmit -= 1.0f;
                emitSpark(pool);
            }
        }
    }

    void draw(fl::CanvasRGB &c, const Camera &cam) const FL_NO_EXCEPT {
        float sx, sy, scale;
        if (!cam.project(mCenter, sx, sy, scale))
            return;
        const float r = mRadius * scale;  // world radius -> px at this depth

        // Stand.
        float gx, gy, gs;
        cam.project(vec3f(mCenter.x, 0.0f, mCenter.z), gx, gy, gs);
        c.drawLine(CRGB(46, 32, 20), gx, gy, sx, sy);

        // Rim, four rotating spokes, hub. Screen y is flipped vs world y.
        c.drawRing(CRGB(120, 80, 30), sx, sy, r, 1.3f);
        for (int i = 0; i < 4; i++) {
            const float a = mTheta + i * 1.5707963f + 0.7853982f;
            c.drawLine(CRGB(70, 48, 22), sx, sy,
                       sx + r * fl::cosf(a), sy - r * fl::sinf(a));
        }
        c.drawDisc(CRGB(160, 120, 60), sx, sy, 0.9f);
    }

  private:
    enum Phase : uint8_t { kPhaseIdle, kPhaseSpinup, kPhaseSpin };

    static constexpr float kStandHeight = 1.0f;
    static constexpr float kSpinupT = 0.9f;
    static constexpr float kOmegaMax = 4.0f;   // rad/s
    static constexpr float kEmitRate = 22.0f;  // sparks / s at full spin
    static constexpr int kHoles = 12;          // stars around the rim

    void emitSpark(ParticlePool &pool) FL_NO_EXCEPT {
        Particle *p = pool.alloc();
        if (!p)
            return;
        // A random one of the rim holes fires, so the spiral has discrete arms.
        const float a = mTheta + 6.2831853f * (float(random8(kHoles)) / float(kHoles));
        const float sa = fl::sinf(a);
        const float ca = fl::cosf(a);
        p->pos = vec3f(mCenter.x + mRadius * ca, mCenter.y + mRadius * sa, mCenter.z);
        const float v = mOmega * mRadius * randomFloat(0.85f, 1.15f);
        p->vel = vec3f(-sa * v + jitter(0.15f), ca * v + jitter(0.15f), jitter(0.15f));
        p->hue = 24.0f;
        p->sat = 64.0f;  // whitish-gold, like real incandescent stars
        p->size = randomFloat(0.8f, 1.4f);
        p->life = p->maxLife = randomFloat(0.8f, 1.5f);
    }

    vec3f mCenter;
    float mRadius = 1.0f;
    float mTheta = 0.0f;
    float mOmega = 0.0f;
    float mT = 0.0f;
    float mEmit = 0.0f;
    float mIdleFor = 1.0f;
    float mSpinFor = 5.0f;
    Phase mPhase = kPhaseIdle;
};

// ---- Scene -------------------------------------------------------------------

class Scene {
  public:
    void reset() FL_NO_EXCEPT {
        mCam = Camera{vec3f(0.0f, kCamHeight, kCamZ), kFocal};
        mPool.clear();
        mFlash.t = 0.0f;
        mCandles[0].reset(-6.5f, 2.0f, 0.6f, mCam);
        mCandles[1].reset(-1.0f, 0.0f, 1.8f, mCam);
        mCandles[2].reset(4.5f, 3.0f, 3.0f, mCam);
        mWheel.reset(7.2f, 4.0f, 1.0f);
    }

    void update(float dt) FL_NO_EXCEPT {
        for (RomanCandle &candle : mCandles)
            candle.update(dt, mPool, mFlash);
        mWheel.update(dt, mPool);
        mFlash.update(dt);
        stepParticles(dt);
    }

    void render(fl::span<CRGB> leds) FL_NO_EXCEPT {
        fl::fadeToBlackBy(leds, static_cast<fl::u8>(kTrailFade));
        fl::CanvasRGB canvas(leds, kWidth, kHeight);
        drawGround(canvas);
        mWheel.draw(canvas, mCam);
        for (const RomanCandle &candle : mCandles)
            drawCandle(canvas, candle);
        mFlash.draw(canvas);
        drawParticles(canvas);
    }

  private:
    void stepParticles(float dt) FL_NO_EXCEPT {
        const float drag = 1.0f - kDrag * dt;
        int i = 0;
        while (i < mPool.count()) {
            Particle &p = mPool[i];
            p.life -= dt;
            if (p.life <= 0.0f) {
                mPool.kill(i);
                continue;
            }
            p.vel.y -= kGravity * dt;
            p.vel *= drag;
            p.pos += p.vel * dt;
            if (p.pos.y < 0.0f) {  // hit the ground: fizzle out
                mPool.kill(i);
                continue;
            }
            ++i;
        }
    }

    void drawGround(fl::CanvasRGB &c) const FL_NO_EXCEPT {
        float gx, gy, gs;
        if (!mCam.project(vec3f(0.0f, 0.0f, 0.0f), gx, gy, gs))
            return;
        c.drawLine(CRGB(22, 16, 10), 0.0f, gy, (float)kWidth, gy);
    }

    void drawCandle(fl::CanvasRGB &c, const RomanCandle &rc) const FL_NO_EXCEPT {
        float sx, sy, scale;
        if (!mCam.project(rc.launchPos(), sx, sy, scale))
            return;
        // Launcher stub.
        c.drawLine(CRGB(40, 28, 16), sx, sy, sx, sy - 5.0f);

        vec3f head;
        if (!rc.head(head))
            return;
        float hx, hy, hs;
        if (!mCam.project(head, hx, hy, hs))
            return;
        const float r = 1.3f * hs / kRefScale;
        c.drawDisc(CRGB(120, 90, 40), hx, hy, r * 3.0f);  // warm glow
        c.drawDisc(CRGB(255, 244, 214), hx, hy, r);       // hot core
    }

    void drawParticles(fl::CanvasRGB &c) const FL_NO_EXCEPT {
        for (int i = 0; i < mPool.count(); i++) {
            const Particle &p = mPool[i];
            float sx, sy, scale;
            if (!mCam.project(p.pos, sx, sy, scale))
                continue;
            float r = p.size * scale / kRefScale;
            r = fl::max(0.5f, fl::min(3.5f, r));
            c.drawDisc(sparkColor(p), sx, sy, r);
        }
    }

    Camera mCam;
    ParticlePool mPool;
    FlashState mFlash;
    RomanCandle mCandles[3];
    CatherineWheel mWheel;
};

}  // namespace fireworks