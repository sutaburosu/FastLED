#pragma once

// General-purpose signal smoothing filters.
//
// IIR (stateful, no buffer):
//   ExponentialSmoother<T>       — first-order exponential (IIR/RC) smoother
//   CascadedEMA<T, Stages>      — N stages of EMA in series (Gaussian-like)
//   LeakyIntegrator<T, K>       — shift-based EMA, cheap for integer/fixed-point
//   BiquadFilter<T>             — second-order IIR (with Butterworth factory)
//   KalmanFilter<T>             — 1D scalar Kalman filter
//   OneEuroFilter<T>            — adaptive velocity-based smoothing (VR/graphics)
//
// FIR (windowed, buffer-backed):
//   MovingAverage<T, N>          — simple moving average (O(1) running sum)
//   WeightedMovingAverage<T, N>  — linearly weighted moving average
//   TriangularFilter<T, N>       — triangular (tent) weighted average
//   GaussianFilter<T, N>         — binomial-coefficient Gaussian approximation
//   MedianFilter<T, N>           — sliding-window median filter
//   AlphaTrimmedMean<T, N>       — sorted window, trim extremes, average middle
//   HampelFilter<T, N>           — median + deviation threshold outlier rejection
//   SavitzkyGolayFilter<T, N>    — polynomial-fit smoothing (preserves peaks)
//   BilateralFilter<T, N>        — edge-preserving value-similarity weighting
//
// Static version — N specified:
//   MedianFilter<float, 5> mf;
//
// Dynamic version — N omitted (defaults to 0), requires capacity at construction:
//   MedianFilter<float> mf(5);

#include "fl/circular_buffer.h"
#include "fl/math.h"
#include "fl/math_macros.h"
#include "fl/stl/algorithm.h"
#include "fl/stl/type_traits.h"

// Detail impl headers
#include "fl/detail/filter/moving_average_impl.h"
#include "fl/detail/filter/median_filter_impl.h"
#include "fl/detail/filter/weighted_moving_average_impl.h"
#include "fl/detail/filter/triangular_filter_impl.h"
#include "fl/detail/filter/gaussian_filter_impl.h"
#include "fl/detail/filter/alpha_trimmed_mean_impl.h"
#include "fl/detail/filter/hampel_filter_impl.h"
#include "fl/detail/filter/savitzky_golay_filter_impl.h"
#include "fl/detail/filter/bilateral_filter_impl.h"

namespace fl {

// ============================================================================
// IIR filters (no buffer, live in fl:: namespace directly)
// ============================================================================

template <typename T = float>
class ExponentialSmoother {
  public:
    explicit ExponentialSmoother(T tau_seconds, T initial = T(0))
        : mTau(tau_seconds), mY(initial) {}

    T update(T input, T dt_seconds) {
        T decay = fl::exp(-(dt_seconds / mTau));
        mY = input + (mY - input) * decay;
        return mY;
    }

    void setTau(T tau_seconds) { mTau = tau_seconds; }
    T value() const { return mY; }
    void reset(T initial = T(0)) { mY = initial; }

  private:
    T mTau;
    T mY;
};

template <typename T = float, int K = 2>
class LeakyIntegrator {
  public:
    LeakyIntegrator() : mY(T(0)) {}
    explicit LeakyIntegrator(T initial) : mY(initial) {}

    T update(T input) {
        mY = mY + shift_right(input - mY);
        return mY;
    }

    T value() const { return mY; }
    void reset(T initial = T(0)) { mY = initial; }

  private:
    template <typename U = T>
    static typename fl::enable_if<fl::is_floating_point<U>::value, U>::type
    shift_right(U val) { return val / static_cast<U>(1 << K); }

    template <typename U = T>
    static typename fl::enable_if<!fl::is_floating_point<U>::value, U>::type
    shift_right(U val) { return val >> K; }

    T mY;
};

template <typename T = float, int Stages = 2>
class CascadedEMA {
  public:
    explicit CascadedEMA(T tau_seconds, T initial = T(0))
        : mTau(tau_seconds) {
        for (int i = 0; i < Stages; ++i) {
            mY[i] = initial;
        }
    }

    T update(T input, T dt_seconds) {
        T decay = fl::exp(-(dt_seconds / mTau));
        T val = input;
        for (int i = 0; i < Stages; ++i) {
            mY[i] = val + (mY[i] - val) * decay;
            val = mY[i];
        }
        return val;
    }

    T value() const { return mY[Stages - 1]; }
    void setTau(T tau_seconds) { mTau = tau_seconds; }

    void reset(T initial = T(0)) {
        for (int i = 0; i < Stages; ++i) mY[i] = initial;
    }

  private:
    T mTau;
    T mY[Stages];
};

template <typename T = float>
class BiquadFilter {
  public:
    BiquadFilter(T b0, T b1, T b2, T a1, T a2)
        : mB0(b0), mB1(b1), mB2(b2), mA1(a1), mA2(a2),
          mX1(T(0)), mX2(T(0)), mY1(T(0)), mY2(T(0)), mLastValue(T(0)) {}

    static BiquadFilter butterworth(float cutoff_hz, float sample_rate) {
        float omega = 2.0f * static_cast<float>(FL_PI) * cutoff_hz / sample_rate;
        float sn = fl::sinf(omega);
        float cs = fl::cosf(omega);
        float alpha = sn * 0.7071067811865476f;
        float a0 = 1.0f + alpha;
        float b0f = (1.0f - cs) * 0.5f / a0;
        float b1f = (1.0f - cs) / a0;
        float b2f = b0f;
        float a1f = (-2.0f * cs) / a0;
        float a2f = (1.0f - alpha) / a0;
        return BiquadFilter(T(b0f), T(b1f), T(b2f), T(a1f), T(a2f));
    }

    T update(T input) {
        T output = mB0 * input + mB1 * mX1 + mB2 * mX2
                 - mA1 * mY1 - mA2 * mY2;
        mX2 = mX1;
        mX1 = input;
        mY2 = mY1;
        mY1 = output;
        mLastValue = output;
        return output;
    }

    T value() const { return mLastValue; }

    void reset() {
        mX1 = mX2 = mY1 = mY2 = mLastValue = T(0);
    }

  private:
    T mB0, mB1, mB2, mA1, mA2;
    T mX1, mX2;
    T mY1, mY2;
    T mLastValue;
};

template <typename T = float>
class KalmanFilter {
  public:
    KalmanFilter(T process_noise, T measurement_noise, T initial = T(0))
        : mQ(process_noise), mR(measurement_noise),
          mX(initial), mP(T(1.0f)), mLastValue(initial) {}

    T update(T measurement) {
        mP = mP + mQ;
        T k = mP / (mP + mR);
        mX = mX + k * (measurement - mX);
        mP = (T(1.0f) - k) * mP;
        mLastValue = mX;
        return mX;
    }

    T value() const { return mLastValue; }

    void reset(T initial = T(0)) {
        mX = initial;
        mP = T(1.0f);
        mLastValue = initial;
    }

  private:
    T mQ;
    T mR;
    T mX;
    T mP;
    T mLastValue;
};

template <typename T = float>
class OneEuroFilter {
  public:
    OneEuroFilter(T min_cutoff, T beta, T d_cutoff = T(1.0f))
        : mMinCutoff(min_cutoff), mBeta(beta), mDCutoff(d_cutoff),
          mX(T(0)), mDX(T(0)), mFirst(true), mLastValue(T(0)) {}

    T update(T input, T dt) {
        if (mFirst) {
            mX = input;
            mDX = T(0);
            mFirst = false;
            mLastValue = input;
            return input;
        }
        if (dt == T(0)) {
            return mLastValue;
        }
        T dx = (input - mX) / dt;
        T alpha_d = computeAlpha(mDCutoff, dt);
        mDX = mDX + alpha_d * (dx - mDX);
        T cutoff = mMinCutoff + mBeta * fl::abs(mDX);
        T alpha = computeAlpha(cutoff, dt);
        mX = mX + alpha * (input - mX);
        mLastValue = mX;
        return mX;
    }

    T value() const { return mLastValue; }

    void reset(T initial = T(0)) {
        mX = initial;
        mDX = T(0);
        mFirst = true;
        mLastValue = initial;
    }

  private:
    static T computeAlpha(T cutoff, T dt) {
        T tau = T(static_cast<float>(2.0 * FL_PI)) * cutoff * dt;
        return tau / (T(1.0f) + tau);
    }

    T mMinCutoff;
    T mBeta;
    T mDCutoff;
    T mX;
    T mDX;
    bool mFirst;
    T mLastValue;
};

// ============================================================================
// FIR filter public API — single class with protected inheritance
// N > 0: static (inlined) buffer, default-constructible
// N == 0: dynamic buffer, requires capacity at construction
// ============================================================================

// --- MovingAverage ---
template <typename T = float, fl::size N = 0>
class MovingAverage : protected detail::MovingAverageImpl<T, N> {
    using Base = detail::MovingAverageImpl<T, N>;
  public:
    using Base::update;
    using Base::value;
    using Base::reset;
    using Base::full;
    using Base::size;
    using Base::capacity;
    using Base::resize;
    MovingAverage() = default;
    explicit MovingAverage(fl::size capacity) : Base(capacity) {}
};

// --- MedianFilter ---
template <typename T = float, fl::size N = 0>
class MedianFilter : protected detail::MedianFilterImpl<T, N> {
    using Base = detail::MedianFilterImpl<T, N>;
  public:
    using Base::update;
    using Base::value;
    using Base::reset;
    using Base::size;
    using Base::capacity;
    using Base::resize;
    MedianFilter() = default;
    explicit MedianFilter(fl::size capacity) : Base(capacity) {}
};

// --- WeightedMovingAverage ---
template <typename T = float, fl::size N = 0>
class WeightedMovingAverage : protected detail::WeightedMovingAverageImpl<T, N> {
    using Base = detail::WeightedMovingAverageImpl<T, N>;
  public:
    using Base::update;
    using Base::value;
    using Base::reset;
    using Base::full;
    using Base::size;
    using Base::capacity;
    using Base::resize;
    WeightedMovingAverage() = default;
    explicit WeightedMovingAverage(fl::size capacity) : Base(capacity) {}
};

// --- TriangularFilter ---
template <typename T = float, fl::size N = 0>
class TriangularFilter : protected detail::TriangularFilterImpl<T, N> {
    using Base = detail::TriangularFilterImpl<T, N>;
  public:
    using Base::update;
    using Base::value;
    using Base::reset;
    using Base::full;
    using Base::size;
    using Base::capacity;
    using Base::resize;
    TriangularFilter() = default;
    explicit TriangularFilter(fl::size capacity) : Base(capacity) {}
};

// --- GaussianFilter ---
template <typename T = float, fl::size N = 0>
class GaussianFilter : protected detail::GaussianFilterImpl<T, N> {
    using Base = detail::GaussianFilterImpl<T, N>;
  public:
    using Base::update;
    using Base::value;
    using Base::reset;
    using Base::full;
    using Base::size;
    using Base::capacity;
    using Base::resize;
    GaussianFilter() = default;
    explicit GaussianFilter(fl::size capacity) : Base(capacity) {}
};

// --- AlphaTrimmedMean ---
template <typename T = float, fl::size N = 0>
class AlphaTrimmedMean : protected detail::AlphaTrimmedMeanImpl<T, N> {
    using Base = detail::AlphaTrimmedMeanImpl<T, N>;
  public:
    using Base::update;
    using Base::value;
    using Base::reset;
    using Base::size;
    using Base::capacity;
    using Base::resize;
    explicit AlphaTrimmedMean(fl::size trim_count = 1) : Base(trim_count) {}
    AlphaTrimmedMean(fl::size capacity, fl::size trim_count) : Base(capacity, trim_count) {}
};

// --- HampelFilter ---
template <typename T = float, fl::size N = 0>
class HampelFilter : protected detail::HampelFilterImpl<T, N> {
    using Base = detail::HampelFilterImpl<T, N>;
  public:
    using Base::update;
    using Base::value;
    using Base::reset;
    using Base::size;
    using Base::capacity;
    using Base::resize;
    explicit HampelFilter(T threshold = T(3.0f)) : Base(threshold) {}
    HampelFilter(fl::size capacity, T threshold) : Base(capacity, threshold) {}
};

// --- SavitzkyGolayFilter ---
template <typename T = float, fl::size N = 0>
class SavitzkyGolayFilter : protected detail::SavitzkyGolayFilterImpl<T, N> {
    using Base = detail::SavitzkyGolayFilterImpl<T, N>;
  public:
    using Base::update;
    using Base::value;
    using Base::reset;
    using Base::full;
    using Base::size;
    using Base::capacity;
    using Base::resize;
    SavitzkyGolayFilter() = default;
    explicit SavitzkyGolayFilter(fl::size capacity) : Base(capacity) {}
};

// --- BilateralFilter ---
template <typename T = float, fl::size N = 0>
class BilateralFilter : protected detail::BilateralFilterImpl<T, N> {
    using Base = detail::BilateralFilterImpl<T, N>;
  public:
    using Base::update;
    using Base::value;
    using Base::reset;
    using Base::full;
    using Base::size;
    using Base::capacity;
    using Base::resize;
    explicit BilateralFilter(T sigma_range = T(1.0f)) : Base(sigma_range) {}
    BilateralFilter(fl::size capacity, T sigma_range) : Base(capacity, sigma_range) {}
};

// ============================================================================
// Deprecated Dynamic* aliases for backward compatibility
// ============================================================================

template <typename T>
using DynamicMovingAverage = MovingAverage<T, 0>;

template <typename T>
using DynamicMedianFilter = MedianFilter<T, 0>;

template <typename T>
using DynamicWeightedMovingAverage = WeightedMovingAverage<T, 0>;

template <typename T>
using DynamicTriangularFilter = TriangularFilter<T, 0>;

template <typename T>
using DynamicGaussianFilter = GaussianFilter<T, 0>;

template <typename T>
using DynamicAlphaTrimmedMean = AlphaTrimmedMean<T, 0>;

template <typename T>
using DynamicHampelFilter = HampelFilter<T, 0>;

template <typename T>
using DynamicSavitzkyGolayFilter = SavitzkyGolayFilter<T, 0>;

template <typename T>
using DynamicBilateralFilter = BilateralFilter<T, 0>;

} // namespace fl
