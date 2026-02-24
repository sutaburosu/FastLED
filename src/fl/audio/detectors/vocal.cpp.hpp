// VocalDetector - Human voice detection implementation
// Part of FastLED Audio System v2.0 - Phase 3 (Differentiators)

#include "fl/audio/detectors/vocal.h"
#include "fl/audio/audio_context.h"
#include "fl/stl/math.h"
#include "fl/stl/algorithm.h"

namespace fl {

VocalDetector::VocalDetector()
    : mVocalActive(false)
    , mPreviousVocalActive(false)
    , mConfidence(0.0f)
    , mSpectralCentroid(0.0f)
    , mSpectralRolloff(0.0f)
    , mFormantRatio(0.0f)
{}

VocalDetector::~VocalDetector() = default;

void VocalDetector::update(shared_ptr<AudioContext> context) {
    mSampleRate = context->getSampleRate();
    mRetainedFFT = context->getFFT(128);
    const FFTBins& fft = *mRetainedFFT;
    mNumBins = static_cast<int>(fft.bins_raw.size());

    // Calculate spectral features
    mSpectralCentroid = calculateSpectralCentroid(fft);
    mSpectralRolloff = calculateSpectralRolloff(fft);
    mFormantRatio = estimateFormantRatio(fft);

    // Calculate raw confidence and apply time-aware smoothing
    float rawConfidence = calculateRawConfidence(mSpectralCentroid, mSpectralRolloff, mFormantRatio);
    static constexpr float kFrameDt = 0.023f;
    float smoothedConfidence = mConfidenceSmoother.update(rawConfidence, kFrameDt);

    // Hysteresis: use separate on/off thresholds to prevent chattering
    bool wantActive;
    if (mVocalActive) {
        wantActive = (smoothedConfidence >= mOffThreshold);
    } else {
        wantActive = (smoothedConfidence >= mOnThreshold);
    }

    // Debounce: require state to persist for MIN_FRAMES_TO_TRANSITION frames
    if (wantActive != mVocalActive) {
        mFramesInState++;
        if (mFramesInState >= MIN_FRAMES_TO_TRANSITION) {
            mVocalActive = wantActive;
            mFramesInState = 0;
        }
    } else {
        mFramesInState = 0;
    }

    // Track state changes for fireCallbacks
    mStateChanged = (mVocalActive != mPreviousVocalActive);
}

void VocalDetector::fireCallbacks() {
    if (mStateChanged) {
        if (onVocal) onVocal(static_cast<u8>(mConfidenceSmoother.value() * 255.0f));
        if (mVocalActive && onVocalStart) onVocalStart();
        if (!mVocalActive && onVocalEnd) onVocalEnd();
        mPreviousVocalActive = mVocalActive;
        mStateChanged = false;
    }
}

void VocalDetector::reset() {
    mVocalActive = false;
    mPreviousVocalActive = false;
    mConfidence = 0.0f;
    mConfidenceSmoother.reset();
    mSpectralCentroid = 0.0f;
    mSpectralRolloff = 0.0f;
    mFormantRatio = 0.0f;
    mFramesInState = 0;
}

float VocalDetector::calculateSpectralCentroid(const FFTBins& fft) {
    float weightedSum = 0.0f;
    float magnitudeSum = 0.0f;

    for (fl::size i = 0; i < fft.bins_raw.size(); i++) {
        float magnitude = fft.bins_raw[i];
        weightedSum += i * magnitude;
        magnitudeSum += magnitude;
    }

    return (magnitudeSum < 1e-6f) ? 0.0f : weightedSum / magnitudeSum;
}

float VocalDetector::calculateSpectralRolloff(const FFTBins& fft) {
    const float rolloffThreshold = 0.85f;
    float totalEnergy = 0.0f;

    // Calculate total energy
    for (fl::size i = 0; i < fft.bins_raw.size(); i++) {
        float magnitude = fft.bins_raw[i];
        totalEnergy += magnitude * magnitude;
    }

    float energyThreshold = totalEnergy * rolloffThreshold;
    float cumulativeEnergy = 0.0f;

    // Find rolloff point
    for (fl::size i = 0; i < fft.bins_raw.size(); i++) {
        float magnitude = fft.bins_raw[i];
        cumulativeEnergy += magnitude * magnitude;
        if (cumulativeEnergy >= energyThreshold) {
            return static_cast<float>(i) / fft.bins_raw.size();
        }
    }

    return 1.0f;
}

float VocalDetector::estimateFormantRatio(const FFTBins& fft) {
    if (fft.bins_raw.size() < 8) return 0.0f;

    // Calculate bin-to-frequency mapping from actual sample rate
    const float nyquist = static_cast<float>(mSampleRate) / 2.0f;
    const int numBins = static_cast<int>(fft.bins_raw.size());
    const float hzPerBin = nyquist / static_cast<float>(numBins);

    // F1 range: 500-900 Hz (first vocal formant)
    const int f1MinBin = fl::fl_max(0, static_cast<int>(500.0f / hzPerBin));
    const int f1MaxBin = fl::fl_min(numBins - 1, static_cast<int>(900.0f / hzPerBin));

    // F2 range: 1200-2400 Hz (second vocal formant)
    const int f2MinBin = fl::fl_max(0, static_cast<int>(1200.0f / hzPerBin));
    const int f2MaxBin = fl::fl_min(numBins - 1, static_cast<int>(2400.0f / hzPerBin));

    // Find peak energy in F1 range
    float f1Energy = 0.0f;
    for (int i = f1MinBin; i <= f1MaxBin && i < numBins; i++) {
        f1Energy = fl::fl_max(f1Energy, fft.bins_raw[i]);
    }

    // Find peak energy in F2 range
    float f2Energy = 0.0f;
    for (int i = f2MinBin; i <= f2MaxBin && i < numBins; i++) {
        f2Energy = fl::fl_max(f2Energy, fft.bins_raw[i]);
    }

    return (f1Energy < 1e-6f) ? 0.0f : f2Energy / f1Energy;
}

float VocalDetector::calculateRawConfidence(float centroid, float rolloff, float formantRatio) {
    // Normalize centroid to 0-1 range using actual bin count
    float normalizedCentroid = centroid / static_cast<float>(mNumBins);

    // Continuous confidence scores for each feature (no hard binary cutoffs)

    // Centroid score: peak at 0.5 (mid-frequency), falls off toward edges
    // Human voice has centroid in ~0.3-0.7 range
    float centroidScore = fl::fl_max(0.0f, 1.0f - fl::fl_abs(normalizedCentroid - 0.5f) * 2.5f);

    // Rolloff score: peak at 0.65 (energy concentrated in lower-mid frequencies)
    float rolloffScore = fl::fl_max(0.0f, 1.0f - fl::fl_abs(rolloff - 0.65f) / 0.3f);

    // Formant score: continuous, peaks at ideal F2/F1 ratio of ~1.4 for vowels
    // Falls off smoothly rather than binary 0/1
    float formantScore = fl::fl_max(0.0f, 1.0f - fl::fl_abs(formantRatio - 1.4f) / 0.6f);

    // Overall confidence is weighted average
    mConfidence = (centroidScore + rolloffScore + formantScore) / 3.0f;
    return mConfidence;
}

} // namespace fl
