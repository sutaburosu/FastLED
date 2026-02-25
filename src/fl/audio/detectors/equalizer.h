#pragma once

#include "fl/audio/audio_detector.h"
#include "fl/audio/audio_context.h"
#include "fl/fft.h"
#include "fl/filter.h"
#include "fl/stl/function.h"
#include "fl/stl/span.h"
#include "fl/stl/vector.h"

namespace fl {

/// Snapshot of equalizer state, passed to onEqualizer callbacks.
struct Equalizer {
    static constexpr int kNumBins = 16;
    float bass = 0;                          ///< 0.0-1.0
    float mid = 0;                           ///< 0.0-1.0
    float treble = 0;                        ///< 0.0-1.0
    float volume = 0;                        ///< 0.0-1.0 (AGC-normalized RMS)
    float zcf = 0;                           ///< 0.0-1.0 (zero-crossing factor)
    span<const float, kNumBins> bins;        ///< 16 bins, each 0.0-1.0
};

/// WLED-style equalizer detector that provides a 16-bin frequency spectrum
/// normalized to 0.0-1.0, plus convenience bass/mid/treble/volume getters.
///
/// Unlike FrequencyBands (which gives raw float band energies), this detector
/// outputs pre-normalized values suitable for direct LED mapping.
///
/// Usage:
/// @code
/// // Via AudioProcessor (recommended):
/// audio->onEqualizer([](const Equalizer& eq) {
///     float bass = eq.bass;        // 0.0-1.0
///     float bin5 = eq.bins[5];     // 0.0-1.0
/// });
///
/// // Or polling:
/// float bass = audio.getEqBass();  // 0.0-1.0
/// @endcode
class EqualizerDetector : public AudioDetector {
public:
    static constexpr int kNumBins = 16;

    EqualizerDetector();
    ~EqualizerDetector() override;

    // AudioDetector interface
    void update(shared_ptr<AudioContext> context) override;
    void fireCallbacks() override;
    bool needsFFT() const override { return true; }
    const char* getName() const override { return "EqualizerDetector"; }
    void reset() override;
    void setSampleRate(int rate) override { mSampleRate = rate; }

    // WLED-compatible getters (all return 0.0-1.0)
    float getBass() const { return mBass; }
    float getMid() const { return mMid; }
    float getTreble() const { return mTreble; }
    float getVolume() const { return mVolume; }
    float getZcf() const { return mZcf; }
    float getBin(int index) const;
    const float* getBins() const { return mBins; }

    // Callback — single struct with everything
    function_list<void(const Equalizer&)> onEqualizer;

private:
    int mSampleRate = 44100;
    float mBins[kNumBins] = {};
    float mBass = 0, mMid = 0, mTreble = 0, mVolume = 0, mZcf = 0;

    // Per-bin adaptive normalization (running max with slow decay)
    vector<AttackDecayFilter<float>> mBinMaxFilters;
    AttackDecayFilter<float> mVolumeMax{0.001f, 4.0f, 0.0f};

    // Smoothing for output stability
    vector<ExponentialSmoother<float>> mBinSmoothers;

    FFT mFFT;
    FFTBins mFFTBins;
};

} // namespace fl
