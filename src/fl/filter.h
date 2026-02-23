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
// Each FIR filter has a sensible default N (window size). You can also set
// N = 0 for a runtime-sized buffer that takes capacity at construction:
//   MedianFilter<float> mf;          // static, default N = 5
//   MedianFilter<float, 0> mf(10);   // dynamic, capacity set at runtime

#include "fl/circular_buffer.h"
#include "fl/force_inline.h"
#include "fl/math.h"
#include "fl/math_macros.h"
#include "fl/stl/algorithm.h"
#include "fl/stl/type_traits.h"

// Detail impl headers — IIR
#include "fl/detail/filter/exponential_smoother_impl.h"
#include "fl/detail/filter/leaky_integrator_impl.h"
#include "fl/detail/filter/cascaded_ema_impl.h"
#include "fl/detail/filter/biquad_filter_impl.h"
#include "fl/detail/filter/kalman_filter_impl.h"
#include "fl/detail/filter/one_euro_filter_impl.h"

// Detail impl headers — FIR
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

// First-order exponential (RC low-pass) smoother. Requires a time constant
// tau and a per-call dt so it works at any sample rate.
//
//   ExponentialSmoother<float> ema(0.1f);   // tau = 100 ms
//   void loop() {
//       float dt = millis_since_last / 1000.0f;
//       float smoothed = ema.update(analogRead(A0), dt);
//   }
//
// Larger tau = slower response. setTau() lets you change it at runtime.
//
// Fixed-point:
//   using FP = fl::fixed_point<16,16>;
//   ExponentialSmoother<FP> ema(FP(0.5f), FP(0.0f));
//   FP v = ema.update(FP(1.0f), FP(0.05f));
template <typename T = float>
class ExponentialSmoother {
  public:
    explicit ExponentialSmoother(T tau_seconds, T initial = T(0))
        : mImpl(tau_seconds, initial) {}
    FASTLED_FORCE_INLINE T update(T input, T dt_seconds) { return mImpl.update(input, dt_seconds); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset(T initial = T(0)) { mImpl.reset(initial); }
    FASTLED_FORCE_INLINE void setTau(T tau_seconds) { mImpl.setTau(tau_seconds); }
  private:
    detail::ExponentialSmootherImpl<T> mImpl;
};

// Shift-based EMA: alpha = 1/(2^K). Very cheap on integer/fixed-point types
// because it uses bit-shift instead of division.
//
//   LeakyIntegrator<int, 3> li;   // alpha = 1/8
//   void loop() {
//       int smoothed = li.update(rawADC);
//   }
//
// For floats it divides by 2^K instead. K=2 (alpha=0.25) is a good default.
template <typename T = float, int K = 2>
class LeakyIntegrator {
  public:
    LeakyIntegrator() = default;
    explicit LeakyIntegrator(T initial) : mImpl(initial) {}
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset(T initial = T(0)) { mImpl.reset(initial); }
  private:
    detail::LeakyIntegratorImpl<T, K> mImpl;
};

// N stages of EMA in series for a near-Gaussian impulse response without a
// buffer. More stages = smoother (but slower to respond).
//
//   CascadedEMA<float, 3> smooth(0.05f);  // 3-stage, tau = 50 ms
//   void loop() {
//       float v = smooth.update(sensorValue, dt);
//   }
//
// Fixed-point:
//   using FP = fl::fixed_point<16,16>;
//   CascadedEMA<FP, 2> smooth(FP(0.1f), FP(0.0f));
//   FP v = smooth.update(FP(reading), FP(dt));
template <typename T = float, int Stages = 2>
class CascadedEMA {
  public:
    explicit CascadedEMA(T tau_seconds, T initial = T(0))
        : mImpl(tau_seconds, initial) {}
    FASTLED_FORCE_INLINE T update(T input, T dt_seconds) { return mImpl.update(input, dt_seconds); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset(T initial = T(0)) { mImpl.reset(initial); }
    FASTLED_FORCE_INLINE void setTau(T tau_seconds) { mImpl.setTau(tau_seconds); }
  private:
    detail::CascadedEMAImpl<T, Stages> mImpl;
};

// Second-order IIR filter. Use the butterworth() factory for a classic
// low-pass with –12 dB/octave roll-off at a given cutoff frequency.
//
//   auto lpf = BiquadFilter<float>::butterworth(100.0f, 1000.0f);
//   // cutoff = 100 Hz, sample rate = 1 kHz
//   void loop() {
//       float filtered = lpf.update(rawSample);
//   }
//
// You can also construct directly with custom coefficients (b0,b1,b2,a1,a2).
template <typename T = float>
class BiquadFilter {
    using Impl = detail::BiquadFilterImpl<T>;
    BiquadFilter(const Impl& impl) : mImpl(impl) {}
  public:
    BiquadFilter(T b0, T b1, T b2, T a1, T a2)
        : mImpl(b0, b1, b2, a1, a2) {}
    static BiquadFilter butterworth(float cutoff_hz, float sample_rate) {
        return BiquadFilter(Impl::butterworth(cutoff_hz, sample_rate));
    }
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset() { mImpl.reset(); }
  private:
    Impl mImpl;
};

// 1-D scalar Kalman filter. Balances process noise (Q) against measurement
// noise (R) to produce an optimal estimate.
//
//   KalmanFilter<float> kf(0.01f, 0.1f);  // Q = 0.01, R = 0.1
//   void loop() {
//       float estimate = kf.update(noisyMeasurement);
//   }
//
// Fixed-point:
//   using FP = fl::fixed_point<16,16>;
//   KalmanFilter<FP> kf(FP(0.01f), FP(0.1f));
//   FP estimate = kf.update(FP(measurement));
template <typename T = float>
class KalmanFilter {
  public:
    KalmanFilter(T process_noise, T measurement_noise, T initial = T(0))
        : mImpl(process_noise, measurement_noise, initial) {}
    FASTLED_FORCE_INLINE T update(T measurement) { return mImpl.update(measurement); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset(T initial = T(0)) { mImpl.reset(initial); }
  private:
    detail::KalmanFilterImpl<T> mImpl;
};

// Adaptive velocity-based smoother (1-Euro filter). Smooths slow movements
// heavily but lets fast movements through with minimal lag. Ideal for
// VR/graphics/pointer input.
//
//   OneEuroFilter<float> oef(1.0f, 0.5f);  // min_cutoff=1 Hz, beta=0.5
//   void loop() {
//       float dt = millis_since_last / 1000.0f;
//       float smoothed = oef.update(pointerX, dt);
//   }
//
// Higher beta = less lag on fast motions but more jitter at rest.
template <typename T = float>
class OneEuroFilter {
  public:
    OneEuroFilter(T min_cutoff, T beta, T d_cutoff = T(1.0f))
        : mImpl(min_cutoff, beta, d_cutoff) {}
    FASTLED_FORCE_INLINE T update(T input, T dt) { return mImpl.update(input, dt); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset(T initial = T(0)) { mImpl.reset(initial); }
  private:
    detail::OneEuroFilterImpl<T> mImpl;
};

// ============================================================================
// FIR filter public API — composition with impl
// N > 0: static (inlined) buffer, default-constructible
// N == 0: dynamic buffer, requires capacity at construction
// ============================================================================

// Simple moving average: O(1) running sum over the last N samples.
// Good general-purpose smoother. Use when you want equal weight on recent history.
//
//   MovingAverage<float> ma;               // default N = 8
//   MovingAverage<float, 16> ma16;         // explicit 16-sample window
//   MovingAverage<float, 0> dyn(32);       // dynamic: capacity set at runtime
//   void loop() {
//       ma.update(analogRead(A0));
//       float smoothed = ma.value();       // average of last 8 readings
//   }
//
// Fixed-point:
//   using FP = fl::fixed_point<16,16>;
//   MovingAverage<FP, 4> ma;
//   ma.update(FP(reading));
//   float result = ma.value().to_float();
template <typename T = float, fl::size N = 8>
class MovingAverage {
  public:
    MovingAverage() = default;
    explicit MovingAverage(fl::size capacity) : mImpl(capacity) {}
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset() { mImpl.reset(); }
    FASTLED_FORCE_INLINE bool full() const { return mImpl.full(); }
    FASTLED_FORCE_INLINE fl::size size() const { return mImpl.size(); }
    FASTLED_FORCE_INLINE fl::size capacity() const { return mImpl.capacity(); }
    FASTLED_FORCE_INLINE void resize(fl::size new_capacity) { mImpl.resize(new_capacity); }
  private:
    detail::MovingAverageImpl<T, N> mImpl;
};

// Sliding-window median: rejects outliers by returning the middle value.
// Best for impulse/spike noise (e.g., bad sensor readings, random spikes).
//
//   MedianFilter<float> mf;                // default N = 5
//   MedianFilter<float, 0> dyn(11);        // dynamic: odd N recommended
//   void loop() {
//       mf.update(distanceSensor());
//       float clean = mf.value();          // spike-free output
//   }
//
// Fixed-point:
//   using FP = fl::fixed_point<16,16>;
//   MedianFilter<FP, 5> mf;
//   mf.update(FP(reading));
//   float result = mf.value().to_float();
template <typename T = float, fl::size N = 5>
class MedianFilter {
  public:
    MedianFilter() = default;
    explicit MedianFilter(fl::size capacity) : mImpl(capacity) {}
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset() { mImpl.reset(); }
    FASTLED_FORCE_INLINE fl::size size() const { return mImpl.size(); }
    FASTLED_FORCE_INLINE fl::size capacity() const { return mImpl.capacity(); }
    FASTLED_FORCE_INLINE void resize(fl::size new_capacity) { mImpl.resize(new_capacity); }
  private:
    detail::MedianFilterImpl<T, N> mImpl;
};

// Linearly weighted moving average: newer samples get more weight.
// Weights are [1, 2, 3, ..., N]. Use when recent data matters more than older.
//
//   WeightedMovingAverage<float> wma;       // default N = 8
//   void loop() {
//       wma.update(sensorValue);
//       float smoothed = wma.value();       // recent-biased average
//   }
//
// Fixed-point:
//   using FP = fl::fixed_point<16,16>;
//   WeightedMovingAverage<FP, 3> wma;
//   wma.update(FP(reading));
//   float result = wma.value().to_float();
template <typename T = float, fl::size N = 8>
class WeightedMovingAverage {
  public:
    WeightedMovingAverage() = default;
    explicit WeightedMovingAverage(fl::size capacity) : mImpl(capacity) {}
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset() { mImpl.reset(); }
    FASTLED_FORCE_INLINE bool full() const { return mImpl.full(); }
    FASTLED_FORCE_INLINE fl::size size() const { return mImpl.size(); }
    FASTLED_FORCE_INLINE fl::size capacity() const { return mImpl.capacity(); }
    FASTLED_FORCE_INLINE void resize(fl::size new_capacity) { mImpl.resize(new_capacity); }
  private:
    detail::WeightedMovingAverageImpl<T, N> mImpl;
};

// Triangular (tent) weighted average: peaks at center, tapers to edges.
// Weights for N=5: [1, 2, 3, 2, 1]. Smoother than MovingAverage, cheaper
// than Gaussian.
//
//   TriangularFilter<float> tf;             // default N = 8
//   void loop() {
//       tf.update(sensorValue);
//       float smoothed = tf.value();
//   }
//
// Fixed-point:
//   using FP = fl::fixed_point<16,16>;
//   TriangularFilter<FP, 5> tf;
//   tf.update(FP(reading));
//   float result = tf.value().to_float();
template <typename T = float, fl::size N = 8>
class TriangularFilter {
  public:
    TriangularFilter() = default;
    explicit TriangularFilter(fl::size capacity) : mImpl(capacity) {}
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset() { mImpl.reset(); }
    FASTLED_FORCE_INLINE bool full() const { return mImpl.full(); }
    FASTLED_FORCE_INLINE fl::size size() const { return mImpl.size(); }
    FASTLED_FORCE_INLINE fl::size capacity() const { return mImpl.capacity(); }
    FASTLED_FORCE_INLINE void resize(fl::size new_capacity) { mImpl.resize(new_capacity); }
  private:
    detail::TriangularFilterImpl<T, N> mImpl;
};

// Binomial-coefficient Gaussian approximation: bell-curve weighting.
// Weights for N=5: [1, 4, 6, 4, 1] (Pascal's triangle row).
// Best frequency-domain response of the simple FIR filters.
//
//   GaussianFilter<float> gf;               // default N = 5
//   void loop() {
//       gf.update(sensorValue);
//       float smoothed = gf.value();
//   }
//
// Fixed-point:
//   using FP = fl::fixed_point<16,16>;
//   GaussianFilter<FP, 5> gf;
//   for (int i = 0; i < 5; ++i) gf.update(FP(7.0f));
//   float result = gf.value().to_float();  // ≈ 7.0
template <typename T = float, fl::size N = 5>
class GaussianFilter {
  public:
    GaussianFilter() = default;
    explicit GaussianFilter(fl::size capacity) : mImpl(capacity) {}
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset() { mImpl.reset(); }
    FASTLED_FORCE_INLINE bool full() const { return mImpl.full(); }
    FASTLED_FORCE_INLINE fl::size size() const { return mImpl.size(); }
    FASTLED_FORCE_INLINE fl::size capacity() const { return mImpl.capacity(); }
    FASTLED_FORCE_INLINE void resize(fl::size new_capacity) { mImpl.resize(new_capacity); }
  private:
    detail::GaussianFilterImpl<T, N> mImpl;
};

// Alpha-trimmed mean: sorts window, trims extremes, averages the middle.
// With N=7 and trim_count=1, sorts 7 values, drops min and max, averages
// the remaining 5. Robust to outliers while still averaging.
//
//   AlphaTrimmedMean<float> atm(1);         // default N=7, trim 1 each end
//   AlphaTrimmedMean<float, 5> atm5(2);     // N=5, trim 2 each end (= median)
//   void loop() {
//       atm.update(sensorValue);
//       float robust = atm.value();
//   }
//
// Fixed-point:
//   using FP = fl::fixed_point<16,16>;
//   AlphaTrimmedMean<FP, 5> atm(1);
//   atm.update(FP(reading));
//   float result = atm.value().to_float();
template <typename T = float, fl::size N = 7>
class AlphaTrimmedMean {
  public:
    explicit AlphaTrimmedMean(fl::size trim_count = 1) : mImpl(trim_count) {}
    AlphaTrimmedMean(fl::size capacity, fl::size trim_count) : mImpl(capacity, trim_count) {}
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset() { mImpl.reset(); }
    FASTLED_FORCE_INLINE fl::size size() const { return mImpl.size(); }
    FASTLED_FORCE_INLINE fl::size capacity() const { return mImpl.capacity(); }
    FASTLED_FORCE_INLINE void resize(fl::size new_capacity, fl::size trim_count) { mImpl.resize(new_capacity, trim_count); }
  private:
    detail::AlphaTrimmedMeanImpl<T, N> mImpl;
};

// Hampel filter: median + MAD-based outlier rejection. Computes the median
// and median absolute deviation (MAD) of the window, then replaces values
// that deviate beyond threshold * MAD with the median.
//
//   HampelFilter<float> hf(3.0f);           // default N=5, threshold=3
//   HampelFilter<float, 7> hf7(2.0f);       // 7-sample window, tighter threshold
//   void loop() {
//       float clean = hf.update(noisyValue); // outliers replaced with median
//   }
template <typename T = float, fl::size N = 5>
class HampelFilter {
  public:
    explicit HampelFilter(T threshold = T(3.0f)) : mImpl(threshold) {}
    HampelFilter(fl::size capacity, T threshold) : mImpl(capacity, threshold) {}
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset() { mImpl.reset(); }
    FASTLED_FORCE_INLINE fl::size size() const { return mImpl.size(); }
    FASTLED_FORCE_INLINE fl::size capacity() const { return mImpl.capacity(); }
    FASTLED_FORCE_INLINE void resize(fl::size new_capacity) { mImpl.resize(new_capacity); }
  private:
    detail::HampelFilterImpl<T, N> mImpl;
};

// Savitzky-Golay: fits a local polynomial (quadratic) to the window and
// returns the center value. Smooths noise while preserving peaks, edges,
// and the overall shape of the signal better than averaging filters.
//
//   SavitzkyGolayFilter<float> sg;           // default N = 5
//   SavitzkyGolayFilter<float, 7> sg7;       // wider window = more smoothing
//   void loop() {
//       sg.update(spectrumBin);
//       float smoothed = sg.value();         // peaks preserved
//   }
//
// Fixed-point:
//   using FP = fl::fixed_point<16,16>;
//   SavitzkyGolayFilter<FP, 5> sg;
//   for (int i = 0; i < 5; ++i) sg.update(FP(3.0f));
//   float result = sg.value().to_float();   // ≈ 3.0
template <typename T = float, fl::size N = 5>
class SavitzkyGolayFilter {
  public:
    SavitzkyGolayFilter() = default;
    explicit SavitzkyGolayFilter(fl::size capacity) : mImpl(capacity) {}
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset() { mImpl.reset(); }
    FASTLED_FORCE_INLINE bool full() const { return mImpl.full(); }
    FASTLED_FORCE_INLINE fl::size size() const { return mImpl.size(); }
    FASTLED_FORCE_INLINE fl::size capacity() const { return mImpl.capacity(); }
    FASTLED_FORCE_INLINE void resize(fl::size new_capacity) { mImpl.resize(new_capacity); }
  private:
    detail::SavitzkyGolayFilterImpl<T, N> mImpl;
};

// Bilateral filter: edge-preserving smoother weighted by value similarity.
// Each sample's weight depends on how close its value is to the newest sample.
// Small sigma_range = only very similar values contribute (sharp edges kept).
// Large sigma_range = all values contribute equally (acts like a box filter).
//
//   BilateralFilter<float> bf(1.0f);        // default N=5, sigma=1.0
//   BilateralFilter<float, 7> bf7(0.5f);    // 7-sample, tighter similarity
//   void loop() {
//       bf.update(ledBrightness);
//       float smoothed = bf.value();         // edges preserved
//   }
template <typename T = float, fl::size N = 5>
class BilateralFilter {
  public:
    explicit BilateralFilter(T sigma_range = T(1.0f)) : mImpl(sigma_range) {}
    BilateralFilter(fl::size capacity, T sigma_range) : mImpl(capacity, sigma_range) {}
    FASTLED_FORCE_INLINE T update(T input) { return mImpl.update(input); }
    FASTLED_FORCE_INLINE T value() const { return mImpl.value(); }
    FASTLED_FORCE_INLINE void reset() { mImpl.reset(); }
    FASTLED_FORCE_INLINE bool full() const { return mImpl.full(); }
    FASTLED_FORCE_INLINE fl::size size() const { return mImpl.size(); }
    FASTLED_FORCE_INLINE fl::size capacity() const { return mImpl.capacity(); }
    FASTLED_FORCE_INLINE void resize(fl::size new_capacity) { mImpl.resize(new_capacity); }
  private:
    detail::BilateralFilterImpl<T, N> mImpl;
};

} // namespace fl
