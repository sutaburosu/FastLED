#pragma once

#include "fl/stl/unique_ptr.h"
#include "fl/stl/span.h"
#include "fl/stl/vector.h"
#include "fl/stl/move.h"
#include "fl/stl/cstring.h"
#include "fl/stl/math.h"
#include "fl/stl/mutex.h"
#include "fl/stl/optional.h"

namespace fl {

class FFTImpl;
class AudioSample;

struct FFTBins {
  public:
    FFTBins(fl::size n) : mSize(n) {
        bins_raw.reserve(n);
        bins_db.reserve(n);
    }

    // Copy constructor and assignment
    FFTBins(const FFTBins &other)
        : bins_raw(other.bins_raw), bins_db(other.bins_db), mSize(other.mSize)
        , mFmin(other.mFmin), mFmax(other.mFmax), mSampleRate(other.mSampleRate) {}
    FFTBins &operator=(const FFTBins &other) {
        if (this != &other) {
            mSize = other.mSize;
            bins_raw = other.bins_raw;
            bins_db = other.bins_db;
            mFmin = other.mFmin;
            mFmax = other.mFmax;
            mSampleRate = other.mSampleRate;
            fl::unique_lock<fl::mutex> lock(mLinearMutex);
            mLinearBins.reset();
        }
        return *this;
    }

    // Move constructor and assignment
    FFTBins(FFTBins &&other) noexcept
        : bins_raw(fl::move(other.bins_raw)), bins_db(fl::move(other.bins_db)), mSize(other.mSize)
        , mFmin(other.mFmin), mFmax(other.mFmax), mSampleRate(other.mSampleRate) {}

    FFTBins &operator=(FFTBins &&other) noexcept {
        if (this != &other) {
            bins_raw = fl::move(other.bins_raw);
            bins_db = fl::move(other.bins_db);
            mSize = other.mSize;
            mFmin = other.mFmin;
            mFmax = other.mFmax;
            mSampleRate = other.mSampleRate;
            fl::unique_lock<fl::mutex> lock(mLinearMutex);
            mLinearBins.reset();
        }
        return *this;
    }

    void clear() {
        bins_raw.clear();
        bins_db.clear();
        fl::unique_lock<fl::mutex> lock(mLinearMutex);
        mLinearBins.reset();
    }

    fl::size size() const { return mSize; }

    // CQ parameters (set by FFTImpl after populating bins)
    float fmin() const { return mFmin; }
    float fmax() const { return mFmax; }
    int sampleRate() const { return mSampleRate; }

    // Set CQ parameters (called by FFTImpl)
    void setParams(float fmin, float fmax, int sampleRate) {
        mFmin = fmin;
        mFmax = fmax;
        mSampleRate = sampleRate;
    }

    // Log-spaced center frequency for CQ bin i
    float binToFreq(int i) const {
        int bands = static_cast<int>(bins_raw.size());
        if (bands <= 1) return mFmin;
        float m = fl::logf(mFmax / mFmin);
        return mFmin * fl::expf(m * static_cast<float>(i) / static_cast<float>(bands - 1));
    }

    // Find which CQ bin contains a given frequency (inverse of binToFreq)
    int freqToBin(float freq) const {
        int bands = static_cast<int>(bins_raw.size());
        if (bands <= 1) return 0;
        if (freq <= mFmin) return 0;
        if (freq >= mFmax) return bands - 1;
        float m = fl::logf(mFmax / mFmin);
        float bin = fl::logf(freq / mFmin) / m * static_cast<float>(bands - 1);
        int result = static_cast<int>(bin + 0.5f);
        if (result < 0) return 0;
        if (result >= bands) return bands - 1;
        return result;
    }

    // Frequency boundary between adjacent CQ bins i and i+1 (geometric mean)
    float binBoundary(int i) const {
        float f_i = binToFreq(i);
        float f_next = binToFreq(i + 1);
        return fl::sqrtf(f_i * f_next);
    }

    // Get linearly-rebinned magnitudes. Same number of bins as bins_raw,
    // but evenly spaced from fmin to fmax. Lazy-computed on first access.
    const fl::vector<float>& getLinearBins() const {
        fl::unique_lock<fl::mutex> lock(mLinearMutex);
        if (!mLinearBins.has_value()) {
            mLinearBins = computeLinearBins();
        }
        return *mLinearBins.ptr();
    }

    // The bins are the output of the FFTImpl (CQ log-spaced magnitudes).
    fl::vector<float> bins_raw;
    // The frequency range of the bins (dB scale).
    fl::vector<float> bins_db;

  private:
    fl::size mSize;
    float mFmin = 174.6f;
    float mFmax = 4698.3f;
    int mSampleRate = 44100;

    mutable fl::mutex mLinearMutex;
    mutable fl::optional<fl::vector<float>> mLinearBins;

    // Redistribute CQ log-spaced energy into linearly-spaced bins
    fl::vector<float> computeLinearBins() const {
        const int numBins = static_cast<int>(bins_raw.size());
        fl::vector<float> linear;
        linear.reserve(numBins);
        if (numBins <= 0) return linear;

        const float linearBinWidth = (mFmax - mFmin) / static_cast<float>(numBins);

        for (int j = 0; j < numBins; ++j) {
            float linLow = mFmin + j * linearBinWidth;
            float linHigh = linLow + linearBinWidth;
            float energy = 0.0f;
            float totalWeight = 0.0f;

            for (int i = 0; i < numBins; ++i) {
                // CQ bin frequency boundaries
                float cqLow = (i == 0) ? mFmin : binBoundary(i - 1);
                float cqHigh = (i == numBins - 1) ? mFmax : binBoundary(i);

                // Overlap between CQ bin and linear bin
                float overlapLow = (linLow > cqLow) ? linLow : cqLow;
                float overlapHigh = (linHigh < cqHigh) ? linHigh : cqHigh;

                if (overlapHigh <= overlapLow) continue;

                float cqWidth = cqHigh - cqLow;
                float fraction = (cqWidth > 0.0f) ? (overlapHigh - overlapLow) / cqWidth : 0.0f;
                energy += bins_raw[i] * fraction;
                totalWeight += fraction;
            }

            linear.push_back((totalWeight > 0.0f) ? energy / totalWeight : 0.0f);
        }
        return linear;
    }
};

struct FFT_Args {
    static int DefaultSamples() { return 512; }
    static int DefaultBands() { return 16; }
    static float DefaultMinFrequency() { return 174.6f; }
    static float DefaultMaxFrequency() { return 4698.3f; }
    static int DefaultSampleRate() { return 44100; }

    int samples;
    int bands;
    float fmin;
    float fmax;
    int sample_rate;

    FFT_Args(int samples = DefaultSamples(), int bands = DefaultBands(),
             float fmin = DefaultMinFrequency(),
             float fmax = DefaultMaxFrequency(),
             int sample_rate = DefaultSampleRate()) {
        // Memset so that this object can be hashed without garbage from packed
        // in data.
        fl::memset(this, 0, sizeof(FFT_Args));
        this->samples = samples;
        this->bands = bands;
        this->fmin = fmin;
        this->fmax = fmax;
        this->sample_rate = sample_rate;
    }

    // Rule of 5 for POD data
    FFT_Args(const FFT_Args &other) = default;
    FFT_Args &operator=(const FFT_Args &other) = default;
    FFT_Args(FFT_Args &&other) noexcept = default;
    FFT_Args &operator=(FFT_Args &&other) noexcept = default;

    bool operator==(const FFT_Args &other) const ;
    bool operator!=(const FFT_Args &other) const { return !(*this == other); }
};

class FFT {
  public:
    FFT();
    ~FFT();

    FFT(FFT &&) = default;
    FFT &operator=(FFT &&) = default;
    FFT(const FFT & other);
    FFT &operator=(const FFT & other);

            void run(const span<const i16> &sample, FFTBins *out,
             const FFT_Args &args = FFT_Args());

    void clear();
    fl::size size() const;

    // FFT's are expensive to create, so we cache them. This sets the size of
    // the cache. The default is 8.
    void setFFTCacheSize(fl::size size);

  private:
    // Get the FFTImpl for the given arguments.
    FFTImpl &get_or_create(const FFT_Args &args);
    struct HashMap;
    scoped_ptr<HashMap> mMap;
};

}; // namespace fl
