// Unit tests for VocalDetector

#include "test.h"
#include "fl/audio.h"
#include "fl/audio/audio_context.h"
#include "fl/audio/detectors/vocal.h"
#include "../test_helpers.h"
#include "fl/stl/vector.h"
#include "fl/stl/math.h"
#include "fl/stl/shared_ptr.h"
#include "fl/stl/cstring.h"
#include "fl/math_macros.h"

using namespace fl;
using fl::audio::test::makeSample;

namespace {

struct AmplitudeLevel {
    const char* label;
    float amplitude;
};

static const AmplitudeLevel kVocalAmplitudes[] = {
    {"very_quiet", 500.0f},   // ~-36 dBFS
    {"quiet",      2000.0f},  // ~-24 dBFS
    {"normal",     10000.0f}, // ~-10 dBFS
    {"loud",       20000.0f}, // ~-4  dBFS
    {"max",        32000.0f}, // ~0   dBFS
};

static AudioSample makeSample_VocalDetector(float freq, fl::u32 timestamp, float amplitude = 16000.0f) {
    return makeSample(freq, timestamp, amplitude);
}

} // anonymous namespace

FL_TEST_CASE("VocalDetector - pure sine is not vocal") {
    VocalDetector detector;
    detector.setSampleRate(44100);

    auto ctx = fl::make_shared<AudioContext>(makeSample_VocalDetector(440.0f, 1000));
    ctx->setSampleRate(44100);
    detector.update(ctx);

    // A pure sine wave should not be detected as vocal
    FL_CHECK_FALSE(detector.isVocal());
}

FL_TEST_CASE("VocalDetector - confidence in valid range") {
    VocalDetector detector;
    detector.setSampleRate(44100);

    auto ctx = fl::make_shared<AudioContext>(makeSample_VocalDetector(440.0f, 1000));
    ctx->setSampleRate(44100);
    detector.update(ctx);

    float conf = detector.getConfidence();
    // Confidence is computed as average of three scores: centroidScore, rolloffScore,
    // formantScore. Individual scores can be slightly negative (rolloffScore down to
    // about -0.86 for extreme inputs), so the average can be slightly negative.
    // For well-behaved inputs, confidence should be in approximately [-0.3, 1.0].
    FL_CHECK_GE(conf, -0.5f);
    FL_CHECK_LE(conf, 1.0f);
}

FL_TEST_CASE("VocalDetector - reset clears state") {
    VocalDetector detector;
    detector.setSampleRate(44100);

    auto ctx = fl::make_shared<AudioContext>(makeSample_VocalDetector(440.0f, 1000));
    ctx->setSampleRate(44100);
    detector.update(ctx);

    detector.reset();
    FL_CHECK_FALSE(detector.isVocal());
    FL_CHECK_EQ(detector.getConfidence(), 0.0f);
}

FL_TEST_CASE("VocalDetector - callbacks don't crash") {
    VocalDetector detector;
    detector.setSampleRate(44100);

    bool changeCallbackInvoked = false;
    bool lastActiveState = true; // Initialize to opposite of expected
    detector.onVocal.add([&changeCallbackInvoked, &lastActiveState](u8 active) {
        changeCallbackInvoked = true;
        lastActiveState = (active > 0);
    });

    auto ctx = fl::make_shared<AudioContext>(makeSample_VocalDetector(440.0f, 1000));
    ctx->setSampleRate(44100);
    detector.update(ctx);
    detector.fireCallbacks();

    // A pure sine at 440Hz should not be vocal. Since detector starts with
    // mVocalActive=false and mPreviousVocalActive=false, and the sine is
    // not vocal, there is no state change so the callback should NOT fire.
    FL_CHECK_FALSE(changeCallbackInvoked);
    // Verify the detector correctly identified the sine as non-vocal
    FL_CHECK_FALSE(detector.isVocal());
}

FL_TEST_CASE("VocalDetector - needsFFT is true") {
    VocalDetector detector;
    FL_CHECK(detector.needsFFT());
}

FL_TEST_CASE("VocalDetector - getName returns correct name") {
    VocalDetector detector;
    FL_CHECK(fl::strcmp(detector.getName(), "VocalDetector") == 0);
}

FL_TEST_CASE("VocalDetector - onVocalStart and onVocalEnd callbacks") {
    VocalDetector detector;
    detector.setSampleRate(44100);
    detector.setThreshold(0.3f);  // Lower threshold for easier triggering

    int startCount = 0;
    int endCount = 0;
    detector.onVocalStart.add([&startCount]() { startCount++; });
    detector.onVocalEnd.add([&endCount]() { endCount++; });

    // Feed frames that might trigger vocal detection, then silence
    // Use a complex multi-harmonic signal that resembles vocal formants
    for (int round = 0; round < 3; ++round) {
        // Complex signal with multiple harmonics (vocal-like)
        auto ctx = fl::make_shared<AudioContext>(makeSample_VocalDetector(300.0f, round * 1000, 15000.0f));
        ctx->setSampleRate(44100);
        ctx->getFFT(128);  // High bin count for formant resolution
        detector.update(ctx);
        detector.fireCallbacks();

        // Silence to potentially trigger vocal end
        fl::vector<fl::i16> silence(512, 0);
        auto silentCtx = fl::make_shared<AudioContext>(
            AudioSample(silence, round * 1000 + 500));
        silentCtx->setSampleRate(44100);
        silentCtx->getFFT(128);
        detector.update(silentCtx);
        detector.fireCallbacks();
    }

    // If vocal end was detected, vocal start must have been detected first
    if (endCount > 0) {
        FL_CHECK_GE(startCount, endCount);
    }
    // Callbacks should not have fired negative times (verify no underflow)
    // and the mechanism should not crash (we got here without crash)
}

FL_TEST_CASE("VocalDetector - amplitude sweep: spectral features stable across dB levels") {
    for (const auto& level : kVocalAmplitudes) {
        VocalDetector detector;
        detector.setSampleRate(44100);

        auto ctx = fl::make_shared<AudioContext>(makeSample_VocalDetector(440.0f, 1000, level.amplitude));
        ctx->setSampleRate(44100);
        ctx->getFFT(128);
        detector.update(ctx);

        float conf = detector.getConfidence();
        // Confidence should be in valid range at all amplitudes
        FL_CHECK_GE(conf, -0.5f);
        FL_CHECK_LE(conf, 1.0f);

        // Pure sine should never trigger vocal detection at any amplitude
        FL_CHECK_FALSE(detector.isVocal());
    }
}

FL_TEST_CASE("VocalDetector - amplitude sweep: confidence consistency for same signal") {
    // Feed 10 frames of 440 Hz sine per amplitude to let EMA settle,
    // then compare confidence across normal/loud/max
    float confidences[5] = {};

    for (int idx = 0; idx < 5; ++idx) {
        const auto& level = kVocalAmplitudes[idx];
        VocalDetector detector;
        detector.setSampleRate(44100);

        // Feed 10 frames to let EMA smoother settle
        for (int frame = 0; frame < 10; ++frame) {
            auto ctx = fl::make_shared<AudioContext>(
                makeSample_VocalDetector(440.0f, frame * 23, level.amplitude));
            ctx->setSampleRate(44100);
            ctx->getFFT(128);
            detector.update(ctx);
        }

        confidences[idx] = detector.getConfidence();
    }

    // Hard assert: normal/loud/max confidences are within 0.15 of each other
    // Ratio-based spectral features should be amplitude-invariant
    float confNormal = confidences[2];
    float confLoud = confidences[3];
    float confMax = confidences[4];

    FL_CHECK_LT(fl::abs(confNormal - confLoud), 0.15f);
    FL_CHECK_LT(fl::abs(confNormal - confMax), 0.15f);
    FL_CHECK_LT(fl::abs(confLoud - confMax), 0.15f);

    // Log very_quiet divergence (FFT quantization noise at low amplitudes)
    FL_MESSAGE("VocalDetector confidence sweep: very_quiet=" << confidences[0]
               << " quiet=" << confidences[1]
               << " normal=" << confidences[2]
               << " loud=" << confidences[3]
               << " max=" << confidences[4]);
}
