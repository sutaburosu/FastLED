#include "fl/audio/detectors/beat.h"
#include "fl/audio/audio_context.h"
#include "fl/stl/math.h"
#include "fl/stl/algorithm.h"

namespace fl {

BeatDetector::BeatDetector()
    : mBeatDetected(false)
    , mBPM(120.0f)
    , mPhase(0.0f)
    , mConfidence(0.0f)
    , mThreshold(1.3f)
    , mSensitivity(1.0f)
    , mSpectralFlux(0.0f)
    , mLastBeatTime(0)
    , mBeatInterval(500)
    , mAdaptiveThreshold(0.0f)
{
    mPreviousMagnitudes.resize(16, 0.0f);
}

BeatDetector::~BeatDetector() = default;

void BeatDetector::update(shared_ptr<AudioContext> context) {
    // Use 30 Hz min frequency so bass bins actually cover bass (20-250 Hz).
    // Default fmin (174.6 Hz) misses bass entirely.
    mRetainedFFT = context->getFFT(16, 30.0f);
    const FFTBins& fft = *mRetainedFFT;
    u32 timestamp = context->getTimestamp();

    // Calculate spectral flux
    mSpectralFlux = calculateSpectralFlux(fft);

    // Update adaptive threshold
    updateAdaptiveThreshold();

    // Detect beat
    mBeatDetected = detectBeat(timestamp);

    if (mBeatDetected) {
        updateTempo(timestamp);
        mLastBeatTime = timestamp;
    }

    // Update phase regardless of beat detection
    updatePhase(timestamp);

    // Update previous magnitudes for next frame
    for (size i = 0; i < fft.raw().size() && i < mPreviousMagnitudes.size(); i++) {
        mPreviousMagnitudes[i] = fft.raw()[i];
    }
}

void BeatDetector::reset() {
    mBeatDetected = false;
    mBPM = 120.0f;
    mPhase = 0.0f;
    mConfidence = 0.0f;
    mSpectralFlux = 0.0f;
    mLastBeatTime = 0;
    mBeatInterval = 500;
    mAdaptiveThreshold = 0.0f;
    fl::fill(mPreviousMagnitudes.begin(), mPreviousMagnitudes.end(), 0.0f);
    mFluxAvg.reset();
}

float BeatDetector::calculateSpectralFlux(const FFTBins& fft) {
    float flux = 0.0f;
    size numBins = fl::fl_min(fft.raw().size(), mPreviousMagnitudes.size());

    // Only consider the bass quarter of FFT bins for beat detection.
    // Musical beats (kick drums) are characterized by bass/low-mid energy.
    // With 16 CQ log-spaced bins from 30-4698 Hz, bins 0-3 cover ~30-200 Hz (bass).
    // Treble transients (hi-hats, cymbals) in higher bins are excluded.
    size bassBins = numBins / 4;
    if (bassBins < 1) bassBins = 1;

    for (size i = 0; i < bassBins; i++) {
        float diff = fft.raw()[i] - mPreviousMagnitudes[i];
        if (diff > 0.0f) {
            flux += diff;
        }
    }

    return flux / static_cast<float>(bassBins);
}

void BeatDetector::updateAdaptiveThreshold() {
    // O(1) running average via MovingAverage filter
    float mean = mFluxAvg.update(mSpectralFlux);
    mAdaptiveThreshold = mean * mThreshold * mSensitivity;
}

bool BeatDetector::detectBeat(u32 timestamp) {
    // Use adaptive threshold with an absolute floor. The floor handles
    // the silence-to-signal transition when adaptive threshold is near zero,
    // and prevents CQ spectral leakage from triggering false beats.
    static constexpr float MIN_FLUX_THRESHOLD = 50.0f;
    float effectiveThreshold = fl::fl_max(mAdaptiveThreshold, MIN_FLUX_THRESHOLD);

    // Check if flux exceeds effective threshold
    if (mSpectralFlux <= effectiveThreshold) {
        return false;
    }

    // Check cooldown period
    u32 timeSinceLastBeat = timestamp - mLastBeatTime;
    if (timeSinceLastBeat < MIN_BEAT_INTERVAL_MS) {
        return false;
    }

    // Calculate confidence based on how much we exceeded threshold
    if (mAdaptiveThreshold > 0.0f) {
        mConfidence = fl::fl_min(1.0f, (mSpectralFlux - mAdaptiveThreshold) / mAdaptiveThreshold);
    } else {
        mConfidence = 1.0f;
    }

    return true;
}

void BeatDetector::updateTempo(u32 timestamp) {
    u32 interval = timestamp - mLastBeatTime;

    // Only update tempo if interval is reasonable
    if (interval >= MIN_BEAT_INTERVAL_MS && interval <= MAX_BEAT_INTERVAL_MS) {
        // Smooth tempo changes
        const float alpha = 0.2f;
        mBeatInterval = static_cast<u32>(
            alpha * static_cast<float>(interval) +
            (1.0f - alpha) * static_cast<float>(mBeatInterval)
        );

        // Convert interval to BPM
        float newBPM = 60000.0f / static_cast<float>(mBeatInterval);

        // Check if tempo changed significantly
        float bpmDiff = fl::fl_abs(newBPM - mBPM);
        mTempoChanged = (bpmDiff > 5.0f);

        mBPM = newBPM;
    }
}

void BeatDetector::updatePhase(u32 timestamp) {
    if (mBeatInterval == 0) {
        mPhase = 0.0f;
        return;
    }

    u32 timeSinceLastBeat = timestamp - mLastBeatTime;
    mPhase = static_cast<float>(timeSinceLastBeat) / static_cast<float>(mBeatInterval);

    // Wrap phase to [0, 1)
    if (mPhase >= 1.0f) {
        mPhase = 1.0f - 0.001f; // Keep slightly below 1.0
    }
}

void BeatDetector::fireCallbacks() {
    if (mBeatDetected) {
        onBeat();
        onOnset(mSpectralFlux);
    }
    if (mTempoChanged) {
        onTempoChange(mBPM, mConfidence);
        mTempoChanged = false;
    }
    onBeatPhase(mPhase);
}

} // namespace fl
