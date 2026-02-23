#pragma once

#include "fl/audio.h"
#include "fl/fft.h"
#include "fl/stl/vector.h"
#include "fl/stl/shared_ptr.h"
#include "fl/stl/weak_ptr.h"
#include "fl/stl/span.h"

namespace fl {

class AudioContext {
public:
    explicit AudioContext(const AudioSample& sample);
    ~AudioContext();

    // ----- Basic Sample Access -----
    const AudioSample& getSample() const { return mSample; }
    span<const i16> getPCM() const { return mSample.pcm(); }
    float getRMS() const { return mSample.rms(); }
    float getZCF() const { return mSample.zcf(); }
    u32 getTimestamp() const { return mSample.timestamp(); }

    // ----- Lazy FFT Computation (with weak_ptr caching) -----
    shared_ptr<const FFTBins> getFFT(
        int bands = 16,
        float fmin = FFT_Args::DefaultMinFrequency(),
        float fmax = FFT_Args::DefaultMaxFrequency()
    );
    bool hasFFT() const { return !mFFTCache.empty(); }

    // ----- FFT History (for temporal analysis) -----
    const vector<FFTBins>& getFFTHistory(int depth = 4);
    bool hasFFTHistory() const { return mFFTHistoryDepth > 0; }
    const FFTBins* getHistoricalFFT(int framesBack) const;

    // ----- Sample Rate -----
    void setSampleRate(int sampleRate) { mSampleRate = sampleRate; }
    int getSampleRate() const { return mSampleRate; }

    // ----- Update & Reset -----
    void setSample(const AudioSample& sample);
    void clearCache();

private:
    static constexpr int MAX_FFT_CACHE_ENTRIES = 4;

    struct FFTCacheEntry {
        FFT_Args args;
        weak_ptr<const FFTBins> bins;
    };

    int mSampleRate = 44100;
    AudioSample mSample;
    FFT mFFT; // FFT engine (has its own kernel cache)
    vector<FFTCacheEntry> mFFTCache; // Weak cache: lives while callers hold shared_ptr
    vector<FFTBins> mFFTHistory;
    int mFFTHistoryDepth = 0;
    int mFFTHistoryIndex = 0;
};

} // namespace fl
