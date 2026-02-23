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
// Static aliases use fl::size N for the window size:
//   MedianFilter<float, 5> mf;
//
// Dynamic aliases use DynamicCircularBuffer under the hood:
//   DynamicMedianFilter<float> mf(5);

#include "fl/circular_buffer.h"
#include "fl/math.h"
#include "fl/math_macros.h"
#include "fl/stl/algorithm.h"
#include "fl/stl/type_traits.h"

namespace fl {

namespace detail {

// Division by integer count — dispatched by type.
template <typename T>
inline typename fl::enable_if<fl::is_floating_point<T>::value, T>::type
div_by_count(T sum, fl::size count) { return sum / static_cast<T>(count); }

template <typename T>
inline typename fl::enable_if<fl::is_integral<T>::value, T>::type
div_by_count(T sum, fl::size count) { return sum / static_cast<T>(count); }

template <typename T>
inline typename fl::enable_if<!fl::is_floating_point<T>::value &&
                              !fl::is_integral<T>::value, T>::type
div_by_count(T sum, fl::size count) { return sum / T(static_cast<float>(count)); }

// ============================================================================
// Buffer-backed filter implementations (detail namespace).
// Users interact via the public aliases below (e.g. MedianFilter<float, 5>).
// ============================================================================

template <typename T, typename Buffer>
class MovingAverageImpl {
  public:
    MovingAverageImpl() : mSum(T(0)) {}
    explicit MovingAverageImpl(fl::size capacity) : mBuf(capacity), mSum(T(0)) {}

    T update(T input) {
        if (mBuf.full()) {
            mSum = mSum - mBuf.front();
        }
        mBuf.push_back(input);
        mSum = mSum + input;
        return value();
    }

    T value() const {
        fl::size count = mBuf.size();
        if (count == 0) {
            return T(0);
        }
        return divByCount(mSum, count);
    }

    void reset() {
        mBuf.clear();
        mSum = T(0);
    }

    bool full() const { return mBuf.full(); }
    fl::size size() const { return mBuf.size(); }
    fl::size capacity() const { return mBuf.capacity(); }

    void resize(fl::size new_capacity) {
        mBuf = Buffer(new_capacity);
        mSum = T(0);
    }

  private:
    template <typename U = T>
    static typename fl::enable_if<fl::is_floating_point<U>::value, U>::type
    divByCount(U sum, fl::size count) {
        return sum / static_cast<U>(count);
    }

    template <typename U = T>
    static typename fl::enable_if<fl::is_integral<U>::value, U>::type
    divByCount(U sum, fl::size count) {
        return sum / static_cast<U>(count);
    }

    template <typename U = T>
    static typename fl::enable_if<!fl::is_floating_point<U>::value &&
                                  !fl::is_integral<U>::value, U>::type
    divByCount(U sum, fl::size count) {
        return sum / U(static_cast<float>(count));
    }

    Buffer mBuf;
    T mSum;
};

template <typename T, typename Buffer>
class MedianFilterImpl {
  public:
    MedianFilterImpl() : mSortedCount(0), mLastMedian(T(0)) {}
    explicit MedianFilterImpl(fl::size capacity)
        : mRing(capacity), mSorted(capacity),
          mSortedCount(0), mLastMedian(T(0)) {}

    T update(T input) {
        if (!mRing.full()) {
            T* base = &mSorted[0];
            T* pos = fl::lower_bound(base, base + mSortedCount, input);
            fl::size idx = static_cast<fl::size>(pos - base);
            for (fl::size i = mSortedCount; i > idx; --i) {
                mSorted[i] = mSorted[i - 1];
            }
            mSorted[idx] = input;
            ++mSortedCount;
        } else {
            T oldest = mRing.front();
            T* base = &mSorted[0];
            T* rm_pos = fl::lower_bound(base, base + mSortedCount, oldest);
            fl::size rm = static_cast<fl::size>(rm_pos - base);
            for (fl::size i = rm; i + 1 < mSortedCount; ++i) {
                mSorted[i] = mSorted[i + 1];
            }
            T* ins_pos =
                fl::lower_bound(base, base + mSortedCount - 1, input);
            fl::size idx = static_cast<fl::size>(ins_pos - base);
            for (fl::size i = mSortedCount - 1; i > idx; --i) {
                mSorted[i] = mSorted[i - 1];
            }
            mSorted[idx] = input;
        }

        mRing.push_back(input);
        mLastMedian = mSorted[mSortedCount / 2];
        return mLastMedian;
    }

    T value() const { return mLastMedian; }

    void reset() {
        mRing.clear();
        mSortedCount = 0;
        mLastMedian = T(0);
    }

    fl::size size() const { return mRing.size(); }
    fl::size capacity() const { return mRing.capacity(); }

    void resize(fl::size new_capacity) {
        mRing = Buffer(new_capacity);
        mSorted = Buffer(new_capacity);
        mSortedCount = 0;
        mLastMedian = T(0);
    }

  private:
    Buffer mRing;
    Buffer mSorted;
    fl::size mSortedCount;
    T mLastMedian;
};

template <typename T, typename Buffer>
class WeightedMovingAverageImpl {
  public:
    WeightedMovingAverageImpl() : mLastValue(T(0)) {}
    explicit WeightedMovingAverageImpl(fl::size capacity)
        : mBuf(capacity), mLastValue(T(0)) {}

    T update(T input) {
        mBuf.push_back(input);
        fl::size n = mBuf.size();
        T weighted_sum = T(0);
        T weight_total = T(0);
        for (fl::size i = 0; i < n; ++i) {
            T w = T(static_cast<float>(i + 1));
            weighted_sum = weighted_sum + mBuf[i] * w;
            weight_total = weight_total + w;
        }
        mLastValue = weighted_sum / weight_total;
        return mLastValue;
    }

    T value() const { return mLastValue; }
    void reset() { mBuf.clear(); mLastValue = T(0); }
    bool full() const { return mBuf.full(); }
    fl::size size() const { return mBuf.size(); }
    fl::size capacity() const { return mBuf.capacity(); }

    void resize(fl::size new_capacity) {
        mBuf = Buffer(new_capacity);
        mLastValue = T(0);
    }

  private:
    Buffer mBuf;
    T mLastValue;
};

template <typename T, typename Buffer>
class TriangularFilterImpl {
  public:
    TriangularFilterImpl() : mLastValue(T(0)) {}
    explicit TriangularFilterImpl(fl::size capacity)
        : mBuf(capacity), mLastValue(T(0)) {}

    T update(T input) {
        mBuf.push_back(input);
        fl::size n = mBuf.size();
        T weighted_sum = T(0);
        T weight_total = T(0);
        for (fl::size i = 0; i < n; ++i) {
            fl::size w_int = fl::fl_min(i + 1, n - i);
            T w = T(static_cast<float>(w_int));
            weighted_sum = weighted_sum + mBuf[i] * w;
            weight_total = weight_total + w;
        }
        mLastValue = weighted_sum / weight_total;
        return mLastValue;
    }

    T value() const { return mLastValue; }
    void reset() { mBuf.clear(); mLastValue = T(0); }
    bool full() const { return mBuf.full(); }
    fl::size size() const { return mBuf.size(); }
    fl::size capacity() const { return mBuf.capacity(); }

    void resize(fl::size new_capacity) {
        mBuf = Buffer(new_capacity);
        mLastValue = T(0);
    }

  private:
    Buffer mBuf;
    T mLastValue;
};

template <typename T, typename Buffer>
class GaussianFilterImpl {
  public:
    GaussianFilterImpl() : mLastValue(T(0)) {}
    explicit GaussianFilterImpl(fl::size capacity)
        : mBuf(capacity), mLastValue(T(0)) {}

    T update(T input) {
        mBuf.push_back(input);
        fl::size n = mBuf.size();
        if (n == 0) { mLastValue = T(0); return mLastValue; }

        T weighted_sum = T(0);
        T weight_total = T(0);
        fl::size binom = 1;
        for (fl::size i = 0; i < n; ++i) {
            T w = T(static_cast<float>(binom));
            weighted_sum = weighted_sum + mBuf[i] * w;
            weight_total = weight_total + w;
            if (i + 1 < n) {
                binom = binom * (n - 1 - i) / (i + 1);
            }
        }
        mLastValue = weighted_sum / weight_total;
        return mLastValue;
    }

    T value() const { return mLastValue; }
    void reset() { mBuf.clear(); mLastValue = T(0); }
    bool full() const { return mBuf.full(); }
    fl::size size() const { return mBuf.size(); }
    fl::size capacity() const { return mBuf.capacity(); }

    void resize(fl::size new_capacity) {
        mBuf = Buffer(new_capacity);
        mLastValue = T(0);
    }

  private:
    Buffer mBuf;
    T mLastValue;
};

template <typename T, typename Buffer>
class AlphaTrimmedMeanImpl {
  public:
    explicit AlphaTrimmedMeanImpl(fl::size trim_count = 1)
        : mSortedCount(0), mTrimCount(trim_count), mLastValue(T(0)) {}
    explicit AlphaTrimmedMeanImpl(fl::size capacity, fl::size trim_count)
        : mRing(capacity), mSorted(capacity),
          mSortedCount(0), mTrimCount(trim_count), mLastValue(T(0)) {}

    T update(T input) {
        if (!mRing.full()) {
            T* base = &mSorted[0];
            T* pos = fl::lower_bound(base, base + mSortedCount, input);
            fl::size idx = static_cast<fl::size>(pos - base);
            for (fl::size i = mSortedCount; i > idx; --i) {
                mSorted[i] = mSorted[i - 1];
            }
            mSorted[idx] = input;
            ++mSortedCount;
        } else {
            T oldest = mRing.front();
            T* base = &mSorted[0];
            T* rm_pos = fl::lower_bound(base, base + mSortedCount, oldest);
            fl::size rm = static_cast<fl::size>(rm_pos - base);
            for (fl::size i = rm; i + 1 < mSortedCount; ++i) {
                mSorted[i] = mSorted[i + 1];
            }
            T* ins_pos = fl::lower_bound(base, base + mSortedCount - 1, input);
            fl::size idx = static_cast<fl::size>(ins_pos - base);
            for (fl::size i = mSortedCount - 1; i > idx; --i) {
                mSorted[i] = mSorted[i - 1];
            }
            mSorted[idx] = input;
        }
        mRing.push_back(input);

        fl::size lo = mTrimCount;
        fl::size hi = (mSortedCount > mTrimCount) ? mSortedCount - mTrimCount : 0;
        if (lo >= hi) {
            mLastValue = mSorted[mSortedCount / 2];
        } else {
            T sum = T(0);
            for (fl::size i = lo; i < hi; ++i) {
                sum = sum + mSorted[i];
            }
            mLastValue = div_by_count(sum, hi - lo);
        }
        return mLastValue;
    }

    T value() const { return mLastValue; }

    void reset() {
        mRing.clear();
        mSortedCount = 0;
        mLastValue = T(0);
    }

    fl::size size() const { return mRing.size(); }
    fl::size capacity() const { return mRing.capacity(); }

    void resize(fl::size new_capacity, fl::size trim_count) {
        mRing = Buffer(new_capacity);
        mSorted = Buffer(new_capacity);
        mSortedCount = 0;
        mTrimCount = trim_count;
        mLastValue = T(0);
    }

  private:
    Buffer mRing;
    Buffer mSorted;
    fl::size mSortedCount;
    fl::size mTrimCount;
    T mLastValue;
};

template <typename T, typename Buffer>
class HampelFilterImpl {
  public:
    explicit HampelFilterImpl(T threshold = T(3.0f))
        : mSortedCount(0), mThreshold(threshold), mLastValue(T(0)) {}
    explicit HampelFilterImpl(fl::size capacity, T threshold = T(3.0f))
        : mRing(capacity), mSorted(capacity),
          mSortedCount(0), mThreshold(threshold), mLastValue(T(0)) {}

    T update(T input) {
        T output = input;

        if (mSortedCount > 0) {
            T median = mSorted[mSortedCount / 2];
            T mad_sum = T(0);
            for (fl::size i = 0; i < mSortedCount; ++i) {
                mad_sum = mad_sum + fl::abs(mSorted[i] - median);
            }
            T mad = div_by_count(mad_sum, mSortedCount);
            T deviation = fl::abs(input - median);
            if (mad == T(0)) {
                if (!(deviation == T(0))) {
                    output = median;
                }
            } else if (deviation > mThreshold * mad) {
                output = median;
            }
        }

        if (!mRing.full()) {
            T* base = &mSorted[0];
            T* pos = fl::lower_bound(base, base + mSortedCount, input);
            fl::size idx = static_cast<fl::size>(pos - base);
            for (fl::size i = mSortedCount; i > idx; --i) {
                mSorted[i] = mSorted[i - 1];
            }
            mSorted[idx] = input;
            ++mSortedCount;
        } else {
            T oldest = mRing.front();
            T* base = &mSorted[0];
            T* rm_pos = fl::lower_bound(base, base + mSortedCount, oldest);
            fl::size rm = static_cast<fl::size>(rm_pos - base);
            for (fl::size i = rm; i + 1 < mSortedCount; ++i) {
                mSorted[i] = mSorted[i + 1];
            }
            T* ins_pos = fl::lower_bound(base, base + mSortedCount - 1, input);
            fl::size idx = static_cast<fl::size>(ins_pos - base);
            for (fl::size i = mSortedCount - 1; i > idx; --i) {
                mSorted[i] = mSorted[i - 1];
            }
            mSorted[idx] = input;
        }
        mRing.push_back(input);

        mLastValue = output;
        return mLastValue;
    }

    T value() const { return mLastValue; }

    void reset() {
        mRing.clear();
        mSortedCount = 0;
        mLastValue = T(0);
    }

    fl::size size() const { return mRing.size(); }
    fl::size capacity() const { return mRing.capacity(); }

    void resize(fl::size new_capacity) {
        mRing = Buffer(new_capacity);
        mSorted = Buffer(new_capacity);
        mSortedCount = 0;
        mLastValue = T(0);
    }

  private:
    Buffer mRing;
    Buffer mSorted;
    fl::size mSortedCount;
    T mThreshold;
    T mLastValue;
};

template <typename T, typename Buffer>
class SavitzkyGolayFilterImpl {
  public:
    SavitzkyGolayFilterImpl() : mLastValue(T(0)) {}
    explicit SavitzkyGolayFilterImpl(fl::size capacity)
        : mBuf(capacity), mLastValue(T(0)) {}

    T update(T input) {
        mBuf.push_back(input);
        fl::size n = mBuf.size();

        if (n < 5) {
            T sum = T(0);
            for (fl::size i = 0; i < n; ++i) sum = sum + mBuf[i];
            mLastValue = div_by_count(sum, n);
            return mLastValue;
        }

        int M = static_cast<int>((n - 1) / 2);
        int base_term = 3 * M * (M + 1) - 1;

        T weighted_sum = T(0);
        T weight_total = T(0);
        for (fl::size j = 0; j < n; ++j) {
            int i = static_cast<int>(j) - M;
            int c = 3 * (base_term - 5 * i * i);
            T w = T(static_cast<float>(c));
            weighted_sum = weighted_sum + mBuf[j] * w;
            weight_total = weight_total + w;
        }

        if (!(weight_total == T(0))) {
            mLastValue = weighted_sum / weight_total;
        } else {
            mLastValue = input;
        }
        return mLastValue;
    }

    T value() const { return mLastValue; }
    void reset() { mBuf.clear(); mLastValue = T(0); }
    bool full() const { return mBuf.full(); }
    fl::size size() const { return mBuf.size(); }
    fl::size capacity() const { return mBuf.capacity(); }

    void resize(fl::size new_capacity) {
        mBuf = Buffer(new_capacity);
        mLastValue = T(0);
    }

  private:
    Buffer mBuf;
    T mLastValue;
};

template <typename T, typename Buffer>
class BilateralFilterImpl {
  public:
    explicit BilateralFilterImpl(T sigma_range = T(1.0f))
        : mSigmaRange(sigma_range), mLastValue(T(0)) {}
    explicit BilateralFilterImpl(fl::size capacity, T sigma_range)
        : mBuf(capacity), mSigmaRange(sigma_range), mLastValue(T(0)) {}

    T update(T input) {
        mBuf.push_back(input);
        fl::size n = mBuf.size();

        T two_sigma_sq = T(2.0f) * mSigmaRange * mSigmaRange;

        // When sigma is zero (or effectively zero), the Gaussian kernel
        // becomes a Dirac delta: only samples identical to the input get
        // weight 1, everything else gets weight 0.  In the limit this
        // just returns the input itself.
        if (two_sigma_sq == T(0)) {
            mLastValue = input;
            return mLastValue;
        }

        T weighted_sum = T(0);
        T weight_total = T(0);

        for (fl::size i = 0; i < n; ++i) {
            T diff = mBuf[i] - input;
            T range_weight = fl::exp(-(diff * diff) / two_sigma_sq);
            weighted_sum = weighted_sum + mBuf[i] * range_weight;
            weight_total = weight_total + range_weight;
        }

        if (!(weight_total == T(0))) {
            mLastValue = weighted_sum / weight_total;
        } else {
            mLastValue = input;
        }
        return mLastValue;
    }

    T value() const { return mLastValue; }
    void reset() { mBuf.clear(); mLastValue = T(0); }
    bool full() const { return mBuf.full(); }
    fl::size size() const { return mBuf.size(); }
    fl::size capacity() const { return mBuf.capacity(); }

    void resize(fl::size new_capacity) {
        mBuf = Buffer(new_capacity);
        mLastValue = T(0);
    }

  private:
    Buffer mBuf;
    T mSigmaRange;
    T mLastValue;
};

} // namespace detail

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
        // Guard against dt==0 which would cause division by zero.
        // Treat zero dt as "no time elapsed" — hold the previous output.
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
// Public aliases — static (compile-time N) and dynamic versions.
//
// Static:   MedianFilter<float, 5> mf;
// Dynamic:  DynamicMedianFilter<float> mf(5);
// ============================================================================

template <typename T = float, fl::size N = 8>
using MovingAverage = detail::MovingAverageImpl<T, StaticCircularBuffer<T, N>>;

template <typename T>
using DynamicMovingAverage = detail::MovingAverageImpl<T, DynamicCircularBuffer<T>>;

template <typename T = float, fl::size N = 5>
using MedianFilter = detail::MedianFilterImpl<T, StaticCircularBuffer<T, N>>;

template <typename T>
using DynamicMedianFilter = detail::MedianFilterImpl<T, DynamicCircularBuffer<T>>;

template <typename T = float, fl::size N = 8>
using WeightedMovingAverage = detail::WeightedMovingAverageImpl<T, StaticCircularBuffer<T, N>>;

template <typename T>
using DynamicWeightedMovingAverage = detail::WeightedMovingAverageImpl<T, DynamicCircularBuffer<T>>;

template <typename T = float, fl::size N = 8>
using TriangularFilter = detail::TriangularFilterImpl<T, StaticCircularBuffer<T, N>>;

template <typename T>
using DynamicTriangularFilter = detail::TriangularFilterImpl<T, DynamicCircularBuffer<T>>;

template <typename T = float, fl::size N = 5>
using GaussianFilter = detail::GaussianFilterImpl<T, StaticCircularBuffer<T, N>>;

template <typename T>
using DynamicGaussianFilter = detail::GaussianFilterImpl<T, DynamicCircularBuffer<T>>;

template <typename T = float, fl::size N = 7>
using AlphaTrimmedMean = detail::AlphaTrimmedMeanImpl<T, StaticCircularBuffer<T, N>>;

template <typename T>
using DynamicAlphaTrimmedMean = detail::AlphaTrimmedMeanImpl<T, DynamicCircularBuffer<T>>;

template <typename T = float, fl::size N = 5>
using HampelFilter = detail::HampelFilterImpl<T, StaticCircularBuffer<T, N>>;

template <typename T>
using DynamicHampelFilter = detail::HampelFilterImpl<T, DynamicCircularBuffer<T>>;

template <typename T = float, fl::size N = 5>
using SavitzkyGolayFilter = detail::SavitzkyGolayFilterImpl<T, StaticCircularBuffer<T, N>>;

template <typename T>
using DynamicSavitzkyGolayFilter = detail::SavitzkyGolayFilterImpl<T, DynamicCircularBuffer<T>>;

template <typename T = float, fl::size N = 5>
using BilateralFilter = detail::BilateralFilterImpl<T, StaticCircularBuffer<T, N>>;

template <typename T>
using DynamicBilateralFilter = detail::BilateralFilterImpl<T, DynamicCircularBuffer<T>>;

} // namespace fl
