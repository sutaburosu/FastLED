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

    // Search cache for matching args
    for (size i = 0; i < mFFTCache.size(); i++) {
        if (mFFTCache[i].args == args) {
            return mFFTCache[i].bins;
        }
    }

    // Not cached — try to recycle a previously-used FFTBins with matching band count
    shared_ptr<FFTBins> bins;
    for (size i = 0; i < mRecyclePool.size(); i++) {
        if (static_cast<int>(mRecyclePool[i]->bands()) == bands) {
            bins = fl::move(mRecyclePool[i]);
            mRecyclePool.erase(mRecyclePool.begin() + i);
            bins->clear(); // Vectors keep capacity — zero allocs
            break;
        }
    }
    if (!bins) {
        bins = fl::make_shared<FFTBins>(bands);
    }

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

void AudioContext::setFFTHistoryDepth(int depth) {
    if (mFFTHistoryDepth != depth) {
        mFFTHistory.clear();
        mFFTHistory.reserve(depth);
        mFFTHistoryDepth = depth;
        mFFTHistoryIndex = 0;
    }
}

const FFTBins* AudioContext::getHistoricalFFT(int framesBack) const {
    if (framesBack < 0 || framesBack >= static_cast<int>(mFFTHistory.size())) {
        return nullptr;
    }
    // Ring buffer lookup: walk backwards from the most-recently-written slot.
    // Adding history.size() before the modulo avoids negative values from
    // the subtraction, since C++ modulo of negative ints is implementation-defined.
    const int n = static_cast<int>(mFFTHistory.size());
    int index = (mFFTHistoryIndex - 1 - framesBack + n) % n;
    return &mFFTHistory[index];
}

void AudioContext::setSample(const AudioSample& sample) {
    // Save current FFT to history (use first cached entry if available)
    if (!mFFTCache.empty() && mFFTHistoryDepth > 0) {
        const shared_ptr<FFTBins>& first = mFFTCache[0].bins;
        if (first) {
            if (static_cast<int>(mFFTHistory.size()) < mFFTHistoryDepth) {
                mFFTHistory.push_back(*first);
                // When the history fills up, wrap index to 0 for ring buffer mode
                mFFTHistoryIndex = static_cast<int>(mFFTHistory.size()) % mFFTHistoryDepth;
            } else {
                mFFTHistory[mFFTHistoryIndex] = *first;
                mFFTHistoryIndex = (mFFTHistoryIndex + 1) % mFFTHistoryDepth;
            }
        }
    }

    // Recycle bins that only the cache holds (use_count == 1).
    // These can be reused next frame without allocation.
    mRecyclePool.clear();
    for (size i = 0; i < mFFTCache.size(); i++) {
        if (mFFTCache[i].bins.use_count() == 1) {
            mRecyclePool.push_back(fl::move(mFFTCache[i].bins));
        }
    }

    mSample = sample;
    // Clear per-frame FFT cache (new sample = new data)
    mFFTCache.clear();
}

void AudioContext::clearCache() {
    mFFTCache.clear();
    mRecyclePool.clear();
    mFFTHistory.clear();
    mFFTHistoryDepth = 0;
    mFFTHistoryIndex = 0;
}

} // namespace fl
