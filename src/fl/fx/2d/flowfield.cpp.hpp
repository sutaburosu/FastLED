/// @file flowfield.cpp.hpp
/// @brief Implementation of 2D flow field visualization

#include "fl/fx/2d/flowfield.h"

#include "fl/math/math.h"

namespace fl {

// ---------------------------------------------------------------------------
//  Perlin2D
// ---------------------------------------------------------------------------

void FlowField::Perlin2D::init(u32 seed) {
    u8 p[256];
    for (int i = 0; i < 256; i++)
        p[i] = (u8)i;
    u32 s = seed;
    for (int i = 255; i > 0; i--) {
        s = s * 1664525u + 1013904223u;
        int j = (int)((s >> 16) % (u32)(i + 1));
        u8 tmp = p[i];
        p[i] = p[j];
        p[j] = tmp;
    }
    for (int i = 0; i < 256; i++) {
        perm[i] = p[i];
        perm[i + 256] = p[i];
    }
}

float FlowField::Perlin2D::noise(float x, float y) const {
    int xi = ((int)floorf(x)) & 255;
    int yi = ((int)floorf(y)) & 255;
    float xf = x - floorf(x);
    float yf = y - floorf(y);
    float u = fade(xf);
    float v = fade(yf);
    int aa = perm[perm[xi] + yi];
    int ab = perm[perm[xi] + yi + 1];
    int ba = perm[perm[xi + 1] + yi];
    int bb = perm[perm[xi + 1] + yi + 1];
    float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u);
    float x2 = lerp(grad(ab, xf, yf - 1.0f),
                     grad(bb, xf - 1.0f, yf - 1.0f), u);
    return lerp(x1, x2, v);
}

float FlowField::Perlin2D::grad(int h, float x, float y) {
    switch (h & 7) {
    case 0:
        return x + y;
    case 1:
        return -x + y;
    case 2:
        return x - y;
    case 3:
        return -x - y;
    case 4:
        return x;
    case 5:
        return -x;
    case 6:
        return y;
    default:
        return -y;
    }
}

// ---------------------------------------------------------------------------
//  FlowField
// ---------------------------------------------------------------------------

FlowField::FlowField(const XYMap &xyMap, const Params &params)
    : Fx2d(xyMap), mParams(params) {
    int w = (int)getWidth();
    int h = (int)getHeight();
    int n = w * h;

    mR.resize(n, 0.0f);
    mG.resize(n, 0.0f);
    mB.resize(n, 0.0f);
    mTR.resize(n, 0.0f);
    mTG.resize(n, 0.0f);
    mTB.resize(n, 0.0f);
    mXProf.resize(w, 0.0f);
    mYProf.resize(h, 0.0f);

    mNoiseGenX.init(42);
    mNoiseGenY.init(1337);
}

float FlowField::fmodPos(float x, float m) {
    float r = fmodf(x, m);
    return r < 0.0f ? r + m : r;
}

float FlowField::clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

u8 FlowField::f2u8(float v) {
    int i = (int)v;
    if (i < 0)
        return 0;
    if (i > 255)
        return 255;
    return (u8)i;
}

CRGB FlowField::rainbow(float t, float speed, float phase) {
    float hue = fmodPos(t * speed + phase, 1.0f);
    CHSV hsv((u8)(hue * 255.0f), 255, 255);
    CRGB rgb;
    hsv2rgb_rainbow(hsv, rgb);
    return rgb;
}

void FlowField::drawDot(float cx, float cy, float diam,
                                  u8 cr, u8 cg, u8 cb) {
    int w = (int)getWidth();
    int h = (int)getHeight();
    float rad = diam * 0.5f;
    int x0 = fl::max(0, (int)floorf(cx - rad - 1.0f));
    int x1 = fl::min(w - 1, (int)ceilf(cx + rad + 1.0f));
    int y0 = fl::max(0, (int)floorf(cy - rad - 1.0f));
    int y1 = fl::min(h - 1, (int)ceilf(cy + rad + 1.0f));
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (x + 0.5f) - cx;
            float dy = (y + 0.5f) - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            float cov = clampf(rad + 0.5f - dist, 0.0f, 1.0f);
            if (cov <= 0.0f)
                continue;
            float inv = 1.0f - cov;
            int i = idx(y, x);
            mR[i] = mR[i] * inv + cr * cov;
            mG[i] = mG[i] * inv + cg * cov;
            mB[i] = mB[i] * inv + cb * cov;
        }
    }
}

void FlowField::drawAALine(float x0, float y0, float x1, float y1,
                                   float t, float colorShift) {
    int w = (int)getWidth();
    int h = (int)getHeight();
    float dx = x1 - x0;
    float dy = y1 - y0;
    int steps = fl::max(1, (int)(fl::max(fabsf(dx), fabsf(dy)) * 3.0f));
    float invSteps = 1.0f / (float)steps;

    for (int i = 0; i <= steps; i++) {
        float u = i * invSteps;
        float px = x0 + dx * u;
        float py = y0 + dy * u;
        CRGB c = rainbow(t, colorShift, u);

        int ix = (int)floorf(px);
        int iy = (int)floorf(py);
        float fx = px - ix;
        float fy = py - iy;

        float weights[4] = {
            (1.0f - fx) * (1.0f - fy),
            fx * (1.0f - fy),
            (1.0f - fx) * fy,
            fx * fy,
        };
        int offX[4] = {0, 1, 0, 1};
        int offY[4] = {0, 0, 1, 1};

        for (int j = 0; j < 4; j++) {
            int cx = ix + offX[j];
            int cy = iy + offY[j];
            if (cx < 0 || cx >= w || cy < 0 || cy >= h)
                continue;
            float wt = weights[j];
            if (wt <= 0.0f)
                continue;
            int gi = idx(cy, cx);
            float inv = 1.0f - wt;
            mR[gi] = mR[gi] * inv + c.r * wt;
            mG[gi] = mG[gi] * inv + c.g * wt;
            mB[gi] = mB[gi] * inv + c.b * wt;
        }
    }
}

void FlowField::emitLissajousLine(float t) {
    int w = (int)getWidth();
    int h = (int)getHeight();
    float cx = (w - 1) * 0.5f;
    float cy = (h - 1) * 0.5f;
    float s = mParams.endpoint_speed;

    // Radii scaled from original 32x32 prototype ratios.
    float x1 = cx + cx * 0.742f * sinf(t * s * 1.13f + 0.20f);
    float y1 = cy + cy * 0.677f * sinf(t * s * 1.71f + 1.30f);
    float x2 = cx + cx * 0.774f * sinf(t * s * 1.89f + 2.20f);
    float y2 = cy + cy * 0.710f * sinf(t * s * 1.37f + 0.70f);

    drawAALine(x1, y1, x2, y2, t, mParams.color_shift);

    CRGB endA = rainbow(t, mParams.color_shift, 0.0f);
    CRGB endB = rainbow(t, mParams.color_shift, 0.5f);
    float discDiam = 1.7f;
    drawDot(x1, y1, discDiam, endA.r, endA.g, endA.b);
    drawDot(x2, y2, discDiam, endB.r, endB.g, endB.b);
}

void FlowField::emitOrbitalDots(float t) {
    int w = (int)getWidth();
    int h = (int)getHeight();
    int minDim = fl::min(w, h);
    int n = mParams.dot_count;
    float fn = (float)n;
    float ocx = w * 0.5f - 0.5f;
    float ocy = h * 0.5f - 0.5f;
    float orad = minDim * 0.35f;
    float base = t * 3.0f;
    float dotDiam = 1.5f;
    for (int i = 0; i < n; i++) {
        float a = base + i * (2.0f * FL_PI / fn);
        float cx = ocx + cosf(a) * orad;
        float cy = ocy + sinf(a) * orad;
        CRGB c = rainbow(t, mParams.color_shift, (float)i / fn);
        drawDot(cx, cy, dotDiam, c.r, c.g, c.b);
    }
}

void FlowField::flowPrepare(float t) {
    int w = (int)getWidth();
    int h = (int)getHeight();
    const float kBaseFreq = 0.23f;

    for (int i = 0; i < w; i++) {
        float v = mNoiseGenX.noise(i * kBaseFreq * mParams.noise_freq_x,
                                   t * -mParams.flow_speed_x);
        mXProf[i] = clampf(v * mParams.flow_amp_x, -1.0f, 1.0f);
    }

    if (mParams.reverse_x_profile) {
        for (int i = 0; i < w / 2; i++) {
            float tmp = mXProf[i];
            mXProf[i] = mXProf[w - 1 - i];
            mXProf[w - 1 - i] = tmp;
        }
    }

    for (int i = 0; i < h; i++) {
        float v = mNoiseGenY.noise(i * kBaseFreq * mParams.noise_freq_y,
                                   t * -mParams.flow_speed_y);
        mYProf[i] = clampf(v * mParams.flow_amp_y, -1.0f, 1.0f);
    }
}

void FlowField::flowAdvect(float dt) {
    int w = (int)getWidth();
    int h = (int)getHeight();
    float halfLife = fl::max(mParams.persistence, 0.001f);
    float fade = powf(0.5f, dt / halfLife);
    float shift = mParams.flow_shift;

    // Pass 1: horizontal row shift.
    for (int y = 0; y < h; y++) {
        float sh = mYProf[y] * shift;
        for (int x = 0; x < w; x++) {
            float sx = fmodPos((float)x - sh, (float)w);
            int ix0 = (int)floorf(sx) % w;
            int ix1 = (ix0 + 1) % w;
            float f = sx - floorf(sx);
            float inv = 1.0f - f;
            int src0 = idx(y, ix0);
            int src1 = idx(y, ix1);
            int dst = idx(y, x);
            mTR[dst] = mR[src0] * inv + mR[src1] * f;
            mTG[dst] = mG[src0] * inv + mG[src1] * f;
            mTB[dst] = mB[src0] * inv + mB[src1] * f;
        }
    }

    // Pass 2: vertical column shift + fade.
    for (int x = 0; x < w; x++) {
        float sh = mXProf[x] * shift;
        for (int y = 0; y < h; y++) {
            float sy = fmodPos((float)y - sh, (float)h);
            int iy0 = (int)floorf(sy) % h;
            int iy1 = (iy0 + 1) % h;
            float f = sy - floorf(sy);
            float inv = 1.0f - f;
            int src0 = idx(iy0, x);
            int src1 = idx(iy1, x);
            int dst = idx(y, x);
            mR[dst] = (mTR[src0] * inv + mTR[src1] * f) * fade;
            mG[dst] = (mTG[src0] * inv + mTG[src1] * f) * fade;
            mB[dst] = (mTB[src0] * inv + mTB[src1] * f) * fade;
        }
    }
}

void FlowField::draw(DrawContext context) {
    if (!mInitialized) {
        mT0 = context.now;
        mLastFrameMs = context.now;
        mInitialized = true;
    }

    float dt = (context.now - mLastFrameMs) * 0.001f;
    mLastFrameMs = context.now;
    float t = (context.now - mT0) * 0.001f;

    flowPrepare(t);
    switch (mParams.emitter_mode) {
    case 0:
        emitLissajousLine(t);
        break;
    case 1:
        emitOrbitalDots(t);
        break;
    case 2:
        emitLissajousLine(t);
        emitOrbitalDots(t);
        break;
    default:
        emitLissajousLine(t);
        break;
    }
    flowAdvect(dt);

    int w = (int)getWidth();
    int h = (int)getHeight();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int ledIdx = mXyMap.mapToIndex(x, y);
            int i = idx(y, x);
            context.leds[ledIdx].r = f2u8(mR[i]);
            context.leds[ledIdx].g = f2u8(mG[i]);
            context.leds[ledIdx].b = f2u8(mB[i]);
        }
    }
}

} // namespace fl
