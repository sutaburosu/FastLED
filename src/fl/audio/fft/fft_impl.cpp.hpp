#ifndef FASTLED_INTERNAL
#define FASTLED_INTERNAL
#endif

#include "fl/fastled.h"

// IWYU pragma: begin_keep
#include "third_party/cq_kernel/cq_kernel.h"
#include "third_party/cq_kernel/kiss_fftr.h"

// IWYU pragma: end_keep
#include "fl/stl/alloca.h"
#include "fl/stl/array.h"
#include "fl/audio/audio.h"
#include "fl/audio/fft/fft.h"
#include "fl/audio/fft/fft_impl.h"
#include "fl/stl/string.h"
#include "fl/stl/compiler_control.h"
#include "fl/stl/fixed_point.h"
#include "fl/stl/vector.h"
#include "fl/gfx/alpha.h"
#include "fl/system/log.h"

#include "fl/stl/cstring.h"

#ifndef FL_AUDIO_SAMPLE_RATE
#define FL_AUDIO_SAMPLE_RATE 44100
#endif

#ifndef FL_FFT_SAMPLE_RATE
#define FL_FFT_SAMPLE_RATE FL_AUDIO_SAMPLE_RATE
#endif

#ifndef FL_FFT_SAMPLES
#define FL_FFT_SAMPLES 512
#endif

#ifndef FL_FFT_BANDS
#define FL_FFT_BANDS 16
#endif

#ifndef FL_FFT_MIN_VAL
#define FL_FFT_MIN_VAL 5000 // Equivalent to 0.15 in Q15
#endif

#ifndef FL_FFT_PRINT_HEADER
#define FL_FFT_PRINT_HEADER 1
#endif

namespace fl {

    // Delegates to FFT_Args::resolve() — single source of truth.

class FFTContext {
  public:
    FFTContext(int samples, int bands, float fmin, float fmax, int sample_rate,
              FFTMode mode, FFTWindow window)
        : m_fftr_cfg(nullptr), m_input_samples(samples),
          m_kernels(nullptr),
          m_mode(mode), m_totalBands(bands), m_fmin(fmin), m_fmax(fmax),
          m_sampleRate(sample_rate), m_window(window) {
        FFT_Args::resolveModeEnums(m_mode, m_window, bands, samples, fmin, fmax);
        fl::memset(&m_cq_cfg, 0, sizeof(m_cq_cfg));

        m_fftr_cfg = kiss_fftr_alloc(samples, 0, nullptr, nullptr);
        if (!m_fftr_cfg) {
            FASTLED_WARN("Failed to allocate FFTImpl context");
            return;
        }

        switch (m_mode) {
        case FFTMode::LOG_REBIN:
            initLogRebin();
            break;
        case FFTMode::CQ_NAIVE:
            initNaive(samples, bands, fmin, fmax, sample_rate);
            break;
        case FFTMode::CQ_HYBRID:
            initHybrid(samples, bands, fmin, fmax, sample_rate);
            break;
        case FFTMode::CQ_OCTAVE:
            initOctaveWise(samples, bands, fmin, fmax, sample_rate);
            break;
        case FFTMode::AUTO:
            FASTLED_WARN("FFTMode::AUTO should have been resolved");
            break;
        }
    }

    ~FFTContext() {
        if (m_fftr_cfg) {
            kiss_fftr_free(m_fftr_cfg);
        }
        if (m_kernels) {
            free_kernels(m_kernels, m_cq_cfg);
        }
        for (int i = 0; i < static_cast<int>(m_octaves.size()); i++) {
            if (m_octaves[i].kernels) {
                free_kernels(m_octaves[i].kernels, m_octaves[i].cfg);
            }
        }
        if (m_hybridSmallFft) {
            kiss_fftr_free(m_hybridSmallFft);
        }
        if (m_hybridMidFft) {
            kiss_fftr_free(m_hybridMidFft);
        }
    }

    fl::size sampleSize() const { return m_input_samples; }

    void run(span<const i16> buffer, FFTBins *out) {
        switch (m_mode) {
        case FFTMode::LOG_REBIN:
            runLogRebin(buffer, out);
            break;
        case FFTMode::CQ_NAIVE:
            runNaive(buffer, out);
            break;
        case FFTMode::CQ_OCTAVE:
            runOctaveWise(buffer, out);
            break;
        case FFTMode::CQ_HYBRID:
            runHybrid(buffer, out);
            break;
        case FFTMode::AUTO:
            FASTLED_WARN("FFTMode::AUTO should have been resolved");
            break;
        }
    }

    fl::string info() const {
        FFTBins tmp(m_totalBands);
        tmp.setParams(m_fmin, m_fmax, m_sampleRate);
        for (int i = 0; i < m_totalBands; ++i) {
            tmp.raw_mut().push_back(0.0f);
        }

        fl::sstream ss;
        ss << "FFTImpl Frequency Bands (CQ log-spaced): ";
        for (int i = 0; i < m_totalBands; ++i) {
            float f_low = (i == 0) ? m_fmin : tmp.binBoundary(i - 1);
            float f_high =
                (i == m_totalBands - 1) ? m_fmax : tmp.binBoundary(i);
            ss << f_low << "Hz-" << f_high << "Hz, ";
        }
        return ss.str();
    }

  private:
    // ---- Log-rebin path (fast, no CQ kernels) ----
    //
    // Single 512-point FFT, then group the linear FFT bins into
    // geometrically-spaced output bins. Same approach as WLED.
    // Cost: ~0.15ms on ESP32-S3. No kernel memory.

    void initLogRebin() {
        // Pre-compute bin edges aligned with CQ center frequencies.
        // CQ center[i] = fmin * exp(logRatio * i / (bands-1)), so
        // binToFreq(i) returns these centers for both modes.
        //
        // Edges are placed at the geometric mean of adjacent centers:
        //   edge[i] = sqrt(center[i-1] * center[i])
        //           = fmin * exp(logRatio * (2*i - 1) / (2*(bands-1)))
        //
        // edge[0] and edge[bands] extend half a bin beyond fmin/fmax.
        const int bands = m_totalBands;
        m_logBinEdges.resize(bands + 1);
        float logRatio = logf(m_fmax / m_fmin);
        if (bands <= 1) {
            m_logBinEdges[0] = m_fmin;
            m_logBinEdges[1] = m_fmax;
        } else {
            float denom = 2.0f * static_cast<float>(bands - 1);
            // Edge below first center (half-bin below fmin)
            m_logBinEdges[0] =
                m_fmin * expf(-logRatio / denom);
            // Intermediate edges: geometric mean of adjacent CQ centers
            for (int i = 1; i < bands; i++) {
                m_logBinEdges[i] =
                    m_fmin *
                    expf(logRatio * (2.0f * static_cast<float>(i) - 1.0f) /
                         denom);
            }
            // Edge above last center (half-bin above fmax)
            m_logBinEdges[bands] =
                m_fmax * expf(logRatio / denom);
        }

        computeBinEdgesQ16();

        // Pre-compute bin mapping LUTs
        buildLogBinLut(m_logBinLut, m_input_samples,
                       static_cast<float>(m_sampleRate), 0, m_totalBands);
        buildLinearBinLut(m_linearBinLut, m_input_samples);

        // Pre-compute window as Q15 integer coefficients
        computeWindow(m_window_buf, m_input_samples, m_window);

        // Pre-compute bin-width normalization factors for LOG_REBIN.
        // Without normalization, wider high-frequency bins accumulate more
        // sidelobe energy than narrow low-frequency bins, creating visible
        // "aliasing" artifacts during a tone sweep.
        computeLogRebinNormFactors(m_logBinNormFactors, m_logBinLut,
                                   m_input_samples, static_cast<float>(m_sampleRate),
                                   0, m_totalBands);
    }

    void runLogRebin(span<const i16> buffer, FFTBins *out) {
        out->setParams(m_fmin, m_fmax, m_sampleRate);
        const int N = m_input_samples;
        const int bands = m_totalBands;
        const int numRawBins = N / 2 + 1;

        // Apply Q15 window (integer multiply, no float)
        FASTLED_STACK_ARRAY(kiss_fft_scalar, windowed, N);
        applyWindow(buffer.data(), m_window_buf.data(), windowed, N);

        FASTLED_STACK_ARRAY(kiss_fft_cpx, fft, N);
        kiss_fftr(m_fftr_cfg, windowed, fft);

        // Deinterleave AoS → SoA and batch-compute magnitudes
        FASTLED_STACK_ARRAY(kiss_fft_scalar, re, numRawBins);
        FASTLED_STACK_ARRAY(kiss_fft_scalar, im, numRawBins);
        FASTLED_STACK_ARRAY(u16, mag, numRawBins);
        deinterleave(fft, re, im, numRawBins);
        batchMag(re, im, mag, numRawBins);

        // Linear bins (same as other paths)
        computeLinearBins(mag, N, out);

        // Group FFT bins into log-spaced output bins (integer accumulation)
        FASTLED_STACK_ARRAY(u32, rawBinsI, bands);
        for (int i = 0; i < bands; ++i) {
            rawBinsI[i] = 0;
        }
        logRebinRange(mag, N, static_cast<float>(m_sampleRate),
                      0, bands, rawBinsI, m_logBinLut);

        // Store raw magnitudes (dB computed lazily by FFTBins::db())
        fl::vector<float> &rawBins = out->raw_mut();
        rawBins.resize(bands);
        for (int i = 0; i < bands; ++i) {
            rawBins[i] = static_cast<float>(rawBinsI[i]);
        }

        // Store bin-width normalization factors so consumers can optionally
        // normalize (e.g. for equalization display). Raw output is unchanged.
        out->setNormFactors(m_logBinNormFactors);
    }

    // ---- Naive single-FFT path (narrow frequency ranges) ----

    void initNaive(int samples, int bands, float fmin, float fmax, int sr) {
        m_cq_cfg.samples = samples;
        m_cq_cfg.bands = bands;
        m_cq_cfg.fmin = fmin;
        m_cq_cfg.fmax = fmax;
        m_cq_cfg.fs = sr;
        m_cq_cfg.min_val = FL_FFT_MIN_VAL;
        m_kernels = generate_kernels(m_cq_cfg);
        buildLinearBinLut(m_linearBinLut, samples);
        // Note: CQ kernels already apply Hamming windowing in frequency domain.
        // Adding time-domain Hanning would double-window and over-attenuate.
    }

    void runNaive(span<const i16> buffer, FFTBins *out) {
        out->setParams(m_fmin, m_fmax, m_sampleRate);
        const int fftSize = m_input_samples;
        const int numRawBins = fftSize / 2 + 1;

        FASTLED_STACK_ARRAY(kiss_fft_cpx, fft, fftSize);
        kiss_fftr(m_fftr_cfg, buffer.data(), fft);

        // Deinterleave AoS → SoA and batch-compute magnitudes
        FASTLED_STACK_ARRAY(kiss_fft_scalar, re, numRawBins);
        FASTLED_STACK_ARRAY(kiss_fft_scalar, im, numRawBins);
        FASTLED_STACK_ARRAY(u16, mag, numRawBins);
        deinterleave(fft, re, im, numRawBins);
        batchMag(re, im, mag, numRawBins);

        computeLinearBins(mag, fftSize, out);

        FASTLED_STACK_ARRAY(kiss_fft_cpx, cq, m_cq_cfg.bands);
        apply_kernels(fft, cq, m_kernels, m_cq_cfg);

        const int bands = m_cq_cfg.bands;
        fl::vector<float> &rawBins = out->raw_mut();
        rawBins.resize(bands);
        for (int i = 0; i < bands; ++i) {
            i32 real = cq[i].r;
            i32 imag = cq[i].i;
#ifdef FIXED_POINT
            rawBins[i] = static_cast<float>(fastMag(real, imag));
#else
            float r2 = float(real * real);
            float i2 = float(imag * imag);
            rawBins[i] = sqrt(r2 + i2);
#endif
        }
    }

    // Fast integer magnitude: max(|re|,|im|) + 0.40625*min(|re|,|im|)
    // Max error ~3.5% vs exact sqrt(re²+im²). No float, no division.
    // 0.40625 = 13/32, exactly representable → bit-exact with original.
    static inline u16 fastMag(i32 re, i32 im) {
        u32 a = (re >= 0) ? static_cast<u32>(re) : static_cast<u32>(-re);
        u32 b = (im >= 0) ? static_cast<u32>(im) : static_cast<u32>(-im);
        u32 mx = (a > b) ? a : b;
        u32 mn = (a <= b) ? a : b;
        static constexpr u16x16 kMinWeight(0.40625f);
        return static_cast<u16>(mx + (kMinWeight * static_cast<i32>(mn)).to_int());
    }

    // Fast integer dB conversion: 20 * log10(x) = 6.02060 * log2(x).
    // Uses MSB position for integer part, corrected linear interpolation
    // for fractional part. Max error ~0.05 dB — imperceptible for audio
    // visualization. No log10f, no division.
    // Internally uses fixed-point u16x16 in FIXED16 mode, converts to float at output.
    static inline float fastDb(u32 x) {
        if (x == 0) return 0.0f;

        // Find highest set bit (integer part of log2)
        int msb = 0;
        {
            u32 v = x;
            if (v >= 0x10000u) { v >>= 16; msb += 16; }
            if (v >= 0x100u)   { v >>= 8;  msb += 8; }
            if (v >= 0x10u)    { v >>= 4;  msb += 4; }
            if (v >= 0x4u)     { v >>= 2;  msb += 2; }
            if (v >= 0x2u)     { msb += 1; }
        }

        // Normalized mantissa in [0, 1) as u16x16
        u32 t_raw;
        if (msb >= 16) {
            t_raw = (x >> (msb - 16)) - 65536u;
        } else {
            t_raw = (x << (16 - msb)) - 65536u;
        }
        u16x16 t = u16x16::from_raw(t_raw);

        // log2(1+t) ~= t + 0.345 * t * (1-t)  [max error ~0.008]
        // 22610 = 0.345 * 65536 (from_raw preserves exact original constant)
        static constexpr u16x16 one(1.0f);
        static constexpr u16x16 kCorrection = u16x16::from_raw(22610u);
        u16x16 complement = one - t;
        u16x16 prod = t * complement;
        u16x16 correction = prod * kCorrection;
        u16x16 frac = t + correction;

        // log2(x) = msb + frac, assembled as u16x16
        u16x16 log2_val = u16x16::from_raw(
            (static_cast<u32>(msb) << 16) + frac.raw());

        // 20*log10(x) = 6.02060 * log2(x)
#if FASTLED_FFT_PRECISION == FASTLED_FFT_FIXED16
        // 394593 = 6.02060 * 65536 (from_raw preserves exact original constant)
        static constexpr u16x16 kDbScale = u16x16::from_raw(394593u);
        u16x16 db = log2_val * kDbScale;
        return db.to_float();
#else
        return 6.02060f * log2_val.to_float();
#endif
    }

    // Deinterleave AoS kiss_fft_cpx into SoA re[]/im[] arrays.
    // Enables auto-vectorization of the subsequent batchMag loop.
    static void deinterleave(const kiss_fft_cpx *cpx,
                             kiss_fft_scalar *re, kiss_fft_scalar *im,
                             int n) {
        for (int i = 0; i < n; ++i) {
            re[i] = cpx[i].r;
            im[i] = cpx[i].i;
        }
    }

    // Batch magnitude: compute fastMag for contiguous SoA re[]/im[] → mag[].
    // Contiguous layout allows compiler to auto-vectorize (4-8 mags per SIMD).
    static void batchMag(const kiss_fft_scalar *re,
                         const kiss_fft_scalar *im,
                         u16 *mag, int n) {
        for (int i = 0; i < n; ++i) {
            mag[i] = fastMag(re[i], im[i]);
        }
    }

    // Compute Q16.16 bin edges from float bin edges for integer inner loops.
    void computeBinEdgesQ16() {
        int n = static_cast<int>(m_logBinEdges.size());
        m_logBinEdgesQ16.resize(n);
        for (int i = 0; i < n; ++i) {
            m_logBinEdgesQ16[i] = u16x16(m_logBinEdges[i]);
        }
    }

    // Build log-bin LUT: for each FFT bin k, pre-compute which output bin
    // it maps to. Moves the binary search from runtime to init.
    void buildLogBinLut(fl::vector<u8>& lut, int fftN, float fs,
                        int binStart, int binEnd) {
        const int numRawBins = fftN / 2 + 1;
        lut.resize(numRawBins);
        const u16x16 rawBinHz(fs / static_cast<float>(fftN));

        for (int k = 0; k < numRawBins; ++k) {
            u16x16 freq = rawBinHz * static_cast<u32>(k);

            int lo = binStart, hi = binEnd - 1;
            while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (m_logBinEdgesQ16[mid] <= freq)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            lut[k] = static_cast<u8>(lo);
        }
    }

    // Build linear-bin LUT: for each FFT bin k, pre-compute which linear
    // output bin it maps to. Moves the u16x16 division from runtime to init.
    void buildLinearBinLut(fl::vector<u8>& lut, int fftN) {
        const int numRawBins = fftN / 2 + 1;
        const int numLinearBins = m_totalBands;
        lut.resize(numRawBins);

        const u16x16 rawBinHz(static_cast<float>(m_sampleRate) /
                              static_cast<float>(fftN));
        const u16x16 halfBin = rawBinHz >> 1;
        const u16x16 fminFP(m_fmin);
        const u16x16 fmaxFP(m_fmax);
        const u16x16 linearBinHz(
            (m_fmax - m_fmin) / static_cast<float>(numLinearBins));

        // Pre-compute loop bounds (stored for runtime use)
        m_linearKStart = 0;
        if (fminFP > halfBin) {
            m_linearKStart = static_cast<int>(
                u16x16::ceil((fminFP - halfBin) / rawBinHz).to_int());
        }
        m_linearKEnd = static_cast<int>(
            u16x16::ceil((fmaxFP + halfBin) / rawBinHz).to_int());
        if (m_linearKEnd > numRawBins) m_linearKEnd = numRawBins;

        for (int k = 0; k < numRawBins; ++k) {
            u16x16 freq = rawBinHz * static_cast<u32>(k);

            if (freq < fminFP) {
                lut[k] = 0;
                continue;
            }
            int linIdx = static_cast<int>(
                ((freq - fminFP) / linearBinHz).to_int());
            if (linIdx >= numLinearBins)
                linIdx = numLinearBins - 1;
            lut[k] = static_cast<u8>(linIdx);
        }
    }

    // Compute window function as alpha16 (UNORM16) coefficients in [0, 1].
    // Uses fixed-point arithmetic throughout to avoid float on MCUs.
    static void computeWindow(fl::vector<alpha16> &win, int N, FFTWindow type) {
        using FP = fl::fixed_point<16, 16>;
        win.resize(N);

        // NONE: rectangular window (all coefficients = 1.0)
        if (type == FFTWindow::NONE) {
            for (int n = 0; n < N; ++n) {
                win[n] = alpha16(65535);
            }
            return;
        }

        const FP two_pi(6.2831853f);   // 2π
        const FP invNm1 = FP(1) / FP(N - 1);
        const FP phase_step = two_pi * invNm1;

        // Blackman-Harris coefficients
        constexpr FP bh_a0(0.35875f);
        constexpr FP bh_a1(0.48829f);
        constexpr FP bh_a2(0.14128f);
        constexpr FP bh_a3(0.01168f);
        // Hanning coefficients
        constexpr FP half(0.5f);
        constexpr FP one(1.0f);

        FP phase(0.0f);
        for (int n = 0; n < N; ++n) {
            FP w;
            switch (type) {
            case FFTWindow::BLACKMAN_HARRIS: {
                // 4-term Blackman-Harris: -92 dB sidelobe rejection
                FP phase2 = phase + phase;
                FP phase3 = phase2 + phase;
                w = bh_a0 - bh_a1 * FP::cos(phase)
                    + bh_a2 * FP::cos(phase2)
                    - bh_a3 * FP::cos(phase3);
                break;
            }
            case FFTWindow::HANNING:
            default:
                w = half * (one - FP::cos(phase));
                break;
            }
            i32 raw = w.raw();
            if (raw < 0) raw = 0;          // guard against FP rounding
            if (raw > 65535) raw = 65535;   // 1.0 in s16x16 = 65536, clamp for u16
            win[n] = alpha16(static_cast<unsigned short>(raw));
            phase = phase + phase_step;
        }
    }

    // Apply window: out[i] = sample[i] * win[i]
    // Window coefficients are alpha16 (UNORM16 [0,1]); uses scale_signed().
    static void applyWindow(const kiss_fft_scalar *samples,
                            const alpha16 *win, kiss_fft_scalar *out, int N) {
        for (int i = 0; i < N; ++i) {
            out[i] = static_cast<kiss_fft_scalar>(
                win[i].scale_signed(static_cast<int>(samples[i])));
        }
    }

    void initWindow() {
        computeWindow(m_window_buf, m_input_samples, m_window);
    }

    // ---- Octave-wise CQT path (wide frequency ranges) ----
    //
    // Instead of one massive FFT + degenerate kernels, split the frequency
    // range into octaves. Each octave uses the same N-point FFT (512) with
    // well-conditioned CQ kernels (N_window >= N/2). The signal is decimated
    // by 2x between octaves via a halfband filter.
    //
    // Memory: ~25KB total vs ~830KB for zero-padding approach.

    struct OctaveInfo {
        int firstBin;
        int numBins;
        cq_kernel_cfg cfg;
        cq_kernels_t kernels;
    };

    void initOctaveWise(int samples, int bands, float fmin, float fmax,
                        int sr) {


        // Use floor so the top octave covers the remaining frequency range
        // rather than creating a tiny sliver octave with 1 bin.
        int numOctaves = static_cast<int>(floorf(log2f(fmax / fmin)));
        if (numOctaves < 1)
            numOctaves = 1;

        // Log-spaced center frequencies for all bins
        float logRatio = logf(fmax / fmin);
        FASTLED_STACK_ARRAY(float, centerFreqs, bands);
        for (int i = 0; i < bands; i++) {
            centerFreqs[i] =
                fmin *
                expf(logRatio * static_cast<float>(i) /
                     static_cast<float>(bands - 1));
        }

        // Assign each bin to an octave: oct j spans [fmin*2^j, fmin*2^(j+1))
        FASTLED_STACK_ARRAY(int, binOctave, bands);
        for (int i = 0; i < bands; i++) {
            int oct =
                static_cast<int>(floorf(log2f(centerFreqs[i] / fmin)));
            if (oct < 0)
                oct = 0;
            if (oct >= numOctaves)
                oct = numOctaves - 1;
            binOctave[i] = oct;
        }

        // Build per-octave CQ kernel sets
        m_octaves.resize(numOctaves);
        m_maxBinsPerOctave = 0;
        for (int oct = 0; oct < numOctaves; oct++) {
            int first = -1, last = -1;
            for (int i = 0; i < bands; i++) {
                if (binOctave[i] == oct) {
                    if (first < 0)
                        first = i;
                    last = i;
                }
            }

            OctaveInfo &oi = m_octaves[oct];
            fl::memset(&oi.cfg, 0, sizeof(oi.cfg));
            oi.kernels = nullptr;
            if (first < 0) {
                oi.firstBin = 0;
                oi.numBins = 0;
                continue;
            }

            oi.firstBin = first;
            oi.numBins = last - first + 1;
            if (oi.numBins > m_maxBinsPerOctave) {
                m_maxBinsPerOctave = oi.numBins;
            }

            // Decimation: top octave (numOctaves-1) uses original sample rate.
            // Each lower octave halves the effective sample rate.
            int decimExp = numOctaves - 1 - oct;
            float effectiveFs =
                static_cast<float>(sr) /
                static_cast<float>(1 << decimExp);

            oi.cfg.samples = samples;
            oi.cfg.bands = oi.numBins;
            oi.cfg.fmin = centerFreqs[first];
            oi.cfg.fmax = (oi.numBins > 1) ? centerFreqs[last]
                                            : centerFreqs[first] * 2.0f;
            oi.cfg.fs = effectiveFs;
            oi.cfg.min_val = FL_FFT_MIN_VAL;

            oi.kernels = generate_kernels(oi.cfg);
        }

        // Pre-allocate reusable buffers
        m_workBuf.resize(samples);
        m_fftOut.resize(samples);

        buildLinearBinLut(m_linearBinLut, samples);
        // Note: CQ kernels already apply Hamming windowing in frequency domain.
        // Adding time-domain Hanning would double-window and over-attenuate.
    }

    void runOctaveWise(span<const i16> buffer, FFTBins *out) {
        const int N = m_input_samples;
        const int numOctaves = static_cast<int>(m_octaves.size());
        const int numRawBins = N / 2 + 1;

        out->setParams(m_fmin, m_fmax, m_sampleRate);

        // Copy input to working buffer
        int workLen = N;
        for (int i = 0; i < N; i++) {
            m_workBuf[i] =
                (i < static_cast<int>(buffer.size())) ? buffer[i] : 0;
        }

        // FFT at full sample rate (for linear bins + top octave CQ)
        kiss_fftr(m_fftr_cfg, m_workBuf.data(), m_fftOut.data());

        // Deinterleave AoS → SoA and batch-compute magnitudes
        FASTLED_STACK_ARRAY(kiss_fft_scalar, re, numRawBins);
        FASTLED_STACK_ARRAY(kiss_fft_scalar, im, numRawBins);
        FASTLED_STACK_ARRAY(u16, mag, numRawBins);
        deinterleave(m_fftOut.data(), re, im, numRawBins);
        batchMag(re, im, mag, numRawBins);

        computeLinearBins(mag, N, out);

        // Prepare CQ output bins
        fl::vector<float> &rawBins = out->raw_mut();
        rawBins.resize(m_totalBands);
        for (int i = 0; i < m_totalBands; i++) {
            rawBins[i] = 0.0f;
        }

        // Pre-allocate CQ accumulator once (avoids alloca in loop)
        FASTLED_STACK_ARRAY(kiss_fft_cpx, cq, m_maxBinsPerOctave);

        // Process octaves from top (highest freq) to bottom (lowest freq).
        // Top octave uses the FFT already computed above.
        // Each lower octave: decimate signal by 2x, then FFT + CQ.
        for (int oct = numOctaves - 1; oct >= 0; oct--) {
            const OctaveInfo &oi = m_octaves[oct];
            if (oi.numBins <= 0 || !oi.kernels)
                continue;

            if (oct != numOctaves - 1) {
                decimateBy2(m_workBuf.data(), workLen);
                workLen = workLen / 2;
                // Zero-pad remainder so FFT sees clean input
                for (int i = workLen; i < N; i++)
                    m_workBuf[i] = 0;
                kiss_fftr(m_fftr_cfg, m_workBuf.data(), m_fftOut.data());
            }

            // Zero the CQ accumulator and apply kernels
            fl::memset(cq, 0, sizeof(kiss_fft_cpx) * oi.numBins);
            apply_kernels(m_fftOut.data(), cq, oi.kernels, oi.cfg);

            for (int i = 0; i < oi.numBins; i++) {
                int binIdx = oi.firstBin + i;
                i32 real = cq[i].r;
                i32 imag = cq[i].i;
#ifdef FIXED_POINT
                rawBins[binIdx] = static_cast<float>(fastMag(real, imag));
#else
                float r2 = float(real * real);
                float i2 = float(imag * imag);
                rawBins[binIdx] = sqrt(r2 + i2);
#endif
            }
        }
    }

    // ---- Hybrid path: dual LOG_REBIN (full-rate upper + decimated bass) ----
    //
    // Two FFT passes, both using LOG_REBIN (no CQ kernels):
    //   1. Full-rate windowed 512-point FFT → LOG_REBIN for upper bins
    //   2. Decimated small FFT (e.g. 64-point) → LOG_REBIN for bass bins
    //
    // The small FFT is much faster than 512-point, and the anti-alias
    // decimation filter removes high-frequency content before bass analysis.

    void initHybrid(int samples, int bands, float fmin, float fmax, int sr) {
        float logRatio = logf(fmax / fmin);

        // Log-spaced center frequencies for all bins
        FASTLED_STACK_ARRAY(float, centerFreqs, bands);
        for (int i = 0; i < bands; i++) {
            centerFreqs[i] =
                fmin * expf(logRatio * static_cast<float>(i) /
                            static_cast<float>(bands - 1));
        }

        // 3-tier split at octave boundaries:
        //   bass/mid  at fmin*4 (~698 Hz, 2 octaves above fmin)
        //   mid/upper at fmin*8 (~1397 Hz, 3 octaves above fmin)
        float bassMidFreq = fmin * 4.0f;
        float midUpperFreq = fmin * 8.0f;

        // Clamp splits to valid range
        if (midUpperFreq >= fmax) midUpperFreq = fmax * 0.5f;
        if (bassMidFreq >= midUpperFreq) bassMidFreq = midUpperFreq * 0.5f;

        // Find split bin indices
        m_hybridSplitBin = 0;
        for (int i = 0; i < bands; i++) {
            if (centerFreqs[i] < bassMidFreq)
                m_hybridSplitBin = i + 1;
        }
        m_hybridMidSplitBin = m_hybridSplitBin;
        for (int i = m_hybridSplitBin; i < bands; i++) {
            if (centerFreqs[i] < midUpperFreq)
                m_hybridMidSplitBin = i + 1;
        }

        // Ensure each tier has at least 1 bin
        if (m_hybridSplitBin < 1) m_hybridSplitBin = 1;
        if (m_hybridMidSplitBin <= m_hybridSplitBin)
            m_hybridMidSplitBin = m_hybridSplitBin + 1;
        if (m_hybridMidSplitBin >= bands)
            m_hybridMidSplitBin = bands - 1;

        // LOG_REBIN bin edges (shared by all three tiers)
        m_logBinEdges.resize(bands + 1);
        if (bands <= 1) {
            m_logBinEdges[0] = fmin;
            m_logBinEdges[1] = fmax;
        } else {
            float denom = 2.0f * static_cast<float>(bands - 1);
            m_logBinEdges[0] = fmin * expf(-logRatio / denom);
            for (int i = 1; i < bands; i++) {
                m_logBinEdges[i] =
                    fmin * expf(logRatio *
                                (2.0f * static_cast<float>(i) - 1.0f) / denom);
            }
            m_logBinEdges[bands] = fmax * expf(logRatio / denom);
        }

        computeBinEdgesQ16();

        // Window for the full-rate 512pt FFT (upper tier)
        initWindow();

        // Mid-tier: 2 decimation steps → samples/4 at sr/4
        // Zero-pad 2x: 128 real samples → 256pt FFT → 43 Hz bins
        m_hybridMidN = samples / 4;
        m_hybridMidFs = static_cast<float>(sr) / 4.0f;
        m_hybridMidFft = kiss_fftr_alloc(m_hybridMidN * 2, 0, nullptr, nullptr);
        m_hybridMidFftOut.resize(m_hybridMidN * 2);
        computeWindow(m_hybridMidWindow, m_hybridMidN, m_window);

        // Bass-tier: 3 decimation steps → samples/8 at sr/8
        m_hybridSmallN = samples / 8;
        m_hybridSmallFs = static_cast<float>(sr) / 8.0f;
        m_hybridSmallFft =
            kiss_fftr_alloc(m_hybridSmallN, 0, nullptr, nullptr);
        m_hybridSmallFftOut.resize(m_hybridSmallN);
        computeWindow(m_hybridBassWindow, m_hybridSmallN, m_window);

        // Work buffer for decimation (reused across phases)
        m_workBuf.resize(samples);
        m_fftOut.resize(samples);

        // Pre-computed bin mapping LUTs for each tier
        buildLogBinLut(m_logBinLut, samples,
                       static_cast<float>(sr),
                       m_hybridMidSplitBin, bands);
        buildLogBinLut(m_logBinLutMid, m_hybridMidN * 2,
                       m_hybridMidFs,
                       m_hybridSplitBin, m_hybridMidSplitBin);
        buildLogBinLut(m_logBinLutBass, m_hybridSmallN,
                       m_hybridSmallFs,
                       0, m_hybridSplitBin);
        buildLinearBinLut(m_linearBinLut, samples);

        // Pre-compute normalization factors for each hybrid tier
        computeLogRebinNormFactors(m_hybridNormUpper, m_logBinLut,
                                   samples, static_cast<float>(sr),
                                   m_hybridMidSplitBin, bands);
        computeLogRebinNormFactors(m_hybridNormMid, m_logBinLutMid,
                                   m_hybridMidN * 2, m_hybridMidFs,
                                   m_hybridSplitBin, m_hybridMidSplitBin);
        computeLogRebinNormFactors(m_hybridNormBass, m_logBinLutBass,
                                   m_hybridSmallN, m_hybridSmallFs,
                                   0, m_hybridSplitBin);

        // Pre-compute merged norm factors (avoids per-frame allocation)
        m_hybridMergedNorm.resize(bands);
        for (int i = 0; i < bands; ++i) {
            if (i >= m_hybridMidSplitBin && i < static_cast<int>(m_hybridNormUpper.size())) {
                m_hybridMergedNorm[i] = m_hybridNormUpper[i];
            } else if (i >= m_hybridSplitBin && i < static_cast<int>(m_hybridNormMid.size())) {
                m_hybridMergedNorm[i] = m_hybridNormMid[i];
            } else if (i < static_cast<int>(m_hybridNormBass.size())) {
                m_hybridMergedNorm[i] = m_hybridNormBass[i];
            } else {
                m_hybridMergedNorm[i] = 1.0f;
            }
        }
    }

    void runHybrid(span<const i16> buffer, FFTBins *out) {
        const int N = m_input_samples;
        const int numRawBins = N / 2 + 1;

        out->setParams(m_fmin, m_fmax, m_sampleRate);

        // Reusable SoA + magnitude buffers (sized to largest FFT)
        FASTLED_STACK_ARRAY(kiss_fft_scalar, re, numRawBins);
        FASTLED_STACK_ARRAY(kiss_fft_scalar, im, numRawBins);
        FASTLED_STACK_ARRAY(u16, mag, numRawBins);

        // Phase 1: Windowed 512pt FFT → LOG_REBIN for upper bins
        FASTLED_STACK_ARRAY(kiss_fft_scalar, windowed, N);
        applyWindow(buffer.data(), m_window_buf.data(), windowed, N);

        kiss_fftr(m_fftr_cfg, windowed, m_fftOut.data());

        deinterleave(m_fftOut.data(), re, im, numRawBins);
        batchMag(re, im, mag, numRawBins);
        computeLinearBins(mag, N, out);

        // Integer accumulation for log-rebin
        FASTLED_STACK_ARRAY(u32, rawBinsI, m_totalBands);
        for (int i = 0; i < m_totalBands; i++) {
            rawBinsI[i] = 0;
        }

        // Upper tier: LOG_REBIN for bins [m_hybridMidSplitBin, m_totalBands)
        logRebinRange(mag, N,
                      static_cast<float>(m_sampleRate),
                      m_hybridMidSplitBin, m_totalBands, rawBinsI,
                      m_logBinLut);

        // Decimate signal: 512 → 256 → 128 (2 steps for mid tier)
        int workLen = N;
        for (int i = 0; i < N; i++) {
            m_workBuf[i] =
                (i < static_cast<int>(buffer.size())) ? buffer[i] : 0;
        }
        decimateBy2(m_workBuf.data(), workLen);
        workLen /= 2;
        decimateBy2(m_workBuf.data(), workLen);
        workLen /= 2;

        // Phase 2: Zero-padded 256pt FFT (128 windowed + 128 zeros) → LOG_REBIN for mid bins
        if (m_hybridMidSplitBin > m_hybridSplitBin && m_hybridMidFft) {
            int midFftN = m_hybridMidN * 2;
            int midRawBins = midFftN / 2 + 1;
            FASTLED_STACK_ARRAY(kiss_fft_scalar, midWindowed, midFftN);
            applyWindow(m_workBuf.data(), m_hybridMidWindow.data(),
                           midWindowed, m_hybridMidN);
            for (int i = m_hybridMidN; i < midFftN; ++i) {
                midWindowed[i] = 0;
            }
            kiss_fftr(m_hybridMidFft, midWindowed,
                      m_hybridMidFftOut.data());
            deinterleave(m_hybridMidFftOut.data(), re, im, midRawBins);
            batchMag(re, im, mag, midRawBins);
            logRebinRange(mag, midFftN,
                          m_hybridMidFs,
                          m_hybridSplitBin, m_hybridMidSplitBin, rawBinsI,
                          m_logBinLutMid);
        }

        // Decimate 1 more step: 128 → 64 (reuse unwindowed workBuf)
        decimateBy2(m_workBuf.data(), workLen);
        workLen /= 2;

        // Phase 3: Windowed 64pt FFT → LOG_REBIN for bass bins
        if (m_hybridSplitBin > 0 && m_hybridSmallFft) {
            int bassRawBins = m_hybridSmallN / 2 + 1;
            FASTLED_STACK_ARRAY(kiss_fft_scalar, bassWindowed, m_hybridSmallN);
            applyWindow(m_workBuf.data(), m_hybridBassWindow.data(),
                           bassWindowed, m_hybridSmallN);
            kiss_fftr(m_hybridSmallFft, bassWindowed,
                      m_hybridSmallFftOut.data());
            deinterleave(m_hybridSmallFftOut.data(), re, im, bassRawBins);
            batchMag(re, im, mag, bassRawBins);
            logRebinRange(mag, m_hybridSmallN,
                          m_hybridSmallFs,
                          0, m_hybridSplitBin, rawBinsI,
                          m_logBinLutBass);
        }

        // Store raw magnitudes (dB computed lazily by FFTBins::db())
        fl::vector<float> &rawBins = out->raw_mut();
        rawBins.resize(m_totalBands);
        for (int i = 0; i < m_totalBands; ++i) {
            rawBins[i] = static_cast<float>(rawBinsI[i]);
        }

        // Use pre-computed merged norm factors (no per-frame allocation)
        out->setNormFactors(m_hybridMergedNorm);
    }

    // ---- Shared utilities ----

    // Compute bin-width normalization factors for LOG_REBIN.
    // Counts how many FFT bins map to each output bin via the LUT,
    // then stores 1/count as the normalization factor. Bins with 0 FFT bins
    // get factor 1.0 (no scaling).
    void computeLogRebinNormFactors(fl::vector<float>& normFactors,
                                     const fl::vector<u8>& lut,
                                     int fftN, float fs,
                                     int binStart, int binEnd) {
        int bands = binEnd - binStart;
        normFactors.resize(binEnd);
        for (int i = 0; i < binEnd; ++i) {
            normFactors[i] = 1.0f;
        }

        // Count FFT bins mapping to each output bin using same bounds as logRebinRange
        const int numRawBins = fftN / 2 + 1;
        const u16x16 rawBinHz(fs / static_cast<float>(fftN));
        const u16x16 halfBin = rawBinHz >> 1;
        const u16x16 loEdge(m_logBinEdges[binStart]);
        const u16x16 hiEdge(m_logBinEdges[binEnd]);

        int kStart = 0;
        if (loEdge > halfBin) {
            kStart = static_cast<int>(
                u16x16::ceil((loEdge - halfBin) / rawBinHz).to_int());
        }
        int kEnd = static_cast<int>(
            u16x16::ceil((hiEdge + halfBin) / rawBinHz).to_int());
        if (kEnd > numRawBins) kEnd = numRawBins;

        FASTLED_STACK_ARRAY(float, counts, binEnd);
        for (int k = kStart; k < kEnd; ++k) {
            counts[lut[k]] += 1.0f;
        }

        for (int i = binStart; i < binEnd; ++i) {
            normFactors[i] = (counts[i] > 0.0f) ? 1.0f / counts[i] : 1.0f;
        }
        (void)bands;
    }

    // LOG_REBIN helper: group FFT bins into CQ output bins [binStart, binEnd)
    // Uses pre-computed LUT for O(1) bin mapping per FFT bin.
    void logRebinRange(const u16 *mag, int fftN, float fs,
                       int binStart, int binEnd,
                       u32 *rawBinsI, const fl::vector<u8>& lut) {
        const int numRawBins = fftN / 2 + 1;
        // Compute loop bounds to skip out-of-range FFT bins
        const u16x16 rawBinHz(fs / static_cast<float>(fftN));
        const u16x16 halfBin = rawBinHz >> 1;
        const u16x16 loEdge(m_logBinEdges[binStart]);
        const u16x16 hiEdge(m_logBinEdges[binEnd]);

        int kStart = 0;
        if (loEdge > halfBin) {
            kStart = static_cast<int>(
                u16x16::ceil((loEdge - halfBin) / rawBinHz).to_int());
        }
        int kEnd = static_cast<int>(
            u16x16::ceil((hiEdge + halfBin) / rawBinHz).to_int());
        if (kEnd > numRawBins) kEnd = numRawBins;

        for (int k = kStart; k < kEnd; ++k) {
            rawBinsI[lut[k]] += static_cast<u32>(mag[k]);
        }
    }

    void computeLinearBins(const u16 *mag, int /*nfft*/, FFTBins *out) {
        const int numLinearBins = m_totalBands;

        fl::vector<float> &linBins = out->linear_mut();
        linBins.resize(numLinearBins);
        out->setLinearParams(m_fmin, m_fmax);

        // Integer accumulation using pre-computed LUT
        FASTLED_STACK_ARRAY(u32, linBinsI, numLinearBins);
        for (int i = 0; i < numLinearBins; ++i) {
            linBinsI[i] = 0;
        }

        for (int k = m_linearKStart; k < m_linearKEnd; ++k) {
            linBinsI[m_linearBinLut[k]] += static_cast<u32>(mag[k]);
        }

        // Convert to float
        for (int i = 0; i < numLinearBins; ++i) {
            linBins[i] = static_cast<float>(linBinsI[i]);
        }
    }

    // 7-tap halfband filter + decimate by 2.
    // h = [-1/32, 0, 9/32, 1/2, 9/32, 0, -1/32]
    // Stopband rejection: ~-45dB per stage (vs -13dB for old 3-tap).
    // Total rejection with 3 decimation stages: ~-135dB.
    static void decimateBy2(kiss_fft_scalar *buf, int len) {
        int outLen = len / 2;
        for (int i = 0; i < outLen; i++) {
            int idx = i * 2;
            auto s = [&](int offset) -> i32 {
                int j = idx + offset;
                if (j < 0) j = 0;
                if (j >= len) j = len - 1;
                return buf[j];
            };
            i32 val = -s(-3) + 9*s(-1) + 16*s(0) + 9*s(1) - s(3);
            buf[i] = static_cast<kiss_fft_scalar>(val / 32);
        }
    }

    // ---- Member variables ----

    // Shared
    kiss_fftr_cfg m_fftr_cfg;
    int m_input_samples;

    // Naive CQ path only
    cq_kernels_t m_kernels;
    cq_kernel_cfg m_cq_cfg;

    // Log-rebin path only
    fl::vector<float> m_logBinEdges;   // bands+1 geometric bin edges (float)
    fl::vector<u16x16> m_logBinEdgesQ16;  // bands+1 geometric bin edges (Q16.16)
    fl::vector<alpha16> m_window_buf;   // N pre-computed window coefficients (UNORM16 [0,1])
    fl::vector<float> m_logBinNormFactors;  // Per-bin width normalization (1/count)

    // Octave-wise CQ path (also used by Hybrid)
    FFTMode m_mode;
    fl::vector<OctaveInfo> m_octaves;
    fl::vector<kiss_fft_scalar> m_workBuf; // reusable decimation buffer
    fl::vector<kiss_fft_cpx> m_fftOut;     // reusable FFT output buffer
    int m_maxBinsPerOctave = 0;

    // Hybrid 3-tier path
    int m_hybridSplitBin = 0;      // bins < this use bass FFT
    int m_hybridNumCqOctaves = 0;
    int m_hybridNumDecim = 0;
    int m_hybridSmallN = 0;        // bass FFT size (samples/8)
    float m_hybridSmallFs = 0.0f;  // bass sample rate (sr/8)
    kiss_fftr_cfg m_hybridSmallFft = nullptr;
    fl::vector<kiss_fft_cpx> m_hybridSmallFftOut;
    // Mid-tier (3-tier hybrid)
    int m_hybridMidN = 0;          // mid FFT size (samples/4)
    float m_hybridMidFs = 0.0f;    // mid sample rate (sr/4)
    kiss_fftr_cfg m_hybridMidFft = nullptr;
    fl::vector<kiss_fft_cpx> m_hybridMidFftOut;
    int m_hybridMidSplitBin = 0;   // bins >= this use upper FFT
    fl::vector<alpha16> m_hybridBassWindow;  // UNORM16 window for bass
    fl::vector<alpha16> m_hybridMidWindow;   // UNORM16 window for mid
    fl::vector<float> m_hybridNormUpper; // Normalization factors for upper tier
    fl::vector<float> m_hybridNormMid;   // Normalization factors for mid tier
    fl::vector<float> m_hybridNormBass;  // Normalization factors for bass tier
    fl::vector<float> m_hybridMergedNorm; // Pre-computed merged norm factors

    // Pre-computed bin mapping LUTs (built at init, used at runtime)
    fl::vector<u8> m_logBinLut;       // FFT bin k → log-bin index (primary FFT)
    fl::vector<u8> m_linearBinLut;    // FFT bin k → linear-bin index (primary FFT)
    fl::vector<u8> m_logBinLutMid;    // HYBRID mid-tier LUT (256pt)
    fl::vector<u8> m_logBinLutBass;   // HYBRID bass-tier LUT (64pt)
    int m_linearKStart = 0;           // Pre-computed linear bin loop bounds
    int m_linearKEnd = 0;

    // Used by both paths
    int m_totalBands;
    float m_fmin, m_fmax;
    int m_sampleRate;
    FFTWindow m_window;
};

FFTImpl::FFTImpl(const FFT_Args &args) {
    mContext = fl::make_unique<FFTContext>(args.samples, args.bands, args.fmin,
                                          args.fmax, args.sample_rate,
                                          args.mode, args.window);
}

FFTImpl::~FFTImpl() { mContext.reset(); }

fl::string FFTImpl::info() const {
    if (mContext) {
        return mContext->info();
    } else {
        FASTLED_WARN("FFTImpl context is not initialized");
        return fl::string();
    }
}

fl::size FFTImpl::sampleSize() const {
    if (mContext) {
        return mContext->sampleSize();
    }
    return 0;
}

FFTImpl::Result FFTImpl::run(const AudioSample &sample, FFTBins *out) {
    auto &audio_sample = sample.pcm();
    span<const i16> slice(audio_sample);
    return run(slice, out);
}

FFTImpl::Result FFTImpl::run(span<const i16> sample, FFTBins *out) {
    if (!mContext) {
        return FFTImpl::Result(false, "FFTImpl context is not initialized");
    }
    if (sample.size() != mContext->sampleSize()) {
        FASTLED_WARN("FFTImpl sample size mismatch");
        return FFTImpl::Result(false, "FFTImpl sample size mismatch");
    }
    mContext->run(sample, out);
    return FFTImpl::Result(true, "");
}

} // namespace fl
