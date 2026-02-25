#include "fl/audio/detectors/equalizer.h"
#include "fl/audio/audio_context.h"
#include "fl/fft.h"
#include "fl/stl/math.h"

namespace fl {

namespace {
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

// Microphone pink noise correction profiles (16-channel gain tables).
// These are the actual measured values from WLED-MM's fftResultPink array.
// Values > 1.0 boost (mic is weak at that frequency), < 1.0 cut (mic is hot).
// Source: WLED-MM audio_reactive.h fftResultPink[][]

// INMP441 (WLED-MM profile 2): Empirically measured MEMS microphone correction
const float kMicProfile_INMP441[16] = {
    1.82f, 1.72f, 1.70f, 1.50f, 1.52f, 1.57f, 1.68f, 1.80f,
    1.89f, 2.00f, 2.11f, 2.21f, 2.30f, 2.90f, 3.86f, 6.29f
};

// ICS-43434 (WLED-MM profile 5): Empirically measured MEMS microphone correction
const float kMicProfile_ICS43434[16] = {
    2.75f, 1.60f, 1.40f, 1.46f, 1.52f, 1.57f, 1.68f, 1.80f,
    1.89f, 2.00f, 2.11f, 2.21f, 2.30f, 1.75f, 2.55f, 3.60f
};

// SPM1423 (WLED-MM profile 7): Empirically measured MEMS microphone correction
const float kMicProfile_SPM1423[16] = {
    1.65f, 1.00f, 1.05f, 1.30f, 1.48f, 1.30f, 1.80f, 3.00f,
    1.50f, 1.65f, 2.56f, 3.00f, 2.60f, 2.30f, 5.00f, 3.00f
};

// Generic MEMS (WLED-MM profile 0 "default"): Generic correction for unknown mics
const float kMicProfile_GenericMEMS[16] = {
    1.70f, 1.71f, 1.73f, 1.78f, 1.68f, 1.56f, 1.55f, 1.63f,
    1.79f, 1.62f, 1.80f, 2.06f, 2.47f, 3.35f, 6.83f, 9.55f
};

// Line-In (WLED-MM profile 1): Line-in input correction
const float kMicProfile_LineIn[16] = {
    2.35f, 1.32f, 1.32f, 1.40f, 1.48f, 1.57f, 1.68f, 1.80f,
    1.89f, 1.95f, 2.14f, 2.26f, 2.50f, 2.90f, 4.20f, 6.50f
};

/// Apply FFT scaling mode to a single bin value
inline float applyScaling(float value, FFTScalingMode mode) {
    switch (mode) {
    case FFTScalingMode::SquareRoot:
        return fl::sqrtf(value);
    case FFTScalingMode::Logarithmic:
        return fl::logf(1.0f + value);
    case FFTScalingMode::None:
    case FFTScalingMode::Linear:
    default:
        return value;
    }
}

/// Get the mic correction profile gains for a given profile enum
const float* getMicProfileGains(MicProfile profile) {
    switch (profile) {
    case MicProfile::INMP441:      return kMicProfile_INMP441;
    case MicProfile::ICS43434:     return kMicProfile_ICS43434;
    case MicProfile::SPM1423:      return kMicProfile_SPM1423;
    case MicProfile::GenericMEMS:  return kMicProfile_GenericMEMS;
    case MicProfile::LineIn:       return kMicProfile_LineIn;
    case MicProfile::None:
    default:                       return nullptr;
    }
}

} // namespace

EqualizerDetector::EqualizerDetector()
{
    mBinMaxFilters.reserve(kNumBins);
    mBinSmoothers.reserve(kNumBins);
    for (int i = 0; i < kNumBins; ++i) {
        mBinMaxFilters.push_back(AttackDecayFilter<float>(mConfig.normAttack, mConfig.normDecay, 0.0f));
        mBinSmoothers.push_back(ExponentialSmoother<float>(mConfig.smoothing));
    }
    mVolumeMax = AttackDecayFilter<float>(mConfig.normAttack, mConfig.normDecay, 0.0f);
    // Initialize mic gains to 1.0 (no correction)
    for (int i = 0; i < kNumBins; ++i) {
        mMicGains[i] = 1.0f;
    }
}

EqualizerDetector::~EqualizerDetector() = default;

void EqualizerDetector::configure(const EqualizerConfig& config) {
    mConfig = config;
    // Rebuild normalization filters
    mBinMaxFilters.clear();
    for (int i = 0; i < kNumBins; ++i) {
        mBinMaxFilters.push_back(AttackDecayFilter<float>(mConfig.normAttack, mConfig.normDecay, 0.0f));
    }
    mVolumeMax = AttackDecayFilter<float>(mConfig.normAttack, mConfig.normDecay, 0.0f);

    // Configure output smoothing: attack/decay or simple exponential
    mUseAttackDecaySmoothing = (mConfig.outputAttack > 0.0f && mConfig.outputDecay > 0.0f);
    mBinSmoothers.clear();
    mBinOutputFilters.clear();
    if (mUseAttackDecaySmoothing) {
        for (int i = 0; i < kNumBins; ++i) {
            mBinOutputFilters.push_back(
                AttackDecayFilter<float>(mConfig.outputAttack, mConfig.outputDecay, 0.0f));
        }
    } else {
        for (int i = 0; i < kNumBins; ++i) {
            mBinSmoothers.push_back(ExponentialSmoother<float>(mConfig.smoothing));
        }
    }

    // Configure spectral equalizer
    if (mConfig.curve != EqualizationCurve::Flat) {
        SpectralEqualizerConfig eqConfig;
        eqConfig.curve = mConfig.curve;
        eqConfig.numBands = kNumBins;
        mSpectralEq.configure(eqConfig);
    }

}

void EqualizerDetector::setMicProfile(MicProfile profile) {
    const float* profileGains = getMicProfileGains(profile);
    mHasMicCorrection = (profileGains != nullptr);
    if (mHasMicCorrection) {
        for (int i = 0; i < kNumBins; ++i) {
            mMicGains[i] = profileGains[i];
        }
    } else {
        for (int i = 0; i < kNumBins; ++i) {
            mMicGains[i] = 1.0f;
        }
    }
}

void EqualizerDetector::update(shared_ptr<AudioContext> context) {
    mSampleRate = context->getSampleRate();

    span<const i16> pcm = context->getPCM();
    if (pcm.size() == 0) return;

    const float dt = computeAudioDt(pcm.size(), mSampleRate);

    // Use AudioContext's cached FFT (shared across detectors)
    mRetainedFFT = context->getFFT(kNumBins, mConfig.minFreq, mConfig.maxFreq);
    if (!mRetainedFFT) return;
    const FFTBins& fftBins = *mRetainedFFT;

    const auto& raw = fftBins.raw();
    const int numBins = fl::min(static_cast<int>(raw.size()), kNumBins);

    // Step 1: Copy raw bins and apply FFT downscale (windowing correction).
    // WLED-MM applies FFT_DOWNSCALE = 0.40 to compensate for window amplitude gain.
    float scaledBins[kNumBins] = {};
    for (int i = 0; i < numBins; ++i) {
        scaledBins[i] = raw[i] * kFFTDownscale;
    }

    // Step 2: Apply microphone correction profile (before scaling, per WLED-MM)
    if (mHasMicCorrection) {
        for (int i = 0; i < numBins; ++i) {
            scaledBins[i] *= mMicGains[i];
        }
    }

    // Step 3: Apply gain to FFT bins (WLED-MM applies AGC/gain here).
    // This scales magnitudes so per-bin normalization tracks source level.
    if (mGain != 1.0f) {
        for (int i = 0; i < numBins; ++i) {
            scaledBins[i] *= mGain;
        }
    }

    // Step 4: Apply FFT scaling mode (after mic correction + gain, per WLED-MM)
    if (mConfig.scalingMode != FFTScalingMode::None &&
        mConfig.scalingMode != FFTScalingMode::Linear) {
        for (int i = 0; i < numBins; ++i) {
            scaledBins[i] = applyScaling(scaledBins[i], mConfig.scalingMode);
        }
    }

    // Step 5: Apply spectral equalization curve (if not flat)
    if (mConfig.curve != EqualizationCurve::Flat) {
        span<const float> inSpan(scaledBins, numBins);
        span<float> outSpan(mEqBuffer, numBins);
        mSpectralEq.apply(inSpan, outSpan);
        for (int i = 0; i < numBins; ++i) {
            scaledBins[i] = mEqBuffer[i];
        }
    }

    // Step 6: Smooth → track running max → normalize to 0.0-1.0
    for (int i = 0; i < numBins; ++i) {
        float smoothed;
        if (mUseAttackDecaySmoothing) {
            smoothed = mBinOutputFilters[i].update(scaledBins[i], dt);
        } else {
            smoothed = mBinSmoothers[i].update(scaledBins[i], dt);
        }
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

    // Volume = RMS of sample, normalized to 0.0-1.0
    float rms = context->getRMS();
    float volumeMax = mVolumeMax.update(rms, dt);
    if (volumeMax < 0.001f) volumeMax = 0.001f;
    mVolume = fl::min(1.0f, rms / volumeMax);

    // Volume normalization factor: 1/volumeMax tells the caller how much the
    // volume was scaled to reach 0-1 range. Higher = quieter input was amplified more.
    mVolumeNormFactor = (rms > 0.001f) ? (mVolume / rms) : 1.0f;

    // Silence detection: signal is silent when RMS is very low
    mIsSilence = (rms < mConfig.silenceThreshold);

    // Zero-crossing factor (already 0.0-1.0 from AudioSample)
    mZcf = context->getZCF();

    // P2: Dominant frequency detection from pre-normalization data (scaledBins).
    // Using scaledBins avoids the self-normalization bias where consistently
    // active bins always read as 1.0 regardless of actual magnitude.
    int peakBin = 0;
    float peakVal = 0.0f;
    float scaledSum = 0.0f;
    for (int i = 0; i < numBins; ++i) {
        scaledSum += scaledBins[i];
        if (scaledBins[i] > peakVal) {
            peakVal = scaledBins[i];
            peakBin = i;
        }
    }
    // Magnitude: peak relative to average across all bins (0-1 range).
    // Reflects how much the peak dominates the spectrum.
    float scaledAvg = (numBins > 0) ? (scaledSum / static_cast<float>(numBins)) : 0.0f;
    mDominantMagnitude = (scaledAvg > 0.001f)
        ? fl::min(1.0f, peakVal / (scaledAvg * static_cast<float>(numBins)))
        : 0.0f;
    mDominantFreqHz = fftBins.binToFreq(peakBin);

    // P2: Volume in dB referenced to full-scale i16 (32767.0)
    constexpr float kFullScale = 32767.0f;
    if (rms > mConfig.silenceThreshold) {
        mVolumeDb = 20.0f * fl::logf(rms / kFullScale) / fl::logf(10.0f);
        if (mVolumeDb < -100.0f) mVolumeDb = -100.0f;
    } else {
        mVolumeDb = -100.0f;
    }
}

void EqualizerDetector::fireCallbacks() {
    if (onEqualizer) {
        Equalizer eq;
        eq.bass = mBass;
        eq.mid = mMid;
        eq.treble = mTreble;
        eq.volume = mVolume;
        eq.zcf = mZcf;
        eq.volumeNormFactor = mVolumeNormFactor;
        eq.isSilence = mIsSilence;
        eq.bins = span<const float, Equalizer::kNumBins>(
            static_cast<const float*>(mBins), Equalizer::kNumBins);
        eq.dominantFreqHz = mDominantFreqHz;
        eq.dominantMagnitude = mDominantMagnitude;
        eq.volumeDb = mVolumeDb;
        onEqualizer(eq);
    }
}

void EqualizerDetector::reset() {
    for (int i = 0; i < kNumBins; ++i) {
        mBins[i] = 0.0f;
        mBinMaxFilters[i].reset(0.0f);
        if (mUseAttackDecaySmoothing) {
            if (i < static_cast<int>(mBinOutputFilters.size())) {
                mBinOutputFilters[i].reset(0.0f);
            }
        } else {
            if (i < static_cast<int>(mBinSmoothers.size())) {
                mBinSmoothers[i].reset();
            }
        }
    }
    mBass = 0;
    mMid = 0;
    mTreble = 0;
    mVolume = 0;
    mZcf = 0;
    mVolumeNormFactor = 1.0f;
    mIsSilence = false;
    mDominantFreqHz = 0.0f;
    mDominantMagnitude = 0.0f;
    mVolumeDb = -100.0f;
    mVolumeMax.reset(0.0f);
}

float EqualizerDetector::getBin(int index) const {
    if (index < 0 || index >= kNumBins) return 0.0f;
    return mBins[index];
}

} // namespace fl
