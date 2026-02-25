#include "fl/audio/detectors/equalizer.h"
#include "fl/audio/audio_context.h"
#include "fl/fft.h"
#include "fl/stl/math.h"

namespace fl {

namespace {
// WLED uses 60Hz-5120Hz range with 16 bins
const float kEqMinFreq = 60.0f;
const float kEqMaxFreq = 5120.0f;

// Bin-to-band mapping (WLED style):
// Bass:   bins 0-3   (~60-320 Hz)
// Mid:    bins 4-10  (~320-2560 Hz)
// Treble: bins 11-15 (~2560-5120 Hz)
const int kBassStart = 0;
const int kBassEnd = 3;
const int kMidStart = 4;
const int kMidEnd = 10;
const int kTrebleStart = 11;
const int kTrebleEnd = 15;
} // namespace

EqualizerDetector::EqualizerDetector()
    : mFFTBins(kNumBins)
{
    mBinMaxFilters.reserve(kNumBins);
    mBinSmoothers.reserve(kNumBins);
    for (int i = 0; i < kNumBins; ++i) {
        mBinMaxFilters.push_back(AttackDecayFilter<float>(0.001f, 4.0f, 0.0f));
        mBinSmoothers.push_back(ExponentialSmoother<float>(0.05f));
    }
}

EqualizerDetector::~EqualizerDetector() = default;

void EqualizerDetector::update(shared_ptr<AudioContext> context) {
    mSampleRate = context->getSampleRate();

    span<const i16> pcm = context->getPCM();
    if (pcm.size() == 0) return;

    const float dt = computeAudioDt(pcm.size(), mSampleRate);

    // Run 16-bin FFT
    FFT_Args args(pcm.size(), kNumBins, kEqMinFreq, kEqMaxFreq, mSampleRate);
    mFFTBins.clear();
    mFFT.run(pcm, &mFFTBins, args);

    const auto& raw = mFFTBins.raw();
    const int numBins = fl::min(static_cast<int>(raw.size()), kNumBins);

    // For each bin: smooth → track running max → normalize to 0.0-1.0
    for (int i = 0; i < numBins; ++i) {
        float smoothed = mBinSmoothers[i].update(raw[i], dt);
        float runningMax = mBinMaxFilters[i].update(smoothed, dt);
        if (runningMax < 0.001f) runningMax = 0.001f;
        mBins[i] = fl::min(1.0f, smoothed / runningMax);
    }
    // Zero any remaining bins
    for (int i = numBins; i < kNumBins; ++i) {
        mBins[i] = 0.0f;
    }

    // Bass = average of bins 0-3
    float bassSum = 0;
    for (int i = kBassStart; i <= kBassEnd; ++i) bassSum += mBins[i];
    mBass = bassSum / static_cast<float>(kBassEnd - kBassStart + 1);

    // Mid = average of bins 4-10
    float midSum = 0;
    for (int i = kMidStart; i <= kMidEnd; ++i) midSum += mBins[i];
    mMid = midSum / static_cast<float>(kMidEnd - kMidStart + 1);

    // Treble = average of bins 11-15
    float trebleSum = 0;
    for (int i = kTrebleStart; i <= kTrebleEnd; ++i) trebleSum += mBins[i];
    mTreble = trebleSum / static_cast<float>(kTrebleEnd - kTrebleStart + 1);

    // Volume = RMS of (already AGC'd) sample, normalized to 0.0-1.0
    float rms = context->getRMS();
    float volumeMax = mVolumeMax.update(rms, dt);
    if (volumeMax < 0.001f) volumeMax = 0.001f;
    mVolume = fl::min(1.0f, rms / volumeMax);

    // Zero-crossing factor (already 0.0-1.0 from AudioSample)
    mZcf = context->getZCF();
}

void EqualizerDetector::fireCallbacks() {
    if (onEqualizer) {
        Equalizer eq;
        eq.bass = mBass;
        eq.mid = mMid;
        eq.treble = mTreble;
        eq.volume = mVolume;
        eq.zcf = mZcf;
        eq.bins = span<const float, Equalizer::kNumBins>(
            static_cast<const float*>(mBins), Equalizer::kNumBins);
        onEqualizer(eq);
    }
}

void EqualizerDetector::reset() {
    for (int i = 0; i < kNumBins; ++i) {
        mBins[i] = 0.0f;
        mBinMaxFilters[i].reset(0.0f);
        mBinSmoothers[i].reset();
    }
    mBass = 0;
    mMid = 0;
    mTreble = 0;
    mVolume = 0;
    mZcf = 0;
    mVolumeMax.reset(0.0f);
}

float EqualizerDetector::getBin(int index) const {
    if (index < 0 || index >= kNumBins) return 0.0f;
    return mBins[index];
}

} // namespace fl
