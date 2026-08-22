#pragma once

/// @file Pyrotechny.h
/// @brief A 3D pyrotechnic scene rendered with the fl::gfx primitives.
///
/// The scene is simulated in a world coordinate system (y up, units ~ metres)
/// with a perspective camera that gently orbits the scene, then projected
/// onto a 128x64 matrix:
///
///   - Fountain - a ground-level emitter that spews a dense spray of small
///     sparks in an upward cone. Each fountain burns its own colour, and the
///     sparks mostly cool and expire before they come back to the ground.
///   - CatherineWheel - a thin spoked wheel on a stand with two rockets
///     mounted diametrically opposed on the rim, always pointing in opposite
///     directions. The rocket thrust spins the wheel up; the white-hot sparks
///     ejected from the rockets fly off faster as the wheel spins faster.
///
/// Both effects are built on one shared particle system: a fixed-size pool
/// with gravity and drag integration. Trails come from a short per-particle
/// position history sampled at 24 Hz; each sample is drawn as a short line
/// segment graded along a temperature ramp (white-hot head, settling into
/// the spark's hue, dying orange-red), and the whole frame fades each cycle
/// (fl::fadeToBlackBy) for the "long exposure" afterglow.
///
/// Effects implement the Emitter interface so new firework types can be
/// composed into the Scene without touching the particle system.

#include "FastLED.h"
#include "fl/gfx/gfx.h"
#include "fl/gfx/colorutils.h"
#include "fl/math/geometry.h"

namespace pyrotechny {

using fl::vec3f;

// ---- Scene tuning ----------------------------------------------------------

static constexpr int kWidth = 128;
static constexpr int kHeight = 64;

static constexpr float kGravity = 1.4f;      // world units / s^2
static constexpr float kDrag = 0.55f;        // 1/s exponential velocity damping
static constexpr float kTrailFade = 40;      // fadeToBlackBy amount per frame

// Camera: kCamHeight above ground, kOrbitRadius behind the scene centre,
// looking at the target point. kFocal is the focal length in px at depth 1,
// i.e. a point at depth d renders at kFocal / d px per world unit. The
// camera swings gently in yaw (see kOrbitAmplitude / kOrbitPeriod) so the
// parallax between the foreground and background objects sells the depth.
static constexpr float kCamHeight = 1.5f;
static constexpr float kOrbitRadius = 20.0f;
static constexpr float kFocal = 150.0f;
static constexpr float kNear = 1.0f;

// Look-at target: mid-height of the display, mid-depth of the scene.
static constexpr float kTargetY = 2.0f;
static constexpr float kTargetZ = 2.0f;

// Gentle orbit: a sinusoidal yaw sweep of +/- kOrbitAmplitude radians over
// one full period.
static constexpr float kOrbitAmplitude = 0.12f;  // ~7 degrees
static constexpr float kOrbitPeriod = 24.0f;     // seconds per full sway

// Particles store their radius in "px at kRefDepth"; it scales with depth.
static constexpr float kRefDepth = 20.0f;
static constexpr float kRefScale = kFocal / kRefDepth;

// Trail segments cool as they age: each segment one sample older than the
// head is rendered at (t - kTrailCool) on the temperature ramp.
static constexpr float kTrailCool = 0.16f;

// ---- Small helpers ---------------------------------------------------------

inline float randomFloat(float lo, float hi) FL_NO_EXCEPT {
    return lo + (hi - lo) * (random16() / 65535.0f);
}

inline float jitter(float amount) FL_NO_EXCEPT {
    return randomFloat(-amount, amount);
}

// Shared colour ramp: white-hot at birth, settling into the spark's hue,
// dying orange-red. t is 1 at birth, 0 at death.
inline CRGB sparkColor(float hue, float sat, float t) FL_NO_EXCEPT {
    t = fl::max(0.0f, fl::min(1.0f, t));
    hue = fl::max(0.0f, fl::min(255.0f, hue));
    sat = fl::max(0.0f, fl::min(255.0f, sat));
    const uint8_t h = static_cast<uint8_t>(16.0f + (hue - 16.0f) * t + 0.5f);
    const uint8_t s = static_cast<uint8_t>(sat * (1.0f - 0.55f * t) + 0.5f);
    const uint8_t v = static_cast<uint8_t>(255.0f * t * t + 0.5f);
    return CRGB(CHSV(h, s, v));
}

// Blend a -> b by t (0..1).
inline CRGB blend(CRGB a, CRGB b, float t) FL_NO_EXCEPT {
    const float k = fl::max(0.0f, fl::min(1.0f, t));
    return CRGB(static_cast<uint8_t>(a.r + (b.r - a.r) * k + 0.5f),
                static_cast<uint8_t>(a.g + (b.g - a.g) * k + 0.5f),
                static_cast<uint8_t>(a.b + (b.b - a.b) * k + 0.5f));
}

// Scale a colour down by k (0..1).
inline CRGB dim(CRGB c, float k) FL_NO_EXCEPT {
    const float m = fl::max(0.0f, fl::min(1.0f, k));
    return CRGB(static_cast<uint8_t>(c.r * m + 0.5f),
                static_cast<uint8_t>(c.g * m + 0.5f),
                static_cast<uint8_t>(c.b * m + 0.5f));
}

// ---- Camera ----------------------------------------------------------------

inline vec3f cross(const vec3f &a, const vec3f &b) FL_NO_EXCEPT {
    return vec3f(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x);
}

inline vec3f normalize(const vec3f &v) FL_NO_EXCEPT {
    const float len = fl::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return v / len;
}

struct Camera {
    vec3f target;  // look-at point
    float focal;
    float yaw;     // orbit angle in radians, 0 = straight on

    // Projects a world point to screen coords. Fails for points behind the
    // near plane. `scale` is px per world unit at the point's depth.
    //
    // The camera sits kOrbitRadius behind the target at kCamHeight and looks
    // at the target; yaw swings it side to side along the orbit.
    bool project(const vec3f &p, float &sx, float &sy, float &scale) const FL_NO_EXCEPT {
        const vec3f cpos(target.x + kOrbitRadius * fl::sinf(yaw), kCamHeight,
                         target.z - kOrbitRadius * fl::cosf(yaw));
        const vec3f f = normalize(target - cpos);  // forward
        const vec3f r = normalize(cross(vec3f(0.0f, 1.0f, 0.0f), f));
        const vec3f u = cross(f, r);               // true up
        const vec3f d = p - cpos;
        const float rz = d.x * f.x + d.y * f.y + d.z * f.z;
        if (rz < kNear)
            return false;
        scale = focal / rz;
        const float rx = d.x * r.x + d.y * r.y + d.z * r.z;
        const float ry = d.x * u.x + d.y * u.y + d.z * u.z;
        sx = 0.5f * kWidth + rx * scale;
        sy = 0.5f * kHeight - ry * scale;  // world y is up, screen y down
        return true;
    }
};

// ---- Particles -------------------------------------------------------------

// A single spark. The trail is a short ring of past positions sampled by
// the pool; the renderer draws one line segment per sample, graded along
// the temperature ramp, so the streak cools from white-hot to red.
struct Particle {
    static constexpr int kTrailPoints = 4;

    vec3f pos;
    vec3f vel;
    float hue;      // base hue 0..255
    float sat;      // base saturation 0..255
    float size;     // radius in px at kRefDepth
    float life;     // seconds remaining
    float maxLife;  // seconds at birth
    vec3f hist[kTrailPoints] = {};
    float histTimer = 0.0f;
    uint8_t histIdx = 0;  // slot that gets written next (oldest after full)
    uint8_t histN = 0;    // valid samples, 0..kTrailPoints
};

// i-th oldest stored trail point; i in [0, p.histN).
inline const vec3f &trailPointAt(const Particle &p, int i) FL_NO_EXCEPT {
    const int idx = (p.histIdx - p.histN + i + 2 * Particle::kTrailPoints) %
                    Particle::kTrailPoints;
    return p.hist[idx];
}

// Record the particle's current position as a new trail sample.
inline void pushTrailPoint(Particle &p) FL_NO_EXCEPT {
    p.hist[p.histIdx] = p.pos;
    p.histIdx = (p.histIdx + 1) % Particle::kTrailPoints;
    if (p.histN < Particle::kTrailPoints)
        p.histN++;
}

// Fixed-size pool shared by every emitter. Particles are integrated in
// step(): gravity, drag, life decay, ground fizzle, trail sampling.
class ParticlePool {
  public:
    static constexpr int kMaxParticles = 640;
    static constexpr float kHistInterval = 1.0f / 24.0f;  // 24 Hz trail sampling

    Particle *alloc() FL_NO_EXCEPT {
        if (mCount >= kMaxParticles)
            return nullptr;
        // The slot may still hold a dead particle's trail state. Reset it so
        // the new spark starts with an empty history — otherwise the
        // renderer draws lines from the new position to the previous
        // occupant's trail points (stray lines across the screen).
        Particle &p = mParts[mCount++];
        p.histTimer = 0.0f;
        p.histIdx = 0;
        p.histN = 0;
        return &p;
    }

    int count() const FL_NO_EXCEPT { return mCount; }
    Particle &operator[](int i) FL_NO_EXCEPT { return mParts[i]; }
    const Particle &operator[](int i) const FL_NO_EXCEPT { return mParts[i]; }
    void clear() FL_NO_EXCEPT { mCount = 0; }

    // Swap-remove; caller must re-check index i afterwards.
    void kill(int i) FL_NO_EXCEPT {
        mParts[i] = mParts[--mCount];
    }

    void step(float dt) FL_NO_EXCEPT {
        const float drag = 1.0f - kDrag * dt;
        int i = 0;
        while (i < mCount) {
            Particle &p = mParts[i];
            p.life -= dt;
            if (p.life <= 0.0f) {
                kill(i);
                continue;
            }
            p.vel.y -= kGravity * dt;
            p.vel *= drag;
            p.pos += p.vel * dt;
            if (p.pos.y < 0.0f) {  // hit the ground: fizzle out
                kill(i);
                continue;
            }
            p.histTimer += dt;
            if (p.histTimer >= kHistInterval) {
                p.histTimer = 0.0f;
                pushTrailPoint(p);
            }
            ++i;
        }
    }

  private:
    Particle mParts[kMaxParticles] = {};
    int mCount = 0;
};

// ---- Emitters ----------------------------------------------------------------

// A firework effect that feeds the particle pool. Concrete effects
// (Fountain, CatherineWheel, ...) implement this so the Scene can host any
// mix of firework types, and new types can be added without touching the
// particle system.
class Emitter {
  public:
    virtual void update(float dt, ParticlePool &pool) FL_NO_EXCEPT = 0;

    // Static geometry (nozzles, wheels, stands) drawn each frame.
    virtual void draw(fl::CanvasRGB &c, const Camera &cam) const FL_NO_EXCEPT {}

  protected:
    virtual ~Emitter() FL_NO_EXCEPT {}
};

// ---- Fountain ----------------------------------------------------------------

// Ground-level fountain: a small launcher that spews a dense spray of tiny
// sparks in an upward cone. Each fountain burns its own colour; the sparks
// mostly cool and expire before they come back down to the ground.
class Fountain : public Emitter {
  public:
    void reset(float x, float z, float hue, float startAt) FL_NO_EXCEPT {
        mNozzle = vec3f(x, 0.0f, z);
        mHue = hue;
        mStartAt = startAt;
        mT = 0.0f;
        mEmit = 0.0f;
    }

    void update(float dt, ParticlePool &pool) override FL_NO_EXCEPT {
        mT += dt;
        if (mT < mStartAt)
            return;
        mEmit += dt * kEmitRate;
        while (mEmit >= 1.0f) {
            mEmit -= 1.0f;
            spawn(pool);
        }
    }

    void draw(fl::CanvasRGB &c, const Camera &cam) const override FL_NO_EXCEPT {
        float sx, sy, scale;
        if (!cam.project(mNozzle, sx, sy, scale))
            return;
        const float r = fl::max(0.5f, scale / kRefScale);
        // Launcher stub and the small bowl at its top.
        c.drawLine(CRGB(12, 9, 5), sx, sy, sx, sy - 4.0f * r);
        c.drawDisc(CRGB(30, 22, 10), sx, sy - 4.0f * r, 1.6f * r);
        // Soft glow tinted with the fountain's colour.
        c.drawDisc(dim(sparkColor(mHue, 140.0f, 0.5f), 0.25f), sx, sy - 4.0f * r,
                   2.6f * r);
    }

  private:
    static constexpr float kEmitRate = 90.0f;  // sparks / s
    static constexpr float kConeCosMin = 0.92f;  // ~23 degrees from vertical
    static constexpr float kSpeedMin = 2.6f;
    static constexpr float kSpeedMax = 3.4f;

    vec3f mNozzle;
    float mHue = 40.0f;
    float mStartAt = 0.0f;
    float mT = 0.0f;
    float mEmit = 0.0f;

    void spawn(ParticlePool &pool) FL_NO_EXCEPT {
        Particle *p = pool.alloc();
        if (!p)
            return;
        // Mostly vertical, uniform around the cone.
        const float phi = randomFloat(0.0f, 6.2831853f);
        const float ct = randomFloat(kConeCosMin, 1.0f);
        const float st = fl::sqrt(1.0f - ct * ct);
        const float speed = randomFloat(kSpeedMin, kSpeedMax);
        p->pos = mNozzle + vec3f(jitter(0.06f), 0.15f, jitter(0.06f));
        p->vel = vec3f(fl::cosf(phi) * st, ct, fl::sinf(phi) * st) * speed;
        p->hue = mHue + jitter(6.0f);  // slight variation within the colour
        p->sat = 210.0f;
        p->size = randomFloat(0.7f, 1.2f);
        p->life = p->maxLife = randomFloat(0.8f, 1.5f);
    }
};

// ---- Catherine wheel ----------------------------------------------------------

// A thin spoked wheel on a stand, with two rockets mounted diametrically
// opposed on the rim. The rockets always point in opposite tangential
// directions; their thrust spins the wheel up. Sparks are ejected from the
// rockets with a speed that grows as the wheel spins faster, so the spiral
// of white-hot stars flies off harder through the spin.
class CatherineWheel : public Emitter {
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
        mSpinFor = randomFloat(4.0f, 6.0f);
    }

    void update(float dt, ParticlePool &pool) override FL_NO_EXCEPT {
        mT += dt;
        float thrust = 0.0f;
        switch (mPhase) {
            case kPhaseIdle:
                if (mT >= mIdleFor) {
                    mPhase = kPhaseSpinup;
                    mT = 0.0f;
                }
                break;
            case kPhaseSpinup:
                thrust = 1.0f;
                if (mOmega >= kOmegaMax) {
                    mPhase = kPhaseSpin;
                    mT = 0.0f;
                }
                break;
            case kPhaseSpin:
                // The rockets taper out as their fuel burns.
                thrust = fl::max(0.0f, 1.0f - mT / mSpinFor);
                if (mT >= mSpinFor) {
                    mPhase = kPhaseCoast;
                    mT = 0.0f;
                }
                break;
            case kPhaseCoast:
                if (mOmega < 0.08f) {
                    mPhase = kPhaseIdle;
                    mT = 0.0f;
                    mIdleFor = randomFloat(2.0f, 4.0f);
                    mSpinFor = randomFloat(4.0f, 6.0f);
                }
                break;
        }
        // Wheel dynamics: rocket thrust drives it, friction bleeds it off.
        // At full burn the equilibrium speed is kThrust / kFriction.
        mOmega += (kThrust * thrust - kFriction * mOmega) * dt;
        if (mOmega < 0.0f)
            mOmega = 0.0f;
        mTheta += mOmega * dt;

        // The rockets spit white-hot sparks while burning.
        if (thrust > 0.02f) {
            mEmit += dt * kSparkRate * thrust;
            while (mEmit >= 1.0f) {
                mEmit -= 1.0f;
                emitSpark(pool);
            }
        }
    }

    void draw(fl::CanvasRGB &c, const Camera &cam) const override FL_NO_EXCEPT {
        float sx, sy, scale;
        if (!cam.project(mCenter, sx, sy, scale))
            return;
        const float r = mRadius * scale;  // world radius -> px at this depth
        const float spin = fl::min(1.0f, mOmega / kOmegaMax);  // 0..1

        // Stand.
        float gx, gy, gs;
        cam.project(vec3f(mCenter.x, 0.0f, mCenter.z), gx, gy, gs);
        c.drawLine(CRGB(12, 9, 5), gx, gy, sx, sy);

        // Rim and four rotating spokes heat up with the spin.
        c.drawRing(blend(CRGB(14, 10, 5), CRGB(60, 35, 12), spin), sx, sy, r, 1.2f);
        const CRGB spoke = blend(CRGB(12, 9, 4), CRGB(40, 24, 10), spin);
        for (int i = 0; i < 4; i++) {
            const float a = mTheta + i * 1.5707963f;
            c.drawLine(spoke, sx, sy, sx + r * fl::cosf(a), sy - r * fl::sinf(a));
        }
        c.drawDisc(blend(CRGB(20, 14, 6), CRGB(70, 45, 16), spin), sx, sy, 1.0f);

        // The two rocket nozzles, diametrically opposed.
        for (int n = 0; n < 2; n++) {
            const float a = mTheta + (n == 0 ? 1.5707963f : -1.5707963f);
            const float nx = sx + r * fl::cosf(a);
            const float ny = sy - r * fl::sinf(a);
            c.drawDisc(CRGB(60, 45, 20), nx, ny, 0.9f);
        }
    }

  private:
    enum Phase : uint8_t { kPhaseIdle, kPhaseSpinup, kPhaseSpin, kPhaseCoast };

    static constexpr float kStandHeight = 1.2f;
    static constexpr float kFriction = 2.0f;   // 1/s
    static constexpr float kThrust = 6.0f;     // rad/s^2 at full burn
    static constexpr float kOmegaMax = 3.0f;   // rad/s at full-burn equilibrium
    static constexpr float kSparkRate = 70.0f; // sparks / s at full burn
    static constexpr float kExhaust = 2.4f;    // u/s ejection speed at standstill

    vec3f mCenter;
    float mRadius = 1.0f;
    float mTheta = 0.0f;
    float mOmega = 0.0f;
    float mT = 0.0f;
    float mEmit = 0.0f;
    float mIdleFor = 1.0f;
    float mSpinFor = 5.0f;
    Phase mPhase = kPhaseIdle;

    // Nozzle angle on the rim for rocket n: theta +/- 90 degrees, i.e. the
    // two rockets sit diametrically opposed and point tangentially in
    // opposite directions.
    float nozzleAngle(int n) const FL_NO_EXCEPT {
        return mTheta + (n == 0 ? 1.5707963f : -1.5707963f);
    }

    void emitSpark(ParticlePool &pool) FL_NO_EXCEPT {
        Particle *p = pool.alloc();
        if (!p)
            return;
        const int n = random8(2);
        const float a = nozzleAngle(n);
        const float sa = fl::sinf(a);
        const float ca = fl::cosf(a);
        p->pos = vec3f(mCenter.x + mRadius * ca, mCenter.y + mRadius * sa,
                       mCenter.z);
        // Ejection: the nozzle's own rim speed plus the rocket exhaust,
        // along the direction of rotation, with a slight outward peel so the
        // spiral peels away from the rim. The rim speed term is what makes
        // the sparks fly off faster as the wheel spins faster.
        const float vTang = mOmega * mRadius + kExhaust * randomFloat(0.85f, 1.15f);
        const float vRad = randomFloat(0.1f, 0.4f);
        p->vel = vec3f(-sa * vTang + ca * vRad, ca * vTang + sa * vRad,
                       jitter(0.3f));
        p->hue = 24.0f;
        p->sat = 70.0f;  // whitish-gold, like real incandescent stars
        p->size = randomFloat(0.8f, 1.4f);
        p->life = p->maxLife = randomFloat(0.9f, 1.6f);
    }
};

// ---- Scene -------------------------------------------------------------------

// Composes the firework emitters and renders the frame:
// fade the previous frame (afterglow), draw the static geometry, then draw
// every particle with its graded trail.
class Scene {
  public:
    static constexpr int kFountainCount = 3;

    void reset() FL_NO_EXCEPT {
        mCam = Camera{vec3f(0.0f, kTargetY, kTargetZ), kFocal, 0.0f};
        mPool.clear();
        mTime = 0.0f;
        // Three fountains at different depths (parallax) and colours.
        mFountains[0].reset(-6.0f, 0.5f, 40.0f, 0.3f);  // gold
        mFountains[1].reset(-0.5f, 3.5f, 96.0f, 0.8f);  // emerald
        mFountains[2].reset(4.5f, 1.5f, 8.0f, 1.3f);    // crimson
        mWheel.reset(7.0f, 2.5f, 1.1f);
    }

    void update(float dt) FL_NO_EXCEPT {
        mTime += dt;
        // Gentle sinusoidal orbit: the parallax between the foreground and
        // background emitters is what sells the 3D depth.
        mCam.yaw = kOrbitAmplitude *
                   fl::sinf(6.2831853f * (mTime / kOrbitPeriod));
        for (Fountain &fountain : mFountains)
            fountain.update(dt, mPool);
        mWheel.update(dt, mPool);
        mPool.step(dt);
    }

    void render(fl::span<CRGB> leds) FL_NO_EXCEPT {
        fl::fadeToBlackBy(leds, static_cast<fl::u8>(kTrailFade));
        fl::CanvasRGB canvas(leds, kWidth, kHeight);
        drawGround(canvas);
        for (const Fountain &fountain : mFountains)
            fountain.draw(canvas, mCam);
        mWheel.draw(canvas, mCam);
        drawParticles(canvas);
    }

  private:
    void drawGround(fl::CanvasRGB &c) const FL_NO_EXCEPT {
        float gx, gy, gs;
        if (!mCam.project(vec3f(0.0f, 0.0f, 0.0f), gx, gy, gs))
            return;
        c.drawLine(CRGB(10, 8, 5), 0.0f, gy, (float)kWidth, gy);
    }

    void drawParticles(fl::CanvasRGB &c) const FL_NO_EXCEPT {
        for (int i = 0; i < mPool.count(); i++) {
            const Particle &p = mPool[i];
            float sx, sy, scale;
            if (!mCam.project(p.pos, sx, sy, scale))
                continue;
            const float t = p.life / p.maxLife;  // 1 at birth, 0 at death
            // Graded trail: walk the history newest -> oldest, cooling each
            // segment a fixed step on the temperature ramp.
            float px = sx;
            float py = sy;
            for (int j = 1; j <= p.histN; j++) {
                float ox, oy, os;
                if (!mCam.project(trailPointAt(p, p.histN - j), ox, oy, os))
                    break;
                c.drawLine(sparkColor(p.hue, p.sat, t - j * kTrailCool), ox, oy,
                           px, py);
                px = ox;
                py = oy;
            }
            // Hot head.
            float r = p.size * scale / kRefScale;
            r = fl::max(0.5f, fl::min(3.0f, r));
            c.drawDisc(sparkColor(p.hue, p.sat, t), sx, sy, r);
        }
    }

    Camera mCam;
    ParticlePool mPool;
    Fountain mFountains[kFountainCount];
    CatherineWheel mWheel;
    float mTime = 0.0f;
};

}  // namespace pyrotechny