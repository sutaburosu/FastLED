#include "fl/audio/audio_context.h"

namespace fl {

AudioContext::AudioContext(const AudioSample& sample)
    : mSample(sample)
    , mFFTHistoryDepth(0)
    , mFFTHistoryIndex(0)
{
    mFFTCache.reserve(MAX_FFT_CACHE_ENTRIES);
}

AudioContext::~AudioContext() = default;

shared_ptr<const FFTBins> AudioContext::getFFT(int bands, float fmin, float fmax) {
    FFT_Args args(mSample.size(), bands, fmin, fmax, mSampleRate);

    // Search cache for matching args (try to lock weak_ptr)
    for (size i = 0; i < mFFTCache.size(); i++) {
        if (mFFTCache[i].args == args) {
            shared_ptr<const FFTBins> existing = mFFTCache[i].bins.lock();
            if (existing) {
                return existing;
            }
            // Expired — remove stale entry and recompute
            mFFTCache.erase(mFFTCache.begin() + i);
            break;
        }
    }

    // Not cached — compute new FFT result
    auto bins = fl::make_shared<FFTBins>(bands);
    fl::span<const fl::i16> sample = mSample.pcm();
    mFFT.run(sample, bins.get(), args);

    // Evict oldest if at capacity
    if (static_cast<int>(mFFTCache.size()) >= MAX_FFT_CACHE_ENTRIES) {
        mFFTCache.erase(mFFTCache.begin());
    }
    FFTCacheEntry entry;
    entry.args = args;
    entry.bins = bins;
    mFFTCache.push_back(fl::move(entry));
    return bins;
}

const vector<FFTBins>& AudioContext::getFFTHistory(int depth) {
    if (mFFTHistoryDepth != depth) {
        mFFTHistory.clear();
        mFFTHistory.reserve(depth);
        mFFTHistoryDepth = depth;
        mFFTHistoryIndex = 0;
    }
    return mFFTHistory;
}

const FFTBins* AudioContext::getHistoricalFFT(int framesBack) const {
    if (framesBack < 0 || framesBack >= static_cast<int>(mFFTHistory.size())) {
        return nullptr;
    }
    int index = (mFFTHistoryIndex - 1 - framesBack + static_cast<int>(mFFTHistory.size())) % static_cast<int>(mFFTHistory.size());
    return &mFFTHistory[index];
}

void AudioContext::setSample(const AudioSample& sample) {
    // Save current FFT to history (use first cached entry if available)
    if (!mFFTCache.empty() && mFFTHistoryDepth > 0) {
        shared_ptr<const FFTBins> locked = mFFTCache[0].bins.lock();
        if (locked) {
            if (static_cast<int>(mFFTHistory.size()) < mFFTHistoryDepth) {
                mFFTHistory.push_back(*locked);
                // When the history fills up, wrap index to 0 for ring buffer mode
                mFFTHistoryIndex = static_cast<int>(mFFTHistory.size()) % mFFTHistoryDepth;
            } else {
                mFFTHistory[mFFTHistoryIndex] = *locked;
                mFFTHistoryIndex = (mFFTHistoryIndex + 1) % mFFTHistoryDepth;
            }
        }
    }

    mSample = sample;
    // Clear per-frame FFT cache (new sample = new data)
    mFFTCache.clear();
}

void AudioContext::clearCache() {
    mFFTCache.clear();
    mFFTHistory.clear();
    mFFTHistoryDepth = 0;
    mFFTHistoryIndex = 0;
}

} // namespace fl
