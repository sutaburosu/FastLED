
#include "fl/audio/fft/fft.h"
#include "fl/stl/compiler_control.h"
#include "fl/audio/fft/fft_impl.h"
#include "fl/hash_map_lru.h"
#include "fl/stl/int.h"
#include "fl/stl/shared_ptr.h"  // For shared_ptr
#include "fl/stl/singleton.h"

namespace fl {

// Recycles fl::vector<float> buffers to avoid repeated allocation.
// Vectors returned to the pool retain their capacity for reuse.
class FloatVectorPool {
  public:
    fl::vector<float> acquire(fl::size capacity) {
        for (fl::size i = 0; i < mPool.size(); ++i) {
            if (mPool[i].capacity() >= capacity) {
                fl::vector<float> v = fl::move(mPool[i]);
                mPool[i] = fl::move(mPool.back());
                mPool.pop_back();
                v.clear();
                return v;
            }
        }
        fl::vector<float> v;
        v.reserve(capacity);
        return v;
    }

    void release(fl::vector<float>&& v) {
        if (v.capacity() > 0 && mPool.size() < kMaxPoolSize) {
            v.clear();
            mPool.push_back(fl::move(v));
        }
    }

    void releaseIfNotEmpty(fl::vector<float>&& v) {
        if (!v.empty()) {
            release(fl::move(v));
        }
    }

  private:
    static const fl::size kMaxPoolSize = 32;
    fl::vector<fl::vector<float>> mPool;
};

void FFTBins::clear() {
    mBinsRaw.clear();
    mBinsLinear.clear();
    mNormFactors.clear();
    mNormalizedDirty = true;
    mDbDirty = true;
}

fl::size FFTBins::bands() const { return mBands; }

fl::span<const float> FFTBins::raw() const { return mBinsRaw; }

fl::span<const float> FFTBins::db() const {
    if (!mDbDirty) {
        return mBinsDb;
    }
    if (mBinsDb.capacity() == 0) {
        mBinsDb = pool().acquire(mBands);
    }
    mBinsDb.resize(mBinsRaw.size());
    for (fl::size i = 0; i < mBinsRaw.size(); ++i) {
        float x = mBinsRaw[i];
        mBinsDb[i] = (x > 0.0f) ? 20.0f * fl::log10f(x) : 0.0f;
    }
    mDbDirty = false;
    return mBinsDb;
}

fl::span<const float> FFTBins::rawNormalized() const {
    if (!mNormalizedDirty) {
        return mBinsRawNormalized;
    }
    if (mBinsRawNormalized.capacity() == 0) {
        mBinsRawNormalized = pool().acquire(mBands);
    }
    mBinsRawNormalized.resize(mBinsRaw.size());
    for (fl::size i = 0; i < mBinsRaw.size(); ++i) {
        float norm = (i < mNormFactors.size()) ? mNormFactors[i] : 1.0f;
        mBinsRawNormalized[i] = mBinsRaw[i] * norm;
    }
    mNormalizedDirty = false;
    return mBinsRawNormalized;
}

fl::span<const float> FFTBins::linear() const { return mBinsLinear; }
float FFTBins::linearFmin() const { return mLinearFmin; }
float FFTBins::linearFmax() const { return mLinearFmax; }
float FFTBins::fmin() const { return mFmin; }
float FFTBins::fmax() const { return mFmax; }
int FFTBins::sampleRate() const { return mSampleRate; }

float FFTBins::binToFreq(int i) const {
    int nbands = static_cast<int>(mBinsRaw.size());
    if (nbands <= 1) return mFmin;
    float m = fl::logf(mFmax / mFmin);
    return mFmin * fl::expf(m * static_cast<float>(i) / static_cast<float>(nbands - 1));
}

int FFTBins::freqToBin(float freq) const {
    int nbands = static_cast<int>(mBinsRaw.size());
    if (nbands <= 1) return 0;
    if (freq <= mFmin) return 0;
    if (freq >= mFmax) return nbands - 1;
    float m = fl::logf(mFmax / mFmin);
    float bin = fl::logf(freq / mFmin) / m * static_cast<float>(nbands - 1);
    int result = static_cast<int>(bin + 0.5f);
    if (result < 0) return 0;
    if (result >= nbands) return nbands - 1;
    return result;
}

float FFTBins::binBoundary(int i) const {
    float f_i = binToFreq(i);
    float f_next = binToFreq(i + 1);
    return fl::sqrtf(f_i * f_next);
}

fl::vector<float>& FFTBins::raw_mut() {
    if (mBinsRaw.capacity() == 0) {
        mBinsRaw = pool().acquire(mBands);
    }
    mNormalizedDirty = true;
    mDbDirty = true;
    return mBinsRaw;
}

fl::vector<float>& FFTBins::linear_mut() {
    if (mBinsLinear.capacity() == 0) {
        mBinsLinear = pool().acquire(mBands);
    }
    return mBinsLinear;
}

void FFTBins::setParams(float fmin, float fmax, int sampleRate) {
    mFmin = fmin;
    mFmax = fmax;
    mSampleRate = sampleRate;
}

void FFTBins::setLinearParams(float linearFmin, float linearFmax) {
    mLinearFmin = linearFmin;
    mLinearFmax = linearFmax;
}

void FFTBins::setNormFactors(const fl::vector<float>& factors) {
    if (mNormFactors.capacity() == 0) {
        mNormFactors = pool().acquire(mBands);
    }
    mNormFactors.resize(factors.size());
    for (fl::size i = 0; i < factors.size(); ++i) {
        mNormFactors[i] = factors[i];
    }
    mNormalizedDirty = true;
}

FloatVectorPool& FFTBins::pool() {
    return Singleton<FloatVectorPool>::instance();
}

FFTBins::FFTBins(fl::size n)
    : mBands(n) {}

FFTBins::~FFTBins() {
    auto& p = pool();
    p.releaseIfNotEmpty(fl::move(mBinsRaw));
    p.releaseIfNotEmpty(fl::move(mBinsLinear));
    p.releaseIfNotEmpty(fl::move(mNormFactors));
    p.releaseIfNotEmpty(fl::move(mBinsDb));
    p.releaseIfNotEmpty(fl::move(mBinsRawNormalized));
}

template <> struct Hash<FFT_Args> {
    fl::u32 operator()(const FFT_Args &key) const noexcept {
        // Hash fields individually to avoid padding-byte issues
        fl::u32 h = 0;
        h ^= MurmurHash3_x86_32(&key.samples, sizeof(key.samples));
        h ^= MurmurHash3_x86_32(&key.bands, sizeof(key.bands)) * 2654435761u;
        h ^= MurmurHash3_x86_32(&key.fmin, sizeof(key.fmin)) * 2246822519u;
        h ^= MurmurHash3_x86_32(&key.fmax, sizeof(key.fmax)) * 3266489917u;
        h ^= MurmurHash3_x86_32(&key.sample_rate, sizeof(key.sample_rate)) * 668265263u;
        int mode_int = static_cast<int>(key.mode);
        h ^= MurmurHash3_x86_32(&mode_int, sizeof(mode_int)) * 374761393u;
        int window_int = static_cast<int>(key.window);
        h ^= MurmurHash3_x86_32(&window_int, sizeof(window_int)) * 2246822519u;
        return h;
    }
};

struct FFT::HashMap : public HashMapLru<FFT_Args, fl::shared_ptr<FFTImpl>> {
    HashMap(fl::size max_size)
        : fl::HashMapLru<FFT_Args, fl::shared_ptr<FFTImpl>>(max_size) {}
};

FFT::FFT() { mMap = fl::make_unique<HashMap>(8); };

FFT::~FFT() = default;

FFT::FFT(const FFT &other) {
    // copy the map
    mMap = fl::make_unique<HashMap>(*other.mMap);
}

FFT &FFT::operator=(const FFT &other) {
    mMap = fl::make_unique<HashMap>(*other.mMap);
    return *this;
}

void FFT::run(const span<const fl::i16> &sample, FFTBins *out,
              const FFT_Args &args) {
    FFT_Args args2 = args;
    args2.samples = sample.size();
    get_or_create(args2).run(sample, out);
}

void FFT::clear() { mMap->clear(); }

fl::size FFT::size() const { return mMap->size(); }

void FFT::setFFTCacheSize(fl::size size) { mMap->setMaxSize(size); }

FFTImpl &FFT::get_or_create(const FFT_Args &args) {
    fl::shared_ptr<FFTImpl> *val = mMap->find_value(args);
    if (val) {
        // we have it.
        return **val;
    }
    // else we have to make a new one.
    fl::shared_ptr<FFTImpl> fft = fl::make_shared<FFTImpl>(args);
    (*mMap)[args] = fft;
    return *fft;
}

void FFT_Args::resolveModeEnums(FFTMode &mode, FFTWindow &window, int bands,
                                int samples, float fmin, float fmax) {
    // Resolve mode first
    if (mode == FFTMode::AUTO) {
        if (bands <= 32) {
            mode = FFTMode::LOG_REBIN;
        } else {
            // Check kernel conditioning: N_window = N * fmin / fmax.
            // When >= 2, CQ_NAIVE (single FFT + kernels) works well.
            // When < 2, kernels degenerate and we need octave-wise CQT.
            int winMin = static_cast<int>(
                static_cast<float>(samples) * fmin / fmax);
            mode = (winMin >= 2) ? FFTMode::CQ_NAIVE : FFTMode::CQ_OCTAVE;
        }
    }

    // Resolve window based on resolved mode
    if (window == FFTWindow::AUTO) {
        switch (mode) {
        case FFTMode::LOG_REBIN:
        case FFTMode::CQ_HYBRID:
            // These paths apply time-domain windowing before FFT.
            // BLACKMAN_HARRIS: -92 dB sidelobe rejection (vs -31 dB HANNING).
            window = FFTWindow::BLACKMAN_HARRIS;
            break;
        case FFTMode::CQ_NAIVE:
        case FFTMode::CQ_OCTAVE:
            // CQ kernels already apply Hamming windowing in frequency domain.
            // No time-domain window is applied on these paths, so this is
            // cosmetic. HANNING is the lighter/cheaper default.
            window = FFTWindow::HANNING;
            break;
        default:
            window = FFTWindow::BLACKMAN_HARRIS;
            break;
        }
    }
}

bool FFT_Args::operator==(const FFT_Args &other) const {
    FL_DISABLE_WARNING_PUSH
    FL_DISABLE_WARNING(float-equal);

    return samples == other.samples && bands == other.bands &&
           fmin == other.fmin && fmax == other.fmax &&
           sample_rate == other.sample_rate && mode == other.mode &&
           window == other.window;

    FL_DISABLE_WARNING_POP
}

} // namespace fl
